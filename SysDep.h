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

#ifndef _SYSDEP_H
#define _SYSDEP_H

#define SYS_SHUTDOWN_SOFT           0

#define LOG_LEV_DEBUG               0
#define LOG_LEV_MESSAGE             1
#define LOG_LEV_WARNING             2
#define LOG_LEV_ERROR               3

#define SYS_PRIORITY_LOWER          (-1)
#define SYS_PRIORITY_NORMAL         0
#define SYS_PRIORITY_HIGHER         1

#define SYS_THREAD_ATTACH           1
#define SYS_THREAD_DETACH           2

#define SYS_MMAP_READ               (1 << 0)
#define SYS_MMAP_WRITE              (1 << 1)

#define SYS_INET46                  (-1)
#define SYS_INET64                  (-2)

#define SYS_IS_VALID_FILENAME(f)    ((strcmp(f, ".") != 0) && (strcmp(f, "..") != 0))

enum SysFileTypes {
	ftNormal = 1,
	ftDirectory,
	ftLink,
	ftOther,

	ftMax
};

struct SYS_FILE_INFO {
	int iFileType;
	SYS_OFF_T llSize;
	time_t tMod;
};

struct SYS_INET_ADDR {
	int iSize;
	unsigned char Addr[128 - sizeof(int)];
};


int SysInitLibrary(void);
void SysCleanupLibrary(void);
int SysAddThreadExitHook(void (*pfHook)(void *, SYS_THREAD, int), void *pPrivate);
int SysShutdownLibrary(int iMode = SYS_SHUTDOWN_SOFT);

int SysSetupSocketBuffers(int *piSndBufSize, int *piRcvBufSize);
SYS_SOCKET SysCreateSocket(int iAddressFamily, int iType, int iProtocol);
void SysCloseSocket(SYS_SOCKET SockFD);
int SysShutdownSocket(SYS_SOCKET SockFD, int iHow);
int SysBlockSocket(SYS_SOCKET SockFD, int iBlocking);
int SysBindSocket(SYS_SOCKET SockFD, const SYS_INET_ADDR *SockName);
void SysListenSocket(SYS_SOCKET SockFD, int iConnections);
int SysRecvData(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout);
int SysRecv(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout);
int SysRecvDataFrom(SYS_SOCKET SockFD, SYS_INET_ADDR *pFrom, char *pszBuffer,
		    int iBufferSize, int iTimeout);
int SysSendData(SYS_SOCKET SockFD, const char *pszBuffer, int iBufferSize, int iTimeout);
int SysSend(SYS_SOCKET SockFD, const char *pszBuffer, int iBufferSize, int iTimeout);
int SysSendDataTo(SYS_SOCKET SockFD, const SYS_INET_ADDR *pTo,
		  const char *pszBuffer, int iBufferSize, int iTimeout);
int SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR *pSockName, int iTimeout);
SYS_SOCKET SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR *pSockName, int iTimeout);
int SysSelect(int iMaxFD, SYS_fd_set *pReadFDs, SYS_fd_set *pWriteFDs, SYS_fd_set *pExcptFDs,
	      int iTimeout);
int SysSendFile(SYS_SOCKET SockFD, const char *pszFileName, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llEndOffset, int iTimeout);
int SysInetAnySetup(SYS_INET_ADDR &AddrInfo, int iFamily, int iPortNo);
int SysGetAddrFamily(SYS_INET_ADDR const &AddrInfo);
int SysGetAddrPort(SYS_INET_ADDR const &AddrInfo);
int SysSetAddrPort(SYS_INET_ADDR &AddrInfo, int iPortNo);
int SysGetHostByName(const char *pszName, int iFamily, SYS_INET_ADDR &AddrInfo);
int SysGetHostByAddr(SYS_INET_ADDR const &AddrInfo, char *pszFQDN, int iSize);
int SysGetPeerInfo(SYS_SOCKET SockFD, SYS_INET_ADDR &AddrInfo);
int SysGetSockInfo(SYS_SOCKET SockFD, SYS_INET_ADDR &AddrInfo);
char *SysInetNToA(SYS_INET_ADDR const &AddrInfo, char *pszIP, int iSize);
char *SysInetRevNToA(SYS_INET_ADDR const &AddrInfo, char *pszRevIP, int iSize);
void const *SysInetAddrData(SYS_INET_ADDR const &AddrInfo, int *piSize);
int SysSameAddress(SYS_INET_ADDR const &NetAddr1, SYS_INET_ADDR const &NetAddr2);
int SysInetIPV6CompatIPV4(SYS_INET_ADDR const &Addr);
int SysInetIPV6ToIPV4(SYS_INET_ADDR const &SAddr, SYS_INET_ADDR &DAddr);
int SysInetAddrMatch(SYS_INET_ADDR const &Addr, SYS_UINT8 const *pMask, int iMaskSize,
		     SYS_INET_ADDR const &TestAddr);
int SysInetAddrMatch(SYS_INET_ADDR const &Addr, SYS_INET_ADDR const &TestAddr);

SYS_SEMAPHORE SysCreateSemaphore(int iInitCount, int iMaxCount);
int SysCloseSemaphore(SYS_SEMAPHORE hSemaphore);
int SysWaitSemaphore(SYS_SEMAPHORE hSemaphore, int iTimeout);
int SysReleaseSemaphore(SYS_SEMAPHORE hSemaphore, int iCount);
int SysTryWaitSemaphore(SYS_SEMAPHORE hSemaphore);

SYS_MUTEX SysCreateMutex(void);
int SysCloseMutex(SYS_MUTEX hMutex);
int SysLockMutex(SYS_MUTEX hMutex, int iTimeout);
int SysUnlockMutex(SYS_MUTEX hMutex);
int SysTryLockMutex(SYS_MUTEX hMutex);

SYS_EVENT SysCreateEvent(int iManualReset);
int SysCloseEvent(SYS_EVENT hEvent);
int SysWaitEvent(SYS_EVENT hEvent, int iTimeout);
int SysSetEvent(SYS_EVENT hEvent);
int SysResetEvent(SYS_EVENT hEvent);
int SysTryWaitEvent(SYS_EVENT hEvent);

SYS_THREAD SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData);
void SysCloseThread(SYS_THREAD ThreadID, int iForce);
int SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority);
int SysWaitThread(SYS_THREAD ThreadID, int iTimeout);
unsigned long SysGetCurrentThreadId(void);
int SysExec(const char *pszCommand, const char *const *pszArgs, int iWaitTimeout = 0,
	    int iPriority = SYS_PRIORITY_NORMAL, int *piExitStatus = NULL);
void SysSetBreakHandler(void (*BreakHandler) (void));

int SysCreateTlsKey(SYS_TLSKEY &TlsKey, void (*pFreeProc) (void *) = NULL);
int SysDeleteTlsKey(SYS_TLSKEY &TlsKey);
int SysSetTlsKeyData(SYS_TLSKEY &TlsKey, void *pData);
void *SysGetTlsKeyData(SYS_TLSKEY &TlsKey);

void SysThreadOnce(SYS_THREAD_ONCE *pThrOnce, void (*pOnceProc) (void));

void *SysAllocNZ(size_t uSize);
void *SysAlloc(size_t uSize);
void SysFree(void *pData);
void *SysRealloc(void *pData, size_t uSize);

int SysLockFile(const char *pszFileName, const char *pszLockExt = ".lock");
int SysUnlockFile(const char *pszFileName, const char *pszLockExt = ".lock");

SYS_HANDLE SysOpenModule(const char *pszFilePath);
int SysCloseModule(SYS_HANDLE hModule);
void *SysGetSymbol(SYS_HANDLE hModule, const char *pszSymbol);

int SysEventLogV(int iLogLevel, const char *pszFormat, va_list Args);
int SysEventLog(int iLogLevel, const char *pszFormat, ...);
int SysLogMessage(int iLogLevel, const char *pszFormat, ...);
void SysSleep(int iTimeout);
void SysMsSleep(int iMsTimeout);
SYS_INT64 SysMsTime(void);
int SysExistFile(const char *pszFilePath);
int SysExistDir(const char *pszDirPath);
SYS_HANDLE SysFirstFile(const char *pszPath, char *pszFileName, int iSize);
int SysIsDirectory(SYS_HANDLE hFind);
SYS_OFF_T SysGetSize(SYS_HANDLE hFind);
int SysNextFile(SYS_HANDLE hFind, char *pszFileName, int iSize);
void SysFindClose(SYS_HANDLE hFind);
int SysGetFileInfo(const char *pszFileName, SYS_FILE_INFO &FI);
int SysSetFileModTime(const char *pszFileName, time_t tMod);
char *SysStrDup(const char *pszString);
char *SysGetEnv(const char *pszVarName);
char *SysGetTmpFile(char *pszFileName);
int SysRemove(const char *pszFileName);
int SysMakeDir(const char *pszPath);
int SysRemoveDir(const char *pszPath);
int SysMoveFile(const char *pszOldName, const char *pszNewName);

int SysVSNPrintf(char *pszBuffer, int iSize, const char *pszFormat, va_list Args);
int SysFileSync(FILE *pFile);

char *SysStrTok(char *pszData, const char *pszDelim, char **ppszSavePtr);
char *SysCTime(time_t *pTimer, char *pszBuffer, int iBufferSize);
struct tm *SysLocalTime(time_t *pTimer, struct tm *pTStruct);
struct tm *SysGMTime(time_t *pTimer, struct tm *pTStruct);
char *SysAscTime(struct tm *pTStruct, char *pszBuffer, int iBufferSize);
long SysGetTimeZone(void);
long SysGetDayLight(void);

int SysGetDiskSpace(const char *pszPath, SYS_INT64 *pTotal, SYS_INT64 *pFree);
int SysMemoryInfo(SYS_INT64 *pRamTotal, SYS_INT64 *pRamFree,
		  SYS_INT64 *pVirtTotal, SYS_INT64 *pVirtFree);

SYS_MMAP SysCreateMMap(const char *pszFileName, unsigned long ulFlags);
void SysCloseMMap(SYS_MMAP hMap);
SYS_OFF_T SysMMapSize(SYS_MMAP hMap);
SYS_OFF_T SysMMapOffsetAlign(SYS_MMAP hMap, SYS_OFF_T llOffset);
void *SysMapMMap(SYS_MMAP hMap, SYS_OFF_T llOffset, SYS_SIZE_T lSize);
int SysUnmapMMap(SYS_MMAP hMap, void *pAddr, SYS_SIZE_T lSize);

#endif

