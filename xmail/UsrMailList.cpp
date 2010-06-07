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
#include "POP3GwLink.h"
#include "UsrMailList.h"

#define MLU_TABLE_LINE_MAX          512

enum MLUsrFileds {
	mlusrAddress = 0,
	mlusrPerms,

	mlusrMax
};

struct MLUsersScanData {
	char szTmpDBFile[SYS_MAX_PATH];
	FILE *pDBFile;
};

static MLUserInfo *UsrMLGetUserFromStrings(char **ppszStrings);
static int UsrMLWriteUser(FILE * pMLUFile, MLUserInfo const *pMLUI);

static MLUserInfo *UsrMLGetUserFromStrings(char **ppszStrings)
{
	int iFieldsCount = StrStringsCount(ppszStrings);

	if (iFieldsCount <= mlusrAddress)
		return NULL;

	MLUserInfo *pMLUI = (MLUserInfo *) SysAlloc(sizeof(MLUserInfo));

	if (pMLUI == NULL)
		return NULL;

	pMLUI->pszAddress = SysStrDup(ppszStrings[mlusrAddress]);

	if (iFieldsCount > mlusrPerms)
		pMLUI->pszPerms = SysStrDup(ppszStrings[mlusrPerms]);
	else
		pMLUI->pszPerms = SysStrDup(DEFAULT_MLUSER_PERMS);

	return pMLUI;
}

MLUserInfo *UsrMLAllocDefault(char const *pszAddress, char const *pszPerms)
{
	MLUserInfo *pMLUI = (MLUserInfo *) SysAlloc(sizeof(MLUserInfo));

	if (pMLUI == NULL)
		return NULL;

	pMLUI->pszAddress = SysStrDup(pszAddress);

	if (pszPerms != NULL)
		pMLUI->pszPerms = SysStrDup(pszPerms);
	else
		pMLUI->pszPerms = SysStrDup(DEFAULT_MLUSER_PERMS);

	return pMLUI;
}

int UsrMLFreeUser(MLUserInfo * pMLUI)
{
	if (pMLUI->pszPerms != NULL)
		SysFree(pMLUI->pszPerms);

	if (pMLUI->pszAddress != NULL)
		SysFree(pMLUI->pszAddress);

	SysFree(pMLUI);

	return 0;
}

int UsrMLCheckUserPost(UserInfo * pUI, char const *pszUser, char const *pszLogonUser)
{
	char *pszClosed = UsrGetUserInfoVar(pUI, "ClosedML");

	if (pszClosed != NULL) {
		int iClosedML = atoi(pszClosed);

		SysFree(pszClosed);

		if (iClosedML) {
			USRML_HANDLE hUsersDB = UsrMLOpenDB(pUI);

			if (hUsersDB == INVALID_USRML_HANDLE)
				return ErrGetErrorCode();

			/* Mailing list scan */
			MLUserInfo *pMLUI = UsrMLGetFirstUser(hUsersDB);

			for (; pMLUI != NULL; pMLUI = UsrMLGetNextUser(hUsersDB)) {
				if (((stricmp(pszUser, pMLUI->pszAddress) == 0) &&
				     (strchr(pMLUI->pszPerms, 'W') != NULL)) ||
				    ((pszLogonUser != NULL) &&
				     (stricmp(pszLogonUser, pMLUI->pszAddress) == 0) &&
				     (strchr(pMLUI->pszPerms, 'A') != NULL))) {
					UsrMLFreeUser(pMLUI);
					UsrMLCloseDB(hUsersDB);

					return 0;
				}

				UsrMLFreeUser(pMLUI);
			}

			UsrMLCloseDB(hUsersDB);

			ErrSetErrorCode(ERR_MLUSER_NOT_FOUND, pszUser);
			return ERR_MLUSER_NOT_FOUND;
		}
	}

	return 0;
}

static int UsrMLWriteUser(FILE * pMLUFile, MLUserInfo const *pMLUI)
{
	/* User email address */
	char *pszQuoted = StrQuote(pMLUI->pszAddress, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pMLUFile, "%s\t", pszQuoted);

	/* User permissions */
	if ((pszQuoted = StrQuote(pMLUI->pszPerms, '"')) == NULL)
		return ErrGetErrorCode();

	fprintf(pMLUFile, "%s\n", pszQuoted);

	SysFree(pszQuoted);

	return 0;
}

int UsrMLAddUser(UserInfo * pUI, MLUserInfo const *pMLUI)
{
	if (UsrGetUserType(pUI) != usrTypeML) {
		ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
		return ERR_USER_NOT_MAILINGLIST;
	}

	char szMLTablePath[SYS_MAX_PATH] = "";

	UsrGetMLTableFilePath(pUI, szMLTablePath, sizeof(szMLTablePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMLTablePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pMLUFile = fopen(szMLTablePath, "r+t");

	if (pMLUFile == NULL) {
		RLckUnlockEX(hResLock);
		ErrSetErrorCode(ERR_NO_USER_MLTABLE_FILE);
		return ERR_NO_USER_MLTABLE_FILE;
	}

	char szMLULine[MLU_TABLE_LINE_MAX] = "";

	while (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szMLULine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= mlusrAddress) &&
		    (stricmp(ppszStrings[mlusrAddress], pMLUI->pszAddress) == 0)) {
			StrFreeStrings(ppszStrings);
			fclose(pMLUFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_MLUSER_ALREADY_EXIST);
			return ERR_MLUSER_ALREADY_EXIST;
		}

		StrFreeStrings(ppszStrings);
	}

	fseek(pMLUFile, 0, SEEK_END);

	if (UsrMLWriteUser(pMLUFile, pMLUI) < 0) {
		fclose(pMLUFile);
		RLckUnlockEX(hResLock);
		return ErrGetErrorCode();
	}

	fclose(pMLUFile);

	RLckUnlockEX(hResLock);

	return 0;
}

int UsrMLRemoveUser(UserInfo * pUI, const char *pszMLUser)
{
	if (UsrGetUserType(pUI) != usrTypeML) {
		ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
		return ERR_USER_NOT_MAILINGLIST;
	}

	char szMLTablePath[SYS_MAX_PATH] = "";

	UsrGetMLTableFilePath(pUI, szMLTablePath, sizeof(szMLTablePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMLTablePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pMLUFile = fopen(szMLTablePath, "rt");

	if (pMLUFile == NULL) {
		RLckUnlockEX(hResLock);
		ErrSetErrorCode(ERR_NO_USER_MLTABLE_FILE);
		return ERR_NO_USER_MLTABLE_FILE;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pMLUFile);
		RLckUnlockEX(hResLock);
		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	int iMLUserFound = 0;
	char szMLULine[MLU_TABLE_LINE_MAX] = "";

	while (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szMLULine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= mlusrAddress) &&
		    (stricmp(ppszStrings[mlusrAddress], pszMLUser) == 0)) {

			++iMLUserFound;

		} else
			fprintf(pTmpFile, "%s\n", szMLULine);

		StrFreeStrings(ppszStrings);
	}

	fclose(pMLUFile);
	fclose(pTmpFile);

	if (iMLUserFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		ErrSetErrorCode(ERR_MLUSER_NOT_FOUND);
		return ERR_MLUSER_NOT_FOUND;
	}
	if (MscMoveFile(szTmpFile, szMLTablePath) < 0) {
		RLckUnlockEX(hResLock);
		return ErrGetErrorCode();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int UsrMLGetUsersFileSnapShot(UserInfo * pUI, const char *pszFileName)
{
	char szMLTablePath[SYS_MAX_PATH] = "";

	UsrGetMLTableFilePath(pUI, szMLTablePath, sizeof(szMLTablePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szMLTablePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (MscCopyFile(pszFileName, szMLTablePath) < 0) {
		RLckUnlockSH(hResLock);
		return ErrGetErrorCode();
	}

	RLckUnlockSH(hResLock);

	return 0;
}

USRML_HANDLE UsrMLOpenDB(UserInfo * pUI)
{
	MLUsersScanData *pMLUSD = (MLUsersScanData *) SysAlloc(sizeof(MLUsersScanData));

	if (pMLUSD == NULL)
		return INVALID_USRML_HANDLE;

	SysGetTmpFile(pMLUSD->szTmpDBFile);

	if (UsrMLGetUsersFileSnapShot(pUI, pMLUSD->szTmpDBFile) < 0) {
		SysFree(pMLUSD);
		return INVALID_USRML_HANDLE;
	}

	if ((pMLUSD->pDBFile = fopen(pMLUSD->szTmpDBFile, "rt")) == NULL) {
		SysRemove(pMLUSD->szTmpDBFile);
		SysFree(pMLUSD);
		return INVALID_USRML_HANDLE;
	}

	return (USRML_HANDLE) pMLUSD;
}

void UsrMLCloseDB(USRML_HANDLE hUsersDB)
{
	MLUsersScanData *pMLUSD = (MLUsersScanData *) hUsersDB;

	fclose(pMLUSD->pDBFile);

	SysRemove(pMLUSD->szTmpDBFile);

	SysFree(pMLUSD);

}

MLUserInfo *UsrMLGetFirstUser(USRML_HANDLE hUsersDB)
{
	MLUsersScanData *pMLUSD = (MLUsersScanData *) hUsersDB;

	rewind(pMLUSD->pDBFile);

	char szMLULine[MLU_TABLE_LINE_MAX] = "";

	while (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUSD->pDBFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szMLULine);

		if (ppszStrings == NULL)
			continue;

		MLUserInfo *pMLUI = UsrMLGetUserFromStrings(ppszStrings);

		if (pMLUI != NULL) {
			StrFreeStrings(ppszStrings);

			return pMLUI;
		}

		StrFreeStrings(ppszStrings);
	}

	return NULL;
}

MLUserInfo *UsrMLGetNextUser(USRML_HANDLE hUsersDB)
{
	MLUsersScanData *pMLUSD = (MLUsersScanData *) hUsersDB;
	char szMLULine[MLU_TABLE_LINE_MAX] = "";

	while (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUSD->pDBFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szMLULine);

		if (ppszStrings == NULL)
			continue;

		MLUserInfo *pMLUI = UsrMLGetUserFromStrings(ppszStrings);

		if (pMLUI != NULL) {
			StrFreeStrings(ppszStrings);

			return pMLUI;
		}

		StrFreeStrings(ppszStrings);
	}

	return NULL;
}
