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
#include "SysDepUnix.h"
#include "AppDefines.h"

#define SCHED_PRIORITY_INC     5
#define SYS_MAX_TH_EXIT_HOOKS  64
#define SYS_PWAIT_HASHSIZE     101

#define SYSERR_EXEC            255

#define SYS_INT_CALL()       (!iShutDown && (errno == EINTR))
#define SYS_STDERR_WRITE(s)  write(2, s, strlen(s))

struct PidWaitData {
	SysListHead Lnk;
	int iPipeFds[2];
	pid_t PID;
	int iExitStatus;
	int iTermSignal;
};

static int iNumThExitHooks;
static volatile int iShutDown;
static pthread_t SigThreadID;
static int iSignalPipe[2];
static SYS_EVENTFD hShdwnEventfd;
static pthread_mutex_t PWaitMutex = PTHREAD_MUTEX_INITIALIZER;
static SysListHead PWaitLists[SYS_PWAIT_HASHSIZE];
static ThreadExitHook ThExitHooks[SYS_MAX_TH_EXIT_HOOKS];
static pthread_mutex_t LogMutex = PTHREAD_MUTEX_INITIALIZER;
static void (*pSysBreakHandler) (void) = NULL;
static int iSndBufSize = -1, iRcvBufSize = -1;

static int SysSetSignal(int iSigNo, void (*pSigProc) (int))
{
	signal(iSigNo, pSigProc);

	return 0;
}

static void SysRunThreadExitHooks(SYS_THREAD ThreadID, int iMode)
{
	int i;

	for (i = iNumThExitHooks - 1; i >= 0; i--)
		(*ThExitHooks[i].pfHook)(ThExitHooks[i].pPrivate, ThreadID, iMode);
}

static void SysGetTimeOfDay(struct timespec *pTS)
{
	struct timeval tvNow;

	gettimeofday(&tvNow, NULL);
	pTS->tv_sec = tvNow.tv_sec;
	pTS->tv_nsec = tvNow.tv_usec * 1000L;
}

static void SysTimeAddOffset(struct timespec *pTS, long iMsOffset)
{
	pTS->tv_sec += iMsOffset / 1000;
	pTS->tv_nsec += (iMsOffset % 1000) * 1000000L;
	if (pTS->tv_nsec >= 1000000000L) {
		pTS->tv_sec += pTS->tv_nsec / 1000000000L;
		pTS->tv_nsec %= 1000000000L;
	}
}

int SysBlockFD(int iFD, int iBlocking)
{
	long lFlags;

	if ((lFlags = fcntl(iFD, F_GETFL, 0)) == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}
	if (iBlocking == 0)
		lFlags |= O_NONBLOCK;
	else
		lFlags &= ~O_NONBLOCK;
	if (fcntl(iFD, F_SETFL, lFlags) == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	return 0;
}

int SysFdWait(int iFD, unsigned int uEvents, int iTimeout)
{
	int iError;
	struct pollfd PollFD[2];

	ZeroData(PollFD);
	PollFD[0].fd = iFD;
	PollFD[0].events = uEvents;
	PollFD[1].fd = SysEventfdWaitFD(hShdwnEventfd);
	PollFD[1].events = POLLIN;
	if ((iError = poll(PollFD, 2, iTimeout)) == -1) {
		ErrSetErrorCode(ERR_WAIT);
		return ERR_WAIT;
	}
	if (iError == 0) {
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	if (PollFD[1].revents & POLLIN) {
		ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
		return ERR_SERVER_SHUTDOWN;
	}

	return 0;
}

static int SysWaitInit(WaitCond *pWC)
{
	if (pthread_mutex_init(&pWC->Mtx, NULL) != 0) {
		ErrSetErrorCode(ERR_MUTEXINIT);
		return ERR_MUTEXINIT;
	}
	if (pthread_cond_init(&pWC->Cond, NULL) != 0) {
		pthread_mutex_destroy(&pWC->Mtx);
		ErrSetErrorCode(ERR_CONDINIT);
		return ERR_CONDINIT;
	}

	return 0;
}

static void SysWaitCleanup(WaitCond *pWC)
{
	pthread_cond_destroy(&pWC->Cond);
	pthread_mutex_destroy(&pWC->Mtx);
}

static void SysWaitLock(WaitCond *pWC)
{
	pthread_mutex_lock(&pWC->Mtx);
}

static void SysWaitUnlock(WaitCond *pWC)
{
	pthread_mutex_unlock(&pWC->Mtx);
}

/*
 * This is supposed to be called while holding the lock (that is, inside
 * a SysWaitLock() / SysWaitUnlock() block).
 */
static void SysWaitWakeup(WaitCond *pWC, int iCount)
{
	if (iCount <= 0)
		pthread_cond_broadcast(&pWC->Cond);
	else
		pthread_cond_signal(&pWC->Cond);
}

static int SysWaitWait(WaitCond *pWC, int iTimeout, int (*pCondCB)(void *),
		       void (*pPostCB)(void *), void *pPrivate)
{
	pthread_mutex_lock(&pWC->Mtx);
	if (iTimeout == SYS_INFINITE_TIMEOUT) {
		while (!(*pCondCB)(pPrivate)) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pWC->Mtx);
			pthread_cond_wait(&pWC->Cond, &pWC->Mtx);
			pthread_cleanup_pop(0);
		}
	} else {
		int iRetCode = 0;
		struct timespec tsExpire;

		SysGetTimeOfDay(&tsExpire);
		SysTimeAddOffset(&tsExpire, iTimeout);
		while (!(*pCondCB)(pPrivate) && iRetCode != ETIMEDOUT) {
			pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
					     &pWC->Mtx);
			iRetCode = pthread_cond_timedwait(&pWC->Cond, &pWC->Mtx,
							  &tsExpire);
			pthread_cleanup_pop(0);
		}
		if (iRetCode == ETIMEDOUT) {
			pthread_mutex_unlock(&pWC->Mtx);

			ErrSetErrorCode(ERR_TIMEOUT);
			return ERR_TIMEOUT;
		}
	}
	if (pPostCB != NULL)
		(*pPostCB)(pPrivate);
	pthread_mutex_unlock(&pWC->Mtx);

	return 0;
}

static void SysPostSignal(int iSignal)
{
	unsigned char uSignal;

	if (iSignal > 0 && iSignal <= 255) {
		/*
		 * write(2) is async-signal-safe, so we should have no worry
		 * in using it inside the signal handler.
		 * The pipe write fd is non blocking, so we our handler should
		 * return pretty fast.
		 */
		uSignal = (unsigned char) iSignal;
		if (write(iSignalPipe[1], &uSignal,
			  sizeof(uSignal)) != sizeof(uSignal))
			SYS_STDERR_WRITE("Signal pipe full!\n");
	} else
		SYS_STDERR_WRITE("Signal out of range!\n");
	SysSetSignal(iSignal, SysPostSignal);
}

static void SysSetupProcessSignals(void)
{
	sigset_t SigMask;

	sigemptyset(&SigMask);
	sigaddset(&SigMask, SIGALRM);
	sigaddset(&SigMask, SIGINT);
	sigaddset(&SigMask, SIGHUP);
	sigaddset(&SigMask, SIGSTOP);
	sigaddset(&SigMask, SIGCHLD);
	sigaddset(&SigMask, SIGQUIT);
	sigaddset(&SigMask, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &SigMask, NULL);

	SysSetSignal(SIGPIPE, SIG_IGN);
	SysSetSignal(SIGTERM, SysPostSignal);
	SysSetSignal(SIGQUIT, SysPostSignal);
	SysSetSignal(SIGINT, SysPostSignal);
	SysSetSignal(SIGHUP, SysPostSignal);
	SysSetSignal(SIGCHLD, SysPostSignal);
	SysSetSignal(SIGALRM, SysPostSignal);
}

static PidWaitData *SysCreatePidWait(pid_t PID)
{
	PidWaitData *pPWD;

	if ((pPWD = (PidWaitData *) SysAlloc(sizeof(PidWaitData))) == NULL)
		return NULL;
	if (pipe(pPWD->iPipeFds) == -1) {
		SysFree(pPWD);
		ErrSetErrorCode(ERR_PIPE);
		return NULL;
	}
	SysBlockFD(pPWD->iPipeFds[0], 0);
	pPWD->PID = PID;
	pPWD->iExitStatus = pPWD->iTermSignal = -1;

	pthread_mutex_lock(&PWaitMutex);
	SYS_LIST_ADDT(&pPWD->Lnk, &PWaitLists[PID % SYS_PWAIT_HASHSIZE]);
	pthread_mutex_unlock(&PWaitMutex);

	return pPWD;
}

static void SysFreePidWait(PidWaitData *pPWD)
{
	if (pPWD != NULL) {
		pthread_mutex_lock(&PWaitMutex);
		SYS_LIST_DEL(&pPWD->Lnk);
		pthread_mutex_unlock(&PWaitMutex);
		SYS_CLOSE_PIPE(pPWD->iPipeFds);
		SysFree(pPWD);
	}
}

static void SysHandle_SIGCHLD(void)
{
	int iStatus;
	pid_t ExitPID;
	SysListHead *pPos, *pHead;
	PidWaitData *pPWD;

	while ((ExitPID = waitpid(-1, &iStatus, WNOHANG)) > 0) {
		pHead = &PWaitLists[ExitPID % SYS_PWAIT_HASHSIZE];

		pthread_mutex_lock(&PWaitMutex);
		for (pPos = SYS_LIST_FIRST(pHead); pPos != NULL;) {
			pPWD = SYS_LIST_ENTRY(pPos, PidWaitData, Lnk);
			pPos = SYS_LIST_NEXT(pPos, pHead);
			if (pPWD->PID == ExitPID) {
				if (WIFEXITED(iStatus))
					pPWD->iExitStatus = WEXITSTATUS(iStatus);
				if (WIFSIGNALED(iStatus))
					pPWD->iTermSignal = WTERMSIG(iStatus);
				SYS_LIST_DEL(&pPWD->Lnk);
				write(pPWD->iPipeFds[1], ".", 1);
			}
		}
		pthread_mutex_unlock(&PWaitMutex);
	}
}

static void *SysSignalThread(void *pData)
{
	unsigned char uSignal;
	sigset_t SigMask;

	sigemptyset(&SigMask);
	sigaddset(&SigMask, SIGALRM);
	sigaddset(&SigMask, SIGINT);
	sigaddset(&SigMask, SIGQUIT);
	sigaddset(&SigMask, SIGTERM);
	sigaddset(&SigMask, SIGHUP);
	sigaddset(&SigMask, SIGCHLD);
	pthread_sigmask(SIG_UNBLOCK, &SigMask, NULL);

	for (;!iShutDown;) {
		if (SysFdWait(iSignalPipe[0], POLLIN, -1) < 0 ||
		    read(iSignalPipe[0], &uSignal,
			 sizeof(uSignal)) != sizeof(uSignal))
			continue;

		switch (uSignal) {
		case SIGTERM:
			iShutDown++;
			break;

		case SIGQUIT:
		case SIGINT:
		case SIGHUP:
			if (pSysBreakHandler != NULL)
				(*pSysBreakHandler)();
			break;

		case SIGCHLD:
			SysHandle_SIGCHLD();
			break;

		case SIGALRM:
			break;
		}
	}

	return NULL;
}

static int SysThreadSetup(ThrData *pTD)
{
	SysRunThreadExitHooks(pTD != NULL ? (SYS_THREAD) pTD: SYS_INVALID_THREAD,
			      SYS_THREAD_ATTACH);
	if (pTD != NULL) {

	}

	return 0;
}

static int SysFreeThreadData(ThrData *pTD)
{
	SysWaitCleanup(&pTD->Wait);
	SysFree(pTD);

	return 0;
}

static void SysThreadExit(ThrData *pTD)
{
	SysWaitLock(&pTD->Wait);
	pTD->iThreadEnded = 1;
	SysWaitWakeup(&pTD->Wait, 0);
	if (--pTD->iUseCount == 0) {
		SysWaitUnlock(&pTD->Wait);
		SysFreeThreadData(pTD);
	} else
		SysWaitUnlock(&pTD->Wait);
}

static void SysThreadCleanup(ThrData *pTD)
{
	SysRunThreadExitHooks(pTD != NULL ? (SYS_THREAD) pTD: SYS_INVALID_THREAD,
			      SYS_THREAD_DETACH);
	if (pTD != NULL)
		SysThreadExit(pTD);
}

int SysInitLibrary(void)
{
	int i;

	iShutDown = 0;
	iNumThExitHooks = 0;
	for (i = 0; i < (int) CountOf(PWaitLists); i++)
		SYS_INIT_LIST_HEAD(&PWaitLists[i]);
	tzset();
	if (SysDepInitLibrary() < 0)
		return ErrGetErrorCode();
	if ((hShdwnEventfd = SysEventfdCreate()) == SYS_INVALID_EVENTFD) {
		SysDepCleanupLibrary();
		return ErrGetErrorCode();
	}
	if (pipe(iSignalPipe) == -1) {
		SysEventfdClose(hShdwnEventfd);
		SysDepCleanupLibrary();
		ErrSetErrorCode(ERR_PIPE);
		return ERR_PIPE;
	}
	SysBlockFD(iSignalPipe[1], 0);
	SysSetupProcessSignals();
	if (pthread_create(&SigThreadID, NULL, SysSignalThread, NULL) != 0) {
		SYS_CLOSE_PIPE(iSignalPipe);
		SysEventfdClose(hShdwnEventfd);
		SysDepCleanupLibrary();
		ErrSetErrorCode(ERR_THREADCREATE);
		return ERR_THREADCREATE;
	}
	if (SysThreadSetup(NULL) < 0) {
		SYS_CLOSE_PIPE(iSignalPipe);
		SysEventfdClose(hShdwnEventfd);
		SysDepCleanupLibrary();
		return ErrGetErrorCode();
	}
	SRand();

	return 0;
}

static void SysPostShutdown(void)
{
	iShutDown++;
	SysEventfdSet(hShdwnEventfd);
	kill(getpid(), SIGTERM);
}

void SysCleanupLibrary(void)
{
	SysPostShutdown();
	SysThreadCleanup(NULL);
	pthread_join(SigThreadID, NULL);
	SYS_CLOSE_PIPE(iSignalPipe);
	SysEventfdClose(hShdwnEventfd);
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

int SysShutdownLibrary(int iMode)
{
	SysPostShutdown();

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

static int SysSetSocketsOptions(SYS_SOCKET SockFD)
{
	int iSize, iActivate;
	struct linger Ling;

	if (iSndBufSize > 0) {
		iSize = iSndBufSize;
		setsockopt((int) SockFD, SOL_SOCKET, SO_SNDBUF, (char const *) &iSize,
			   sizeof(iSize));
	}
	if (iRcvBufSize > 0) {
		iSize = iRcvBufSize;
		setsockopt((int) SockFD, SOL_SOCKET, SO_RCVBUF, (char const *) &iSize,
			   sizeof(iSize));
	}
	iActivate = 1;
	if (setsockopt(SockFD, SOL_SOCKET, SO_REUSEADDR, (char const *) &iActivate,
		       sizeof(iActivate)) != 0) {
		ErrSetErrorCode(ERR_SETSOCKOPT);
		return ERR_SETSOCKOPT;
	}
	/* Disable linger */
	ZeroData(Ling);

	setsockopt(SockFD, SOL_SOCKET, SO_LINGER, (char const *) &Ling, sizeof(Ling));

	/* Set KEEPALIVE if supported */
	setsockopt(SockFD, SOL_SOCKET, SO_KEEPALIVE, (char const *) &iActivate,
		   sizeof(iActivate));

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
	return SysBlockFD((int) SockFD, iBlocking);
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
	int iRecvBytes;

	if (SysFdWait((int) SockFD, POLLIN, iTimeout) < 0)
		return ErrGetErrorCode();

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
	int iRtxBytes = 0, iRtxCurrent;

	while (iRtxBytes < iBufferSize) {
		iRtxCurrent = SysRecvData(SockFD, pszBuffer + iRtxBytes,
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
	socklen_t SockALen = (socklen_t) sizeof(pFrom->Addr);
	int iRecvBytes;

	if (SysFdWait((int) SockFD, POLLIN, iTimeout) < 0)
		return ErrGetErrorCode();

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

int SysSendData(SYS_SOCKET SockFD, char const *pszBuffer, int iBufferSize, int iTimeout)
{
	int iSendBytes;

	if (SysFdWait((int) SockFD, POLLOUT, iTimeout) < 0)
		return ErrGetErrorCode();

	while ((iSendBytes = send((int) SockFD, pszBuffer, iBufferSize, 0)) == -1 &&
	       SYS_INT_CALL());

	if (iSendBytes == -1) {
		ErrSetErrorCode(ERR_NETWORK);
		return ERR_NETWORK;
	}

	return iSendBytes;
}

int SysSend(SYS_SOCKET SockFD, char const *pszBuffer, int iBufferSize, int iTimeout)
{
	int iRtxBytes = 0, iRtxCurrent;

	while (iRtxBytes < iBufferSize) {
		iRtxCurrent = SysSendData(SockFD, pszBuffer + iRtxBytes,
					  iBufferSize - iRtxBytes, iTimeout);
		if (iRtxCurrent <= 0)
			return iRtxBytes;
		iRtxBytes += iRtxCurrent;
	}

	return iRtxBytes;
}

int SysSendDataTo(SYS_SOCKET SockFD, const SYS_INET_ADDR *pTo,
		  char const *pszBuffer, int iBufferSize, int iTimeout)
{
	int iSendBytes;

	if (SysFdWait((int) SockFD, POLLOUT, iTimeout) < 0)
		return ErrGetErrorCode();

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
	int iError;

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

	iError = SysFdWait((int) SockFD, POLLOUT, iTimeout);

	SysBlockSocket(SockFD, 1);

	return iError;
}

SYS_SOCKET SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR *pSockName, int iTimeout)
{
	if (SysFdWait((int) SockFD, POLLIN, iTimeout) < 0)
		return ErrGetErrorCode();

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
	if (iTimeout >= 0) {
		TV.tv_sec = iTimeout / 1000;
		TV.tv_usec = (iTimeout % 1000) * 1000;
	}

	int iSelectResult = select(iMaxFD + 1, pReadFDs, pWriteFDs, pExcptFDs,
				   iTimeout >= 0 ? &TV: NULL);

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

int SysSendFileMMap(SYS_SOCKET SockFD, char const *pszFileName, SYS_OFF_T llBaseOffset,
		    SYS_OFF_T llEndOffset, int iTimeout)
{
	int iFD, iCurrSend, iSndBuffSize;
	SYS_OFF_T llAlnOff, llFileSize;
	SYS_INT64 tStart;
	void *pMapAddress;
	char *pszBuffer;

	if ((iFD = open(pszFileName, O_RDONLY)) == -1) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName);
		return ERR_FILE_OPEN;
	}
	llFileSize = (SYS_OFF_T) lseek(iFD, 0, SEEK_END);
	lseek(iFD, 0, SEEK_SET);
	llAlnOff = llBaseOffset & ~((SYS_OFF_T) sysconf(_SC_PAGESIZE) - 1);
	if (llEndOffset == -1)
		llEndOffset = llFileSize;
	if (llBaseOffset > llFileSize || llEndOffset > llFileSize ||
	    llBaseOffset > llEndOffset) {
		close(iFD);
		ErrSetErrorCode(ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}
	if ((pMapAddress = mmap((char *) 0, llEndOffset - llAlnOff, PROT_READ,
				MAP_SHARED, iFD, llAlnOff)) == (void *) -1) {
		close(iFD);
		ErrSetErrorCode(ERR_MMAP);
		return ERR_MMAP;
	}
	iSndBuffSize = (iTimeout != SYS_INFINITE_TIMEOUT) ? MIN_TCP_SEND_SIZE:
		MAX_TCP_SEND_SIZE;
	pszBuffer = (char *) pMapAddress + (long) (llBaseOffset - llAlnOff);

	while (llBaseOffset < llEndOffset) {
		iCurrSend = (int) Min(iSndBuffSize, llEndOffset - llBaseOffset);
		tStart = SysMsTime();
		if ((iCurrSend = SysSendData(SockFD, pszBuffer, iCurrSend, iTimeout)) < 0) {
			ErrorPush();
			munmap((char *) pMapAddress, llEndOffset - llAlnOff);
			close(iFD);
			return ErrorPop();
		}
		if (iSndBuffSize < MAX_TCP_SEND_SIZE &&
		    ((SysMsTime() - tStart) * K_IO_TIME_RATIO) < (SYS_INT64) iTimeout)
			iSndBuffSize = Min(iSndBuffSize * 2, MAX_TCP_SEND_SIZE);
		pszBuffer += iCurrSend;
		llBaseOffset += iCurrSend;
	}
	munmap(pMapAddress, (long) (llEndOffset - llAlnOff));
	close(iFD);

	return 0;
}

#if !defined(SYS_HAS_SENDFILE)

int SysSendFile(SYS_SOCKET SockFD, char const *pszFileName, SYS_OFF_T llBaseOffset,
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
	if (SysWaitInit(&pSD->Wait) < 0) {
		SysFree(pSD);
		return SYS_INVALID_SEMAPHORE;
	}
	pSD->iSemCounter = iInitCount;
	pSD->iMaxCount = iMaxCount;

	return (SYS_SEMAPHORE) pSD;
}

int SysCloseSemaphore(SYS_SEMAPHORE hSemaphore)
{
	SemData *pSD = (SemData *) hSemaphore;

	if (pSD != NULL) {
		SysWaitCleanup(&pSD->Wait);
		SysFree(pSD);
	}

	return 0;
}

static int SysSemCondCB(void *pPrivate)
{
	SemData *pSD = (SemData *) pPrivate;

	return pSD->iSemCounter > 0;
}

static void SysSemPostCB(void *pPrivate)
{
	SemData *pSD = (SemData *) pPrivate;

	pSD->iSemCounter -= 1;
}

int SysWaitSemaphore(SYS_SEMAPHORE hSemaphore, int iTimeout)
{
	SemData *pSD = (SemData *) hSemaphore;

	return SysWaitWait(&pSD->Wait, iTimeout, SysSemCondCB, SysSemPostCB, pSD);
}

int SysReleaseSemaphore(SYS_SEMAPHORE hSemaphore, int iCount)
{
	SemData *pSD = (SemData *) hSemaphore;

	SysWaitLock(&pSD->Wait);
	pSD->iSemCounter += iCount;
	if (pSD->iSemCounter > 0) {
		if (pSD->iSemCounter > 1)
			SysWaitWakeup(&pSD->Wait, 0);
		else
			SysWaitWakeup(&pSD->Wait, 1);
	}
	SysWaitUnlock(&pSD->Wait);

	return 0;
}

int SysTryWaitSemaphore(SYS_SEMAPHORE hSemaphore)
{
	SemData *pSD = (SemData *) hSemaphore;

	SysWaitLock(&pSD->Wait);
	if (pSD->iSemCounter <= 0) {
		SysWaitUnlock(&pSD->Wait);
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	pSD->iSemCounter -= 1;
	SysWaitUnlock(&pSD->Wait);

	return 0;
}

SYS_MUTEX SysCreateMutex(void)
{
	MutexData *pMD = (MutexData *) SysAlloc(sizeof(MutexData));

	if (pMD == NULL)
		return SYS_INVALID_MUTEX;
	if (SysWaitInit(&pMD->Wait) < 0) {
		SysFree(pMD);
		return SYS_INVALID_MUTEX;
	}
	pMD->iLocked = 0;

	return (SYS_MUTEX) pMD;
}

int SysCloseMutex(SYS_MUTEX hMutex)
{
	MutexData *pMD = (MutexData *) hMutex;

	if (pMD != NULL) {
		SysWaitCleanup(&pMD->Wait);
		SysFree(pMD);
	}

	return 0;
}

static int SysMtxCondCB(void *pPrivate)
{
	MutexData *pMD = (MutexData *) pPrivate;

	return !pMD->iLocked;
}

static void SysMtxPostCB(void *pPrivate)
{
	MutexData *pMD = (MutexData *) pPrivate;

	pMD->iLocked = 1;
}

int SysLockMutex(SYS_MUTEX hMutex, int iTimeout)
{
	MutexData *pMD = (MutexData *) hMutex;

	return SysWaitWait(&pMD->Wait, iTimeout, SysMtxCondCB, SysMtxPostCB, pMD);
}

int SysUnlockMutex(SYS_MUTEX hMutex)
{
	MutexData *pMD = (MutexData *) hMutex;

	SysWaitLock(&pMD->Wait);
	pMD->iLocked = 0;
	SysWaitWakeup(&pMD->Wait, 1);
	SysWaitUnlock(&pMD->Wait);

	return 0;
}

int SysTryLockMutex(SYS_MUTEX hMutex)
{
	MutexData *pMD = (MutexData *) hMutex;

	SysWaitLock(&pMD->Wait);
	if (pMD->iLocked) {
		SysWaitUnlock(&pMD->Wait);
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	pMD->iLocked = 1;
	SysWaitUnlock(&pMD->Wait);

	return 0;
}

SYS_EVENT SysCreateEvent(int iManualReset)
{
	EventData *pED = (EventData *) SysAlloc(sizeof(EventData));

	if (pED == NULL)
		return SYS_INVALID_EVENT;
	if (SysWaitInit(&pED->Wait) < 0) {
		SysFree(pED);
		return SYS_INVALID_EVENT;
	}
	pED->iSignaled = 0;
	pED->iManualReset = iManualReset;

	return (SYS_EVENT) pED;
}

int SysCloseEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	if (pED != NULL) {
		SysWaitCleanup(&pED->Wait);
		SysFree(pED);
	}

	return 0;
}

static int SysEvtCondCB(void *pPrivate)
{
	EventData *pED = (EventData *) pPrivate;

	return pED->iSignaled;
}

static void SysEvtPostCB(void *pPrivate)
{
	EventData *pED = (EventData *) pPrivate;

	if (!pED->iManualReset)
		pED->iSignaled = 0;
}

int SysWaitEvent(SYS_EVENT hEvent, int iTimeout)
{
	EventData *pED = (EventData *) hEvent;

	return SysWaitWait(&pED->Wait, iTimeout, SysEvtCondCB, SysEvtPostCB, pED);
}

int SysSetEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	SysWaitLock(&pED->Wait);
	pED->iSignaled = 1;
	if (pED->iManualReset)
		SysWaitWakeup(&pED->Wait, 0);
	else
		SysWaitWakeup(&pED->Wait, 1);
	SysWaitUnlock(&pED->Wait);

	return 0;
}

int SysResetEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	SysWaitLock(&pED->Wait);
	pED->iSignaled = 0;
	SysWaitUnlock(&pED->Wait);

	return 0;
}

int SysTryWaitEvent(SYS_EVENT hEvent)
{
	EventData *pED = (EventData *) hEvent;

	SysWaitLock(&pED->Wait);
	if (!pED->iSignaled) {
		SysWaitUnlock(&pED->Wait);
		ErrSetErrorCode(ERR_TIMEOUT);
		return ERR_TIMEOUT;
	}
	if (!pED->iManualReset)
		pED->iSignaled = 0;
	SysWaitUnlock(&pED->Wait);

	return 0;
}

/*
 * PEvents are events that can be waited together with socket descriptors.
 * On Unix, the best and more efficient event implementation is the one using
 * the pthread API, which, unfortunately, does not allow us to poll/select
 * the resulting events together with socket descriptors.
 */
SYS_PEVENT SysCreatePEvent(int iManualReset)
{
	PEventData *pPED;

	if ((pPED = (PEventData *) SysAlloc(sizeof(PEventData))) == NULL)
		return SYS_INVALID_PEVENT;
	pPED->iManualReset = iManualReset;
	if ((pPED->hEventfd = SysEventfdCreate()) == SYS_INVALID_EVENTFD) {
		SysFree(pPED);
		return SYS_INVALID_PEVENT;
	}

	return (SYS_PEVENT) pPED;
}

int SysClosePEvent(SYS_PEVENT hPEvent)
{
	PEventData *pPED = (PEventData *) hPEvent;

	if (pPED != NULL) {
		SysEventfdClose(pPED->hEventfd);
		SysFree(pPED);
	}

	return 0;
}

int SysWaitPEvent(SYS_PEVENT hPEvent, int iTimeout)
{
	PEventData *pPED = (PEventData *) hPEvent;
	int iWaitFD, iCTimeout;
	SYS_INT64 tNow, tExp;

	iWaitFD = SysEventfdWaitFD(pPED->hEventfd);
	tExp = (iTimeout > 0) ? SysMsTime() + iTimeout: 0;
	for (;;) {
		if (iTimeout > 0) {
			tNow = SysMsTime();
			tNow = Min(tNow, tExp);
			iCTimeout = (int) (tExp - tNow);
		} else
			iCTimeout = iTimeout;
		if (SysFdWait(iWaitFD, POLLIN, iCTimeout) < 0)
			return ErrGetErrorCode();
		if (pPED->iManualReset ||
		    SysEventfdReset(pPED->hEventfd) > 0)
			break;
	}

	return 0;
}

int SysSetPEvent(SYS_PEVENT hPEvent)
{
	PEventData *pPED = (PEventData *) hPEvent;

	return SysEventfdSet(pPED->hEventfd);
}

int SysResetPEvent(SYS_PEVENT hPEvent)
{
	PEventData *pPED = (PEventData *) hPEvent;

	return SysEventfdReset(pPED->hEventfd);
}

int SysTryWaitPEvent(SYS_PEVENT hPEvent)
{
	return SysWaitPEvent(hPEvent, 0);
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
	if (SysWaitInit(&pTD->Wait) < 0) {
		SysFree(pTD);
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

void SysCloseThread(SYS_THREAD ThreadID, int iForce)
{
	ThrData *pTD = (ThrData *) ThreadID;

	if (pTD != NULL) {
		pthread_detach(pTD->ThreadId);
		SysWaitLock(&pTD->Wait);
		if (iForce && !pTD->iThreadEnded)
			pthread_cancel(pTD->ThreadId);
		if (--pTD->iUseCount == 0) {
			SysWaitUnlock(&pTD->Wait);
			SysFreeThreadData(pTD);
		} else
			SysWaitUnlock(&pTD->Wait);
	}
}

static int SysThrCondCB(void *pPrivate)
{
	ThrData *pTD = (ThrData *) pPrivate;

	return pTD->iThreadEnded;
}

int SysWaitThread(SYS_THREAD ThreadID, int iTimeout)
{
	ThrData *pTD = (ThrData *) ThreadID;

	return SysWaitWait(&pTD->Wait, iTimeout, SysThrCondCB, NULL, pTD);
}

unsigned long SysGetCurrentThreadId(void)
{
	return (unsigned long) pthread_self();
}

static int SysWaitPid(PidWaitData *pPWD, int iTimeout)
{
	unsigned char uDot;

	if (SysFdWait(pPWD->iPipeFds[0], POLLIN, iTimeout) < 0)
		return ErrGetErrorCode();
	if (read(pPWD->iPipeFds[0], &uDot, 1) != 1) {
		ErrSetErrorCode(ERR_WAIT);
		return ERR_WAIT;
	}

	return 0;
}

int SysExec(char const *pszCommand, char const *const *pszArgs, int iWaitTimeout,
	    int iPriority, int *piExitStatus)
{
	int iError, iExitStatus;
	pid_t ProcessID;
	PidWaitData *pPWD;
	int iPipe[2];

	if (pipe(iPipe) == -1) {
		ErrSetErrorCode(ERR_PIPE);
		return ERR_PIPE;
	}
	ProcessID = fork();
	if (ProcessID == 0) {
		/*
		 * Wait for the unlock from the parent. We need to wait that
		 * the parent has created a PidWaitData strcture and added it
		 * to the list under the SIGCHLD handler control.  If we fail
		 * to do that, we might miss the wakeup in case the child
		 * process terminates quicly.
		 */
		read(iPipe[0], &ProcessID, sizeof(ProcessID));

		SYS_CLOSE_PIPE(iPipe);

		/* Execute the command */
		execv(pszCommand, (char **) pszArgs);

		/* We can only use async-signal safe functions, so we use write() directly */
		SYS_STDERR_WRITE("execv error: cmd='");
		SYS_STDERR_WRITE(pszCommand);
		SYS_STDERR_WRITE("'\n");

		_exit(SYSERR_EXEC);
	}
	if (ProcessID == (pid_t) -1) {
		SYS_CLOSE_PIPE(iPipe);
		ErrSetErrorCode(ERR_FORK);
		return ERR_FORK;
	}
	switch (iPriority) {
	case SYS_PRIORITY_NORMAL:
		setpriority(PRIO_PROCESS, ProcessID, 0);
		break;

	case SYS_PRIORITY_LOWER:
		setpriority(PRIO_PROCESS, ProcessID, SCHED_PRIORITY_INC);
		break;

	case SYS_PRIORITY_HIGHER:
		setpriority(PRIO_PROCESS, ProcessID, -SCHED_PRIORITY_INC);
		break;
	}
	if (iWaitTimeout > 0 || iWaitTimeout == SYS_INFINITE_TIMEOUT) {
		pPWD = SysCreatePidWait(ProcessID);
		iError = (pPWD == NULL) ? ErrGetErrorCode(): 0;

		/*
		 * Unlock the child process only once we dropped the PID wait into
		 * our handling list.
		 */
		write(iPipe[1], &ProcessID, sizeof(ProcessID));
		SYS_CLOSE_PIPE(iPipe);

		if (pPWD == NULL) {
			ErrSetErrorCode(iError);
			return iError;
		}
		if (SysWaitPid(pPWD, iWaitTimeout) < 0) {
			SysFreePidWait(pPWD);
			return ErrGetErrorCode();
		}
		iExitStatus = pPWD->iExitStatus;
		SysFreePidWait(pPWD);

		if (iExitStatus == SYSERR_EXEC) {
			ErrSetErrorCode(ERR_PROCESS_EXECUTE, pszCommand);
			return ERR_PROCESS_EXECUTE;
		}
	} else {
		/*
		 * Unlock the child process only once we dropped the PID wait into
		 * our handling list.
		 */
		write(iPipe[1], &ProcessID, sizeof(ProcessID));
		SYS_CLOSE_PIPE(iPipe);
		iExitStatus = -1;
	}
	if (piExitStatus != NULL)
		*piExitStatus = iExitStatus;

	return 0;
}

void SysSetBreakHandler(void (*pBreakHandler) (void))
{
	pSysBreakHandler = pBreakHandler;
}

unsigned long SysGetCurrentProcessId(void)
{
	return getpid();
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

void *SysAllocNZ(unsigned int uSize)
{
	void *pData = malloc(uSize);

	if (pData == NULL)
		ErrSetErrorCode(ERR_MEMORY);

	return pData;
}

void *SysAlloc(unsigned int uSize)
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

void *SysRealloc(void *pData, unsigned int uSize)
{
	void *pNewData = realloc(pData, uSize);

	if (pNewData == NULL)
		ErrSetErrorCode(ERR_MEMORY);

	return pNewData;
}

int SysLockFile(char const *pszFileName, char const *pszLockExt)
{
	int iFD;
	char szLockFile[SYS_MAX_PATH], szLock[128];

	snprintf(szLockFile, sizeof(szLockFile) - 1, "%s%s", pszFileName, pszLockExt);
	if ((iFD = open(szLockFile, O_CREAT | O_EXCL | O_RDWR,
			S_IREAD | S_IWRITE)) == -1) {
		ErrSetErrorCode(ERR_LOCKED);
		return ERR_LOCKED;
	}
	sprintf(szLock, "%lu", (unsigned long) SysGetCurrentThreadId());
	write(iFD, szLock, strlen(szLock) + 1);
	close(iFD);

	return 0;
}

int SysUnlockFile(char const *pszFileName, char const *pszLockExt)
{
	char szLockFile[SYS_MAX_PATH];

	snprintf(szLockFile, sizeof(szLockFile) - 1, "%s%s", pszFileName, pszLockExt);
	if (unlink(szLockFile) != 0) {
		ErrSetErrorCode(ERR_NOT_LOCKED);
		return ERR_NOT_LOCKED;
	}

	return 0;
}

SYS_HANDLE SysOpenModule(char const *pszFilePath)
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
	if (hModule != SYS_INVALID_HANDLE)
		dlclose((void *) hModule);

	return 0;
}

void *SysGetSymbol(SYS_HANDLE hModule, char const *pszSymbol)
{
	void *pSymbol = dlsym((void *) hModule, pszSymbol);

	if (pSymbol == NULL) {
		ErrSetErrorCode(ERR_LOADMODULESYMBOL, pszSymbol);
		return NULL;
	}

	return pSymbol;
}

int SysEventLogV(int iLogLevel, char const *pszFormat, va_list Args)
{
	openlog(APP_NAME_STR, LOG_PID | LOG_CONS, LOG_DAEMON);

	char szBuffer[2048];

	vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);
	syslog(LOG_DAEMON | (iLogLevel == LOG_LEV_ERROR ? LOG_ERR:
			     iLogLevel == LOG_LEV_WARNING ? LOG_WARNING: LOG_INFO),
	       "%s", szBuffer);
	closelog();

	return 0;
}

int SysEventLog(int iLogLevel, char const *pszFormat, ...)
{
	int iError;
	va_list Args;

	va_start(Args, pszFormat);
	iError = SysEventLogV(iLogLevel, pszFormat, Args);
	va_end(Args);

	return iError;
}

int SysLogMessage(int iLogLevel, char const *pszFormat, ...)
{
	va_list Args;
	extern bool bServerDebug;

	pthread_mutex_lock(&LogMutex);
	va_start(Args, pszFormat);
	if (bServerDebug)
		/* Debug implementation */
		vprintf(pszFormat, Args);
	else {
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

int SysSleep(int iTimeout)
{
	return SysMsSleep(iTimeout * 1000);
}

int SysMsSleep(int iMsTimeout)
{
	struct pollfd PollFD[1];

	ZeroData(PollFD);
	PollFD[0].fd = SysEventfdWaitFD(hShdwnEventfd);
	PollFD[0].events = POLLIN;
	if (poll(PollFD, 1, iMsTimeout) == -1) {
		ErrSetErrorCode(ERR_WAIT);
		return ERR_WAIT;
	}
	if (PollFD[0].revents & POLLIN) {
		ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
		return ERR_SERVER_SHUTDOWN;
	}

	return 0;
}

SYS_INT64 SysMsTime(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0;

	return 1000 * (SYS_INT64) tv.tv_sec + (SYS_INT64) tv.tv_usec / 1000;
}

int SysExistFile(char const *pszFilePath)
{
	struct stat FStat;

	if (stat(pszFilePath, &FStat) != 0)
		return 0;

	return S_ISDIR(FStat.st_mode) ? 0: 1;
}

int SysExistDir(char const *pszDirPath)
{
	struct stat FStat;

	if (stat(pszDirPath, &FStat) != 0)
		return 0;

	return S_ISDIR(FStat.st_mode) ? 1: 0;
}

SYS_HANDLE SysFirstFile(char const *pszPath, char *pszFileName, int iSize)
{
	DIR *pDIR = opendir(pszPath);

	if (pDIR == NULL) {
		ErrSetErrorCode(ERR_OPENDIR);
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

	char szFilePath[SYS_MAX_PATH];

	snprintf(szFilePath, sizeof(szFilePath) - 1, "%s%s", pFFD->szPath,
		 pFFD->FDE.DE.d_name);
	if (stat(szFilePath, &pFFD->FStat) != 0) {
		SysFree(pFFD);
		closedir(pDIR);

		ErrSetErrorCode(ERR_STAT);
		return SYS_INVALID_HANDLE;
	}

	return (SYS_HANDLE) pFFD;
}

int SysIsDirectory(SYS_HANDLE hFind)
{
	FileFindData *pFFD = (FileFindData *) hFind;

	return S_ISDIR(pFFD->FStat.st_mode) ? 1: 0;
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

	char szFilePath[SYS_MAX_PATH];

	snprintf(szFilePath, sizeof(szFilePath) - 1, "%s%s", pFFD->szPath,
		 pFFD->FDE.DE.d_name);
	if (stat(szFilePath, &pFFD->FStat) != 0) {
		ErrSetErrorCode(ERR_STAT);
		return 0;
	}

	return 1;
}

void SysFindClose(SYS_HANDLE hFind)
{
	FileFindData *pFFD = (FileFindData *) hFind;

	if (pFFD != NULL) {
		closedir(pFFD->pDIR);
		SysFree(pFFD);
	}
}

int SysGetFileInfo(char const *pszFileName, SYS_FILE_INFO &FI)
{
	struct stat stat_buffer;

	if (stat(pszFileName, &stat_buffer) != 0) {
		ErrSetErrorCode(ERR_STAT);
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

int SysSetFileModTime(char const *pszFileName, time_t tMod)
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

char *SysStrDup(char const *pszString)
{
	int iStrLength = strlen(pszString);
	char *pszBuffer = (char *) SysAllocNZ(iStrLength + 1);

	if (pszBuffer != NULL)
		memcpy(pszBuffer, pszString, iStrLength + 1);

	return pszBuffer;
}

char *SysGetEnv(char const *pszVarName)
{
	char const *pszValue = getenv(pszVarName);

	return (pszValue != NULL) ? SysStrDup(pszValue): NULL;
}

char *SysGetTempDir(char *pszPath, int iMaxPath)
{
	char const *pszEnv;

	if ((pszEnv = getenv("XMAIL_TEMP")) == NULL)
		pszEnv = "/tmp/";
	StrNCpy(pszPath, pszEnv, iMaxPath - 1);
	AppendSlash(pszPath);

	return pszPath;
}

int SysRemove(char const *pszFileName)
{
	if (unlink(pszFileName) != 0) {
		ErrSetErrorCode(ERR_FILE_DELETE);
		return ERR_FILE_DELETE;
	}

	return 0;
}

int SysMakeDir(char const *pszPath)
{
	if (mkdir(pszPath, 0700) != 0) {
		ErrSetErrorCode(ERR_DIR_CREATE);
		return ERR_DIR_CREATE;
	}

	return 0;
}

int SysRemoveDir(char const *pszPath)
{
	if (rmdir(pszPath) != 0) {
		ErrSetErrorCode(ERR_DIR_DELETE);
		return ERR_DIR_DELETE;
	}

	return 0;
}

int SysMoveFile(char const *pszOldName, char const *pszNewName)
{
	if (rename(pszOldName, pszNewName) != 0) {
		ErrSetErrorCode(ERR_FILE_MOVE);
		return ERR_FILE_MOVE;
	}

	return 0;
}

int SysVSNPrintf(char *pszBuffer, int iSize, char const *pszFormat, va_list Args)
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

char *SysStrTok(char *pszData, char const *pszDelim, char **ppszSavePtr)
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

	return (tmCurr.tm_isdst <= 0) ? 0: 3600;
}

SYS_MMAP SysCreateMMap(char const *pszFileName, unsigned long ulFlags)
{
	int iFD = open(pszFileName, (ulFlags & SYS_MMAP_WRITE) ? O_RDWR: O_RDONLY);

	if (iFD < 0) {
		ErrSetErrorCode(ERR_FILE_OPEN);
		return SYS_INVALID_MMAP;
	}

	struct stat StatBuf;

	if (fstat(iFD, &StatBuf) < 0) {
		close(iFD);
		ErrSetErrorCode(ERR_STAT);
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

	if (pMMD != NULL) {
		if (pMMD->iNumMaps > 0) {

		}
		close(pMMD->iFD);
		SysFree(pMMD);
	}
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

