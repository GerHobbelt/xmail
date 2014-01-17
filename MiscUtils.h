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

#ifndef _MISCUTILS_H
#define _MISCUTILS_H

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
	const char *pszName;
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
int MscUniqueFile(const char *pszDir, char *pszFilePath, int iMaxPath);
int MscRecvTextFile(const char *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
		    int (*pStopProc) (void *) = NULL, void *pParam = NULL);
int MscSendTextFile(const char *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
		    int (*pStopProc) (void *) = NULL, void *pParam = NULL);
int MscSendFileCRLF(const char *pszFilePath, BSOCK_HANDLE hBSock, int iTimeout);
char *MscTranslatePath(char *pszPath);
void *MscLoadFile(const char *pszFilePath, unsigned long *pulFileSize);
int MscLockFile(const char *pszFileName, int iMaxWait, int iWaitStep = LOCK_FILE_WAITSTEP);
int MscGetTimeNbrString(char *pszTimeStr, int iStringSize, time_t tTime = 0);
int MscGetTime(struct tm &tmLocal, int &iDiffHours, int &iDiffMins, time_t tCurr = 0);
char *MscStrftime(struct tm const *ptmTime, char *pszDateStr, int iSize);
int MscGetTimeStr(char *pszTimeStr, int iStringSize, time_t tCurr = 0);
int MscGetDirectorySize(const char *pszPath, bool bRecurse, SYS_OFF_T &llDirSize,
			unsigned long &ulNumFiles, int (*pFNValidate) (const char *) = NULL);
FSCAN_HANDLE MscFirstFile(const char *pszPath, int iListDirs, char *pszFileName, int iSize);
int MscNextFile(FSCAN_HANDLE hFileScan, char *pszFileName, int iSize);
void MscCloseFindFile(FSCAN_HANDLE hFileScan);
int MscGetFileList(const char *pszPath, int iListDirs, SysListHead *pHead);
int MscCreateEmptyFile(const char *pszFileName);
int MscClearDirectory(const char *pszPath, int iRecurseSubs = 1);
int MscCopyFile(const char *pszCopyTo, const char *pszCopyFrom);
int MscAppendFile(const char *pszCopyTo, const char *pszCopyFrom);
int MscCopyFile(FILE *pFileOut, FILE *pFileIn, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llCopySize);
int MscDos2UnixFile(FILE *pFileOut, FILE *pFileIn);
int MscMoveFile(const char *pszOldName, const char *pszNewName);
char *MscGetString(FILE *pFile, char *pszBuffer, int iMaxChars);
char *MscFGets(char *pszLine, int iLineSize, FILE *pFile);
char *MscGetConfigLine(char *pszLine, int iLineSize, FILE *pFile, bool bSkipComments = true);
int MscGetPeerHost(SYS_SOCKET SockFD, char *pszFQDN, int iSize);
int MscGetSockHost(SYS_SOCKET SockFD, char *pszFQDN, int iSize);
int MscGetServerAddress(const char *pszServer, SYS_INET_ADDR &SvrAddr, int iPortNo = 0);
int MscSplitFQDN(const char *pszFQDN, char *pszHost, int iHSize,
		 char *pszDomain, int iDSize);
char *MscLogFilePath(const char *pszLogFile, char *pszLogFilePath);
int MscFileLog(const char *pszLogFile, const char *pszFormat, ...);
int MscSplitPath(const char *pszFilePath, char *pszDir, int iDSize,
		 char *pszFName, int iFSize, char *pszExt, int iESize);
int MscGetFileName(const char *pszFilePath, char *pszFileName);
int MscCreateClientSocket(const char *pszServer, int iPortNo, int iSockType,
			  SYS_SOCKET *pSockFD, SYS_INET_ADDR *pSvrAddr,
			  SYS_INET_ADDR *pSockAddr, int iTimeout);
int MscCreateServerSockets(int iNumAddr, SYS_INET_ADDR const *pSvrAddr, int iFamily,
			   int iPortNo, int iListenSize, SYS_SOCKET *pSockFDs,
			   int &iNumSockFDs);
int MscGetMaxSockFD(SYS_SOCKET const *pSockFDs, int iNumSockFDs);
int MscAcceptServerConnection(SYS_SOCKET const *pSockFDs, int iNumSockFDs,
			      SYS_SOCKET *pConnSockFD, int &iNumConnSockFD, int iTimeout);
int MscLoadAddressFilter(const char *const *ppszFilter, int iNumTokens, AddressFilter &AF);
int MscAddressMatch(AddressFilter const &AF, SYS_INET_ADDR const &TestAddr);
int MscCheckAllowedIP(const char *pszMapFile, const SYS_INET_ADDR &PeerInfo, bool bDefault);
char **MscGetIPProperties(const char *pszFileName, const SYS_INET_ADDR *pPeerInfo);
int MscHostSubMatch(const char *pszHostName, const char *pszHostMatch);
char **MscGetHNProperties(const char *pszFileName, const char *pszHostName);
int MscMD5Authenticate(const char *pszPassword, const char *pszTimeStamp, const char *pszDigest);
char *MscExtractServerTimeStamp(const char *pszResponse, char *pszTimeStamp, int iMaxTimeStamp);
int MscRootedName(const char *pszHostName);
int MscCramMD5(const char *pszSecret, const char *pszChallenge, char *pszDigest);
unsigned long MscHashString(const char *pszBuffer, int iLength,
			    unsigned long ulHashInit = HASH_INIT_VALUE);
int MscSplitAddressPort(const char *pszConnSpec, char *pszAddress, int &iPortNo, int iDefPortNo);
SYS_UINT16 MscReadUint16(void const *pData);
SYS_UINT32 MscReadUint32(void const *pData);
SYS_UINT64 MscReadUint64(void const *pData);
void *MscWriteUint16(void *pData, SYS_UINT16 uValue);
void *MscWriteUint32(void *pData, SYS_UINT32 uValue);
void *MscWriteUint64(void *pData, SYS_UINT64 uValue);
int MscCmdStringCheck(const char *pszString);
int MscGetSectionSize(FileSection const *pFS, SYS_OFF_T *pllSize);
int MscIsIPDomain(const char *pszDomain, char *pszIP, int iIPSize);
int MscReplaceTokens(char **ppszTokens, char *(*pLkupProc)(void *, const char *, int),
		     void *pPriv);
int MscGetAddrString(SYS_INET_ADDR const &AddrInfo, char *pszAStr, int iSize);
unsigned int MscServiceThread(void *pThreadData);
int MscSslEnvCB(void *pPrivate, int iID, void const *pData);
int MscParseOptions(const char *pszOpts, int (*pfAssign)(void *, const char *, const char *),
		    void *pPrivate);
void MscSysFreeCB(void *pPrivate, void *pData);

#endif

