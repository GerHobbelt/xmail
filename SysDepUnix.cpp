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
#include "SysDepUnix.h"
#include "AppDefines.h"


#define SCHED_PRIORITY_INC          5
#define SYS_MAX_TH_EXIT_HOOKS       64

#define WAIT_PID_TIME_STEP          250
#define WAIT_TIMEO_EXIT_STATUS      255
#define WAIT_ERROR_EXIT_STATUS      254

static int SysSetSignal(int iSigNo, void (*pSigProc) (int));
static void SysIgnoreProc(int iSignal);
static void SysRunThreadExitHooks(SYS_THREAD ThreadID, int iMode);
static int SysSetSocketsOptions(SYS_SOCKET SockFD);
static int SysFreeThreadData(ThrData *pTD);
static void *SysThreadStartup(void *pThreadData);
static int SysThreadSetup(ThrData *pTD);
static void SysThreadCleanup(ThrData *pTD);
static int SysSafeMsSleep(int iMsTimeout);
static int SysWaitPID(pid_t PID, int *piExitCode, int iTimeout);
static void SysBreakHandlerRoutine(int iSignal);
static int SysSetupWait(WaitData *pWD);
static int SysWait(WaitData *pWD, int iMsTimeout);
static void SysCleanupWait(WaitData *pWD);
static int SysWait(WaitData *pWD, int iMsTimeout);

static int iNumThExitHooks;
static volatile int iShutDown;
static ThreadExitHook ThExitHooks[SYS_MAX_TH_EXIT_HOOKS];
static pthread_mutex_t LogMutex = PTHREAD_MUTEX_INITIALIZER;
static void (*SysBreakHandler) (void) = NULL;
static int iSndBufSize = -1, iRcvBufSize = -1;

static int SysSetSignal(int iSigNo, void (*pSigProc) (int))
{
	signal(iSigNo, pSigProc);

	return 0;
}

static void SysIgnoreProc(int iSignal)
{
	SysSetSignal(iSignal, SysIgnoreProc);
}

int SysInitLibrary(void)
{
	iShutDown = 0;
	iNumThExitHooks = 0;
	tzset();
	if (SysDepInitLibrary() < 0 ||
	    SysThreadSetup(NULL) < 0)
		return ErrGetErrorCode();

	return 0;
}

void SysCleanupLibrary(void)
{
	SysThreadCleanup(NULL);
	SysDepCleanupLibrary();
}

int SysAddThreadExitHook(void (*pfHook)(void *, SYS_THREAD, int), void *pPrivate)
{
	if (iNumThExitHooks >= SYS_MAX_TH_EXIT_HOOKS)
		return -1;
	ThExitHooks[iNumThExitHooks].pfHook = pfHook;
	ThExitHooks[iNumThExitHooks].pPrivate = pPrivate;
	iNumThExitHooks++;

	return 0;
}

static void SysRunThreadExitHooks(SYS_THREAD ThreadID, int iMode)
{
	int i;

	for (i = iNumThExitHooks - 1; i >= 0; i--)
		(*ThExitHooks[i].pfHook)(ThExitHooks[i].pPrivate, ThreadID, iMode);
}

int SysShutdownLibrary(int iMode)
{
	iShutDown++;
	kill(0, SIGQUIT);

	return 0;
}

int SysSetupSocketBuffers(int *piSndBufSize, int *piRcvBufSize)
{
	if (piSndBufSize != NULL)
		iSndBufSize = *piSndBufSize;
	if (piRcvBufSize != NULL)
		iRcvBufSize = *piRcvBufSize;

	return 0;
}

SYS_SOCKET SysCreateSocket(int iAddressFamily, int iType, int iProtocol)
{
	int SockFD = socket(iAddressFamily, iType, iProtocol);

	if (SockFD == -1) {
		ErrSetErrorCode(ERR_SOCKET_CREATE);
		return SYS_INVALID_SOCKET;
	}
	if (SysSetSocketsOptions((SYS_SOCKET) SockFD) < 0) {
		SysCloseSocket((SYS_SOCKET) SockFD);

		return SYS_INVALID_SOCKET;
	}

	return (SYS_SOCKET) SockFD;
}

int SysBlockSocket(SYS_SOCKET SockFD, int iBlocking)
{
	long lSockFlags = fcntl((int) SockFD, F_GETFL, 0);

	if (lSockFlags == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iBlocking == 0)
		lSockFlags |= O_NONBLOCK;
	else
		lSockFlags &= ~O_NONBLOCK;
	if (fcntl((int) SockFD, F_SETFL, lSockFlags) == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	return 0;
}

static int SysSetSocketsOptions(SYS_SOCKET SockFD)
{
	/* Set socket buffer sizes */
	if (iSndBufSize > 0) {
		int iSize = iSndBufSize;

		setsockopt((int) SockFD, SOL_SOCKET, SO_SNDBUF, (const char *) &iSize,
			   sizeof(iSize));
	}
	if (iRcvBufSize > 0) {
		int iSize = iRcvBufSize;

		setsockopt((int) SockFD, SOL_SOCKET, SO_RCVBUF, (const char *) &iSize,
			   sizeof(iSize));
	}

	int iActivate = 1;

	if (setsockopt(SockFD, SOL_SOCKET, SO_REUSEADDR, (const char *) &iActivate,
		       sizeof(iActivate)) != 0) {
		ErrSetErrorCode(ERR_SETSOCKOPT);
		return ERR_SETSOCKOPT;
	}
	/* Disable linger */
	struct linger Ling;

	ZeroData(Ling);
	Ling.l_onoff = 0;
	Ling.l_linger = 0;

	setsockopt(SockFD, SOL_SOCKET, SO_LINGER, (const char *) &Ling, sizeof(Ling));

	/* Set KEEPALIVE if supported */
	setsockopt(SockFD, SOL_SOCKET, SO_KEEPALIVE, (const char *) &iActivate,
		   sizeof(iActivate));

	return 0;
}

void SysCloseSocket(SYS_SOCKET SockFD)
{
	close(SockFD);
}

int SysShutdownSocket(SYS_SOCKET SockFD, int iHow)
{
	if (shutdown(SockFD, iHow)) {
		ErrSetErrorCode(ERR_SOCKET_SHUTDOWN);
		return ERR_SOCKET_SHUTDOWN;
	}

	return 0;
}

int SysBindSocket(SYS_SOCKET SockFD, const SYS_INET_ADDR *SockName)
{
	if (bind((int) SockFD, (const struct sockaddr *) SockName->Addr,
		 SockName->iSize) == -1) {
		ErrSetErrorCode(ERR_SOCKET_BIND);
		return ERR_SOCKET_BIND;
	}

	return 0;
}

void SysListenSocket(SYS_SOCKET SockFD, int iConnections)
{
	listen((int) SockFD, iConnections);
}

int SysRecvData(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{
	struct pollfd pfds;

	ZeroData(pfds);
	pfds.fd = (int) SockFD;
	pfds.events = POLLIN;

	int iPollResult = poll(&pfds, 1, iTimeout * 1000);

	if (iPollResult == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iPollResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	int iRecvBytes;

	while ((iRecvBytes = recv((int) SockFD, pszBuffer, iBufferSize, 0)) == -1 &&
	       SYS_INT_CALL());

	if (iRecvBytes == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	return iRecvBytes;
}

int SysRecv(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{
	int iRtxBytes = 0;

	while (iRtxBytes < iBufferSize) {
		int iRtxCurrent = SysRecvData(SockFD, pszBuffer + iRtxBytes,
					      iBufferSize - iRtxBytes, iTimeout);

		if (iRtxCurrent <= 0)
			return iRtxBytes;
		iRtxBytes += iRtxCurrent;
	}

	return iRtxBytes;
}

int SysRecvDataFrom(SYS_SOCKET SockFD, SYS_INET_ADDR *pFrom, char *pszBuffer,
		    int iBufferSize, int iTimeout)
{
	struct pollfd pfds;

	ZeroData(pfds);
	pfds.fd = (int) SockFD;
	pfds.events = POLLIN;

	int iPollResult = poll(&pfds, 1, iTimeout * 1000);

	if (iPollResult == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iPollResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	socklen_t SockALen = (socklen_t) sizeof(pFrom->Addr);
	int iRecvBytes;

	while ((iRecvBytes = recvfrom((int) SockFD, pszBuffer, iBufferSize, 0,
				      (struct sockaddr *) pFrom->Addr,
				      &SockALen)) == -1 && SYS_INT_CALL());

	if (iRecvBytes == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	pFrom->iSize = (int) SockALen;

	return iRecvBytes;
}

int SysSendData(SYS_SOCKET SockFD, const char *pszBuffer, int iBufferSize, int iTimeout)
{
	struct pollfd pfds;

	ZeroData(pfds);
	pfds.fd = (int) SockFD;
	pfds.events = POLLOUT;

	int iPollResult = poll(&pfds, 1, iTimeout * 1000);

	if (iPollResult == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iPollResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	int iSendBytes;

	while ((iSendBytes = send((int) SockFD, pszBuffer, iBufferSize, 0)) == -1 &&
	       SYS_INT_CALL());

	if (iSendBytes == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	return iSendBytes;
}

int SysSend(SYS_SOCKET SockFD, const char *pszBuffer, int iBufferSize, int iTimeout)
{
	int iRtxBytes = 0;

	while (iRtxBytes < iBufferSize) {
		int iRtxCurrent = SysSendData(SockFD, pszBuffer + iRtxBytes,
					      iBufferSize - iRtxBytes, iTimeout);

		if (iRtxCurrent <= 0)
			return iRtxBytes;
		iRtxBytes += iRtxCurrent;
	}

	return iRtxBytes;
}

int SysSendDataTo(SYS_SOCKET SockFD, const SYS_INET_ADDR *pTo,
		  const char *pszBuffer, int iBufferSize, int iTimeout)
{
	struct pollfd pfds;

	ZeroData(pfds);
	pfds.fd = (int) SockFD;
	pfds.events = POLLOUT;

	int iPollResult = poll(&pfds, 1, iTimeout * 1000);

	if (iPollResult == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iPollResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	int iSendBytes;

	while ((iSendBytes = sendto((int) SockFD, pszBuffer, iBufferSize, 0,
				    (const struct sockaddr *) pTo->Addr,
				    pTo->iSize)) == -1 &&
	       SYS_INT_CALL());

	if (iSendBytes == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	return iSendBytes;
}

int SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR *pSockName, int iTimeout)
{
	if (SysBlockSocket(SockFD, 0) < 0)
		return ErrGetErrorCode();

	if (connect((int) SockFD, (const struct sockaddr *) pSockName->Addr,
		    pSockName->iSize) == 0) {
		SysBlockSocket(SockFD, 1);
		return 0;
	}
	if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
		SysBlockSocket(SockFD, 1);

		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	struct pollfd pfds;

	ZeroData(pfds);
	pfds.fd = (int) SockFD;
	pfds.events = POLLOUT;

	int iPollResult = poll(&pfds, 1, iTimeout * 1000);

	SysBlockSocket(SockFD, 1);
	if (iPollResult == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iPollResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	return 0;
}

SYS_SOCKET SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR *pSockName, int iTimeout)
{
	struct pollfd pfds;

	ZeroData(pfds);
	pfds.fd = (int) SockFD;
	pfds.events = POLLIN;

	int iPollResult = poll(&pfds, 1, iTimeout * 1000);

	if (iPollResult == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return SYS_INVALID_SOCKET;
	}
	if (iPollResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return SYS_INVALID_SOCKET;
	}

	socklen_t SockALen = (socklen_t) sizeof(pSockName->Addr);
	int iAcptSock = accept((int) SockFD,
			       (struct sockaddr *) pSockName->Addr, &SockALen);

	if (iAcptSock == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return SYS_INVALID_SOCKET;
	}
	if (SysSetSocketsOptions((SYS_SOCKET) iAcptSock) < 0) {
		SysCloseSocket((SYS_SOCKET) iAcptSock);

		return SYS_INVALID_SOCKET;
	}
	pSockName->iSize = (int) SockALen;

	return (SYS_SOCKET) iAcptSock;
}

int SysSelect(int iMaxFD, SYS_fd_set *pReadFDs, SYS_fd_set *pWriteFDs, SYS_fd_set *pExcptFDs,
	      int iTimeout)
{
	struct timeval TV;

	ZeroData(TV);
	TV.tv_sec = iTimeout;
	TV.tv_usec = 0;

	int iSelectResult = select(iMaxFD + 1, pReadFDs, pWriteFDs, pExcptFDs, &TV);

	if (iSelectResult == -1) {
		ErrSetErrorCode(ERR_SELECT);
		return ERR_SELECT;
	}
	if (iSelectResult == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	return iSelectResult;
}

int SysSendFileMMap(SYS_SOCKET SockFD, const char *pszFileName, SYS_OFF_T llBaseOffset,
		    SYS_OFF_T llEndOffset, int iTimeout)
{
	int iFileID = open(pszFileName, O_RDONLY);

	if (iFileID == -1) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName);
		return ERR_FILE_OPEN;
	}

	SYS_OFF_T llAlnOff, llFileSize;

	llFileSize = (SYS_OFF_T) lseek(iFileID, 0, SEEK_END);
	lseek(iFileID, 0, SEEK_SET);
	llAlnOff = llBaseOffset & ~((SYS_OFF_T) sysconf(_SC_PAGESIZE) - 1);
	if (llEndOffset == -1)
		llEndOffset = llFileSize;
	if (llBaseOffset > llFileSize || llEndOffset > llFileSize ||
	    llBaseOffset > llEndOffset) {
		close(iFileID);
		ErrSetErrorCode(ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}

	void *pMapAddress = (void *) mmap((char *) 0, llEndOffset - llAlnOff,
					  PROT_READ, MAP_SHARED, iFileID, llAlnOff);

	if (pMapAddress == (void *) -1) {
		close(iFileID);
		ErrSetErrorCode(ERR_MMAP);
		return ERR_MMAP;
	}
	/* Send the file */
	size_t iSndBuffSize = MIN_TCP_SEND_SIZE;
	char *pszBuffer = (char *) pMapAddress + (llBaseOffset - llAlnOff);
	time_t tStart;

	while (llBaseOffset < llEndOffset) {
		int iCurrSend = (int) Min(iSndBuffSize, llEndOffset - llBaseOffset);		// [i_a] Following post by Olivier Stoeneberg, 2009/07/16

		tStart = time(NULL);
		if ((iCurrSend = SysSendData(SockFD, pszBuffer, iCurrSend, iTimeout)) < 0) {
			ErrorPush();
			munmap((char *) pMapAddress, llEndOffset - llAlnOff);
			close(iFileID);
			return ErrorPop();
		}

		if ((((time(NULL) - tStart) * K_IO_TIME_RATIO) < iTimeout) &&
		    (iSndBuffSize < MAX_TCP_SEND_SIZE))
			iSndBuffSize = Min(iSndBuffSize * 2, MAX_TCP_SEND_SIZE);

		pszBuffer += iCurrSend;
		llBaseOffset += iCurrSend;
	}
	munmap((char *) pMapAddress, llEndOffset - llAlnOff);
	close(iFileID);

	return 0;
}

#if !defined(SYS_HAS_SENDFILE)

int SysSendFile(SYS_SOCKET SockFD, const char *pszFileName, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llEndOffset, int iTimeout)
{
	return SysSendFileMMap(SockFD, pszFileName, llBaseOffset, llEndOffset, iTimeout);
}

#endif /* !SYS_HAS_SENDFILE */

SYS_SEMAPHORE SysCreateSemaphore(int iInitCount, int iMaxCount)
{
	SemData *pSD = (SemData *) SysAlloc(sizeof(SemData));

	if (pSD == NULL)
		return SYS_INVALID_SEMAPHORE;

	if (pthread_mutex_init(&pSD->Mtx, NULL) != 0) {
		SysFree(pSD);

		ErrSetErrorCode(ERR_MUTEXINIT, NULL);
		return SYS_INVALID_SEMAPHORE;
	}
	if (pthread_cond_init(&pSD->WaitCond, NULL) != 0) {
		pthread_mutex_destroy(&pSD->Mtx);
		SysFree(pSD);

		ErrSetErrorCode(ERR_CONDINIT, NULL);
		return SYS_INVALID_SEMAPHORE;
	}
	pSD->iSemCounter = iInitCount;
	pSD->iMaxCount = iMaxCount;

	return (SYS_SEMAPHORE) pSD;
}

int SysCloseSemaphore(SYS_SEMAPHORE hSemaphore)
{
	SemData *pSD = (SemData *) hSemaphore;

	pthread_cond_destroy(&pSD->WaitCond);
	pthread_mutex_destroy(&pSD->Mtx);
	SysFree(pSD);

	return 0;
}

int SysWaitSemaphore(SYS_SEMAPHORE hSemaphore, int iTimeout)
{
	SemData *pSD = (SemData *) hSemaphore;

	pthread_mutex_lock(&pSD->Mtx);
	if (iTimeout == SYS_INFINITE_TIMEOUT) {
		while (pSD->iSemCounter <= 0) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pSD->Mtx);
			pthread_cond_wait(&pSD->WaitCond, &pSD->Mtx);
			pthread_cleanup_pop(0);
		}
		pSD->iSemCounter -= 1;
	} else {
		int iRetCode = 0;
		struct timeval tvNow;
		struct timespec tsTimeout;

		gettimeofday(&tvNow, NULL);
		tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
		tsTimeout.tv_nsec = tvNow.tv_usec * 1000;
		while ((pSD->iSemCounter <= 0) && (iRetCode != ETIMEDOUT)) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pSD->Mtx);
			iRetCode = pthread_cond_timedwait(&pSD->WaitCond, &pSD->Mtx,
							  &tsTimeout);
			pthread_cleanup_pop(0);
		}
		if (iRetCode == ETIMEDOUT) {
			pthread_mutex_unlock(&pSD->Mtx);
			ErrSetErrorCode(ERR_TIMEOUT);
			return ERR_TIMEOUT;
		}
		pSD->iSemCounter -= 1;
	}
	pthread_mutex_unlock(&pSD->Mtx);

	return 0;
}

int SysReleaseSemaphore(SYS_SEMAPHORE hSemaphore, int iCount)
{
	SemData *pSD = (SemData *) hSemaphore;

	pthread_mutex_lock(&pSD->Mtx);
	pSD->iSemCounter += iCount;
	if (pSD->iSemCounter > 0) {
		if (pSD->iSemCounter > 1)
			pthread_cond_broadcast(&pSD->WaitCond);
		else
			pthread_cond_signal(&pSD->WaitCond);
	}
	pthread_mutex_unlock(&pSD->Mtx);

	return 0;
}

int SysTryWaitSemaphore(SYS_SEMAPHORE hSemaphore)
{
	SemData *pSD = (SemData *) hSemaphore;

	pthread_mutex_lock(&pSD->Mtx);
	if (pSD->iSemCounter <= 0) {
		pthread_mutex_unlock(&pSD->Mtx);
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	pSD->iSemCounter -= 1;
	pthread_mutex_unlock(&pSD->Mtx);

	return 0;
}

SYS_MUTEX SysCreateMutex(void)
{
	MutexData *pMD = (MutexData *) SysAlloc(sizeof(MutexData));

	if (pMD == NULL)
		return SYS_INVALID_MUTEX;

	if (pthread_mutex_init(&pMD->Mtx, NULL) != 0) {
		SysFree(pMD);
		ErrSetErrorCode(ERR_MUTEXINIT);
		return SYS_INVALID_MUTEX;
	}
	if (pthread_cond_init(&pMD->WaitCond, NULL) != 0) {
		pthread_mutex_destroy(&pMD->Mtx);
		SysFree(pMD);
		ErrSetErrorCode(ERR_CONDINIT);
		return SYS_INVALID_MUTEX;
	}
	pMD->iLocked = 0;

	return (SYS_MUTEX) pMD;
}

int SysCloseMutex(SYS_MUTEX hMutex)
{
	MutexData *pMD = (MutexData *) hMutex;

	pthread_cond_destroy(&pMD->WaitCond);
	pthread_mutex_destroy(&pMD->Mtx);
	SysFree(pMD);

	return 0;
}

int SysLockMutex(SYS_MUTEX hMutex, int iTimeout)
{
	MutexData *pMD = (MutexData *) hMutex;

	pthread_mutex_lock(&pMD->Mtx);
	if (iTimeout == SYS_INFINITE_TIMEOUT) {
		while (pMD->iLocked) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pMD->Mtx);
			pthread_cond_wait(&pMD->WaitCond, &pMD->Mtx);
			pthread_cleanup_pop(0);
		}
		pMD->iLocked = 1;
	} else {
		int iRetCode = 0;
		struct timeval tvNow;
		struct timespec tsTimeout;

		gettimeofday(&tvNow, NULL);
		tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
		tsTimeout.tv_nsec = tvNow.tv_usec * 1000;
		while (pMD->iLocked && (iRetCode != ETIMEDOUT)) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pMD->Mtx);
			iRetCode = pthread_cond_timedwait(&pMD->WaitCond, &pMD->Mtx,
							  &tsTimeout);
			pthread_cleanup_pop(0);
		}
		if (iRetCode == ETIMEDOUT) {
			pthread_mutex_unlock(&pMD->Mtx);
			ErrSetErrorCode(ERR_TIMEOUT);
			return ERR_TIMEOUT;
		}
		pMD->iLocked = 1;
	}
	pthread_mutex_unlock(&pMD->Mtx);

	return 0;
}

int SysUnlockMutex(SYS_MUTEX hMutex)
{
	MutexData *pMD = (MutexData *) hMutex;

	pthread_mutex_lock(&pMD->Mtx);
	pMD->iLocked = 0;
	pthread_cond_signal(&pMD->WaitCond);
	pthread_mutex_unlock(&pMD->Mtx);

	return 0;
}

int SysTryLockMutex(SYS_MUTEX hMutex)
{
	MutexData *pMD = (MutexData *) hMutex;

	pthread_mutex_lock(&pMD->Mtx);
	if (pMD->iLocked) {
		pthread_mutex_unlock(&pMD->Mtx);
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	pMD->iLocked = 1;
	pthread_mutex_unlock(&pMD->Mtx);

	return 0;
}

SYS_EVENT SysCreateEvent(int iManualReset)
{
	EventData *pED = (EventData *) SysAlloc(sizeof(EventData));

	if (pED == NULL)
		return SYS_INVALID_EVENT;

	if (pthread_mutex_init(&pED->Mtx, NULL) != 0) {
		SysFree(pED);
		ErrSetErrorCode(ERR_MUTEXINIT);
		return SYS_INVALID_EVENT;
	}
	if (pthread_cond_init(&pED->WaitCond, NULL) != 0) {
		pthread_mutex_destroy(&pED->Mtx);
		SysFree(pED);
		ErrSetErrorCode(ERR_CONDINIT);
		return SYS_INVALID_EVENT;
	}

	pED->iSignaled = 0;
	pED->iManualReset = iManualReset;

	return (SYS_EVENT) pED;
}

int SysCloseEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	pthread_cond_destroy(&pED->WaitCond);
	pthread_mutex_destroy(&pED->Mtx);
	SysFree(pED);

	return 0;
}

int SysWaitEvent(SYS_EVENT hEvent, int iTimeout)
{
	EventData *pED = (EventData *) hEvent;

	pthread_mutex_lock(&pED->Mtx);
	if (iTimeout == SYS_INFINITE_TIMEOUT) {
		while (!pED->iSignaled) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pED->Mtx);
			pthread_cond_wait(&pED->WaitCond, &pED->Mtx);
			pthread_cleanup_pop(0);
		}
		if (!pED->iManualReset)
			pED->iSignaled = 0;
	} else {
		int iRetCode = 0;
		struct timeval tvNow;
		struct timespec tsTimeout;

		gettimeofday(&tvNow, NULL);
		tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
		tsTimeout.tv_nsec = tvNow.tv_usec * 1000;
		while (!pED->iSignaled && (iRetCode != ETIMEDOUT)) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pED->Mtx);
			iRetCode = pthread_cond_timedwait(&pED->WaitCond, &pED->Mtx,
							  &tsTimeout);
			pthread_cleanup_pop(0);
		}
		if (iRetCode == ETIMEDOUT) {
			pthread_mutex_unlock(&pED->Mtx);
			ErrSetErrorCode(ERR_TIMEOUT);
			return ERR_TIMEOUT;
		}
		if (!pED->iManualReset)
			pED->iSignaled = 0;
	}
	pthread_mutex_unlock(&pED->Mtx);

	return 0;
}

int SysSetEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	pthread_mutex_lock(&pED->Mtx);
	pED->iSignaled = 1;
	if (pED->iManualReset)
		pthread_cond_broadcast(&pED->WaitCond);
	else
		pthread_cond_signal(&pED->WaitCond);
	pthread_mutex_unlock(&pED->Mtx);

	return 0;
}

int SysResetEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	pthread_mutex_lock(&pED->Mtx);
	pED->iSignaled = 0;
	pthread_mutex_unlock(&pED->Mtx);

	return 0;
}

int SysTryWaitEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	pthread_mutex_lock(&pED->Mtx);
	if (!pED->iSignaled) {
		pthread_mutex_unlock(&pED->Mtx);
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	if (!pED->iManualReset)
		pED->iSignaled = 0;
	pthread_mutex_unlock(&pED->Mtx);

	return 0;
}

static int SysFreeThreadData(ThrData *pTD)
{
	pthread_cond_destroy(&pTD->ExitWaitCond);
	pthread_mutex_destroy(&pTD->Mtx);
	SysFree(pTD);

	return 0;
}

static void *SysThreadStartup(void *pThreadData)
{
	int iExitCode;
	ThrData *pTD = (ThrData *) pThreadData;

	SysThreadSetup(pTD);
	pthread_cleanup_push((void (*)(void *)) SysThreadCleanup, pTD);

	pTD->iExitCode = iExitCode = (*pTD->ThreadProc)(pTD->pThreadData);

	pthread_cleanup_pop(1);

	return (void *) (long) iExitCode;
}

SYS_THREAD SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData)
{
	ThrData *pTD;
	pthread_attr_t ThrAttr;

	if ((pTD = (ThrData *) SysAlloc(sizeof(ThrData))) == NULL)
		return SYS_INVALID_THREAD;
	pTD->ThreadProc = pThreadProc;
	pTD->pThreadData = pThreadData;
	pTD->iExitCode = -1;
	pTD->iUseCount = 2;
	if (pthread_mutex_init(&pTD->Mtx, NULL) != 0) {
		SysFree(pTD);

		ErrSetErrorCode(ERR_MUTEXINIT);
		return SYS_INVALID_THREAD;
	}
	if (pthread_cond_init(&pTD->ExitWaitCond, NULL) != 0) {
		pthread_mutex_destroy(&pTD->Mtx);
		SysFree(pTD);

		ErrSetErrorCode(ERR_CONDINIT);
		return SYS_INVALID_THREAD;
	}
	pthread_attr_init(&ThrAttr);
	pthread_attr_setscope(&ThrAttr, PTHREAD_SCOPE_SYSTEM);
	if (pthread_create(&pTD->ThreadId, &ThrAttr, SysThreadStartup, pTD) != 0) {
		pthread_attr_destroy(&ThrAttr);

		ErrSetErrorCode(ERR_THREADCREATE);
		return SYS_INVALID_THREAD;
	}
	pthread_attr_destroy(&ThrAttr);

	return (SYS_THREAD) pTD;
}

static int SysThreadSetup(ThrData *pTD)
{
	sigset_t SigMask;

	sigemptyset(&SigMask);
	sigaddset(&SigMask, SIGALRM);
	sigaddset(&SigMask, SIGINT);
	sigaddset(&SigMask, SIGHUP);
	sigaddset(&SigMask, SIGSTOP);
	sigaddset(&SigMask, SIGCHLD);

	pthread_sigmask(SIG_BLOCK, &SigMask, NULL);

	sigemptyset(&SigMask);
	sigaddset(&SigMask, SIGQUIT);

	pthread_sigmask(SIG_UNBLOCK, &SigMask, NULL);

	SysSetSignal(SIGQUIT, SysIgnoreProc);
	SysSetSignal(SIGPIPE, SysIgnoreProc);
	SysSetSignal(SIGCHLD, SysIgnoreProc);

	SysRunThreadExitHooks(pTD != NULL ? (SYS_THREAD) pTD: SYS_INVALID_THREAD,
			      SYS_THREAD_ATTACH);

	if (pTD != NULL) {

	}

	return 0;
}

static void SysThreadCleanup(ThrData *pTD)
{
	SysRunThreadExitHooks(pTD != NULL ? (SYS_THREAD) pTD: SYS_INVALID_THREAD,
			      SYS_THREAD_DETACH);

	if (pTD != NULL) {
		pthread_mutex_lock(&pTD->Mtx);
		pTD->iThreadEnded = 1;
		pthread_cond_broadcast(&pTD->ExitWaitCond);
		if (--pTD->iUseCount == 0) {
			pthread_mutex_unlock(&pTD->Mtx);
			SysFreeThreadData(pTD);
		} else
			pthread_mutex_unlock(&pTD->Mtx);
	}
}

void SysCloseThread(SYS_THREAD ThreadID, int iForce)
{
	ThrData *pTD = (ThrData *) ThreadID;

	pthread_mutex_lock(&pTD->Mtx);
	pthread_detach(pTD->ThreadId);
	if (iForce && !pTD->iThreadEnded)
		pthread_cancel(pTD->ThreadId);
	if (--pTD->iUseCount == 0) {
		pthread_mutex_unlock(&pTD->Mtx);
		SysFreeThreadData(pTD);
	} else
		pthread_mutex_unlock(&pTD->Mtx);
}

int SysWaitThread(SYS_THREAD ThreadID, int iTimeout)
{
	ThrData *pTD = (ThrData *) ThreadID;

	pthread_mutex_lock(&pTD->Mtx);
	if (iTimeout == SYS_INFINITE_TIMEOUT) {
		while (!pTD->iThreadEnded) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &pTD->Mtx);
			pthread_cond_wait(&pTD->ExitWaitCond, &pTD->Mtx);
			pthread_cleanup_pop(0);
		}
	} else {
		int iRetCode = 0;
		struct timeval tvNow;
		struct timespec tsTimeout;

		gettimeofday(&tvNow, NULL);
		tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
		tsTimeout.tv_nsec = tvNow.tv_usec * 1000;
		while (!pTD->iThreadEnded && (iRetCode != ETIMEDOUT)) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pTD->Mtx);
			iRetCode = pthread_cond_timedwait(&pTD->ExitWaitCond, &pTD->Mtx,
							  &tsTimeout);
			pthread_cleanup_pop(0);
		}
		if (iRetCode == ETIMEDOUT) {
			pthread_mutex_unlock(&pTD->Mtx);

			ErrSetErrorCode(ERR_TIMEOUT);
			return ERR_TIMEOUT;
		}
	}
	pthread_mutex_unlock(&pTD->Mtx);

	return 0;
}

unsigned long SysGetCurrentThreadId(void)
{
	return (unsigned long) pthread_self();
}

static int SysSafeMsSleep(int iMsTimeout)
{
	struct pollfd Dummy;

	ZeroData(Dummy);
	return (poll(&Dummy, 0, iMsTimeout) == 0) ? 1: 0;
}

static int SysWaitPID(pid_t PID, int *piExitCode, int iTimeout)
{
	pid_t ExitPID;
	int iExitStatus, iStatus;

	iTimeout *= 1000;
	do {
		if ((ExitPID = (pid_t) waitpid(PID, &iStatus, WNOHANG)) == PID) {
			if (!WIFEXITED(iStatus))
				return ERR_WAITPID;

			iExitStatus = WEXITSTATUS(iStatus);
			break;
		}
		SysSafeMsSleep(WAIT_PID_TIME_STEP);
		iTimeout -= WAIT_PID_TIME_STEP;
	} while (iTimeout > 0);
	if (PID != ExitPID)
		return ERR_TIMEOUT;

	if (piExitCode != NULL)
		*piExitCode = iExitStatus;

	return 0;
}

int SysExec(const char *pszCommand, const char *const *pszArgs, int iWaitTimeout,
	    int iPriority, int *piExitStatus)
{
	int iExitStatus;
	pid_t ProcessID, ChildID, ExitPID;
	int iPPipe[2], iCPipe[2];

	if (pipe(iPPipe) == -1) {
		ErrSetErrorCode(ERR_PIPE);
		return ERR_PIPE;
	}
	if (pipe(iCPipe) == -1) {
		close(iPPipe[1]);
		close(iPPipe[0]);
		ErrSetErrorCode(ERR_PIPE);
		return ERR_PIPE;
	}

	ProcessID = fork();
	if (ProcessID == 0) {
		ChildID = fork();
		if (ChildID == 0) {
			close(iPPipe[1]);
			close(iPPipe[0]);

			/* Wait for the unlock from the parent */
			read(iCPipe[0], &ChildID, sizeof(ChildID));

			close(iCPipe[1]);
			close(iCPipe[0]);

			/* Execute the command */
			execv(pszCommand, (char **) pszArgs);

			/* We can only use async-signal safe functions, so we use write() directly */
			write(2, "execv error: cmd='", 18);
			write(2, pszCommand, strlen(pszCommand));
			write(2, "'\n", 2);

			_exit(WAIT_ERROR_EXIT_STATUS);
		}

		close(iCPipe[1]);
		close(iCPipe[0]);

		/* Tell the parent about the child-child PID */
		write(iPPipe[1], &ChildID, sizeof(ChildID));

		close(iPPipe[1]);
		close(iPPipe[0]);

		if (ChildID == (pid_t) -1)
			_exit(WAIT_ERROR_EXIT_STATUS);

		/* Wait for the child */
		iExitStatus = WAIT_TIMEO_EXIT_STATUS;
		if (iWaitTimeout > 0)
			SysWaitPID(ChildID, &iExitStatus, iWaitTimeout);

		_exit(iExitStatus);
	}

	if (ProcessID == (pid_t) -1 ||
	    read(iPPipe[0], &ChildID, sizeof(ChildID)) != sizeof(ChildID)) {
		close(iCPipe[1]);
		close(iCPipe[0]);
		close(iPPipe[1]);
		close(iPPipe[0]);
		ErrSetErrorCode(ERR_FORK);
		return ERR_FORK;
	}
	close(iPPipe[1]);
	close(iPPipe[0]);

	if (ChildID != (pid_t) -1) {
		/* Set process priority */
		switch (iPriority) {
		case SYS_PRIORITY_NORMAL:
			setpriority(PRIO_PROCESS, ChildID, 0);
			break;

		case SYS_PRIORITY_LOWER:
			setpriority(PRIO_PROCESS, ChildID, SCHED_PRIORITY_INC);
			break;

		case SYS_PRIORITY_HIGHER:
			setpriority(PRIO_PROCESS, ChildID, -SCHED_PRIORITY_INC);
			break;
		}

		/* Unlock the child */
		write(iCPipe[1], &ChildID, sizeof(ChildID));
	}
	close(iCPipe[1]);
	close(iCPipe[0]);

	/* Wait for completion (or timeout) */
	while (((ExitPID = (pid_t) waitpid(ProcessID, &iExitStatus, 0)) != ProcessID) &&
	       (errno == EINTR));

	if (ExitPID == ProcessID && WIFEXITED(iExitStatus))
		iExitStatus = WEXITSTATUS(iExitStatus);
	else
		iExitStatus = WAIT_TIMEO_EXIT_STATUS;

	if (iWaitTimeout > 0) {
		if (iExitStatus == WAIT_TIMEO_EXIT_STATUS) {
			ErrSetErrorCode(ERR_TIMEOUT);
			return ERR_TIMEOUT;
		}
		if (iExitStatus == WAIT_ERROR_EXIT_STATUS) {
			ErrSetErrorCode(ERR_FORK);
			return ERR_FORK;
		}
	} else
		iExitStatus = -1;

	if (piExitStatus != NULL)
		*piExitStatus = iExitStatus;

	return 0;
}

static void SysBreakHandlerRoutine(int iSignal)
{
	if (SysBreakHandler != NULL)
		SysBreakHandler();
	SysSetSignal(iSignal, SysBreakHandlerRoutine);
}

void SysSetBreakHandler(void (*BreakHandler) (void))
{
	SysBreakHandler = BreakHandler;

	/* Setup signal handlers and enable signals */
	SysSetSignal(SIGINT, SysBreakHandlerRoutine);
	SysSetSignal(SIGHUP, SysBreakHandlerRoutine);

	sigset_t SigMask;

	sigemptyset(&SigMask);
	sigaddset(&SigMask, SIGINT);
	sigaddset(&SigMask, SIGHUP);

	pthread_sigmask(SIG_UNBLOCK, &SigMask, NULL);

}

int SysCreateTlsKey(SYS_TLSKEY &TlsKey, void (*pFreeProc) (void *))
{
	if (pthread_key_create(&TlsKey, pFreeProc) != 0) {
		ErrSetErrorCode(ERR_NOMORE_TLSKEYS);
		return ERR_NOMORE_TLSKEYS;
	}

	return 0;
}

int SysDeleteTlsKey(SYS_TLSKEY &TlsKey)
{
	pthread_key_delete(TlsKey);

	return 0;
}

int SysSetTlsKeyData(SYS_TLSKEY &TlsKey, void *pData)
{
	if (pthread_setspecific(TlsKey, pData) != 0) {
		ErrSetErrorCode(ERR_INVALID_TLSKEY);
		return ERR_INVALID_TLSKEY;
	}

	return 0;
}

void *SysGetTlsKeyData(SYS_TLSKEY &TlsKey)
{
	return pthread_getspecific(TlsKey);
}

void SysThreadOnce(SYS_THREAD_ONCE *pThrOnce, void (*pOnceProc) (void))
{
	pthread_once(pThrOnce, pOnceProc);
}

void *SysAllocNZ(size_t uSize)
{
	void *pData = malloc(uSize);

	if (pData == NULL)
		ErrSetErrorCode(ERR_MEMORY);

	return pData;
}

void *SysAlloc(size_t uSize)
{
	void *pData = SysAllocNZ(uSize);

	if (pData != NULL)
		memset(pData, 0, uSize);

	return pData;
}

void SysFree(void *pData)
{
	free(pData);
}

void *SysRealloc(void *pData, size_t uSize)
{
	void *pNewData = realloc(pData, uSize);

	if (pNewData == NULL)
		ErrSetErrorCode(ERR_MEMORY);

	return pNewData;
}

int SysLockFile(const char *pszFileName, const char *pszLockExt)
{
	int iFileID;
	char szLockFile[SYS_MAX_PATH] = "";

	snprintf(szLockFile, sizeof(szLockFile) - 1, "%s%s", pszFileName, pszLockExt);
	if ((iFileID = open(szLockFile, O_CREAT | O_EXCL | O_RDWR,
			    S_IREAD | S_IWRITE)) == -1) {
		ErrSetErrorCode(ERR_LOCKED);
		return ERR_LOCKED;
	}

	char szLock[128] = "";

	sprintf(szLock, "%lu", (unsigned long) SysGetCurrentThreadId());
	write(iFileID, szLock, strlen(szLock) + 1);
	close(iFileID);

	return 0;
}

int SysUnlockFile(const char *pszFileName, const char *pszLockExt)
{
	char szLockFile[SYS_MAX_PATH] = "";

	snprintf(szLockFile, sizeof(szLockFile) - 1, "%s%s", pszFileName, pszLockExt);
	if (unlink(szLockFile) != 0) {
		ErrSetErrorCode(ERR_NOT_LOCKED);
		return ERR_NOT_LOCKED;
	}

	return 0;
}

SYS_HANDLE SysOpenModule(const char *pszFilePath)
{
	void *pModule = dlopen(pszFilePath, RTLD_LAZY);

	if (pModule == NULL) {
		ErrSetErrorCode(ERR_LOADMODULE, pszFilePath);
		return SYS_INVALID_HANDLE;
	}

	return (SYS_HANDLE) pModule;
}

int SysCloseModule(SYS_HANDLE hModule)
{
	dlclose((void *) hModule);

	return 0;
}

void *SysGetSymbol(SYS_HANDLE hModule, const char *pszSymbol)
{
	void *pSymbol = dlsym((void *) hModule, pszSymbol);

	if (pSymbol == NULL) {
		ErrSetErrorCode(ERR_LOADMODULESYMBOL, pszSymbol);
		return NULL;
	}

	return pSymbol;
}

int SysEventLogV(int iLogLevel, const char *pszFormat, va_list Args)
{
	openlog(APP_NAME_STR, LOG_PID, LOG_DAEMON);

	char szBuffer[2048] = "";

	vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);
	syslog(LOG_DAEMON | LOG_ERR, "%s", szBuffer);
	closelog();

	return 0;
}

int SysEventLog(int iLogLevel, const char *pszFormat, ...)
{
	va_list Args;

	va_start(Args, pszFormat);

	int iLogResult = SysEventLogV(iLogLevel, pszFormat, Args);

	va_end(Args);

	return 0;
}

int SysLogMessage(int iLogLevel, const char *pszFormat, ...)
{
	extern bool bServerDebug;

	pthread_mutex_lock(&LogMutex);

	va_list Args;

	va_start(Args, pszFormat);
	if (bServerDebug) {
		/* Debug implementation */
		vprintf(pszFormat, Args);
	} else {
		switch (iLogLevel) {
		case LOG_LEV_WARNING:
		case LOG_LEV_ERROR:
			SysEventLogV(iLogLevel, pszFormat, Args);
			break;
		}
	}
	va_end(Args);
	pthread_mutex_unlock(&LogMutex);

	return 0;
}

void SysSleep(int iTimeout)
{
	SysMsSleep(iTimeout * 1000);
}

static int SysSetupWait(WaitData *pWD)
{
	if (pthread_mutex_init(&pWD->Mtx, NULL) != 0) {
		ErrSetErrorCode(ERR_MUTEXINIT);
		return ERR_MUTEXINIT;
	}
	if (pthread_cond_init(&pWD->WaitCond, NULL) != 0) {
		pthread_mutex_destroy(&pWD->Mtx);
		ErrSetErrorCode(ERR_CONDINIT);
		return ERR_CONDINIT;
	}

	return 0;
}

static int SysWait(WaitData *pWD, int iMsTimeout)
{
	struct timespec TV;
	struct timeval TmNow;
	int iErrorCode;

	gettimeofday(&TmNow, NULL);

	TmNow.tv_sec += iMsTimeout / 1000;
	TmNow.tv_usec += (iMsTimeout % 1000) * 1000;
	TmNow.tv_sec += TmNow.tv_usec / 1000000;
	TmNow.tv_usec %= 1000000;

	TV.tv_sec = TmNow.tv_sec;
	TV.tv_nsec = TmNow.tv_usec * 1000;

	pthread_mutex_lock(&pWD->Mtx);
	pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock, &pWD->Mtx);

	iErrorCode = pthread_cond_timedwait(&pWD->WaitCond, &pWD->Mtx, &TV);

	pthread_cleanup_pop(1);

	if (iErrorCode == ETIMEDOUT) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}

	return 0;
}

static void SysCleanupWait(WaitData *pWD)
{
	pthread_mutex_destroy(&pWD->Mtx);
	pthread_cond_destroy(&pWD->WaitCond);
}

void SysMsSleep(int iMsTimeout)
{
	WaitData WD;

	if (SysSetupWait(&WD) == 0) {
		SysWait(&WD, iMsTimeout);
		SysCleanupWait(&WD);
	}
}

SYS_INT64 SysMsTime(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0;

	return 1000 * (SYS_INT64) tv.tv_sec + (SYS_INT64) tv.tv_usec / 1000;
}

int SysExistFile(const char *pszFilePath)
{
	struct stat FStat;

	if (stat(pszFilePath, &FStat) != 0)
		return 0;

	return (S_ISDIR(FStat.st_mode)) ? 0: 1;
}

int SysExistDir(const char *pszDirPath)
{
	struct stat FStat;

	if (stat(pszDirPath, &FStat) != 0)
		return 0;

	return (S_ISDIR(FStat.st_mode)) ? 1: 0;
}

SYS_HANDLE SysFirstFile(const char *pszPath, char *pszFileName, int iSize)
{
	DIR *pDIR = opendir(pszPath);

	if (pDIR == NULL) {
		ErrSetErrorCode(ERR_OPENDIR, pszPath); /* [i_a] */
		return SYS_INVALID_HANDLE;
	}

	FilledDirent FDE;
	struct dirent *pDirEntry = NULL;

#if (_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

	readdir_r(pDIR, &FDE.DE, &pDirEntry);

#else

	pDirEntry = readdir_r(pDIR, &FDE.DE);

#endif

	if (pDirEntry == NULL) {
		closedir(pDIR);
		return SYS_INVALID_HANDLE;
	}

	FileFindData *pFFD = (FileFindData *) SysAlloc(sizeof(FileFindData));

	if (pFFD == NULL) {
		closedir(pDIR);
		return SYS_INVALID_HANDLE;
	}

	strcpy(pFFD->szPath, pszPath);
	AppendSlash(pFFD->szPath);
	pFFD->pDIR = pDIR;
	pFFD->FDE = FDE;

	StrNCpy(pszFileName, pFFD->FDE.DE.d_name, iSize);

	char szFilePath[SYS_MAX_PATH] = "";

	snprintf(szFilePath, sizeof(szFilePath) - 1, "%s%s", pFFD->szPath,
		 pFFD->FDE.DE.d_name);
	if (stat(szFilePath, &pFFD->FStat) != 0) {
		SysFree(pFFD);
		closedir(pDIR);

		ErrSetErrorCode(ERR_STAT, szFilePath); /* [i_a] */
		return SYS_INVALID_HANDLE;
	}

	return (SYS_HANDLE) pFFD;
}

int SysIsDirectory(SYS_HANDLE hFind)
{
	FileFindData *pFFD = (FileFindData *) hFind;

	return (S_ISDIR(pFFD->FStat.st_mode)) ? 1: 0;
}

SYS_OFF_T SysGetSize(SYS_HANDLE hFind)
{
	FileFindData *pFFD = (FileFindData *) hFind;

	return pFFD->FStat.st_size;
}

int SysNextFile(SYS_HANDLE hFind, char *pszFileName, int iSize)
{
	FileFindData *pFFD = (FileFindData *) hFind;
	struct dirent *pDirEntry = NULL;

#if (_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

	readdir_r(pFFD->pDIR, &pFFD->FDE.DE, &pDirEntry);

#else

	pDirEntry = readdir_r(pFFD->pDIR, &pFFD->FDE.DE);

#endif

	if (pDirEntry == NULL)
		return 0;

	StrNCpy(pszFileName, pFFD->FDE.DE.d_name, iSize);

	char szFilePath[SYS_MAX_PATH] = "";

	snprintf(szFilePath, sizeof(szFilePath) - 1, "%s%s", pFFD->szPath, pFFD->FDE.DE.d_name);
	if (stat(szFilePath, &pFFD->FStat) != 0) {
		ErrSetErrorCode(ERR_STAT, szFilePath); /* [i_a] */
		return 0;
	}

	return 1;
}

void SysFindClose(SYS_HANDLE hFind)
{
	FileFindData *pFFD = (FileFindData *) hFind;

	closedir(pFFD->pDIR);
	SysFree(pFFD);
}

int SysGetFileInfo(const char *pszFileName, SYS_FILE_INFO &FI)
{
	struct stat stat_buffer;

	if (stat(pszFileName, &stat_buffer) != 0) {
		ErrSetErrorCode(ERR_STAT, pszFileName); /* [i_a] */
		return ERR_STAT;
	}

	ZeroData(FI);
	FI.iFileType = (S_ISREG(stat_buffer.st_mode)) ? ftNormal:
		((S_ISDIR(stat_buffer.st_mode)) ?ftDirectory:
		 ((S_ISLNK(stat_buffer.st_mode)) ? ftLink: ftOther));
	FI.llSize = stat_buffer.st_size;
	FI.tMod = stat_buffer.st_mtime;

	return 0;
}

int SysSetFileModTime(const char *pszFileName, time_t tMod)
{
	struct utimbuf TMB;

	TMB.actime = tMod;
	TMB.modtime = tMod;
	if (utime(pszFileName, &TMB) != 0) {
		ErrSetErrorCode(ERR_SET_FILE_TIME);
		return ERR_SET_FILE_TIME;
	}

	return 0;
}

char *SysStrDup(const char *pszString)
{
	int iStrLength = strlen(pszString);
	char *pszBuffer = (char *) SysAllocNZ(iStrLength + 1);

	if (pszBuffer != NULL)
		memcpy(pszBuffer, pszString, iStrLength + 1);

	return pszBuffer;
}

char *SysGetEnv(const char *pszVarName)
{
	const char *pszValue = getenv(pszVarName);

	return (pszValue != NULL) ? SysStrDup(pszValue): NULL;
}

char *SysGetTmpFile(char *pszFileName)
{
	unsigned long ulThreadID = SysGetCurrentThreadId(), ulFileID;
	static unsigned long ulFileSeqNr = 0;
	static pthread_mutex_t TmpFMutex = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&TmpFMutex);
	ulFileID = ++ulFileSeqNr;
	pthread_mutex_unlock(&TmpFMutex);

	SysSNPrintf(pszFileName, SYS_MAX_PATH - 1, "/tmp/msrv%lx.%lx.tmp", ulThreadID,
		    ulFileID);

	return pszFileName;
}

int SysRemove(const char *pszFileName)
{
	if (unlink(pszFileName) != 0) {
		ErrSetErrorCode(ERR_FILE_DELETE, pszFileName); /* [i_a] */
		return ERR_FILE_DELETE;
	}

	return 0;
}

int SysMakeDir(const char *pszPath)
{
	if (mkdir(pszPath, 0700) != 0) {
		ErrSetErrorCode(ERR_DIR_CREATE, pszPath); /* [i_a] */
		return ERR_DIR_CREATE;
	}

	return 0;
}

int SysRemoveDir(const char *pszPath)
{
	if (rmdir(pszPath) != 0) {
		ErrSetErrorCode(ERR_DIR_DELETE, pszPath); /* [i_a] */
		return ERR_DIR_DELETE;
	}

	return 0;
}

int SysMoveFile(const char *pszOldName, const char *pszNewName)
{
	if (rename(pszOldName, pszNewName) != 0) {
		ErrSetErrorCode(ERR_FILE_MOVE);
		return ERR_FILE_MOVE;
	}

	return 0;
}

int SysVSNPrintf(char *pszBuffer, int iSize, const char *pszFormat, va_list Args)
{
	int iPrintResult = vsnprintf(pszBuffer, iSize, pszFormat, Args);

	return (iPrintResult < iSize) ? iPrintResult: -1;
}

int SysFileSync(FILE *pFile)
{
	if (fflush(pFile) || fsync(fileno(pFile))) {
		ErrSetErrorCode(ERR_FILE_WRITE);
		return ERR_FILE_WRITE;
	}

	return 0;
}

char *SysStrTok(char *pszData, const char *pszDelim, char **ppszSavePtr)
{
	return strtok_r(pszData, pszDelim, ppszSavePtr);
}

char *SysCTime(time_t *pTimer, char *pszBuffer, int iBufferSize)
{
#if (_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

	return ctime_r(pTimer, pszBuffer);
#else

	return ctime_r(pTimer, pszBuffer, iBufferSize);
#endif
}

struct tm *SysLocalTime(time_t *pTimer, struct tm *pTStruct)
{
	return localtime_r(pTimer, pTStruct);
}

struct tm *SysGMTime(time_t *pTimer, struct tm *pTStruct)
{
	return gmtime_r(pTimer, pTStruct);
}

char *SysAscTime(struct tm *pTStruct, char *pszBuffer, int iBufferSize)
{
#if (_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

	return asctime_r(pTStruct, pszBuffer);
#else

	return asctime_r(pTStruct, pszBuffer, iBufferSize);
#endif
}

long SysGetDayLight(void)
{
	time_t tCurr = time(NULL);
	struct tm tmCurr;

	localtime_r(&tCurr, &tmCurr);

	return (long) ((tmCurr.tm_isdst <= 0) ? 0: 3600);
}

SYS_MMAP SysCreateMMap(const char *pszFileName, unsigned long ulFlags)
{
	int iFD = open(pszFileName, (ulFlags & SYS_MMAP_WRITE) ? O_RDWR: O_RDONLY);

	if (iFD < 0) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName); /* [i_a] */
		return SYS_INVALID_MMAP;
	}

	struct stat StatBuf;

	if (fstat(iFD, &StatBuf) < 0) {
		close(iFD);
		ErrSetErrorCode(ERR_STAT, pszFileName); /* [i_a] */
		return SYS_INVALID_MMAP;
	}

	MMapData *pMMD = (MMapData *) SysAlloc(sizeof(MMapData));

	if (pMMD == NULL) {
		close(iFD);
		return SYS_INVALID_MMAP;
	}

	pMMD->ulPageSize = (unsigned long) sysconf(_SC_PAGESIZE);
	pMMD->iFD = iFD;
	pMMD->iNumMaps = 0;
	pMMD->ulFlags = ulFlags;
	pMMD->llFileSize = (SYS_OFF_T) StatBuf.st_size;

	return (SYS_MMAP) pMMD;
}

void SysCloseMMap(SYS_MMAP hMap)
{
	MMapData *pMMD = (MMapData *) hMap;

	if (pMMD->iNumMaps > 0) {

	}
	close(pMMD->iFD);
	SysFree(pMMD);
}

SYS_OFF_T SysMMapSize(SYS_MMAP hMap)
{
	MMapData *pMMD = (MMapData *) hMap;

	return pMMD->llFileSize;
}

SYS_OFF_T SysMMapOffsetAlign(SYS_MMAP hMap, SYS_OFF_T llOffset)
{
	MMapData *pMMD = (MMapData *) hMap;

	return llOffset & ~((SYS_OFF_T) pMMD->ulPageSize - 1);
}

void *SysMapMMap(SYS_MMAP hMap, SYS_OFF_T llOffset, SYS_SIZE_T lSize)
{
	MMapData *pMMD = (MMapData *) hMap;

	if (llOffset % pMMD->ulPageSize) {
		ErrSetErrorCode(ERR_INVALID_MMAP_OFFSET);
		return NULL;
	}

	int iMapFlags = 0;

	if (pMMD->ulFlags & SYS_MMAP_READ)
		iMapFlags |= PROT_READ;
	if (pMMD->ulFlags & SYS_MMAP_WRITE)
		iMapFlags |= PROT_WRITE;

	void *pMapAddress = (void *) mmap((char *) 0, (size_t) lSize, iMapFlags,
					  MAP_SHARED, pMMD->iFD, llOffset);

	if (pMapAddress == (void *) -1) {
		ErrSetErrorCode(ERR_MMAP);
		return NULL;
	}
	pMMD->iNumMaps++;

	return pMapAddress;
}

int SysUnmapMMap(SYS_MMAP hMap, void *pAddr, SYS_SIZE_T lSize)
{
	MMapData *pMMD = (MMapData *) hMap;

	if (munmap((char *) pAddr, lSize) < 0) {
		ErrSetErrorCode(ERR_MUNMAP);
		return ERR_MUNMAP;
	}
	pMMD->iNumMaps--;

	return 0;
}

