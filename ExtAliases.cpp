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
#include "MessQueue.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "TabIndex.h"
#include "ExtAliases.h"

#define SVR_EXT_ALIAS_FILE          "extaliases.tab"
#define ALIAS_TABLE_LINE_MAX        1024

enum ExtAliasFields {
	ealRmtDomain = 0,
	ealRmtName,
	ealDomain,
	ealName,

	ealMax
};

struct ExAlDBScanData {
	char szTmpDBFile[SYS_MAX_PATH];
	FILE *pDBFile;
};

static int ExAlRebuildAliasIndexes(char const *pszAliasFilePath);
static char *ExAlGetTableFilePath(char *pszLnkFilePath, int iMaxPath);
static ExtAlias *ExAlGetAliasFromStrings(char **ppszStrings);
static int ExAlWriteAlias(FILE * pAliasFile, ExtAlias * pExtAlias);

static int iIdxExAlias_RmtDomain_RmtName[] = {
	ealRmtDomain,
	ealRmtName,

	INDEX_SEQUENCE_TERMINATOR
};

int ExAlCheckAliasIndexes(void)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));

	/* Align RmtDomain-RmtName index */
	if (TbixCheckIndex(szAliasFilePath, iIdxExAlias_RmtDomain_RmtName, false) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int ExAlRebuildAliasIndexes(char const *pszAliasFilePath)
{
	/* Rebuild RmtDomain-RmtName index */
	if (TbixCreateIndex(pszAliasFilePath, iIdxExAlias_RmtDomain_RmtName, false) < 0)
		return ErrGetErrorCode();

	return 0;
}

static char *ExAlGetTableFilePath(char *pszLnkFilePath, int iMaxPath)
{
	CfgGetRootPath(pszLnkFilePath, iMaxPath);
	StrNCat(pszLnkFilePath, SVR_EXT_ALIAS_FILE, iMaxPath);

	return pszLnkFilePath;
}

static ExtAlias *ExAlGetAliasFromStrings(char **ppszStrings)
{
	int iFieldsCount = StrStringsCount(ppszStrings);

	if (iFieldsCount < ealMax)
		return NULL;

	ExtAlias *pExtAlias = (ExtAlias *) SysAlloc(sizeof(ExtAlias));

	if (pExtAlias == NULL)
		return NULL;

	pExtAlias->pszRmtDomain = SysStrDup(ppszStrings[ealRmtDomain]);
	pExtAlias->pszRmtName = SysStrDup(ppszStrings[ealRmtName]);
	pExtAlias->pszDomain = SysStrDup(ppszStrings[ealDomain]);
	pExtAlias->pszName = SysStrDup(ppszStrings[ealName]);

	return pExtAlias;
}

ExtAlias *ExAlAllocAlias(void)
{
	ExtAlias *pExtAlias = (ExtAlias *) SysAlloc(sizeof(ExtAlias));

	if (pExtAlias == NULL)
		return NULL;

	pExtAlias->pszRmtDomain = NULL;
	pExtAlias->pszRmtName = NULL;
	pExtAlias->pszDomain = NULL;
	pExtAlias->pszName = NULL;

	return pExtAlias;
}

void ExAlFreeAlias(ExtAlias * pExtAlias)
{
	SysFree(pExtAlias->pszDomain);
	SysFree(pExtAlias->pszName);
	SysFree(pExtAlias->pszRmtDomain);
	SysFree(pExtAlias->pszRmtName);
	SysFree(pExtAlias);
}

static int ExAlWriteAlias(FILE * pAliasFile, ExtAlias * pExtAlias)
{
	/* Remote domain */
	char *pszQuoted = StrQuote(pExtAlias->pszRmtDomain, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAliasFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Remote user */
	pszQuoted = StrQuote(pExtAlias->pszRmtName, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAliasFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Domain */
	pszQuoted = StrQuote(pExtAlias->pszDomain, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAliasFile, "%s\t", pszQuoted);

	SysFree(pszQuoted);

	/* Local user */
	pszQuoted = StrQuote(pExtAlias->pszName, '"');

	if (pszQuoted == NULL)
		return ErrGetErrorCode();

	fprintf(pAliasFile, "%s\n", pszQuoted);

	SysFree(pszQuoted);

	return 0;
}

int ExAlAddAlias(ExtAlias * pExtAlias)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAliasFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pAliasFile = fopen(szAliasFilePath, "r+t");

	if (pAliasFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_EXTALIAS_FILE_NOT_FOUND);
		return ERR_EXTALIAS_FILE_NOT_FOUND;
	}

	char szAliasLine[ALIAS_TABLE_LINE_MAX] = "";

	while (MscFGets(szAliasLine, sizeof(szAliasLine) - 1, pAliasFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAliasLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= ealMax &&
		    stricmp(pExtAlias->pszRmtDomain, ppszStrings[ealRmtDomain]) == 0 &&
		    stricmp(pExtAlias->pszRmtName, ppszStrings[ealRmtName]) == 0) {
			StrFreeStrings(ppszStrings);
			fclose(pAliasFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_EXTALIAS_EXIST);
			return ERR_EXTALIAS_EXIST;
		}
		StrFreeStrings(ppszStrings);
	}
	fseek(pAliasFile, 0, SEEK_END);
	if (ExAlWriteAlias(pAliasFile, pExtAlias) < 0) {
		fclose(pAliasFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_WRITE_EXTALIAS_FILE);
		return ERR_WRITE_EXTALIAS_FILE;
	}
	fclose(pAliasFile);

	/* Rebuild indexes */
	if (ExAlRebuildAliasIndexes(szAliasFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	RLckUnlockEX(hResLock);

	return 0;
}

ExtAlias *ExAlGetAlias(char const *pszRmtDomain, char const *pszRmtName)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szAliasFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return NULL;

	/* Lookup record using the specified index */
	char **ppszTabTokens = TbixLookup(szAliasFilePath, iIdxExAlias_RmtDomain_RmtName, false,
					  pszRmtDomain,
					  pszRmtName,
					  NULL);

	if (ppszTabTokens == NULL) {
		RLckUnlockSH(hResLock);
		ErrSetErrorCode(ERR_EXTALIAS_NOT_FOUND);

		return NULL;
	}

	ExtAlias *pExtAlias = ExAlGetAliasFromStrings(ppszTabTokens);

	StrFreeStrings(ppszTabTokens);
	RLckUnlockSH(hResLock);

	return pExtAlias;
}

int ExAlRemoveAlias(ExtAlias * pExtAlias)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";
	char szTmpFile[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));
	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAliasFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pAliasFile = fopen(szAliasFilePath, "rt");

	if (pAliasFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_EXTALIAS_FILE_NOT_FOUND);
		return ERR_EXTALIAS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pAliasFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	int iAliasFound = 0;
	char szAliasLine[ALIAS_TABLE_LINE_MAX] = "";

	while (MscFGets(szAliasLine, sizeof(szAliasLine) - 1, pAliasFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAliasLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= ealMax &&
		    stricmp(pExtAlias->pszRmtDomain, ppszStrings[ealRmtDomain]) == 0 &&
		    stricmp(pExtAlias->pszRmtName, ppszStrings[ealRmtName]) == 0) {
			++iAliasFound;
		} else
			fprintf(pTmpFile, "%s\n", szAliasLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pAliasFile);
	fclose(pTmpFile);

	if (iAliasFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_EXTALIAS_NOT_FOUND);
		return ERR_EXTALIAS_NOT_FOUND;
	}
	if (MscMoveFile(szTmpFile, szAliasFilePath) < 0) {
		RLckUnlockEX(hResLock);
		return ErrGetErrorCode();
	}

	/* Rebuild indexes */
	if (ExAlRebuildAliasIndexes(szAliasFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int ExAlRemoveUserAliases(const char *pszDomain, const char *pszName)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";
	char szTmpFile[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));
	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAliasFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pAliasFile = fopen(szAliasFilePath, "rt");

	if (pAliasFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_EXTALIAS_FILE_NOT_FOUND);
		return ERR_EXTALIAS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pAliasFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	int iAliasFound = 0;
	char szAliasLine[ALIAS_TABLE_LINE_MAX] = "";

	while (MscFGets(szAliasLine, sizeof(szAliasLine) - 1, pAliasFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAliasLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= ealMax && stricmp(pszDomain, ppszStrings[ealDomain]) == 0 &&
		    stricmp(pszName, ppszStrings[ealName]) == 0) {
			++iAliasFound;
		} else
			fprintf(pTmpFile, "%s\n", szAliasLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pAliasFile);
	fclose(pTmpFile);

	if (iAliasFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		return 0;
	}
	if (MscMoveFile(szTmpFile, szAliasFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (ExAlRebuildAliasIndexes(szAliasFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int ExAlRemoveDomainAliases(const char *pszDomain)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";
	char szTmpFile[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));
	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szAliasFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pAliasFile = fopen(szAliasFilePath, "rt");

	if (pAliasFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_EXTALIAS_FILE_NOT_FOUND);
		return ERR_EXTALIAS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pAliasFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	int iAliasFound = 0;
	char szAliasLine[ALIAS_TABLE_LINE_MAX] = "";

	while (MscFGets(szAliasLine, sizeof(szAliasLine) - 1, pAliasFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAliasLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= ealMax && stricmp(pszDomain, ppszStrings[ealDomain]) == 0) {

			++iAliasFound;
		} else
			fprintf(pTmpFile, "%s\n", szAliasLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pAliasFile);
	fclose(pTmpFile);

	if (iAliasFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		return 0;
	}
	if (MscMoveFile(szTmpFile, szAliasFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (ExAlRebuildAliasIndexes(szAliasFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int ExAlGetDBFileSnapShot(const char *pszFileName)
{
	char szAliasFilePath[SYS_MAX_PATH] = "";

	ExAlGetTableFilePath(szAliasFilePath, sizeof(szAliasFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szAliasFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (MscCopyFile(pszFileName, szAliasFilePath) < 0) {
		RLckUnlockSH(hResLock);
		return ErrGetErrorCode();
	}

	RLckUnlockSH(hResLock);

	return 0;
}

EXAL_HANDLE ExAlOpenDB(void)
{
	ExAlDBScanData *pGLSD = (ExAlDBScanData *) SysAlloc(sizeof(ExAlDBScanData));

	if (pGLSD == NULL)
		return INVALID_EXAL_HANDLE;

	SysGetTmpFile(pGLSD->szTmpDBFile);
	if (ExAlGetDBFileSnapShot(pGLSD->szTmpDBFile) < 0) {
		SysFree(pGLSD);
		return INVALID_EXAL_HANDLE;
	}
	if ((pGLSD->pDBFile = fopen(pGLSD->szTmpDBFile, "rt")) == NULL) {
		SysRemove(pGLSD->szTmpDBFile);
		SysFree(pGLSD);
		return INVALID_EXAL_HANDLE;
	}

	return (EXAL_HANDLE) pGLSD;
}

void ExAlCloseDB(EXAL_HANDLE hLinksDB)
{
	ExAlDBScanData *pGLSD = (ExAlDBScanData *) hLinksDB;

	fclose(pGLSD->pDBFile);
	SysRemove(pGLSD->szTmpDBFile);
	SysFree(pGLSD);
}

ExtAlias *ExAlGetFirstAlias(EXAL_HANDLE hLinksDB)
{
	ExAlDBScanData *pGLSD = (ExAlDBScanData *) hLinksDB;

	rewind(pGLSD->pDBFile);

	ExtAlias *pExtAlias = NULL;
	char szAliasLine[ALIAS_TABLE_LINE_MAX] = "";

	while (pExtAlias == NULL &&
	       MscFGets(szAliasLine, sizeof(szAliasLine) - 1, pGLSD->pDBFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAliasLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= ealMax)
			pExtAlias = ExAlGetAliasFromStrings(ppszStrings);

		StrFreeStrings(ppszStrings);
	}

	return pExtAlias;
}

ExtAlias *ExAlGetNextAlias(EXAL_HANDLE hLinksDB)
{
	ExAlDBScanData *pGLSD = (ExAlDBScanData *) hLinksDB;

	ExtAlias *pExtAlias = NULL;
	char szAliasLine[ALIAS_TABLE_LINE_MAX] = "";

	while (pExtAlias == NULL &&
	       MscFGets(szAliasLine, sizeof(szAliasLine) - 1, pGLSD->pDBFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAliasLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= ealMax)
			pExtAlias = ExAlGetAliasFromStrings(ppszStrings);

		StrFreeStrings(ppszStrings);
	}

	return pExtAlias;
}

