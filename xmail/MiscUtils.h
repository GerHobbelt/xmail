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

#ifndef _MISCUTILS_H
#define _MISCUTILS_H

#include "Hash.h"

#define LOCK_FILE_WAITSTEP     1

#define INVALID_FSCAN_HANDLE   ((FSCAN_HANDLE) 0)

#define HASH_INIT_VALUE        5381

#define THCF_USE_SSL           (1 << 0)
#define THCF_SHUTDOWN          (1 << 1)

typedef struct FSCAN_HANDLE_struct {
} *FSCAN_HANDLE;

struct AddressFilter {
	SYS_INET_ADDR Addr;
	SYS_UINT8 Mask[sizeof(SYS_INET_ADDR)];
};

struct ThreadConfig {
	char const *pszName;
	unsigned int (*pfThreadProc)(void *);
	long (*pfThreadCnt)(ThreadConfig const *);
	SHB_HANDLE hThShb;
	unsigned long ulFlags;
	int iNumAddr;
	SYS_INET_ADDR SvrAddr[MAX_ACCEPT_ADDRESSES];
	int iNumSockFDs;
	SYS_SOCKET SockFDs[MAX_ACCEPT_ADDRESSES];
};

struct ThreadCreateCtx {
	SYS_SOCKET SockFD;
	ThreadConfig const *pThCfg;
};


int MscDatumAlloc(Datum *pDm, void const *pData, long lSize);
LstDatum *MscLstDatumAlloc(void const *pData, long lSize);
int MscLstDatumAddT(SysListHead *pHead, void const *pData, long lSize);
void MscFreeDatumList(SysListHead *pHead);
int MscUniqueFile(char const *pszDir, char *pszFilePath, int iMaxPath);
void MscSafeGetTmpFile(char *pszPath, int iMaxPath);
int MscRecvTextFile(char const *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
		    int (*pStopProc) (void *) = NULL, void *pParam = NULL);
int MscSendTextFile(char const *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
		    int (*pStopProc) (void *) = NULL, void *pParam = NULL);
int MscSendFileCRLF(char const *pszFilePath, BSOCK_HANDLE hBSock, int iTimeout);
char *MscTranslatePath(char *pszPath);
void *MscLoadFile(char const *pszFilePath, unsigned long *pulFileSize);
int MscLockFile(char const *pszFileName, int iMaxWait, int iWaitStep = LOCK_FILE_WAITSTEP);
int MscGetTimeNbrString(char *pszTimeStr, int iStringSize, time_t tTime = 0);
int MscGetTime(struct tm &tmLocal, int &iDiffHours, int &iDiffMins, time_t tCurr = 0);
char *MscStrftime(struct tm const *ptmTime, char *pszDateStr, int iSize);
int MscGetTimeStr(char *pszTimeStr, int iStringSize, time_t tCurr = 0);
int MscGetDirectorySize(char const *pszPath, bool bRecurse, SYS_OFF_T &llDirSize,
			unsigned long &ulNumFiles, int (*pFNValidate) (char const *) = NULL);
FSCAN_HANDLE MscFirstFile(char const *pszPath, int iListDirs, char *pszFileName, int iSize);
int MscNextFile(FSCAN_HANDLE hFileScan, char *pszFileName, int iSize);
void MscCloseFindFile(FSCAN_HANDLE hFileScan);
int MscGetFileList(char const *pszPath, int iListDirs, SysListHead *pHead);
int MscCreateEmptyFile(char const *pszFileName);
int MscClearDirectory(char const *pszPath, int iRecurseSubs = 1);
int MscCopyFile(char const *pszCopyTo, char const *pszCopyFrom);
int MscAppendFile(char const *pszCopyTo, char const *pszCopyFrom);
int MscCopyFile(FILE *pFileOut, FILE *pFileIn, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llCopySize);
int MscDos2UnixFile(FILE *pFileOut, FILE *pFileIn);
int MscMoveFile(char const *pszOldName, char const *pszNewName);
char *MscGetString(FILE *pFile, char *pszBuffer, int iMaxChars);
char *MscFGets(char *pszLine, int iLineSize, FILE *pFile);
char *MscGetConfigLine(char *pszLine, int iLineSize, FILE *pFile, bool bSkipComments = true);
int MscGetPeerHost(SYS_SOCKET SockFD, char *pszFQDN, int iSize);
int MscGetSockHost(SYS_SOCKET SockFD, char *pszFQDN, int iSize);
int MscGetServerAddress(char const *pszServer, SYS_INET_ADDR &SvrAddr, int iPortNo = 0);
int MscSplitFQDN(char const *pszFQDN, char *pszHost, int iHSize,
		 char *pszDomain, int iDSize);
char *MscLogFilePath(char const *pszLogFile, char *pszLogFilePath);
int MscFileLog(char const *pszLogFile, char const *pszFormat, ...);
int MscSplitPath(char const *pszFilePath, char *pszDir, int iDSize,
		 char *pszFName, int iFSize, char *pszExt, int iESize);
int MscGetFileName(char const *pszFilePath, char *pszFileName);
int MscCreateClientSocket(char const *pszServer, int iPortNo, int iSockType,
			  SYS_SOCKET *pSockFD, SYS_INET_ADDR *pSvrAddr,
			  SYS_INET_ADDR *pSockAddr, int iTimeout);
int MscCreateServerSockets(int iNumAddr, SYS_INET_ADDR const *pSvrAddr, int iFamily,
			   int iPortNo, int iListenSize, SYS_SOCKET *pSockFDs,
			   int &iNumSockFDs);
int MscGetMaxSockFD(SYS_SOCKET const *pSockFDs, int iNumSockFDs);
int MscAcceptServerConnection(SYS_SOCKET const *pSockFDs, int iNumSockFDs,
			      SYS_SOCKET *pConnSockFD, int &iNumConnSockFD, int iTimeout);
int MscLoadAddressFilter(char const *const *ppszFilter, int iNumTokens, AddressFilter &AF);
int MscAddressMatch(AddressFilter const &AF, SYS_INET_ADDR const &TestAddr);
int MscCheckAllowedIP(char const *pszMapFile, const SYS_INET_ADDR &PeerInfo, bool bDefault);
char **MscGetIPProperties(char const *pszFileName, const SYS_INET_ADDR *pPeerInfo);
int MscHostSubMatch(char const *pszHostName, char const *pszHostMatch);
char **MscGetHNProperties(char const *pszFileName, char const *pszHostName);
int MscMD5Authenticate(char const *pszPassword, char const *pszTimeStamp, char const *pszDigest);
char *MscExtractServerTimeStamp(char const *pszResponse, char *pszTimeStamp, int iMaxTimeStamp);
int MscRootedName(char const *pszHostName);
int MscCramMD5(char const *pszSecret, char const *pszChallenge, char *pszDigest);
unsigned long MscHashString(char const *pszBuffer, int iLength,
			    unsigned long ulHashInit = HASH_INIT_VALUE);
int MscSplitAddressPort(char const *pszConnSpec, char *pszAddress, int &iPortNo, int iDefPortNo);
SYS_UINT16 MscReadUint16(void const *pData);
SYS_UINT32 MscReadUint32(void const *pData);
SYS_UINT64 MscReadUint64(void const *pData);
void *MscWriteUint16(void *pData, SYS_UINT16 uValue);
void *MscWriteUint32(void *pData, SYS_UINT32 uValue);
void *MscWriteUint64(void *pData, SYS_UINT64 uValue);
int MscCmdStringCheck(char const *pszString);
int MscGetSectionSize(FileSection const *pFS, SYS_OFF_T *pllSize);
int MscIsIPDomain(char const *pszDomain, char *pszIP, int iIPSize);
int MscReplaceTokens(char **ppszTokens, char *(*pLkupProc)(void *, char const *, int),
		     void *pPriv);
int MscGetAddrString(SYS_INET_ADDR const &AddrInfo, char *pszAStr, int iSize);
unsigned int MscServiceThread(void *pThreadData);
int MscSslEnvCB(void *pPrivate, int iID, void const *pData);
int MscParseOptions(char const *pszOpts, int (*pfAssign)(void *, char const *, char const *),
		    void *pPrivate);
void MscSysFreeCB(void *pPrivate, void *pData);
void MscRandomizeStringsOrder(char **ppszStrings);
unsigned long MscStringHashCB(void *pPrivate, HashDatum const *pDatum);
int MscStringCompareCB(void *pPrivate, HashDatum const *pDatum1,
		       HashDatum const *pDatum2);

#endif

