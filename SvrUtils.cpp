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
#include "Hash.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "SMAILSvr.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "MiscUtils.h"

#define SVR_PROFILE_FILE            "server.tab"
#define MESSAGEID_FILE              "message.id"
#define SMTP_SPOOL_DIR              "spool"
#define SVR_PROFILE_LINE_MAX        2048
#define SYS_RES_CHECK_INTERVAL      8
#define SVR_CFGHASH_INITSIZE        32

struct ServerInfoVar {
	HashNode HN;
	char *pszValue;
};

struct ServerConfigData {
	RLCK_HANDLE hResLock;
	int iWriteLock;
	HASH_HANDLE hHash;
};

static char *SvrGetProfileFilePath(char *pszFilePath, int iMaxPath);
static void SvrFreeConfigVar(ServerInfoVar *pSIV);
static void SvrHFreeConfigVar(void *pPrivate, HashNode *pHN);
static ServerConfigData *SvrAllocConfig(RLCK_HANDLE hResLock, int iWriteLock);
static ServerInfoVar *SvrAllocVar(const char *pszName, const char *pszValue);
static ServerInfoVar *SvrGetUserVar(HASH_HANDLE hHash, const char *pszName);
static int SvrWriteConfig(HASH_HANDLE hHash, FILE *pFile);
static int SvrReadConfig(HASH_HANDLE hHash, const char *pszFilePath);
static char *SvrGetProtoIPPropFile(const char *pszProto, char *pszFileName, int iMaxName);
static char *SvrGetProtoHNPropFile(const char *pszProto, char *pszFileName, int iMaxName);

static char *SvrGetProfileFilePath(char *pszFilePath, int iMaxPath)
{
	CfgGetRootPath(pszFilePath, iMaxPath);

	StrNCat(pszFilePath, SVR_PROFILE_FILE, iMaxPath);

	return pszFilePath;
}

static void SvrFreeConfigVar(ServerInfoVar *pSIV)
{
	SysFree(pSIV->HN.Key.pData);
	SysFree(pSIV->pszValue);
	SysFree(pSIV);
}

static void SvrHFreeConfigVar(void *pPrivate, HashNode *pHN)
{
	ServerInfoVar *pSIV = SYS_LIST_ENTRY(pHN, ServerInfoVar, HN);

	SvrFreeConfigVar(pSIV);
}

static ServerConfigData *SvrAllocConfig(RLCK_HANDLE hResLock, int iWriteLock,
					const char *pszProfilePath)
{
	ServerConfigData *pSCD;

	if ((pSCD = (ServerConfigData *) SysAlloc(sizeof(ServerConfigData))) == NULL)
		return NULL;
	pSCD->hResLock = hResLock;
	pSCD->iWriteLock = iWriteLock;
	if ((pSCD->hHash = HashCreate(SVR_CFGHASH_INITSIZE)) == INVALID_HASH_HANDLE) {
		SysFree(pSCD);
		return NULL;
	}
	if (SvrReadConfig(pSCD->hHash, pszProfilePath) < 0) {
		HashFree(pSCD->hHash, SvrHFreeConfigVar, NULL);
		SysFree(pSCD);
		return NULL;
	}

	return pSCD;
}

SVRCFG_HANDLE SvrGetConfigHandle(int iWriteLock)
{
	RLCK_HANDLE hResLock;
	ServerConfigData *pSCD;
	char szProfilePath[SYS_MAX_PATH];
	char szResLock[SYS_MAX_PATH];

	SvrGetProfileFilePath(szProfilePath, sizeof(szProfilePath));
	if (iWriteLock) {
		if ((hResLock =
		     RLckLockEX(CfgGetBasedPath(szProfilePath, szResLock,
						sizeof(szResLock)))) == INVALID_RLCK_HANDLE)
			return INVALID_SVRCFG_HANDLE;
	} else {
		if ((hResLock =
		     RLckLockSH(CfgGetBasedPath(szProfilePath, szResLock,
						sizeof(szResLock)))) == INVALID_RLCK_HANDLE)
			return INVALID_SVRCFG_HANDLE;
	}
	if ((pSCD = SvrAllocConfig(hResLock, iWriteLock, szProfilePath)) == NULL) {
		if (iWriteLock)
			RLckUnlockEX(hResLock);
		else
			RLckUnlockSH(hResLock);
		return INVALID_SVRCFG_HANDLE;
	}

	return (SVRCFG_HANDLE) pSCD;
}

void SvrReleaseConfigHandle(SVRCFG_HANDLE hSvrConfig)
{
	ServerConfigData *pSCD = (ServerConfigData *) hSvrConfig;

	if (pSCD->iWriteLock)
		RLckUnlockEX(pSCD->hResLock);
	else
		RLckUnlockSH(pSCD->hResLock);
	HashFree(pSCD->hHash, SvrHFreeConfigVar, NULL);
	SysFree(pSCD);
}

char *SvrGetConfigVar(SVRCFG_HANDLE hSvrConfig, const char *pszName, const char *pszDefault)
{
	ServerConfigData *pSCD = (ServerConfigData *) hSvrConfig;
	ServerInfoVar *pSIV;

	if ((pSIV = SvrGetUserVar(pSCD->hHash, pszName)) != NULL)
		return SysStrDup(pSIV->pszValue);

	return pszDefault != NULL ? SysStrDup(pszDefault): NULL;
}

bool SvrTestConfigFlag(const char *pszName, bool bDefault, SVRCFG_HANDLE hSvrConfig)
{
	char szValue[64] = "";

	SvrConfigVar(pszName, szValue, sizeof(szValue) - 1, hSvrConfig, (bDefault) ? "1": "0");

	return atoi(szValue) != 0 ? true: false;
}

int SvrGetConfigInt(const char *pszName, int iDefault, SVRCFG_HANDLE hSvrConfig)
{
	char szValue[64] = "";

	return SvrConfigVar(pszName, szValue, sizeof(szValue) - 1, hSvrConfig, NULL) < 0 ||
		IsEmptyString(szValue) ? iDefault: atoi(szValue);
}

int SysFlushConfig(SVRCFG_HANDLE hSvrConfig)
{
	ServerConfigData *pSCD = (ServerConfigData *) hSvrConfig;
	int iError;
	FILE *pFile;
	char szProfilePath[SYS_MAX_PATH];

	if (!pSCD->iWriteLock) {
		ErrSetErrorCode(ERR_SVR_PRFILE_NOT_LOCKED);
		return ERR_SVR_PRFILE_NOT_LOCKED;
	}

	SvrGetProfileFilePath(szProfilePath, sizeof(szProfilePath));
	if ((pFile = fopen(szProfilePath, "wt")) == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	iError = SvrWriteConfig(pSCD->hHash, pFile);

	fclose(pFile);

	return iError;
}

static ServerInfoVar *SvrAllocVar(const char *pszName, const char *pszValue)
{
	char *pszDName;
	ServerInfoVar *pSIV;

	if ((pSIV = (ServerInfoVar *) SysAlloc(sizeof(ServerInfoVar))) == NULL)
		return NULL;
	HashInitNode(&pSIV->HN);
	pszDName = SysStrDup(pszName);
	DatumStrSet(&pSIV->HN.Key, pszDName);
	pSIV->pszValue = SysStrDup(pszValue);

	return pSIV;
}

static ServerInfoVar *SvrGetUserVar(HASH_HANDLE hHash, const char *pszName)
{
	HashNode *pHNode;
	HashEnum HEnum;
	Datum Key;

	DatumStrSet(&Key, pszName);
	if (HashGetFirst(hHash, &Key, &HEnum, &pHNode) < 0)
		return NULL;

	return SYS_LIST_ENTRY(pHNode, ServerInfoVar, HN);
}

static int SvrWriteConfig(HASH_HANDLE hHash, FILE *pFile)
{
	SysListHead *pPos;
	HashNode *pHNode;
	ServerInfoVar *pSIV;
	char *pszQuoted;

	if (HashFirst(hHash, &pPos, &pHNode) == 0) {
		do {
			pSIV = SYS_LIST_ENTRY(pHNode, ServerInfoVar, HN);

			if ((pszQuoted = StrQuote(pSIV->HN.Key.pData, '"')) == NULL)
				return ErrGetErrorCode();
			fprintf(pFile, "%s\t", pszQuoted);
			SysFree(pszQuoted);

			if ((pszQuoted = StrQuote(pSIV->pszValue, '"')) == NULL)
				return ErrGetErrorCode();
			fprintf(pFile, "%s\n", pszQuoted);
			SysFree(pszQuoted);
		} while (HashNext(hHash, &pPos, &pHNode) == 0);
	}

	return 0;
}

static int SvrReadConfig(HASH_HANDLE hHash, const char *pszFilePath)
{
	FILE *pFile;
	RLCK_HANDLE hResLock;
	char szResLock[SYS_MAX_PATH];
	char szProfileLine[SVR_PROFILE_LINE_MAX];

	if ((hResLock = RLckLockSH(CfgGetBasedPath(pszFilePath, szResLock,
						   sizeof(szResLock)))) == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();
	if ((pFile = fopen(pszFilePath, "rt")) == NULL) {
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_NO_USER_PRFILE, pszFilePath);
		return ERR_NO_USER_PRFILE;
	}
	while (MscGetConfigLine(szProfileLine, sizeof(szProfileLine) - 1,
				pFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szProfileLine);

		if (ppszStrings == NULL)
			continue;
		if (StrStringsCount(ppszStrings) >= 2) {
			ServerInfoVar *pSIV = SvrAllocVar(ppszStrings[0], ppszStrings[1]);

			if (pSIV != NULL && HashAdd(hHash, &pSIV->HN) < 0) {
				SvrFreeConfigVar(pSIV);
				StrFreeStrings(ppszStrings);
				fclose(pFile);
				RLckUnlockSH(hResLock);
				return ErrGetErrorCode();
			}
		}
		StrFreeStrings(ppszStrings);
	}
	fclose(pFile);
	RLckUnlockSH(hResLock);

	return 0;
}

int SvrGetMessageID(SYS_UINT64 *pullMessageID)
{
	char szMsgIDFile[SYS_MAX_PATH] = "";

	CfgGetRootPath(szMsgIDFile, sizeof(szMsgIDFile));
	StrNCat(szMsgIDFile, MESSAGEID_FILE, sizeof(szMsgIDFile));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMsgIDFile, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pMsgIDFile = fopen(szMsgIDFile, "r+b");

	if (pMsgIDFile == NULL) {
		if ((pMsgIDFile = fopen(szMsgIDFile, "wb")) == NULL) {
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_FILE_CREATE, szMsgIDFile);
			return ERR_FILE_CREATE;
		}
		*pullMessageID = 1;
	} else {
		char szMessageID[128] = "";

		if ((MscGetString(pMsgIDFile, szMessageID, sizeof(szMessageID) - 1) == NULL) ||
		    !isdigit(szMessageID[0])) {
			fclose(pMsgIDFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_INVALID_FILE, szMsgIDFile);
			return ERR_INVALID_FILE;
		}
		if (sscanf(szMessageID, SYS_LLU_FMT, pullMessageID) != 1) {
			fclose(pMsgIDFile);
			RLckUnlockEX(hResLock);

			ErrSetErrorCode(ERR_INVALID_FILE, szMsgIDFile);
			return ERR_INVALID_FILE;
		}
	}

	++*pullMessageID;
	fseek(pMsgIDFile, 0, SEEK_SET);
	fprintf(pMsgIDFile, SYS_LLU_FMT "\r\n", *pullMessageID);
	fclose(pMsgIDFile);
	RLckUnlockEX(hResLock);

	return 0;
}

char *SvrGetLogsDir(char *pszLogsPath, int iMaxPath)
{
	CfgGetRootPath(pszLogsPath, iMaxPath);
	StrNCat(pszLogsPath, SVR_LOGS_DIR, iMaxPath);

	return pszLogsPath;
}

char *SvrGetSpoolDir(char *pszSpoolPath, int iMaxPath)
{
	CfgGetRootPath(pszSpoolPath, iMaxPath);
	StrNCat(pszSpoolPath, SMTP_SPOOL_DIR, iMaxPath);

	return pszSpoolPath;
}

int SvrConfigVar(const char *pszVarName, char *pszVarValue, int iMaxVarValue,
		 SVRCFG_HANDLE hSvrConfig, const char *pszDefault)
{
	int iReleaseConfig = 0;

	if (hSvrConfig == INVALID_SVRCFG_HANDLE) {
		if ((hSvrConfig = SvrGetConfigHandle()) == INVALID_SVRCFG_HANDLE)
			return ErrGetErrorCode();

		++iReleaseConfig;
	}

	char *pszValue = SvrGetConfigVar(hSvrConfig, pszVarName, pszDefault);

	if (pszValue == NULL) {
		if (iReleaseConfig)
			SvrReleaseConfigHandle(hSvrConfig);

		ErrSetErrorCode(ERR_CFG_VAR_NOT_FOUND);
		return ERR_CFG_VAR_NOT_FOUND;
	}

	strncpy(pszVarValue, pszValue, iMaxVarValue - 1);
	pszVarValue[iMaxVarValue - 1] = '\0';

	SysFree(pszValue);
	if (iReleaseConfig)
		SvrReleaseConfigHandle(hSvrConfig);

	return 0;
}

int SvrCheckDiskSpace(unsigned long ulMinSpace)
{
	time_t tNow = time(NULL);
	static SYS_INT64 FreeSpace = 0;
	static time_t tLastCheck = 0;

	if (tNow > (tLastCheck + SYS_RES_CHECK_INTERVAL)) {
		SYS_INT64 TotalSpace;
		char szRootDir[SYS_MAX_PATH] = "";

		tLastCheck = tNow;
		CfgGetRootPath(szRootDir, sizeof(szRootDir));
		if (SysGetDiskSpace(szRootDir, &TotalSpace, &FreeSpace) < 0)
			return ErrGetErrorCode();
	}
	if (FreeSpace < (SYS_INT64) ulMinSpace) {
		ErrSetErrorCode(ERR_LOW_DISK_SPACE);
		return ERR_LOW_DISK_SPACE;
	}

	return 0;
}

int SvrCheckVirtMemSpace(unsigned long ulMinSpace)
{
	time_t tNow = time(NULL);
	static SYS_INT64 FreeSpace = 0;
	static time_t tLastCheck = 0;

	if (tNow > tLastCheck + SYS_RES_CHECK_INTERVAL) {
		SYS_INT64 RamTotal, RamFree, VirtTotal;

		tLastCheck = tNow;
		if (SysMemoryInfo(&RamTotal, &RamFree, &VirtTotal, &FreeSpace) < 0)
			return ErrGetErrorCode();
	}
	if (FreeSpace < (SYS_INT64) ulMinSpace) {
		ErrSetErrorCode(ERR_LOW_VM_SPACE);
		return ERR_LOW_VM_SPACE;
	}

	return 0;
}

static char *SvrGetProtoIPPropFile(const char *pszProto, char *pszFileName, int iMaxName)
{
	char szMailRoot[SYS_MAX_PATH] = "";

	CfgGetRootPath(szMailRoot, sizeof(szMailRoot));
	SysSNPrintf(pszFileName, iMaxName, "%s%s.ipprop.tab", szMailRoot, pszProto);

	return pszFileName;
}

static char *SvrGetProtoHNPropFile(const char *pszProto, char *pszFileName, int iMaxName)
{
	char szMailRoot[SYS_MAX_PATH] = "";

	CfgGetRootPath(szMailRoot, sizeof(szMailRoot));
	SysSNPrintf(pszFileName, iMaxName, "%s%s.hnprop.tab", szMailRoot, pszProto);

	return pszFileName;
}

int SvrEnumProtoProps(const char *pszProto, const SYS_INET_ADDR *pPeerInfo,
		      const char *pszHostName, int (*pfEnum)(void *, const char *, const char *),
		      void *pPrivate)
{
	int i;
	char **ppszProps;
	char szProtoFile[SYS_MAX_PATH] = "";

	if (pPeerInfo != NULL) {
		SvrGetProtoIPPropFile(pszProto, szProtoFile, sizeof(szProtoFile) - 1);

		if ((ppszProps = MscGetIPProperties(szProtoFile, pPeerInfo)) != NULL) {
			for (i = 1; ppszProps[i] != NULL; i++) {
				char *pszName = ppszProps[i];
				char *pszVal = strchr(pszName, '=');

				if (pszVal != NULL)
					*pszVal++ = '\0';
				if ((*pfEnum)(pPrivate, pszName, pszVal) < 0) {
					StrFreeStrings(ppszProps);
					return ErrGetErrorCode();
				}
			}
			StrFreeStrings(ppszProps);
		}
	}

	SvrGetProtoHNPropFile(pszProto, szProtoFile, sizeof(szProtoFile) - 1);
	if (SysExistFile(szProtoFile)) {
		char szHostName[SYS_MAX_PATH] = "";

		if (pszHostName == NULL) {
			if (pPeerInfo == NULL) {
				ErrSetErrorCode(ERR_INVALID_PARAMETER);
				return ERR_INVALID_PARAMETER;
			}
			if (SysGetHostByAddr(*pPeerInfo, szHostName, sizeof(szHostName)) < 0)
				return ErrGetErrorCode();
			pszHostName = szHostName;
		}
		if ((ppszProps = MscGetHNProperties(szProtoFile, pszHostName)) != NULL) {
			for (i = 1; ppszProps[i] != NULL; i++) {
				char *pszName = ppszProps[i];
				char *pszVal = strchr(pszName, '=');

				if (pszVal != NULL)
					*pszVal++ = '\0';
				if ((*pfEnum)(pPrivate, pszName, pszVal) < 0) {
					StrFreeStrings(ppszProps);
					return ErrGetErrorCode();
				}
			}
			StrFreeStrings(ppszProps);
		}
	}

	return 0;
}

