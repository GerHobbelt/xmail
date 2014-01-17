/*
 *  XMail by Davide Libenzi (Intranet and Internet mail server)
 *  Copyright (C) 1999,..,2010  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "ShBlocks.h"
#include "ResLocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "UsrAuth.h"

#define USER_AUTH_DIR               "userauth"
#define AUTH_LINE_MAX               1024
#define AUTH_AUTHENTICATE_CONFIG    "userauth"
#define AUTH_ADD_CONFIG             "useradd"
#define AUTH_MODIFY_CONFIG          "useredit"
#define AUTH_DEL_CONFIG             "userdel"
#define AUTH_DROPDOMAIN_CONFIG      "domaindrop"
#define USER_AUTH_TIMEOUT           60000
#define USER_AUTH_PRIORITY          SYS_PRIORITY_NORMAL
#define AUTH_SUCCESS_CODE           0

struct UAuthMacroSubstCtx {
	char const *pszDomain;
	char const *pszUsername;
	char const *pszPassword;
	UserInfo *pUI;
};

static int UAthGetConfigPath(char const *pszService, char const *pszDomain, char *pszConfigPath);
static int UAthExecAuthOp(char const *pszService, char const *pszAuthOp,
			  char const *pszDomain, char const *pszUsername, UserInfo *pUI);
static char *UAthAuthMacroLkupProc(void *pPrivate, char const *pszName, int iSize);
static int UAthMacroSubstitutes(char **ppszCmdTokens, char const *pszDomain,
				char const *pszUsername, char const *pszPassword, UserInfo *pUI);

char *UAthGetRootPath(char const *pszService, char *pszAuthPath, int iMaxPath)
{
	CfgGetRootPath(pszAuthPath, iMaxPath);

	StrNCat(pszAuthPath, USER_AUTH_DIR, iMaxPath);
	AppendSlash(pszAuthPath);
	StrNCat(pszAuthPath, pszService, iMaxPath);
	AppendSlash(pszAuthPath);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at user auth directory: '%s'\n", pszAuthPath);

	return pszAuthPath;
}

static int UAthGetConfigPath(char const *pszService, char const *pszDomain, char *pszConfigPath)
{
	char szAuthPath[SYS_MAX_PATH] = "";

	UAthGetRootPath(pszService, szAuthPath, sizeof(szAuthPath));

	/* Check domain specific config */
	sprintf(pszConfigPath, "%s%s.tab", szAuthPath, pszDomain);
	SysLogMessage(LOG_LEV_DEBUG, "Going to look at user auth file: '%s'\n", pszConfigPath);

	if (SysExistFile(pszConfigPath))
		return 0;

	/* Check default config */
	sprintf(pszConfigPath, "%s.tab", szAuthPath);
	SysLogMessage(LOG_LEV_DEBUG, "Going to look at user auth file: '%s'\n", pszConfigPath);

	if (SysExistFile(pszConfigPath))
		return 0;

	ErrSetErrorCode(ERR_NO_EXTERNAL_AUTH_DEFINED);
	return ERR_NO_EXTERNAL_AUTH_DEFINED;
}

static char *UAthAuthMacroLkupProc(void *pPrivate, char const *pszName, int iSize)
{
	UAuthMacroSubstCtx *pUATH = (UAuthMacroSubstCtx *) pPrivate;

	if (MemMatch(pszName, iSize, "DOMAIN", 6)) {

		return SysStrDup(pUATH->pszDomain != NULL ? pUATH->pszDomain: "");
	} else if (MemMatch(pszName, iSize, "USER", 4)) {

		return SysStrDup(pUATH->pszUsername != NULL ? pUATH->pszUsername: "");
	} else if (MemMatch(pszName, iSize, "PASSWD", 6)) {

		return SysStrDup(pUATH->pszPassword != NULL ? pUATH->pszPassword: "");
	} else if (MemMatch(pszName, iSize, "PATH", 4)) {
		char szUserPath[SYS_MAX_PATH] = "";

		if (pUATH->pUI != NULL)
			UsrGetUserPath(pUATH->pUI, szUserPath, sizeof(szUserPath), 0);

		return SysStrDup(szUserPath);
	}

	return SysStrDup("");
}

static int UAthMacroSubstitutes(char **ppszCmdTokens, char const *pszDomain,
				char const *pszUsername, char const *pszPassword, UserInfo *pUI)
{
	UAuthMacroSubstCtx UATH;

	ZeroData(UATH);
	UATH.pszDomain = pszDomain;
	UATH.pszUsername = pszUsername;
	UATH.pszPassword = pszPassword;
	UATH.pUI = pUI;

	return MscReplaceTokens(ppszCmdTokens, UAthAuthMacroLkupProc, &UATH);
}

static int UAthExecAuthOp(char const *pszService, char const *pszAuthOp,
			  char const *pszDomain, char const *pszUsername,
			  char const *pszPassword, UserInfo *pUI)
{
	char szAuthConfigPath[SYS_MAX_PATH] = "";

	if (UAthGetConfigPath(pszService, pszDomain, szAuthConfigPath) < 0)
		return ErrGetErrorCode();

	FILE *pAuthFile = fopen(szAuthConfigPath, "rt");

	if (pAuthFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, szAuthConfigPath);
		return ERR_FILE_OPEN;
	}

	char szAuthLine[AUTH_LINE_MAX] = "";

	while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL) {
		char **ppszCmdTokens = StrGetTabLineStrings(szAuthLine);

		if (ppszCmdTokens == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszCmdTokens);

		if (iFieldsCount > 1 && stricmp(ppszCmdTokens[0], pszAuthOp) == 0) {
			/* Do auth line macro substitution */
			UAthMacroSubstitutes(ppszCmdTokens, pszDomain, pszUsername, pszPassword,
					     pUI);

			int iExitCode = 0;

			if (SysExec(ppszCmdTokens[1], &ppszCmdTokens[1], USER_AUTH_TIMEOUT,
				    USER_AUTH_PRIORITY, &iExitCode) == 0) {
				if (iExitCode != AUTH_SUCCESS_CODE) {
					StrFreeStrings(ppszCmdTokens);
					fclose(pAuthFile);

					ErrSetErrorCode(ERR_EXTERNAL_AUTH_FAILURE);
					return ERR_EXTERNAL_AUTH_FAILURE;
				}
				StrFreeStrings(ppszCmdTokens);
				fclose(pAuthFile);

				return 0;
			} else {
				StrFreeStrings(ppszCmdTokens);
				fclose(pAuthFile);

				SysLogMessage(LOG_LEV_MESSAGE,
					      "Execution error in authentication file \"%s\"\n",
					      szAuthConfigPath);

				ErrSetErrorCode(ERR_EXTERNAL_AUTH_FAILURE);
				return ERR_EXTERNAL_AUTH_FAILURE;
			}
		}
		StrFreeStrings(ppszCmdTokens);
	}

	fclose(pAuthFile);

	ErrSetErrorCode(ERR_NO_EXTERNAL_AUTH_DEFINED);
	return ERR_NO_EXTERNAL_AUTH_DEFINED;
}

int UAthAuthenticateUser(char const *pszService, char const *pszDomain,
			 char const *pszUsername, char const *pszPassword)
{
	return (UAthExecAuthOp(pszService, AUTH_AUTHENTICATE_CONFIG, pszDomain, pszUsername,
			       pszPassword, NULL));
}

int UAthAddUser(char const *pszService, UserInfo *pUI)
{
	return (UAthExecAuthOp(pszService, AUTH_ADD_CONFIG, pUI->pszDomain, pUI->pszName,
			       pUI->pszPassword, pUI));
}

int UAthModifyUser(char const *pszService, UserInfo *pUI)
{
	return (UAthExecAuthOp(pszService, AUTH_MODIFY_CONFIG, pUI->pszDomain, pUI->pszName,
			       pUI->pszPassword, pUI));
}

int UAthDelUser(char const *pszService, UserInfo *pUI)
{
	return (UAthExecAuthOp(pszService, AUTH_DEL_CONFIG, pUI->pszDomain, pUI->pszName,
			       pUI->pszPassword, pUI));
}

int UAthDropDomain(char const *pszService, char const *pszDomain)
{
	return UAthExecAuthOp(pszService, AUTH_DROPDOMAIN_CONFIG, pszDomain, NULL, NULL, NULL);
}

