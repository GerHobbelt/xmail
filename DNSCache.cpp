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
#include "BuffSock.h"
#include "SList.h"
#include "MailConfig.h"
#include "MessQueue.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"
#include "DNS.h"
#include "DNSCache.h"

#define DNS_CACHE_DIRCTORY      "dnscache"
#define DNS_MX_CACHE_DIRCTORY   "mx"
#define DNS_NS_CACHE_DIRCTORY   "ns"
#define DNS_CACHE_LINE_MAX      2048

static int CDNS_CleanupPath(char const *pszCachePath);
static char *CDNS_GetCacheFilePath(char const *pszDomain, char const *pszSubDir,
				   char *pszFilePath);
static int CDNS_MxLoad(char const *pszDomain, char *&pszMXDomains);
static int CDNS_MxSave(char const *pszDomain, char const *pszMXDomains, SYS_UINT32 TTL);
static int CDNS_BuildStrMX(DNSAnswer *pAns, char *&pszMXDomains, SYS_UINT32 *pTTL);
static int CDNS_QueryMX(char const *pszDomain, char *&pszMXDomains, SYS_UINT32 *pTTL);
static int CDNS_QueryDirectMX(char const *pszDNSServer, char const *pszDomain,
			      int iQuerySockType, char *&pszMXDomains, SYS_UINT32 *pTTL);

static int iNumCacheDirs = DNS_HASH_NUM_DIRS;

static int CDNS_CleanupPath(char const *pszCachePath)
{
	return 0;
}

int CDNS_Initialize(int iCacheDirCount)
{
	while (!IsPrimeNumber(iCacheDirCount))
		++iCacheDirCount;

	iNumCacheDirs = iCacheDirCount;

	/* Setup cache subdirectories */
	char szCacheBasePath[SYS_MAX_PATH] = "";

	CfgGetRootPath(szCacheBasePath, sizeof(szCacheBasePath));

	StrNCat(szCacheBasePath, DNS_CACHE_DIRCTORY, sizeof(szCacheBasePath));
	AppendSlash(szCacheBasePath);

	for (int ii = 0; ii < iNumCacheDirs; ii++) {
		/* Setup MX cache subdirectories */
		char szCachePath[SYS_MAX_PATH] = "";

		sprintf(szCachePath, "%s%s" SYS_SLASH_STR "%d",
			szCacheBasePath, DNS_MX_CACHE_DIRCTORY, ii);

		if (SysExistDir(szCachePath)) {

			if (CDNS_CleanupPath(szCachePath) < 0)
				return ErrGetErrorCode();

		} else {

			if (SysMakeDir(szCachePath) < 0)
				return ErrGetErrorCode();

		}

		/* Setup NS cache subdirectories */

	}

	return 0;
}

static char *CDNS_GetCacheFilePath(char const *pszDomain, char const *pszSubDir,
				   char *pszFilePath)
{
	char szRootPath[SYS_MAX_PATH] = "";

	CfgGetRootPath(szRootPath, sizeof(szRootPath));

	/* Calculate domain string hash */
	char *pszLwrDomain = SysStrDup(pszDomain);

	if (pszLwrDomain == NULL)
		return NULL;

	StrLower(pszLwrDomain);

	unsigned long ulStringHash = MscHashString(pszLwrDomain, (int)strlen(pszLwrDomain));

	/* Build cache file path */
	SysSNPrintf(pszFilePath, SYS_MAX_PATH - 1,
		    "%s%s" SYS_SLASH_STR "%s" SYS_SLASH_STR "%u" SYS_SLASH_STR "%s", szRootPath,
		    DNS_CACHE_DIRCTORY, pszSubDir, (unsigned int) (ulStringHash % iNumCacheDirs),
		    pszLwrDomain);

	SysFree(pszLwrDomain);

	return pszFilePath;
}

static int CDNS_MxLoad(char const *pszDomain, char *&pszMXDomains)
{
	/* Build cached file path */
	char szFilePath[SYS_MAX_PATH] = "";

	if (CDNS_GetCacheFilePath(pszDomain, DNS_MX_CACHE_DIRCTORY, szFilePath) == NULL)
		return ErrGetErrorCode();

	/* Try to get file infos */
	SYS_FILE_INFO FI;

	if (SysGetFileInfo(szFilePath, FI) < 0)
		return ErrGetErrorCode();

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pCacheFile = fopen(szFilePath, "rt");

	if (pCacheFile == NULL) {
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_FILE_OPEN, szFilePath); /* [i_a] */
		return ERR_FILE_OPEN;
	}
	/* Read TTL line ( 1st ) and check if it's time expired */
	char szCacheLine[DNS_CACHE_LINE_MAX] = "";

	if (MscFGets(szCacheLine, sizeof(szCacheLine) - 1, pCacheFile) == NULL) {
		fclose(pCacheFile);
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_DNS_CACHE_FILE_FMT);
		return ERR_DNS_CACHE_FILE_FMT;
	}

	unsigned long ulCurTime = (unsigned long) time(NULL);
	unsigned long ulTTL = (unsigned long) atol(szCacheLine);

	if (ulCurTime > ((unsigned long) FI.tMod + ulTTL)) {
		fclose(pCacheFile);
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_DNS_CACHE_FILE_EXPIRED);
		return ERR_DNS_CACHE_FILE_EXPIRED;
	}
	/* Read MX domains line ( 2nd ) */
	if (MscFGets(szCacheLine, sizeof(szCacheLine) - 1, pCacheFile) == NULL) {
		fclose(pCacheFile);
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_DNS_CACHE_FILE_FMT);
		return ERR_DNS_CACHE_FILE_FMT;
	}
	if ((pszMXDomains = SysStrDup(szCacheLine)) == NULL) {
		ErrorPush();
		fclose(pCacheFile);
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}
	fclose(pCacheFile);
	RLckUnlockSH(hResLock);

	return 0;
}

static int CDNS_MxSave(char const *pszDomain, char const *pszMXDomains, SYS_UINT32 TTL)
{
	/* Build cached file path */
	char szFilePath[SYS_MAX_PATH] = "";

	if (CDNS_GetCacheFilePath(pszDomain, DNS_MX_CACHE_DIRCTORY, szFilePath) == NULL)
		return ErrGetErrorCode();

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pCacheFile = fopen(szFilePath, "wt");

	if (pCacheFile == NULL) {
		RLckUnlockEX(hResLock);

		ErrSetErrorCode(ERR_FILE_CREATE, szFilePath);
		return ERR_FILE_CREATE;
	}
	/* 1st line (TTL) */
	fprintf(pCacheFile, "%lu\n", (unsigned long) TTL);

	/* 2nd line (MX domains) */
	fprintf(pCacheFile, "%s\n", pszMXDomains);

	fclose(pCacheFile);

	RLckUnlockEX(hResLock);

	return 0;
}

static int CDNS_BuildStrMX(DNSAnswer *pAns, char *&pszMXDomains, SYS_UINT32 *pTTL)
{
	SysListHead *pLnk;
	DNSRecord *pRec;
	DynString DynMX;

	if (StrDynInit(&DynMX) < 0)
		return ErrGetErrorCode();
	for (pLnk = SYS_LIST_FIRST(&pAns->RecsLst[QTYPE_MX]); pLnk != NULL;
	     pLnk = SYS_LIST_NEXT(pLnk, &pAns->RecsLst[QTYPE_MX])) {
		pRec = SYS_LIST_ENTRY(pLnk, DNSRecord, Lnk);
		if (StrDynSize(&DynMX) > 0)
			StrDynAdd(&DynMX, ",");
		if (StrDynPrint(&DynMX, "%u:%s", (unsigned int) pRec->U.MX.Pref,
				pRec->szName) < 0) {
			StrDynFree(&DynMX);
			return ErrGetErrorCode();
		}
		*pTTL = pRec->TTL;
	}
	if (StrDynSize(&DynMX) == 0) {
		StrDynFree(&DynMX);
		ErrSetErrorCode(ERR_DNS_NOTFOUND);
		return ERR_DNS_NOTFOUND;
	}
	pszMXDomains = StrDynDrop(&DynMX, NULL);

	return 0;
}

static int CDNS_QueryMX(char const *pszDomain, char *&pszMXDomains, SYS_UINT32 *pTTL)
{
	int iError;
	DNSAnswer Ans;

	if (DNS_Query(pszDomain, QTYPE_MX, &Ans) < 0)
		return ErrGetErrorCode();

	iError = CDNS_BuildStrMX(&Ans, pszMXDomains, pTTL);

	DNS_FreeAnswer(&Ans);

	return iError;
}

static int CDNS_QueryDirectMX(char const *pszDNSServer, char const *pszDomain,
			      int iQuerySockType, char *&pszMXDomains, SYS_UINT32 *pTTL)
{
	int iError;
	DNSAnswer Ans;

	if (DNS_QueryDirect(pszDNSServer, pszDomain, QTYPE_MX, iQuerySockType,
			    &Ans) < 0)
		return ErrGetErrorCode();

	iError = CDNS_BuildStrMX(&Ans, pszMXDomains, pTTL);

	DNS_FreeAnswer(&Ans);

	return iError;
}

int CDNS_GetDomainMX(char const *pszDomain, char *&pszMXDomains, char const *pszSmartDNS)
{
	/* Try to get the cached copy */
	if (CDNS_MxLoad(pszDomain, pszMXDomains) == 0)
		return 0;

	/* If the list of smart DNS hosts is NULL, do a full DNS query */
	SYS_UINT32 TTL = 0;

	if (pszSmartDNS == NULL) {
		if (CDNS_QueryMX(pszDomain, pszMXDomains, &TTL) < 0)
			return ErrGetErrorCode();
		CDNS_MxSave(pszDomain, pszMXDomains, TTL);

		return 0;
	}

	/* Parse the list of smart DNS hosts */
	char **ppszTokens = StrTokenize(pszSmartDNS, ",:");

	if (ppszTokens == NULL)
		return ErrGetErrorCode();

	int iTokensCount = StrStringsCount(ppszTokens);

	if (iTokensCount < 2) {
		StrFreeStrings(ppszTokens);

		ErrSetErrorCode(ERR_BAD_SMARTDNSHOST_SYNTAX);
		return ERR_BAD_SMARTDNSHOST_SYNTAX;
	}
	/* Walk through the list of smart DNS hosts to find a DNS response */
	for (int i = 0; i < (iTokensCount - 1); i += 2) {
		int iType = (stricmp(ppszTokens[i + 1], "tcp") == 0) ?
			DNS_QUERY_TCP: DNS_QUERY_UDP;

		if (CDNS_QueryDirectMX(ppszTokens[i], pszDomain, iType,
				       pszMXDomains, &TTL) == 0) {
			StrFreeStrings(ppszTokens);
			CDNS_MxSave(pszDomain, pszMXDomains, TTL);

			return 0;
		}
	}
	StrFreeStrings(ppszTokens);

	return ErrGetErrorCode();
}

