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
#include "POP3GwLink.h"
#include "ExtAliases.h"
#include "UsrUtils.h"
#include "UsrAuth.h"
#include "TabIndex.h"
#include "MailDomains.h"
#include "AliasDomain.h"

#define ADOMAIN_FILE                "aliasdomain.tab"
#define WILD_ADOMAIN_HASH           0
#define ADOMAIN_LINE_MAX            512

struct ADomainScanData {
	char szTmpDBFile[SYS_MAX_PATH];
	FILE *pDBFile;
	char **ppszStrings;
};

static bool ADomIsWildAlias(char const *pszAlias);
static int ADomCalcAliasHash(char const *const *ppszTabTokens, int const *piFieldsIdx,
			     TabIdxUINT *puHashVal, bool bCaseSens);
static int ADomRebuildADomainIndexes(char const *pszADomainFilePath);
static char *ADomGetADomainFilePath(char *pszADomainFilePath, int iMaxPath);
static int ADomLookupDomainLK(const char *pszADomainFilePath, const char *pszADomain,
			      char *pszDomain, bool bWildMatch);

static int iIdxADomain_Alias[] = {
	adomADomain,

	INDEX_SEQUENCE_TERMINATOR
};

static bool ADomIsWildAlias(char const *pszAlias)
{
	return (strchr(pszAlias, '*') != NULL) || (strchr(pszAlias, '?') != NULL);
}

static int ADomCalcAliasHash(char const *const *ppszTabTokens, int const *piFieldsIdx,
			     TabIdxUINT *puHashVal, bool bCaseSens)
{
	/* This will group wild alias ( * ? ) */
	int iFieldsCount = StrStringsCount(ppszTabTokens);

	if (iFieldsCount > adomADomain && ADomIsWildAlias(ppszTabTokens[adomADomain])) {
		*puHashVal = WILD_ADOMAIN_HASH;

		return 0;
	}

	return TbixCalculateHash(ppszTabTokens, piFieldsIdx, puHashVal, bCaseSens);
}

int ADomCheckDomainsIndexes(void)
{
	char szADomainFilePath[SYS_MAX_PATH] = "";

	ADomGetADomainFilePath(szADomainFilePath, sizeof(szADomainFilePath));

	/* Align RmtDomain-RmtName index */
	if (TbixCheckIndex(szADomainFilePath, iIdxADomain_Alias, false, ADomCalcAliasHash) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int ADomRebuildADomainIndexes(char const *pszADomainFilePath)
{
	/* Rebuild RmtDomain-RmtName index */
	if (TbixCreateIndex(pszADomainFilePath, iIdxADomain_Alias, false, ADomCalcAliasHash) < 0)
		return ErrGetErrorCode();

	return 0;
}

static char *ADomGetADomainFilePath(char *pszADomainFilePath, int iMaxPath)
{
	CfgGetRootPath(pszADomainFilePath, iMaxPath);
	StrNCat(pszADomainFilePath, ADOMAIN_FILE, iMaxPath);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at config file: '%s'\n", pszADomainFilePath);

	return pszADomainFilePath;
}

static int ADomLookupDomainLK(const char *pszADomainFilePath, const char *pszADomain,
			      char *pszDomain, bool bWildMatch)
{
	/* Lookup record using the specified index ( lookup precise aliases ) */
	char **ppszTabTokens = TbixLookup(pszADomainFilePath, iIdxADomain_Alias, false,
					  pszADomain,
					  NULL);

	if (ppszTabTokens != NULL) {
		if (pszDomain != NULL)
			StrNCpy(pszDomain, ppszTabTokens[adomDomain], MAX_HOST_NAME);

		StrFreeStrings(ppszTabTokens);

		return 1;
	}
	/* We can stop here if wild alias matching is not required */
	if (!bWildMatch)
		return 0;

	/* Lookup record using the specified index ( lookup wild aliases grouped */
	/* under WILD_ADOMAIN_HASH hash key ) */
	TabIdxUINT uLkHVal = WILD_ADOMAIN_HASH;
	INDEX_HANDLE hIndexLookup = TbixOpenHandle(pszADomainFilePath, iIdxADomain_Alias,
						   &uLkHVal, 1);

	if (hIndexLookup != INVALID_INDEX_HANDLE) {
		char **ppszTabTokens;

		for (ppszTabTokens = TbixFirstRecord(hIndexLookup); ppszTabTokens != NULL;
		     ppszTabTokens = TbixNextRecord(hIndexLookup)) {
			int iFieldsCount = StrStringsCount(ppszTabTokens);

			if (iFieldsCount >= adomMax &&
			    StrIWildMatch(pszADomain, ppszTabTokens[adomADomain])) {
				if (pszDomain != NULL)
					StrNCpy(pszDomain, ppszTabTokens[adomDomain],
						MAX_HOST_NAME);

				StrFreeStrings(ppszTabTokens);
				TbixCloseHandle(hIndexLookup);

				return 1;
			}
			StrFreeStrings(ppszTabTokens);
		}
		TbixCloseHandle(hIndexLookup);
	}

	return 0;
}

int ADomLookupDomain(const char *pszADomain, char *pszDomain, bool bWildMatch)
{
	char szADomainFilePath[SYS_MAX_PATH] = "";

	ADomGetADomainFilePath(szADomainFilePath, sizeof(szADomainFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szADomainFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return 0;

	int iLookupResult = ADomLookupDomainLK(szADomainFilePath, pszADomain,
					       pszDomain, bWildMatch);

	RLckUnlockSH(hResLock);

	return iLookupResult;
}

int ADomAddADomain(char const *pszADomain, char const *pszDomain)
{
	char szADomainFilePath[SYS_MAX_PATH] = "";

	ADomGetADomainFilePath(szADomainFilePath, sizeof(szADomainFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szADomainFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pDomainsFile = fopen(szADomainFilePath, "r+t");

	if (pDomainsFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_ADOMAIN_FILE_NOT_FOUND, szADomainFilePath);
		return ERR_ADOMAIN_FILE_NOT_FOUND;
	}

	char szADomainLine[ADOMAIN_LINE_MAX] = "";

	while (MscGetConfigLine(szADomainLine, sizeof(szADomainLine) - 1, pDomainsFile) != NULL) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szADomainLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= adomMax &&
		    stricmp(pszADomain, ppszStrings[adomADomain]) == 0) {
			StrFreeStrings(ppszStrings);
			fclose(pDomainsFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_ADOMAIN_EXIST);
			return ERR_ADOMAIN_EXIST;
		}

		StrFreeStrings(ppszStrings);
	}
	fseek(pDomainsFile, 0, SEEK_END);
	fprintf(pDomainsFile, "\"%s\"\t\"%s\"\n", pszADomain, pszDomain);
	fclose(pDomainsFile);

	/* Rebuild indexes */
	if (ADomRebuildADomainIndexes(szADomainFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int ADomRemoveADomain(char const *pszADomain)
{
	char szADomainFilePath[SYS_MAX_PATH] = "";
	char szTmpFile[SYS_MAX_PATH] = "";

	ADomGetADomainFilePath(szADomainFilePath, sizeof(szADomainFilePath));
	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szADomainFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		SysRemove(szTmpFile);
		return ErrorPop();
	}

	FILE *pDomainsFile = fopen(szADomainFilePath, "rt");

	if (pDomainsFile == NULL) {
		RLckUnlockEX(hResLock);
		SysRemove(szTmpFile);

		ErrSetErrorCode(ERR_ADOMAIN_FILE_NOT_FOUND, szADomainFilePath);
		return ERR_ADOMAIN_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pDomainsFile);
		RLckUnlockEX(hResLock);
		SysRemove(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile);
		return ERR_FILE_CREATE;
	}

	int iADomainFound = 0;
	char szADomainLine[ADOMAIN_LINE_MAX] = "";

	/* [i_a] every config file supports comments */
	while (MscFGets(szADomainLine, sizeof(szADomainLine) - 1, pDomainsFile) != NULL) {
		if (szADomainLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szADomainLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szADomainLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szADomainLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= adomMax &&
		    stricmp(pszADomain, ppszStrings[adomADomain]) == 0) {
			++iADomainFound;
		} else
			fprintf(pTmpFile, "%s\n", szADomainLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pDomainsFile);
	fclose(pTmpFile);

	if (iADomainFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_ADOMAIN_NOT_FOUND);
		return ERR_ADOMAIN_NOT_FOUND;
	}
	if (MscMoveFile(szTmpFile, szADomainFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (ADomRebuildADomainIndexes(szADomainFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int ADomRemoveLinkedDomains(char const *pszDomain)
{
	char szADomainFilePath[SYS_MAX_PATH] = "";
	char szTmpFile[SYS_MAX_PATH] = "";

	ADomGetADomainFilePath(szADomainFilePath, sizeof(szADomainFilePath));
	SysGetTmpFile(szTmpFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szADomainFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		SysRemove(szTmpFile);
		return ErrorPop();
	}

	FILE *pDomainsFile = fopen(szADomainFilePath, "rt");

	if (pDomainsFile == NULL) {
		RLckUnlockEX(hResLock);
		SysRemove(szTmpFile);

		ErrSetErrorCode(ERR_ADOMAIN_FILE_NOT_FOUND, szADomainFilePath);
		return ERR_ADOMAIN_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pDomainsFile);
		RLckUnlockEX(hResLock);
		SysRemove(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile);
		return ERR_FILE_CREATE;
	}

	int iDomainFound = 0;
	char szADomainLine[ADOMAIN_LINE_MAX] = "";

	/* [i_a] every config file supports comments */
	while (MscFGets(szADomainLine, sizeof(szADomainLine) - 1, pDomainsFile) != NULL) {
		if (szADomainLine[0] == TAB_COMMENT_CHAR) {
			fprintf(pTmpFile, "%s\n", szADomainLine);
			continue;
		}

		char **ppszStrings = StrGetTabLineStrings(szADomainLine);

		if (ppszStrings == NULL) {
			fprintf(pTmpFile, "%s\n", szADomainLine);
			continue;
		}

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= adomMax &&
		    stricmp(pszDomain, ppszStrings[adomDomain]) == 0) {

			++iDomainFound;
		} else
			fprintf(pTmpFile, "%s\n", szADomainLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pDomainsFile);
	fclose(pTmpFile);

	if (iDomainFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);
		return 0;
	}
	if (MscMoveFile(szTmpFile, szADomainFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (ADomRebuildADomainIndexes(szADomainFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int ADomGetADomainFileSnapShot(const char *pszFileName)
{
	char szADomainFilePath[SYS_MAX_PATH] = "";

	ADomGetADomainFilePath(szADomainFilePath, sizeof(szADomainFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szADomainFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (MscCopyFile(pszFileName, szADomainFilePath) < 0) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}
	RLckUnlockSH(hResLock);

	return 0;
}

ADOMAIN_HANDLE ADomOpenDB(void)
{
	ADomainScanData *pDSD = (ADomainScanData *) SysAlloc(sizeof(ADomainScanData));

	if (pDSD == NULL)
		return INVALID_ADOMAIN_HANDLE;

	SysGetTmpFile(pDSD->szTmpDBFile);
	if (ADomGetADomainFileSnapShot(pDSD->szTmpDBFile) < 0) {
		CheckRemoveFile(pDSD->szTmpDBFile);
		SysFree(pDSD);
		return INVALID_ADOMAIN_HANDLE;
	}
	if ((pDSD->pDBFile = fopen(pDSD->szTmpDBFile, "rt")) == NULL) {
		SysRemove(pDSD->szTmpDBFile);
		SysFree(pDSD);
		return INVALID_ADOMAIN_HANDLE;
	}

	pDSD->ppszStrings = NULL;

	return (ADOMAIN_HANDLE) pDSD;
}

void ADomCloseDB(ADOMAIN_HANDLE hDomainsDB)
{
	ADomainScanData *pDSD = (ADomainScanData *) hDomainsDB;

	fclose(pDSD->pDBFile);
	SysRemove(pDSD->szTmpDBFile);
	if (pDSD->ppszStrings != NULL)
		StrFreeStrings(pDSD->ppszStrings);
	SysFree(pDSD);
}

char const *const *ADomGetFirstDomain(ADOMAIN_HANDLE hDomainsDB)
{
	ADomainScanData *pDSD = (ADomainScanData *) hDomainsDB;

	rewind(pDSD->pDBFile);

	if (pDSD->ppszStrings != NULL)
		StrFreeStrings(pDSD->ppszStrings), pDSD->ppszStrings = NULL;

	char szADomainLine[ADOMAIN_LINE_MAX] = "";

	while (MscGetConfigLine(szADomainLine, sizeof(szADomainLine) - 1, pDSD->pDBFile) != NULL) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szADomainLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= adomMax)
			return pDSD->ppszStrings = ppszStrings;

		StrFreeStrings(ppszStrings);
	}

	return NULL;
}

char const *const *ADomGetNextDomain(ADOMAIN_HANDLE hDomainsDB)
{
	ADomainScanData *pDSD = (ADomainScanData *) hDomainsDB;

	if (pDSD->ppszStrings != NULL)
		StrFreeStrings(pDSD->ppszStrings), pDSD->ppszStrings = NULL;

	char szADomainLine[ADOMAIN_LINE_MAX] = "";

	while (MscGetConfigLine(szADomainLine, sizeof(szADomainLine) - 1, pDSD->pDBFile) != NULL) {  /* [i_a] */
		char **ppszStrings = StrGetTabLineStrings(szADomainLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= adomMax)
			return pDSD->ppszStrings = ppszStrings;

		StrFreeStrings(ppszStrings);
	}

	return NULL;
}

