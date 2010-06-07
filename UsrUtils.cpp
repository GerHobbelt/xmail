/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,..,2004  Davide Libenzi
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
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "MailDomains.h"
#include "POP3GwLink.h"
#include "ExtAliases.h"
#include "Maildir.h"
#include "TabIndex.h"
#include "SMTPUtils.h"
#include "AliasDomain.h"
#include "UsrAuth.h"

/*
[i_a]
#define SVR_TABLE_FILE              "mailusers.tab"
#define SVR_ALIAS_FILE              "aliases.tab"
*/
#define WILD_ALIASES_HASH           0
#define USR_TABLE_LINE_MAX          2048
#define USR_ALIAS_LINE_MAX          512
/*
[i_a]
#define USER_PROFILE_FILE           "user.tab"
#define DEFAULT_USER_PROFILE_FILE   "userdef.tab"
#define MAILBOX_DIRECTORY           "mailbox"

#define POP3_LOCKS_DIR              "pop3locks"
#define MLUSERS_TABLE_FILE          "mlusers.tab"
#define MAILPROCESS_FILE            "mailproc.tab"
#define USR_DOMAIN_TMPDIR           ".tmp"
#define USR_TMPDIR                  "tmp"
*/

enum UsrFileds {
	usrDomain = 0,
	usrName,
	usrPassword,
	usrID,
	usrPath,
	usrType,

	usrMax
};

struct UserInfoVar {
	LISTLINK LL;
	char *pszName;
	char *pszValue;
};

enum AliasFileds {
	alsDomain = 0,
	alsAlias,
	alsName,

	alsMax
};

struct UsersDBScanData {
	char szTmpDBFile[SYS_MAX_PATH];
	FILE *pDBFile;
};

struct AliasDBScanData {
	char szTmpDBFile[SYS_MAX_PATH];
	FILE *pDBFile;
};

static int UsrCalcAliasHash(const char *const *ppszTabTokens, int const *piFieldsIdx,
			    TabIdxUINT *puHashVal, bool bCaseSens);
static int UsrRebuildUsersIndexes(const char *pszUsrFilePath);
static int UsrRebuildAliasesIndexes(const char *pszAlsFilePath);
static char *UsrGetTableFilePath(char *pszUsrFilePath, int iMaxPath);
static char *UsrGetAliasFilePath(char *pszAlsFilePath, int iMaxPath);
static UserInfo *UsrGetUserFromStrings(char **ppszStrings, int iLoadUCfg);
static UserInfoVar *UsrAllocVar(const char *pszName, const char *pszValue);
static void UsrFreeVar(UserInfoVar *pUIV);
static void UsrFreeInfoList(HSLIST &InfoList);
static UserInfoVar *UsrGetUserVar(HSLIST &InfoList, const char *pszName);
static int UsrWriteInfoList(HSLIST &InfoList, FILE *pProfileFile);
static int UsrLoadUserInfo(HSLIST &InfoList, unsigned int uUserID, const char *pszFilePath);
static int UsrGetDefaultInfoFile(const char *pszDomain, char *pszInfoFile, int iMaxPath);
static int UsrLoadUserDefaultInfo(HSLIST &InfoList, const char *pszDomain = NULL);
static int UsrAliasLookupNameLK(const char *pszAlsFilePath, const char *pszDomain,
				const char *pszAlias, char *pszName = NULL, bool bWildMatch =
				true);
static int UsrWriteAlias(FILE *pAlsFile, AliasInfo *pAI);
static bool UsrIsWildAlias(const char *pszAlias);
static int UsrRemoveUserAlias(const char *pszDomain, const char *pszName);
static UserInfo *UsrGetUserByNameLK(const char *pszUsrFilePath, const char *pszDomain,
				    const char *pszName);
static UserInfo *UsrGetUserByNameOrAliasNDA(const char *pszDomain, const char *pszName,
					    char *pszRealAddr);
static int UsrDropUserEnv(UserInfo *pUI);
static int UsrWriteUser(UserInfo *pUI, FILE *pUsrFile);
static const char *UsrGetMailboxDir(void);
static int UsrCreateMailbox(const char *pszUsrUserPath);
static int UsrPrepareUserEnv(UserInfo *pUI);
static char *UsrGetPop3LocksPath(UserInfo *pUI, char *pszPop3LockPath, int iMaxPath);

static int iIdxUser_Domain_Name[] = {
	usrDomain,
	usrName,

	INDEX_SEQUENCE_TERMINATOR
};

static int iIdxAlias_Domain_Alias[] = {
	alsDomain,
	alsAlias,

	INDEX_SEQUENCE_TERMINATOR
};

static int UsrCalcAliasHash(const char *const *ppszTabTokens, int const *piFieldsIdx,
			    TabIdxUINT *puHashVal, bool bCaseSens)
{
	/* This will group wild alias ( * ? ) */
	int iFieldsCount = StrStringsCount(ppszTabTokens);

	if (iFieldsCount > alsAlias &&
	    (UsrIsWildAlias(ppszTabTokens[alsAlias]) || UsrIsWildAlias(ppszTabTokens[alsDomain])))
	{
		*puHashVal = WILD_ALIASES_HASH;

		return 0;
	}

	return TbixCalculateHash(ppszTabTokens, piFieldsIdx, puHashVal, bCaseSens);
}

int UsrCheckUsersIndexes(void)
{
	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	/* Align Domain-Name index */
	if (TbixCheckIndex(szUsrFilePath, iIdxUser_Domain_Name, false) < 0)
		return ErrGetErrorCode();

	return 0;
}

int UsrCheckAliasesIndexes(void)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	/* Align Domain-Alias index */
	if (TbixCheckIndex(szAlsFilePath, iIdxAlias_Domain_Alias, false, UsrCalcAliasHash) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UsrRebuildUsersIndexes(const char *pszUsrFilePath)
{
	/* Rebuild Domain-Name index */
	if (TbixCreateIndex(pszUsrFilePath, iIdxUser_Domain_Name, false) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UsrRebuildAliasesIndexes(const char *pszAlsFilePath)
{
	/* Rebuild Domain-Alias index */
	if (TbixCreateIndex(pszAlsFilePath, iIdxAlias_Domain_Alias, false, UsrCalcAliasHash) < 0)
		return ErrGetErrorCode();

	return 0;
}

static char *UsrGetTableFilePath(char *pszUsrFilePath, int iMaxPath)
{
	CfgGetRootPath(pszUsrFilePath, iMaxPath);
	StrNCat(pszUsrFilePath, SVR_TABLE_FILE, iMaxPath);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at config file: '%s'\n", pszUsrFilePath);

	return pszUsrFilePath;
}

static char *UsrGetAliasFilePath(char *pszAlsFilePath, int iMaxPath)
{
	CfgGetRootPath(pszAlsFilePath, iMaxPath);
	StrNCat(pszAlsFilePath, SVR_ALIAS_FILE, iMaxPath);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at config file: '%s'\n", pszAlsFilePath);

	return pszAlsFilePath;
}

char *UsrGetMLTableFilePath(UserInfo *pUI, char *pszMLTablePath, int iMaxPath)
{
	UsrGetUserPath(pUI, pszMLTablePath, iMaxPath, 1);
	StrNCat(pszMLTablePath, MLUSERS_TABLE_FILE, iMaxPath);

	return pszMLTablePath;
}

UserType UsrGetUserType(UserInfo *pUI)
{
	if (pUI->pszType == NULL)
		return usrTypeError;

	switch (ToUpper(pUI->pszType[0])) {
	case 'U':
		return usrTypeUser;

	case 'M':
		return usrTypeML;
	}

	return usrTypeError;
}

UserInfo *UsrCreateDefaultUser(const char *pszDomain, const char *pszName,
			       const char *pszPassword, UserType TypeUser)
{
	UserInfo *pUI = (UserInfo *) SysAlloc(sizeof(UserInfo));

	if (pUI == NULL)
		return NULL;

	pUI->pszDomain = SysStrDup(pszDomain);
	pUI->uUserID = 0;
	pUI->pszName = SysStrDup(pszName);
	pUI->pszPassword = SysStrDup(pszPassword);
	pUI->pszPath = SysStrDup(pszName);
	pUI->pszType = SysStrDup((TypeUser == usrTypeUser) ? "U": "M");

	/* Load user profile */
	ListInit(pUI->InfoList);

	UsrLoadUserDefaultInfo(pUI->InfoList, pszDomain);

	return pUI;
}

static UserInfo *UsrGetUserFromStrings(char **ppszStrings, int iLoadUCfg)
{
	int iFieldsCount = StrStringsCount(ppszStrings);

	if (iFieldsCount < usrMax)
		return NULL;

	char szPassword[512] = "";

	if (StrDeCrypt(ppszStrings[usrPassword], szPassword) == NULL)
		return NULL;

	UserInfo *pUI = (UserInfo *) SysAlloc(sizeof(UserInfo));

	if (pUI == NULL)
		return NULL;

	pUI->pszDomain = SysStrDup(ppszStrings[usrDomain]);
	pUI->uUserID = (unsigned int) atol(ppszStrings[usrID]);
	pUI->pszName = SysStrDup(ppszStrings[usrName]);
	pUI->pszPassword = SysStrDup(szPassword);
	pUI->pszPath = SysStrDup(ppszStrings[usrPath]);
	pUI->pszType = SysStrDup(ppszStrings[usrType]);

	/* Load user profile */
	ListInit(pUI->InfoList);

	if (iLoadUCfg) {
		char szUsrFilePath[SYS_MAX_PATH] = "";

		UsrGetUserPath(pUI, szUsrFilePath, sizeof(szUsrFilePath), 1);
		StrNCat(szUsrFilePath, USER_PROFILE_FILE, sizeof(szUsrFilePath));
		UsrLoadUserInfo(pUI->InfoList, pUI->uUserID, szUsrFilePath);
	}

	return pUI;
}

void UsrFreeUserInfo(UserInfo *pUI)
{
	UsrFreeInfoList(pUI->InfoList);

	SysFree(pUI->pszDomain);
	SysFree(pUI->pszPassword);
	SysFree(pUI->pszName);
	SysFree(pUI->pszPath);
	SysFree(pUI->pszType);
	SysFree(pUI);
}

char *UsrGetUserInfoVar(UserInfo *pUI, const char *pszName, const char *pszDefault)
{
	UserInfoVar *pUIV = UsrGetUserVar(pUI->InfoList, pszName);

	if (pUIV != NULL)
		return SysStrDup(pUIV->pszValue);

	return (pszDefault != NULL) ? SysStrDup(pszDefault): NULL;
}

int UsrGetUserInfoVarInt(UserInfo *pUI, const char *pszName, int iDefault)
{
	UserInfoVar *pUIV = UsrGetUserVar(pUI->InfoList, pszName);

	return (pUIV != NULL) ? atoi(pUIV->pszValue): iDefault;
}

int UsrDelUserInfoVar(UserInfo *pUI, const char *pszName)
{
	UserInfoVar *pUIV = UsrGetUserVar(pUI->InfoList, pszName);

	if (pUIV == NULL) {
		ErrSetErrorCode(ERR_USER_VAR_NOT_FOUND);
		return ERR_USER_VAR_NOT_FOUND;
	}
	ListRemovePtr(pUI->InfoList, (PLISTLINK) pUIV);
	UsrFreeVar(pUIV);

	return 0;
}

int UsrSetUserInfoVar(UserInfo *pUI, const char *pszName, const char *pszValue)
{
	UserInfoVar *pUIV = UsrGetUserVar(pUI->InfoList, pszName);

	if (pUIV != NULL) {
		SysFree(pUIV->pszValue);
		pUIV->pszValue = SysStrDup(pszValue);
	} else {
		UserInfoVar *pUIV = UsrAllocVar(pszName, pszValue);

		if (pUIV == NULL)
			return ErrGetErrorCode();

		ListAddTail(pUI->InfoList, (PLISTLINK) pUIV);
	}

	return 0;
}

char **UsrGetProfileVars(UserInfo *pUI)
{
	int iVarsCount = ListGetCount(pUI->InfoList);
	char **ppszVars = (char **) SysAlloc((iVarsCount + 1) * sizeof(char *));

	if (ppszVars == NULL)
		return NULL;

	int iCurrVar = 0;
	UserInfoVar *pUIV = (UserInfoVar *) ListFirst(pUI->InfoList);

	for (; pUIV != INVALID_SLIST_PTR; pUIV = (UserInfoVar *)
		     ListNext(pUI->InfoList, (PLISTLINK) pUIV))
		ppszVars[iCurrVar++] = SysStrDup(pUIV->pszName);

	ppszVars[iCurrVar] = NULL;

	return ppszVars;
}

static UserInfoVar *UsrAllocVar(const char *pszName, const char *pszValue)
{
	UserInfoVar *pUIV = (UserInfoVar *) SysAlloc(sizeof(UserInfoVar));

	if (pUIV == NULL)
		return NULL;

	ListLinkInit(pUIV);
	pUIV->pszName = SysStrDup(pszName);
	pUIV->pszValue = SysStrDup(pszValue);

	return pUIV;
}

static void UsrFreeVar(UserInfoVar *pUIV)
{
	SysFree(pUIV->pszName);
	SysFree(pUIV->pszValue);
	SysFree(pUIV);
}

static void UsrFreeInfoList(HSLIST &InfoList)
{
	UserInfoVar *pUIV;

	while ((pUIV = (UserInfoVar *) ListRemove(InfoList)) != INVALID_SLIST_PTR)
		UsrFreeVar(pUIV);

}

static UserInfoVar *UsrGetUserVar(HSLIST &InfoList, const char *pszName)
{
	UserInfoVar *pUIV = (UserInfoVar *) ListFirst(InfoList);

	for (; pUIV != INVALID_SLIST_PTR; pUIV = (UserInfoVar *)
		     ListNext(InfoList, (PLISTLINK) pUIV))
		if (strcmp(pUIV->pszName, pszName) == 0)
			return pUIV;

	return NULL;
}

static int UsrWriteInfoList(HSLIST &InfoList, FILE *pProfileFile)
{
	UserInfoVar *pUIV = (UserInfoVar *) ListFirst(InfoList);

	for (; pUIV != INVALID_SLIST_PTR; pUIV = (UserInfoVar *)
		     ListNext(InfoList, (PLISTLINK) pUIV)) {
		/* Write variabile name */
		char *pszQuoted = StrQuote(pUIV->pszName, '"');

		if (pszQuoted == NULL)
			return ErrGetErrorCode();

		fprintf(pProfileFile, "%s\t", pszQuoted);

		SysFree(pszQuoted);

		/* Write variabile value */
		pszQuoted = StrQuote(pUIV->pszValue, '"');

		if (pszQuoted == NULL)
			return ErrGetErrorCode();

		fprintf(pProfileFile, "%s\n", pszQuoted);

		SysFree(pszQuoted);
	}

	return 0;
}

static int UsrLoadUserInfo(HSLIST &InfoList, unsigned int uUserID, const char *pszFilePath)
{
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(pszFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pProfileFile = fopen(pszFilePath, "rt");

	if (pProfileFile == NULL) {
		RLckUnlockSH(hResLock);
		ErrSetErrorCode(ERR_NO_USER_PRFILE, pszFilePath); /* [i_a] */
		return ERR_NO_USER_PRFILE;
	}

	char szProfileLine[USR_TABLE_LINE_MAX] = "";

	while (MscGetConfigLine(szProfileLine, sizeof(szProfileLine) - 1, pProfileFile) != NULL) {  /* [i_a] */
		if (szProfileLine[0] == TAB_COMMENT_CHAR)
			continue;

		char **ppszStrings = StrGetTabLineStrings(szProfileLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount == 2) {
			UserInfoVar *pUIV = UsrAllocVar(ppszStrings[0], ppszStrings[1]);

			if (pUIV != NULL)
				ListAddTail(InfoList, (PLISTLINK) pUIV);
		}

		StrFreeStrings(ppszStrings);
	}

	fclose(pProfileFile);

	RLckUnlockSH(hResLock);

	return 0;
}

static int UsrGetDefaultInfoFile(const char *pszDomain, char *pszInfoFile, int iMaxPath)
{
	if (pszDomain != NULL) {
		/* Try to lookup domain specific configuration */
		MDomGetDomainPath(pszDomain, pszInfoFile, iMaxPath, 1);

		StrNCat(pszInfoFile, DEFAULT_USER_PROFILE_FILE, iMaxPath);

		if (SysExistFile(pszInfoFile))
			return 0;

	}
	/* Try to lookup global configuration */
	CfgGetRootPath(pszInfoFile, iMaxPath);

	StrNCat(pszInfoFile, DEFAULT_USER_PROFILE_FILE, iMaxPath);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at config file: '%s'\n", pszInfoFile);

	if (!SysExistFile(pszInfoFile)) {
		ErrSetErrorCode(ERR_NO_USER_DEFAULT_PRFILE);
		return ERR_NO_USER_DEFAULT_PRFILE;
	}

	return 0;
}

static int UsrLoadUserDefaultInfo(HSLIST &InfoList, const char *pszDomain)
{
	char szUserDefFilePath[SYS_MAX_PATH] = "";

	if (UsrGetDefaultInfoFile(pszDomain, szUserDefFilePath, sizeof(szUserDefFilePath)) < 0)
		return ErrGetErrorCode();

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szUserDefFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pProfileFile = fopen(szUserDefFilePath, "rt");

	if (pProfileFile == NULL) {
		ErrSetErrorCode(ERR_NO_USER_DEFAULT_PRFILE, szUserDefFilePath);
		RLckUnlockSH(hResLock);
		return ERR_NO_USER_DEFAULT_PRFILE;
	}

	char szProfileLine[USR_TABLE_LINE_MAX] = "";

	while (MscGetConfigLine(szProfileLine, sizeof(szProfileLine) - 1, pProfileFile) != NULL) {  /* [i_a] */
		if (szProfileLine[0] == TAB_COMMENT_CHAR)
			continue;

		char **ppszStrings = StrGetTabLineStrings(szProfileLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount == 2) {
			UserInfoVar *pUIV = UsrAllocVar(ppszStrings[0], ppszStrings[1]);

			if (pUIV != NULL)
				ListAddTail(InfoList, (PLISTLINK) pUIV);
		}

		StrFreeStrings(ppszStrings);
	}

	fclose(pProfileFile);

	RLckUnlockSH(hResLock);

	return 0;
}

static int UsrAliasLookupNameLK(const char *pszAlsFilePath, const char *pszDomain,
				const char *pszAlias, char *pszName, bool bWildMatch)
{
	/* Lookup record using the specified index ( lookup precise aliases ) */
	char **ppszTabTokens = TbixLookup(pszAlsFilePath, iIdxAlias_Domain_Alias, false,
					  pszDomain,
					  pszAlias,
					  NULL);

	if (ppszTabTokens != NULL) {
		if (pszName != NULL)
			strcpy(pszName, ppszTabTokens[alsName]);

		StrFreeStrings(ppszTabTokens);

		return 1;
	}
	/* We can stop here if wild alias matching is not required */
	if (!bWildMatch)
		return 0;

	/* Lookup record using the specified index ( lookup wild aliases grouped */
	/* under WILD_ALIASES_HASH hash key ) */
	TabIdxUINT uLkHVal = WILD_ALIASES_HASH;
	INDEX_HANDLE hIndexLookup = TbixOpenHandle(pszAlsFilePath, iIdxAlias_Domain_Alias,
						   &uLkHVal, 1);

	if (hIndexLookup != INVALID_INDEX_HANDLE) {
		int iMaxLength = -1;
		char **ppszTabTokens;

		for (ppszTabTokens = TbixFirstRecord(hIndexLookup); ppszTabTokens != NULL;
		     ppszTabTokens = TbixNextRecord(hIndexLookup)) {
			int iFieldsCount = StrStringsCount(ppszTabTokens);

			if (iFieldsCount >= alsMax &&
			    StrIWildMatch(pszDomain, ppszTabTokens[alsDomain]) &&
			    StrIWildMatch(pszAlias, ppszTabTokens[alsAlias])) {
				int iLength = (int)strlen(ppszTabTokens[alsDomain]) +
					(int)strlen(ppszTabTokens[alsAlias]);

				if (iLength > iMaxLength) {
					iMaxLength = iLength;

					if (pszName != NULL)
						strcpy(pszName, ppszTabTokens[alsName]);
				}
			}
			StrFreeStrings(ppszTabTokens);
		}
		TbixCloseHandle(hIndexLookup);

		if (iMaxLength > 0)
			return 1;
	}

	return 0;
}

int UsrAliasLookupName(const char *pszDomain, const char *pszAlias,
		       char *pszName, bool bWildMatch)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szAlsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return 0;

	int iLookupResult = UsrAliasLookupNameLK(szAlsFilePath, pszDomain,
						 pszAlias, pszName, bWildMatch);

	RLckUnlockSH(hResLock);

	return iLookupResult;
}

static int UsrWriteAlias(FILE *pAlsFile, AliasInfo *pAI)
{
	/* Domain */
	char *pszQuoted = StrQuote(pAI->pszDomain, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAlsFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Alias */
	pszQuoted = StrQuote(pAI->pszAlias, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAlsFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Account name */
	pszQuoted = StrQuote(pAI->pszName, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAlsFile, "%s\n", pszQuoted);

	SysFree(pszQuoted);

	return 0;
}

AliasInfo *UsrAllocAlias(const char *pszDomain, const char *pszAlias, const char *pszName)
{
	AliasInfo *pAI = (AliasInfo *) SysAlloc(sizeof(AliasInfo));

	if (pAI == NULL)
		return NULL;

	pAI->pszDomain = (pszDomain != NULL) ? SysStrDup(pszDomain): NULL;
	pAI->pszAlias = (pszAlias != NULL) ? SysStrDup(pszAlias): NULL;
	pAI->pszName = (pszName != NULL) ? SysStrDup(pszName): NULL;

	return pAI;
}

void UsrFreeAlias(AliasInfo *pAI)
{
	SysFree(pAI->pszDomain);
	SysFree(pAI->pszAlias);
	SysFree(pAI->pszName);
	SysFree(pAI);
}

static bool UsrIsWildAlias(const char *pszAlias)
{
	return (strchr(pszAlias, '*') != NULL) || (strchr(pszAlias, '?') != NULL);
}

int UsrAddAlias(AliasInfo *pAI)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAlsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pAlsFile = fopen(szAlsFilePath, "r+t");

	if (pAlsFile == NULL) {
		ErrSetErrorCode(ERR_ALIAS_FILE_NOT_FOUND, szAlsFilePath);
		RLckUnlockEX(hResLock);
		return ERR_ALIAS_FILE_NOT_FOUND;
	}

	char szAlsLine[USR_ALIAS_LINE_MAX] = "";

	while (MscGetConfigLine(szAlsLine, sizeof(szAlsLine) - 1, pAlsFile) != NULL) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szAlsLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= alsMax) &&
		    (stricmp(pAI->pszDomain, ppszStrings[alsDomain]) == 0) &&
		    (stricmp(pAI->pszAlias, ppszStrings[alsAlias]) == 0)) {
			StrFreeStrings(ppszStrings);
			fclose(pAlsFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_ALIAS_EXIST);
			return ERR_ALIAS_EXIST;
		}

		StrFreeStrings(ppszStrings);
	}
	fseek(pAlsFile, 0, SEEK_END);
	if (UsrWriteAlias(pAlsFile, pAI) < 0) {
		fclose(pAlsFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_WRITE_ALIAS_FILE);
		return ERR_WRITE_ALIAS_FILE;
	}
	fclose(pAlsFile);

	/* Rebuild indexes */
	if (UsrRebuildAliasesIndexes(szAlsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int UsrRemoveAlias(const char *pszDomain, const char *pszAlias)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAlsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pAlsFile = fopen(szAlsFilePath, "rt");

	if (pAlsFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_ALIAS_FILE_NOT_FOUND, szAlsFilePath);
		return ERR_ALIAS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pAlsFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	int iAliasFound = 0;
	char szAlsLine[USR_ALIAS_LINE_MAX] = "";

	while (MscFGets(szAlsLine, sizeof(szAlsLine) - 1, pAlsFile) != NULL) {
		if (szAlsLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szAlsLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szAlsLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szAlsLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= alsMax) && (stricmp(pszDomain, ppszStrings[alsDomain]) == 0)
		    && (stricmp(pszAlias, ppszStrings[alsAlias]) == 0)) {

			++iAliasFound;

		} else
			fprintf(pTmpFile, "%s\n", szAlsLine);

		StrFreeStrings(ppszStrings);
	}
	fclose(pAlsFile);
	fclose(pTmpFile);

	if (iAliasFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_ALIAS_NOT_FOUND);
		return ERR_ALIAS_NOT_FOUND;
	}
	if (MscMoveFile(szTmpFile, szAlsFilePath) < 0) {
		RLckUnlockEX(hResLock);
		return ErrGetErrorCode();
	}

	/* Rebuild indexes */
	if (UsrRebuildAliasesIndexes(szAlsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int UsrRemoveDomainAliases(const char *pszDomain)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAlsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pAlsFile = fopen(szAlsFilePath, "rt");

	if (pAlsFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_ALIAS_FILE_NOT_FOUND, szAlsFilePath);
		return ERR_ALIAS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pAlsFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	int iAliasFound = 0;
	char szAlsLine[USR_ALIAS_LINE_MAX] = "";

	while (MscFGets(szAlsLine, sizeof(szAlsLine) - 1, pAlsFile) != NULL) {
		if (szAlsLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szAlsLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szAlsLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szAlsLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= alsMax) && (stricmp(pszDomain, ppszStrings[alsDomain]) == 0)) {

			++iAliasFound;

		} else
			fprintf(pTmpFile, "%s\n", szAlsLine);

		StrFreeStrings(ppszStrings);
	}
	fclose(pAlsFile);
	fclose(pTmpFile);

	if (iAliasFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		return 0;
	}
	if (MscMoveFile(szTmpFile, szAlsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (UsrRebuildAliasesIndexes(szAlsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

static int UsrRemoveUserAlias(const char *pszDomain, const char *pszName)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAlsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pAlsFile = fopen(szAlsFilePath, "rt");

	if (pAlsFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_ALIAS_FILE_NOT_FOUND, szAlsFilePath);
		return ERR_ALIAS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pAlsFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	char szUserAddress[MAX_ADDR_NAME] = "";

	sprintf(szUserAddress, "%s@%s", pszName, pszDomain);

	int iAliasFound = 0;
	char szAlsLine[USR_ALIAS_LINE_MAX] = "";

	while (MscFGets(szAlsLine, sizeof(szAlsLine) - 1, pAlsFile) != NULL) {
		if (szAlsLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szAlsLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szAlsLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szAlsLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= alsMax) &&
		    (((stricmp(pszName, ppszStrings[alsName]) == 0) &&
		      (stricmp(pszDomain, ppszStrings[alsDomain]) == 0)) ||
		     (stricmp(szUserAddress, ppszStrings[alsName]) == 0))) {

			++iAliasFound;

		} else
			fprintf(pTmpFile, "%s\n", szAlsLine);

		StrFreeStrings(ppszStrings);
	}
	fclose(pAlsFile);
	fclose(pTmpFile);

	if (iAliasFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		return 0;
	}
	if (MscMoveFile(szTmpFile, szAlsFilePath) < 0) {
		RLckUnlockEX(hResLock);
		return ErrGetErrorCode();
	}

	/* Rebuild indexes */
	if (UsrRebuildAliasesIndexes(szAlsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

static UserInfo *UsrGetUserByNameLK(const char *pszUsrFilePath, const char *pszDomain,
				    const char *pszName)
{
	/* Lookup record using the specified index */
	char **ppszTabTokens = TbixLookup(pszUsrFilePath, iIdxUser_Domain_Name, false,
					  pszDomain,
					  pszName,
					  NULL);

	if (ppszTabTokens == NULL) {
		ErrSetErrorCode(ERR_USER_NOT_FOUND);

		return NULL;
	}

	UserInfo *pUI = UsrGetUserFromStrings(ppszTabTokens, 1);

	StrFreeStrings(ppszTabTokens);

	return pUI;
}

UserInfo *UsrLookupUser(const char *pszDomain, const char *pszName)
{
	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return NULL;

	UserInfo *pUI = UsrGetUserByNameLK(szUsrFilePath, pszDomain, pszName);

	RLckUnlockSH(hResLock);

	return pUI;
}

UserInfo *UsrGetUserByName(const char *pszDomain, const char *pszName)
{
	/* Check for alias domain */
	UserInfo *pUI = UsrLookupUser(pszDomain, pszName);
	char szADomain[MAX_HOST_NAME] = "";

	/* Check for alias domain if first lookup failed */
	if (pUI == NULL && ADomLookupDomain(pszDomain, szADomain, true))
		pUI = UsrLookupUser(szADomain, pszName);

	return pUI;
}

static UserInfo *UsrGetUserByNameOrAliasNDA(const char *pszDomain, const char *pszName,
					    char *pszRealAddr)
{
	const char *pszAliasedUser = NULL;
	const char *pszAliasedDomain = NULL;
	char szAliasedAccount[MAX_ADDR_NAME] = "";
	char szAliasedName[MAX_ADDR_NAME] = "";
	char szAliasedDomain[MAX_ADDR_NAME] = "";

	if (UsrAliasLookupName(pszDomain, pszName, szAliasedAccount)) {
		if (USmtpSplitEmailAddr(szAliasedAccount, szAliasedName, szAliasedDomain) < 0) {
			pszAliasedUser = szAliasedAccount;
			pszAliasedDomain = pszDomain;
		} else {
			pszAliasedUser = szAliasedName;
			pszAliasedDomain = szAliasedDomain;
		}
	}

	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return NULL;

	UserInfo *pUI = UsrGetUserByNameLK(szUsrFilePath, pszDomain, pszName);

	if (pUI == NULL && pszAliasedUser != NULL)
		pUI = UsrGetUserByNameLK(szUsrFilePath, pszAliasedDomain, pszAliasedUser);

	if (pUI != NULL && pszRealAddr != NULL)
		UsrGetAddress(pUI, pszRealAddr);

	RLckUnlockSH(hResLock);

	return pUI;
}

UserInfo *UsrGetUserByNameOrAlias(const char *pszDomain, const char *pszName, char *pszRealAddr)
{
	UserInfo *pUI = UsrGetUserByNameOrAliasNDA(pszDomain, pszName, pszRealAddr);
	char szADomain[MAX_HOST_NAME] = "";

	/* Check for alias domain if first lookup failed */
	if (pUI == NULL && ADomLookupDomain(pszDomain, szADomain, true))
		pUI = UsrGetUserByNameOrAliasNDA(szADomain, pszName, pszRealAddr);

	return pUI;
}

int UsrRemoveUser(const char *pszDomain, const char *pszName, unsigned int uUserID)
{
	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pUsrFile = fopen(szUsrFilePath, "rt");

	if (pUsrFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_USERS_FILE_NOT_FOUND);
		return ERR_USERS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pUsrFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	UserInfo *pUI = NULL;
	char szUsrLine[USR_TABLE_LINE_MAX] = "";

	while (MscFGets(szUsrLine, sizeof(szUsrLine) - 1, pUsrFile) != NULL) {
		if (szUsrLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szUsrLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szUsrLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= usrMax) && (stricmp(pszDomain, ppszStrings[usrDomain]) == 0)
		    && (((uUserID != 0) && (uUserID == (unsigned int) atol(ppszStrings[usrID])))
			|| ((pszName != NULL) &&
			    (stricmp(pszName, ppszStrings[usrName]) == 0)))) {
			if (pUI != NULL)
				UsrFreeUserInfo(pUI);

			pUI = UsrGetUserFromStrings(ppszStrings, 1);
		} else
			fprintf(pTmpFile, "%s\n", szUsrLine);

		StrFreeStrings(ppszStrings);
	}
	fclose(pUsrFile);
	fclose(pTmpFile);

	if (pUI == NULL) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_USER_NOT_FOUND);
		return ERR_USER_NOT_FOUND;
	}
	if (MscMoveFile(szTmpFile, szUsrFilePath) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (UsrRebuildUsersIndexes(szUsrFilePath) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	GwLkRemoveUserLinks(pUI->pszDomain, pUI->pszName);
	ExAlRemoveUserAliases(pUI->pszDomain, pUI->pszName);
	UsrRemoveUserAlias(pUI->pszDomain, pUI->pszName);

	/* Try ( if defined ) to remove external auth user */
	UAthDelUser(AUTH_SERVICE_POP3, pUI);

	UsrDropUserEnv(pUI);
	UsrFreeUserInfo(pUI);

	return 0;
}

int UsrModifyUser(UserInfo *pUI)
{
	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pUsrFile = fopen(szUsrFilePath, "rt");

	if (pUsrFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_USERS_FILE_NOT_FOUND, szUsrFilePath);
		return ERR_USERS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pUsrFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	UserInfo *pFoundUI = NULL;
	char szUsrLine[USR_TABLE_LINE_MAX] = "";

	while (MscFGets(szUsrLine, sizeof(szUsrLine) - 1, pUsrFile) != NULL) {
		if (szUsrLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szUsrLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szUsrLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= usrMax) && (pFoundUI == NULL) &&
		    (pUI->uUserID == (unsigned int) atol(ppszStrings[usrID])) &&
		    (stricmp(pUI->pszDomain, ppszStrings[usrDomain]) == 0) &&
		    (stricmp(pUI->pszName, ppszStrings[usrName]) == 0)) {
			if ((UsrWriteUser(pUI, pTmpFile) < 0) ||
			    ((pFoundUI = UsrGetUserFromStrings(ppszStrings, 1)) == NULL)) {
				ErrorPush();
				fclose(pUsrFile);
				fclose(pTmpFile);
				SysRemove(szTmpFile);
				RLckUnlockEX(hResLock);
				return ErrorPop();
			}
		} else
			fprintf(pTmpFile, "%s\n", szUsrLine);

		StrFreeStrings(ppszStrings);
	}
	fclose(pUsrFile);
	fclose(pTmpFile);

	if (pFoundUI == NULL) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_USER_NOT_FOUND);
		return ERR_USER_NOT_FOUND;
	}

	/* Try ( if defined ) to modify external auth user */
	UAthModifyUser(AUTH_SERVICE_POP3, pUI);

	UsrFreeUserInfo(pFoundUI);

	if (MscMoveFile(szTmpFile, szUsrFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (UsrRebuildUsersIndexes(szUsrFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int UsrRemoveDomainUsers(const char *pszDomain)
{
	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pUsrFile = fopen(szUsrFilePath, "rt");

	if (pUsrFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_USERS_FILE_NOT_FOUND, szUsrFilePath);
		return ERR_USERS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pUsrFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	int iUsersFound = 0;
	char szUsrLine[USR_TABLE_LINE_MAX] = "";

	while (MscFGets(szUsrLine, sizeof(szUsrLine) - 1, pUsrFile) != NULL) {
		if (szUsrLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szUsrLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szUsrLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= usrMax) && (stricmp(pszDomain, ppszStrings[usrDomain]) == 0)) {
			++iUsersFound;
		} else
			fprintf(pTmpFile, "%s\n", szUsrLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pUsrFile);
	fclose(pTmpFile);

	if (iUsersFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		return 0;
	}
	if (MscMoveFile(szTmpFile, szUsrFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (UsrRebuildUsersIndexes(szUsrFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

static int UsrDropUserEnv(UserInfo *pUI)
{
	/* User directory cleaning */
	char szUsrUserPath[SYS_MAX_PATH] = "";

	UsrGetUserPath(pUI, szUsrUserPath, sizeof(szUsrUserPath), 0);

	if (MscClearDirectory(szUsrUserPath) < 0)
		return ErrGetErrorCode();

	/* User directory removing */
	if (SysRemoveDir(szUsrUserPath) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UsrWriteUser(UserInfo *pUI, FILE *pUsrFile)
{
	/* Domain */
	char *pszQuoted = StrQuote(pUI->pszDomain, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pUsrFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Name */
	pszQuoted = StrQuote(pUI->pszName, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pUsrFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Password */
	char szPassword[512] = "";

	StrCrypt(pUI->pszPassword, szPassword);

	pszQuoted = StrQuote(szPassword, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pUsrFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* UserID */
	fprintf(pUsrFile, "%u\t", pUI->uUserID);

	/* Directory */
	pszQuoted = StrQuote(pUI->pszPath, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pUsrFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* User type */
	pszQuoted = StrQuote(pUI->pszType, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pUsrFile, "%s\n", pszQuoted);

	SysFree(pszQuoted);

	return 0;
}

int UsrAddUser(UserInfo *pUI)
{
	/* Search for overlapping alias ( wildcard alias not checked here ) */
	if (UsrAliasLookupName(pUI->pszDomain, pUI->pszName, NULL, false)) {
		ErrSetErrorCode(ERR_ALIAS_EXIST);
		return ERR_ALIAS_EXIST;
	}

	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pUsrFile = fopen(szUsrFilePath, "r+t");

	if (pUsrFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_USERS_FILE_NOT_FOUND, szUsrFilePath);
		return ERR_USERS_FILE_NOT_FOUND;
	}

	unsigned int uMaxUserID = 0;
	char szUsrLine[USR_TABLE_LINE_MAX] = "";

	while (MscGetConfigLine(szUsrLine, sizeof(szUsrLine) - 1, pUsrFile) != NULL) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= usrMax) {
			if ((stricmp(pUI->pszDomain, ppszStrings[usrDomain]) == 0) &&
			    (stricmp(pUI->pszName, ppszStrings[usrName]) == 0)) {
				StrFreeStrings(ppszStrings);
				fclose(pUsrFile);
				RLckUnlockEX(hResLock);

				ErrSetErrorCode(ERR_USER_EXIST);
				return ERR_USER_EXIST;
			}

			unsigned int uUserID = (unsigned int) atol(ppszStrings[usrID]);

			if (uUserID > uMaxUserID)
				uMaxUserID = uUserID;
		}

		StrFreeStrings(ppszStrings);
	}

	pUI->uUserID = uMaxUserID + 1;

	if (UsrPrepareUserEnv(pUI) < 0) {
		fclose(pUsrFile);
		RLckUnlockEX(hResLock);
		return ErrGetErrorCode();
	}

	fseek(pUsrFile, 0, SEEK_END);

	if (UsrWriteUser(pUI, pUsrFile) < 0) {
		fclose(pUsrFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_WRITE_USERS_FILE);
		return ERR_WRITE_USERS_FILE;
	}

	fclose(pUsrFile);

	/* Rebuild indexes */
	if (UsrRebuildUsersIndexes(szUsrFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	/* Try ( if defined ) to add external auth user */
	UAthAddUser(AUTH_SERVICE_POP3, pUI);

	return 0;
}

static const char *UsrGetMailboxDir(void)
{
	return (iMailboxType == XMAIL_MAILBOX) ? MAILBOX_DIRECTORY: MAILDIR_DIRECTORY;
}

static int UsrCreateMailbox(const char *pszUsrUserPath)
{
	if (iMailboxType == XMAIL_MAILBOX) {
		/* Create mailbox directory */
		char szUsrMailboxPath[SYS_MAX_PATH] = "";

		StrSNCpy(szUsrMailboxPath, pszUsrUserPath);

		AppendSlash(szUsrMailboxPath);
		StrSNCat(szUsrMailboxPath, MAILBOX_DIRECTORY);

		if (SysMakeDir(szUsrMailboxPath) < 0)
			return ErrGetErrorCode();

		return 0;
	}

	return MdirCreateStructure(pszUsrUserPath);
}

static int UsrPrepareUserEnv(UserInfo *pUI)
{
	char szUsrUserPath[SYS_MAX_PATH] = "";

	UsrGetUserPath(pUI, szUsrUserPath, sizeof(szUsrUserPath), 0);

	/* Create main directory */
	if (SysMakeDir(szUsrUserPath) < 0)
		return ErrGetErrorCode();

	if (UsrGetUserType(pUI) == usrTypeUser) {
		/* Create mailbox directory */
		if (UsrCreateMailbox(szUsrUserPath) < 0) {
			ErrorPush();
			MscClearDirectory(szUsrUserPath);
			SysRemoveDir(szUsrUserPath);
			return ErrorPop();
		}
	} else {
		/* Create mailing list users file */
		char szMLUsersFilePath[SYS_MAX_PATH] = "";

		StrSNCpy(szMLUsersFilePath, szUsrUserPath);

		AppendSlash(szMLUsersFilePath);
		StrSNCat(szMLUsersFilePath, MLUSERS_TABLE_FILE);

		if (MscCreateEmptyFile(szMLUsersFilePath) < 0) {
			ErrorPush();
			MscClearDirectory(szUsrUserPath);
			SysRemoveDir(szUsrUserPath);
			return ErrorPop();
		}
	}

	/* Create profile file */
	char szUsrProfileFilePath[SYS_MAX_PATH] = "";

	StrSNCpy(szUsrProfileFilePath, szUsrUserPath);

	AppendSlash(szUsrProfileFilePath);
	StrSNCat(szUsrProfileFilePath, USER_PROFILE_FILE);

	FILE *pProfileFile = fopen(szUsrProfileFilePath, "wt");

	if (pProfileFile == NULL) {
		MscClearDirectory(szUsrUserPath);
		SysRemoveDir(szUsrUserPath);

		ErrSetErrorCode(ERR_FILE_CREATE, szUsrProfileFilePath); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	UsrWriteInfoList(pUI->InfoList, pProfileFile);

	fclose(pProfileFile);

	return 0;
}

int UsrFlushUserVars(UserInfo *pUI)
{
	char szUsrUserPath[SYS_MAX_PATH] = "";

	UsrGetUserPath(pUI, szUsrUserPath, sizeof(szUsrUserPath), 0);

	/* Build profile file path */
	char szUsrProfileFilePath[SYS_MAX_PATH] = "";

	StrSNCpy(szUsrProfileFilePath, szUsrUserPath);

	AppendSlash(szUsrProfileFilePath);
	StrSNCat(szUsrProfileFilePath, USER_PROFILE_FILE);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szUsrProfileFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pProfileFile = fopen(szUsrProfileFilePath, "wt");

	if (pProfileFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_FILE_CREATE, szUsrProfileFilePath); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	UsrWriteInfoList(pUI->InfoList, pProfileFile);

	fclose(pProfileFile);

	RLckUnlockEX(hResLock);

	return 0;
}

int UsrGetDBFileSnapShot(const char *pszFileName)
{
	char szUsrFilePath[SYS_MAX_PATH] = "";

	UsrGetTableFilePath(szUsrFilePath, sizeof(szUsrFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szUsrFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (MscCopyFile(pszFileName, szUsrFilePath) < 0) {
		RLckUnlockSH(hResLock);
		return ErrGetErrorCode();
	}

	RLckUnlockSH(hResLock);

	return 0;
}

USRF_HANDLE UsrOpenDB(void)
{
	UsersDBScanData *pUDBSD = (UsersDBScanData *) SysAlloc(sizeof(UsersDBScanData));

	if (pUDBSD == NULL)
		return INVALID_USRF_HANDLE;

	SysGetTmpFile(pUDBSD->szTmpDBFile);

	if (UsrGetDBFileSnapShot(pUDBSD->szTmpDBFile) < 0) {
		SysFree(pUDBSD);
		return INVALID_USRF_HANDLE;
	}

	if ((pUDBSD->pDBFile = fopen(pUDBSD->szTmpDBFile, "rt")) == NULL) {
		SysRemove(pUDBSD->szTmpDBFile);
		SysFree(pUDBSD);
		return INVALID_USRF_HANDLE;
	}

	return (USRF_HANDLE) pUDBSD;
}

void UsrCloseDB(USRF_HANDLE hUsersDB)
{
	UsersDBScanData *pUDBSD = (UsersDBScanData *) hUsersDB;

	fclose(pUDBSD->pDBFile);
	SysRemove(pUDBSD->szTmpDBFile);
	SysFree(pUDBSD);
}

UserInfo *UsrGetFirstUser(USRF_HANDLE hUsersDB, int iLoadUCfg)
{
	UsersDBScanData *pUDBSD = (UsersDBScanData *) hUsersDB;

	rewind(pUDBSD->pDBFile);

	UserInfo *pUI = NULL;
	char szUsrLine[USR_TABLE_LINE_MAX] = "";

	while ((pUI == NULL) &&
	       (MscGetConfigLine(szUsrLine, sizeof(szUsrLine) - 1, pUDBSD->pDBFile) != NULL)) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= usrMax)
			pUI = UsrGetUserFromStrings(ppszStrings, iLoadUCfg);

		StrFreeStrings(ppszStrings);
	}

	return pUI;
}

UserInfo *UsrGetNextUser(USRF_HANDLE hUsersDB, int iLoadUCfg)
{
	UsersDBScanData *pUDBSD = (UsersDBScanData *) hUsersDB;

	UserInfo *pUI = NULL;
	char szUsrLine[USR_TABLE_LINE_MAX] = "";

	while ((pUI == NULL) &&
	       (MscGetConfigLine(szUsrLine, sizeof(szUsrLine) - 1, pUDBSD->pDBFile) != NULL)) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= usrMax)
			pUI = UsrGetUserFromStrings(ppszStrings, iLoadUCfg);

		StrFreeStrings(ppszStrings);
	}

	return pUI;
}

static char *UsrGetPop3LocksPath(UserInfo *pUI, char *pszPop3LockPath, int iMaxPath)
{
	CfgGetRootPath(pszPop3LockPath, iMaxPath);

	StrNCat(pszPop3LockPath, POP3_LOCKS_DIR, iMaxPath);
	AppendSlash(pszPop3LockPath);

	char szUserAddress[MAX_ADDR_NAME] = "";

	UsrGetAddress(pUI, szUserAddress);
	StrNCat(pszPop3LockPath, szUserAddress, iMaxPath);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at POP3 lock file: '%s'\n", pszPop3LockPath);

	return pszPop3LockPath;
}

int UsrPOP3Lock(UserInfo *pUI)
{
	char szLockPath[SYS_MAX_PATH] = "";

	UsrGetPop3LocksPath(pUI, szLockPath, sizeof(szLockPath));

	if (SysLockFile(szLockPath) < 0)
		return ErrGetErrorCode();

	return 0;
}

void UsrPOP3Unlock(UserInfo *pUI)
{
	char szLockPath[SYS_MAX_PATH] = "";

	UsrGetPop3LocksPath(pUI, szLockPath, sizeof(szLockPath));
	SysUnlockFile(szLockPath);
}

int UsrClearPop3LocksDir(void)
{
	char szLocksDir[SYS_MAX_PATH] = "";

	CfgGetRootPath(szLocksDir, sizeof(szLocksDir));
	StrNCat(szLocksDir, POP3_LOCKS_DIR, sizeof(szLocksDir));

	return MscClearDirectory(szLocksDir);
}

/*
 * This function is intended to create a temporary file name so that
 * a system move (rename) of such file into a user mailbox (or private directory)
 * will succeed. Since the system temporary directory may be on another
 * mount (or drive:), this is required insted of SysGetTmpFile().
 * If the DOMAIN private directory exists, the temporary file will be
 * generated inside there, otherwise inside the XMail temporary directory.
 */
int UsrGetTmpFile(const char *pszDomain, char *pszTmpFile, int iMaxPath)
{
	char szTmpDir[SYS_MAX_PATH] = "";

	if (pszDomain != NULL) {
		MDomGetDomainPath(pszDomain, szTmpDir, sizeof(szTmpDir) - 1, 1);
		StrNCat(szTmpDir, USR_DOMAIN_TMPDIR, sizeof(szTmpDir) - 1);
		if (SysExistDir(szTmpDir)) {
			if (MscUniqueFile(szTmpDir, pszTmpFile, iMaxPath) < 0)
				return ErrGetErrorCode();

			return 0;
		}
	}
	CfgGetRootPath(szTmpDir, sizeof(szTmpDir));
	StrNCat(szTmpDir, USR_TMPDIR, sizeof(szTmpDir));
	if (!SysExistDir(szTmpDir) && SysMakeDir(szTmpDir) < 0)
		return ErrGetErrorCode();

	return MscUniqueFile(szTmpDir, pszTmpFile, iMaxPath);
}

char *UsrGetUserPath(UserInfo *pUI, char *pszUserPath, int iMaxPath, int iFinalSlash)
{
	MDomGetDomainPath(pUI->pszDomain, pszUserPath, iMaxPath, 1);
	StrNCat(pszUserPath, pUI->pszPath, iMaxPath);
	if (iFinalSlash)
		AppendSlash(pszUserPath);

	return pszUserPath;
}

char *UsrGetMailboxPath(UserInfo *pUI, char *pszMBPath, int iMaxPath, int iFinalSlash)
{
	UsrGetUserPath(pUI, pszMBPath, iMaxPath, 1);

	StrNCat(pszMBPath, UsrGetMailboxDir(), iMaxPath);
	if (iFinalSlash)
		AppendSlash(pszMBPath);

	return pszMBPath;
}

int UsrMoveToMailBox(UserInfo *pUI, const char *pszFileName, const char *pszMessageID)
{
	if (iMailboxType == XMAIL_MAILBOX) {
		/* Setup full mailbox file path */
		char szMBPath[SYS_MAX_PATH] = "";
		char szMBFile[SYS_MAX_PATH] = "";

		UsrGetMailboxPath(pUI, szMBPath, sizeof(szMBPath), 0);
		SysSNPrintf(szMBFile, sizeof(szMBFile),
			    "%s" SYS_SLASH_STR "%s", szMBPath, pszMessageID);

		char szResLock[SYS_MAX_PATH] = "";
		RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMBPath, szResLock,
								  sizeof(szResLock)));

		if (hResLock == INVALID_RLCK_HANDLE)
			return ErrGetErrorCode();
		if (SysMoveFile(pszFileName, szMBFile) < 0) {
			ErrorPush();
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
		RLckUnlockEX(hResLock);
	} else {
		/* Get user Maildir path */
		char szMBPath[SYS_MAX_PATH] = "";

		UsrGetMailboxPath(pUI, szMBPath, sizeof(szMBPath), 0);

		char szResLock[SYS_MAX_PATH] = "";
		RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMBPath, szResLock,
								  sizeof(szResLock)));

		if (hResLock == INVALID_RLCK_HANDLE)
			return ErrGetErrorCode();

		if (MdirMoveMessage(szMBPath, pszFileName, pszMessageID) < 0) {
			ErrorPush();
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
		RLckUnlockEX(hResLock);
	}

	return 0;
}

int UsrGetMailProcessFile(UserInfo *pUI, const char *pszMPPath, unsigned long ulFlags)
{
	int iAppendFiles = 0;
	char szMPFilePath[SYS_MAX_PATH] = "";

	if (ulFlags & GMPROC_DOMAIN) {
		if (MDomGetDomainPath(pUI->pszDomain, szMPFilePath, sizeof(szMPFilePath) - 1,
				      1) == NULL)
			return ErrGetErrorCode();
		StrNCat(szMPFilePath, MAILPROCESS_FILE, sizeof(szMPFilePath) - 1);

		if (SysExistFile(szMPFilePath)) {
			char szResLock[SYS_MAX_PATH] = "";
			RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szMPFilePath, szResLock,
									  sizeof(szResLock)));

			if (hResLock == INVALID_RLCK_HANDLE)
				return ErrGetErrorCode();

			if (MscAppendFile(pszMPPath, szMPFilePath) < 0) {
				ErrorPush();
				CheckRemoveFile(pszMPPath);
				RLckUnlockSH(hResLock);
				return ErrorPop();
			}
			RLckUnlockSH(hResLock);
			iAppendFiles++;
		}
	}
	if (ulFlags & GMPROC_USER) {
		if (UsrGetUserPath(pUI, szMPFilePath, sizeof(szMPFilePath), 1) == NULL)
			return ErrGetErrorCode();
		StrNCat(szMPFilePath, MAILPROCESS_FILE, sizeof(szMPFilePath));

		if (SysExistFile(szMPFilePath)) {
			char szResLock[SYS_MAX_PATH] = "";
			RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szMPFilePath, szResLock,
									  sizeof(szResLock)));

			if (hResLock == INVALID_RLCK_HANDLE)
				return ErrGetErrorCode();

			if (MscAppendFile(pszMPPath, szMPFilePath) < 0) {
				ErrorPush();
				CheckRemoveFile(pszMPPath);
				RLckUnlockSH(hResLock);
				return ErrorPop();
			}
			RLckUnlockSH(hResLock);
			iAppendFiles++;
		}
	}
	if (ulFlags && !iAppendFiles) {
		ErrSetErrorCode(ERR_NO_MAILPROC_FILE);
		return ERR_NO_MAILPROC_FILE;
	}

	return 0;
}

int UsrSetMailProcessFile(UserInfo *pUI, const char *pszMPPath, int iWhich)
{
	char szMPFilePath[SYS_MAX_PATH] = "";

	if (iWhich == GMPROC_DOMAIN) {
		if (MDomGetDomainPath(pUI->pszDomain, szMPFilePath, sizeof(szMPFilePath) - 1,
				      1) == NULL)
			return ErrGetErrorCode();
	} else if (iWhich == GMPROC_USER) {
		if (UsrGetUserPath(pUI, szMPFilePath, sizeof(szMPFilePath), 1) == NULL)
			return ErrGetErrorCode();
	} else {
		ErrSetErrorCode(ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}
	StrNCat(szMPFilePath, MAILPROCESS_FILE, sizeof(szMPFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMPFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (pszMPPath != NULL) {
		if (MscCopyFile(szMPFilePath, pszMPPath) < 0) {
			ErrorPush();
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
	} else
		SysRemove(szMPFilePath);

	RLckUnlockEX(hResLock);

	return 0;
}

char *UsrGetAddress(UserInfo *pUI, char *pszAddress)
{
	sprintf(pszAddress, "%s@%s", pUI->pszName, pUI->pszDomain);

	return pszAddress;
}

int UsrGetAliasDBFileSnapShot(const char *pszFileName)
{
	char szAlsFilePath[SYS_MAX_PATH] = "";

	UsrGetAliasFilePath(szAlsFilePath, sizeof(szAlsFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szAlsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (MscCopyFile(pszFileName, szAlsFilePath) < 0) {
		RLckUnlockSH(hResLock);
		return ErrGetErrorCode();
	}
	RLckUnlockSH(hResLock);

	return 0;
}

ALSF_HANDLE UsrAliasOpenDB(void)
{
	AliasDBScanData *pADBSD = (AliasDBScanData *) SysAlloc(sizeof(AliasDBScanData));

	if (pADBSD == NULL)
		return INVALID_ALSF_HANDLE;

	SysGetTmpFile(pADBSD->szTmpDBFile);
	if (UsrGetAliasDBFileSnapShot(pADBSD->szTmpDBFile) < 0) {
		SysFree(pADBSD);
		return INVALID_ALSF_HANDLE;
	}
	if ((pADBSD->pDBFile = fopen(pADBSD->szTmpDBFile, "rt")) == NULL) {
		SysRemove(pADBSD->szTmpDBFile);
		SysFree(pADBSD);
		return INVALID_ALSF_HANDLE;
	}

	return (ALSF_HANDLE) pADBSD;
}

void UsrAliasCloseDB(ALSF_HANDLE hAliasDB)
{
	AliasDBScanData *pADBSD = (AliasDBScanData *) hAliasDB;

	fclose(pADBSD->pDBFile);
	SysRemove(pADBSD->szTmpDBFile);
	SysFree(pADBSD);
}

AliasInfo *UsrAliasGetFirst(ALSF_HANDLE hAliasDB)
{
	AliasDBScanData *pADBSD = (AliasDBScanData *) hAliasDB;

	rewind(pADBSD->pDBFile);

	AliasInfo *pAI = NULL;
	char szUsrLine[USR_ALIAS_LINE_MAX] = "";

	while ((pAI == NULL) &&
	       (MscGetConfigLine(szUsrLine, sizeof(szUsrLine) - 1, pADBSD->pDBFile) != NULL)) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= alsMax)
			pAI =
				UsrAllocAlias(ppszStrings[alsDomain], ppszStrings[alsAlias],
					      ppszStrings[alsName]);

		StrFreeStrings(ppszStrings);
	}

	return pAI;
}

AliasInfo *UsrAliasGetNext(ALSF_HANDLE hAliasDB)
{
	AliasDBScanData *pADBSD = (AliasDBScanData *) hAliasDB;

	AliasInfo *pAI = NULL;
	char szUsrLine[USR_ALIAS_LINE_MAX] = "";

	while (pAI == NULL &&
	       MscGetConfigLine(szUsrLine, sizeof(szUsrLine) - 1, pADBSD->pDBFile) != NULL) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szUsrLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= alsMax)
			pAI = UsrAllocAlias(ppszStrings[alsDomain], ppszStrings[alsAlias],
					    ppszStrings[alsName]);

		StrFreeStrings(ppszStrings);
	}

	return pAI;
}

