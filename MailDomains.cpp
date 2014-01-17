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
#include "MessQueue.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"
#include "POP3GwLink.h"
#include "ExtAliases.h"
#include "UsrUtils.h"
#include "UsrAuth.h"
#include "TabIndex.h"
#include "AliasDomain.h"
#include "SMAILUtils.h"
#include "MailDomains.h"

#define MAIL_DOMAINS_DIR            "domains"
#define MAIL_DOMAINS_FILE           "domains.tab"
#define MAIL_DOMAINS_LINE_MAX       512

enum PopDomainFileds {
	domDomain = 0,

	domMax
};

struct DomainsScanData {
	char szTmpDBFile[SYS_MAX_PATH];
	FILE *pDBFile;
	char szCurrDomain[256];
};

static int MDomRebuildDomainsIndexes(char const *pszDomainsFilePath);
static char *MDomGetDomainsFilePath(char *pszDomainsFilePath, int iMaxPath);

static int iIdxDomains_Domain[] = {
	domDomain,

	INDEX_SEQUENCE_TERMINATOR
};

int MDomCheckDomainsIndexes(void)
{
	char szDomainsFilePath[SYS_MAX_PATH] = "";

	MDomGetDomainsFilePath(szDomainsFilePath, sizeof(szDomainsFilePath));

	/* Align RmtDomain-RmtName index */
	if (TbixCheckIndex(szDomainsFilePath, iIdxDomains_Domain, false) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int MDomRebuildDomainsIndexes(char const *pszDomainsFilePath)
{
	/* Rebuild RmtDomain-RmtName index */
	if (TbixCreateIndex(pszDomainsFilePath, iIdxDomains_Domain, false) < 0)
		return ErrGetErrorCode();

	return 0;
}

char *MDomGetDomainPath(char const *pszDomain, char *pszDomainPath, int iMaxPath, int iFinalSlash)
{
	/* Make the domain lower-case */
	char szLoDomain[SYS_MAX_PATH] = "";

	StrSNCpy(szLoDomain, pszDomain);
	StrLower(szLoDomain);

	CfgGetRootPath(pszDomainPath, iMaxPath);

	StrNCat(pszDomainPath, MAIL_DOMAINS_DIR, iMaxPath);
	AppendSlash(pszDomainPath);
	StrNCat(pszDomainPath, szLoDomain, iMaxPath);

	if (iFinalSlash)
		AppendSlash(pszDomainPath);

	return pszDomainPath;
}

static char *MDomGetDomainsFilePath(char *pszDomainsFilePath, int iMaxPath)
{
	CfgGetRootPath(pszDomainsFilePath, iMaxPath);

	StrNCat(pszDomainsFilePath, MAIL_DOMAINS_FILE, iMaxPath);

	return pszDomainsFilePath;
}

int MDomLookupDomain(char const *pszDomain)
{
	char szDomainsFilePath[SYS_MAX_PATH] = "";

	MDomGetDomainsFilePath(szDomainsFilePath, sizeof(szDomainsFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szDomainsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* Lookup record using the specified index */
	char **ppszTabTokens = TbixLookup(szDomainsFilePath, iIdxDomains_Domain, false,
					  pszDomain,
					  NULL);

	if (ppszTabTokens == NULL) {
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_DOMAIN_NOT_HANDLED, pszDomain);
		return ERR_DOMAIN_NOT_HANDLED;
	}
	StrFreeStrings(ppszTabTokens);
	RLckUnlockSH(hResLock);

	return 0;
}

int MDomAddDomain(char const *pszDomain)
{
	char szDomainsFilePath[SYS_MAX_PATH] = "";

	MDomGetDomainsFilePath(szDomainsFilePath, sizeof(szDomainsFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szDomainsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pDomainsFile = fopen(szDomainsFilePath, "r+t");

	if (pDomainsFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_ALIAS_FILE_NOT_FOUND);
		return ERR_ALIAS_FILE_NOT_FOUND;
	}

	char szDomainsLine[MAIL_DOMAINS_LINE_MAX] = "";

	while (MscFGets(szDomainsLine, sizeof(szDomainsLine) - 1, pDomainsFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szDomainsLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= domMax) && (stricmp(pszDomain, ppszStrings[domDomain]) == 0)) {
			StrFreeStrings(ppszStrings);
			fclose(pDomainsFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_DOMAIN_ALREADY_HANDLED);
			return ERR_DOMAIN_ALREADY_HANDLED;
		}
		StrFreeStrings(ppszStrings);
	}
	fseek(pDomainsFile, 0, SEEK_END);

	fprintf(pDomainsFile, "\"%s\"\n", pszDomain);

	fclose(pDomainsFile);

	/* Rebuild indexes */
	if (MDomRebuildDomainsIndexes(szDomainsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	/* Create domain directory */
	char szDomainPath[SYS_MAX_PATH] = "";

	MDomGetDomainPath(pszDomain, szDomainPath, sizeof(szDomainPath), 0);

	if (SysMakeDir(szDomainPath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	/* Create cmd alias directory */
	if (USmlCreateCmdAliasDomainDir(pszDomain) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int MDomRemoveDomain(char const *pszDomain)
{
	char szDomainsFilePath[SYS_MAX_PATH] = "";

	MDomGetDomainsFilePath(szDomainsFilePath, sizeof(szDomainsFilePath));

	char szTmpFile[SYS_MAX_PATH] = "";

	UsrGetTmpFile(NULL, szTmpFile, sizeof(szTmpFile));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szDomainsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CheckRemoveFile(szTmpFile);
		return ErrorPop();
	}

	FILE *pDomainsFile = fopen(szDomainsFilePath, "rt");

	if (pDomainsFile == NULL) {
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_DOMAINS_FILE_NOT_FOUND);
		return ERR_DOMAINS_FILE_NOT_FOUND;
	}

	FILE *pTmpFile = fopen(szTmpFile, "wt");

	if (pTmpFile == NULL) {
		fclose(pDomainsFile);
		RLckUnlockEX(hResLock);
		CheckRemoveFile(szTmpFile);

		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	int iDomainsFound = 0;
	char szDomainsLine[MAIL_DOMAINS_LINE_MAX] = "";

	while (MscFGets(szDomainsLine, sizeof(szDomainsLine) - 1, pDomainsFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szDomainsLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= domMax) && (stricmp(pszDomain, ppszStrings[domDomain]) == 0)) {
			++iDomainsFound;
		} else
			fprintf(pTmpFile, "%s\n", szDomainsLine);
		StrFreeStrings(ppszStrings);
	}
	fclose(pDomainsFile);
	fclose(pTmpFile);

	if (iDomainsFound == 0) {
		SysRemove(szTmpFile);
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_DOMAIN_NOT_HANDLED);
		return ERR_DOMAIN_NOT_HANDLED;
	}
	if (MscMoveFile(szTmpFile, szDomainsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	/* Rebuild indexes */
	if (MDomRebuildDomainsIndexes(szDomainsFilePath) < 0) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	RLckUnlockEX(hResLock);

	/* Domain cleanup */
	if (UsrRemoveDomainUsers(pszDomain) < 0 ||
	    UsrRemoveDomainAliases(pszDomain) < 0 ||
	    ExAlRemoveDomainAliases(pszDomain) < 0 ||
	    GwLkRemoveDomainLinks(pszDomain) < 0 ||
	    ADomRemoveLinkedDomains(pszDomain) < 0)
		return ErrGetErrorCode();

	/* Try ( if defined ) to drop external auth domain */
	UAthDropDomain(AUTH_SERVICE_POP3, pszDomain);

	/* Directory cleanup */
	char szDomainPath[SYS_MAX_PATH] = "";

	MDomGetDomainPath(pszDomain, szDomainPath, sizeof(szDomainPath), 0);

	if (MscClearDirectory(szDomainPath) < 0)
		return ErrGetErrorCode();

	if (SysRemoveDir(szDomainPath) < 0)
		return ErrGetErrorCode();

	/* Remove the cmd alias directory */
	if (USmlDeleteCmdAliasDomainDir(pszDomain) < 0)
		return ErrGetErrorCode();

	return 0;
}

int MDomGetDomainsFileSnapShot(const char *pszFileName)
{
	char szDomainsFilePath[SYS_MAX_PATH] = "";

	MDomGetDomainsFilePath(szDomainsFilePath, sizeof(szDomainsFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szDomainsFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (MscCopyFile(pszFileName, szDomainsFilePath) < 0) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}
	RLckUnlockSH(hResLock);

	return 0;
}

DOMLS_HANDLE MDomOpenDB(void)
{
	DomainsScanData *pDSD = (DomainsScanData *) SysAlloc(sizeof(DomainsScanData));

	if (pDSD == NULL)
		return INVALID_DOMLS_HANDLE;

	UsrGetTmpFile(NULL, pDSD->szTmpDBFile, sizeof(pDSD->szTmpDBFile));

	if (MDomGetDomainsFileSnapShot(pDSD->szTmpDBFile) < 0) {
		CheckRemoveFile(pDSD->szTmpDBFile);
		SysFree(pDSD);
		return INVALID_DOMLS_HANDLE;
	}

	if ((pDSD->pDBFile = fopen(pDSD->szTmpDBFile, "rt")) == NULL) {
		SysRemove(pDSD->szTmpDBFile);
		SysFree(pDSD);
		return INVALID_DOMLS_HANDLE;
	}

	return (DOMLS_HANDLE) pDSD;
}

void MDomCloseDB(DOMLS_HANDLE hDomainsDB)
{
	DomainsScanData *pDSD = (DomainsScanData *) hDomainsDB;

	fclose(pDSD->pDBFile);
	SysRemove(pDSD->szTmpDBFile);
	SysFree(pDSD);
}

char const *MDomGetFirstDomain(DOMLS_HANDLE hDomainsDB)
{
	DomainsScanData *pDSD = (DomainsScanData *) hDomainsDB;

	rewind(pDSD->pDBFile);

	const char *pszDomain = NULL;
	char szDomainsLine[MAIL_DOMAINS_LINE_MAX] = "";

	while ((pszDomain == NULL) &&
	       (MscFGets(szDomainsLine, sizeof(szDomainsLine) - 1, pDSD->pDBFile) != NULL)) {
		char **ppszStrings = StrGetTabLineStrings(szDomainsLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= domMax) {
			StrSNCpy(pDSD->szCurrDomain, ppszStrings[0]);
			pszDomain = pDSD->szCurrDomain;
		}
		StrFreeStrings(ppszStrings);
	}

	return pszDomain;
}

char const *MDomGetNextDomain(DOMLS_HANDLE hDomainsDB)
{
	DomainsScanData *pDSD = (DomainsScanData *) hDomainsDB;

	const char *pszDomain = NULL;
	char szDomainsLine[MAIL_DOMAINS_LINE_MAX] = "";

	while ((pszDomain == NULL) &&
	       (MscFGets(szDomainsLine, sizeof(szDomainsLine) - 1, pDSD->pDBFile) != NULL)) {
		char **ppszStrings = StrGetTabLineStrings(szDomainsLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= domMax) {
			StrSNCpy(pDSD->szCurrDomain, ppszStrings[0]);
			pszDomain = pDSD->szCurrDomain;
		}
		StrFreeStrings(ppszStrings);
	}

	return pszDomain;
}

int MDomGetClientDomain(char const *pszFQDN, char *pszClientDomain, int iMaxDomain)
{
	for (; pszFQDN != NULL;) {
		if (MDomIsHandledDomain(pszFQDN) == 0) {
			StrNCpy(pszClientDomain, pszFQDN, iMaxDomain);

			return 0;
		}
		if ((pszFQDN = strchr(pszFQDN, '.')) != NULL)
			++pszFQDN;
	}

	ErrSetErrorCode(ERR_NO_HANDLED_DOMAIN);
	return ERR_NO_HANDLED_DOMAIN;
}

int MDomIsHandledDomain(char const *pszDomain)
{
	/* Check for alias domain */
	char szADomain[MAX_HOST_NAME] = "";

	if (ADomLookupDomain(pszDomain, szADomain, true))
		pszDomain = szADomain;

	return MDomLookupDomain(pszDomain);
}

