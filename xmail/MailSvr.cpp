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
#include "SList.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "MiscUtils.h"
#include "SSLBind.h"
#include "ResLocks.h"
#include "POP3Svr.h"
#include "SMTPSvr.h"
#include "SMAILSvr.h"
#include "PSYNCSvr.h"
#include "DNS.h"
#include "DNSCache.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "ExtAliases.h"
#include "AliasDomain.h"
#include "MailDomains.h"
#include "POP3GwLink.h"
#include "CTRLSvr.h"
#include "FINGSvr.h"
#include "LMAILSvr.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define ENV_MAIN_PATH               "MAIL_ROOT"
#define ENV_CMD_LINE                "MAIL_CMD_LINE"
#define SVR_SHUTDOWN_FILE           ".shutdown"
#define STD_SMAIL_THREADS           16
#define MAX_SMAIL_THREADS           256
#define STD_SMAIL_RETRY_TIMEOUT     480
#define STD_SMAIL_RETRY_INCR_RATIO  16
#define STD_SMAIL_MAX_RETRY         32
#define STD_PSYNC_INTERVAL          120
#define STD_PSYNC_NUM_THREADS       8
#define MAX_PSYNC_NUM_THREADS       32
#define STD_POP3_BADLOGIN_WAIT      5
#define MAX_POP3_THREADS            1024
#define MAX_SMTP_THREADS            1024
#define STD_SMTP_MAX_RCPTS          100
#define MAX_CTRL_THREADS            512
#define STD_LMAIL_THREADS           3
#define MAX_LMAIL_THREADS           17
#define STD_LMAILTHREAD_SLEEP_TIME  2
#define SVR_EXIT_WAIT               480
#define STD_SERVER_SESSION_TIMEOUT  90
#define MAX_CLIENTS_WAIT            300
#define CTRL_SERVER_SESSION_TIMEOUT 120
#define SERVER_SLEEP_TIMESLICE      2
#define SHUTDOWN_CHECK_TIME         2
#define STD_POP3AUTH_EXPIRE_TIME    (15 * 60)
#define FILTER_TIMEOUT              90
#define SVR_MAX_SERVICES            32


enum SvrServices {
	SVC_SMAIL = 0,
	SVC_CTRL,
	SVC_CRTLS,
	SVC_POP3,
	SVC_SMTP,
	SVC_FING,
	SVC_LMAIL,
	SVC_PSYNC,
	SVC_SMTPS,
	SVC_POP3S,
	SVC_CTRLS,

	SVC_MAX
};

struct SvrShutdownCtx {
	void (*pfShutdown)(void *);
	void *pPrivate;
};

/* External visible variabiles */
SHB_HANDLE hShbFING;
SHB_HANDLE hShbCTRL;
SHB_HANDLE hShbPOP3;
SHB_HANDLE hShbSMTP;
SHB_HANDLE hShbSMAIL;
SHB_HANDLE hShbPSYNC;
SHB_HANDLE hShbLMAIL;
char szMailPath[SYS_MAX_PATH];
QUEUE_HANDLE hSpoolQueue;
SYS_SEMAPHORE hSyncSem;
bool bServerDebug;
int iFilterTimeout = FILTER_TIMEOUT;
bool bFilterLogEnabled = false;
int iLogRotateHours = LOG_ROTATE_HOURS;
int iQueueSplitLevel = STD_QUEUEFS_DIRS_X_LEVEL;
int iAddrFamily = AF_INET;
int iPOP3ClientTimeout = STD_SERVER_TIMEOUT;

#ifdef __UNIX__
int iMailboxType = XMAIL_MAILDIR;
#else
int iMailboxType = XMAIL_MAILBOX;
#endif

/* Local visible variabiles */
static int iNumShCtxs;
static SvrShutdownCtx ShCtxs[SVR_MAX_SERVICES];
static char szShutdownFile[SYS_MAX_PATH];
static bool bServerShutdown = false;
static int iNumSMAILThreads;
static int iNumLMAILThreads;
static SYS_THREAD hCTRLThread;
static ThreadConfig ThCfgCTRL;
static SYS_THREAD hCTRLSThread;
static ThreadConfig ThCfgCTRLS;
static SYS_THREAD hFINGThread;
static ThreadConfig ThCfgFING;
static SYS_THREAD hPOP3Thread;
static ThreadConfig ThCfgPOP3;
static SYS_THREAD hPOP3SThread;
static ThreadConfig ThCfgPOP3S;
static SYS_THREAD hSMTPThread;
static ThreadConfig ThCfgSMTP;
static SYS_THREAD hSMTPSThread;
static ThreadConfig ThCfgSMTPS;
static SYS_THREAD hSMAILThreads[MAX_SMAIL_THREADS];
static SYS_THREAD hLMAILThreads[MAX_LMAIL_THREADS];
static SYS_THREAD hPSYNCThread;

static void SvrShutdownCleanup(void)
{
	CheckRemoveFile(szShutdownFile);
	bServerShutdown = false;
}

static int SvrSetShutdown(void)
{
	/* Set the shutdown flag and shutdown the library */
	bServerShutdown = true;

	SysShutdownLibrary();

	FILE *pFile = fopen(szShutdownFile, "wt");

	if (pFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, szShutdownFile);
		return ERR_FILE_CREATE;
	}

	char szShutdownTimeStr[256] = "";

	MscGetTimeStr(szShutdownTimeStr, sizeof(szShutdownTimeStr));

	fprintf(pFile, "%s\n", szShutdownTimeStr);

	fclose(pFile);

	return 0;
}

static int SvrAddShutdown(void (*pfShutdown)(void *), void *pPrivate)
{
	if (iNumShCtxs >= SVR_MAX_SERVICES)
		return -1;
	ShCtxs[iNumShCtxs].pfShutdown = pfShutdown;
	ShCtxs[iNumShCtxs].pPrivate = pPrivate;

	return iNumShCtxs++;
}

static void SvrDoShutdown(void)
{
	int i;

	for (i = iNumShCtxs - 1; i >= 0; i--)
		(*ShCtxs[i].pfShutdown)(ShCtxs[i].pPrivate);
}

static void SvrShutdown__ThreadConfig(void *pPrivate)
{
	ThreadConfig *pThCfg = (ThreadConfig *) pPrivate;

	pThCfg->ulFlags |= THCF_SHUTDOWN;
}

static int SvrAddServerAddress(char const *pszServer, SYS_INET_ADDR *pSvrAddr,
			       int *piPos, int iSize)
{
	int iError;

	if (*piPos >= iSize) {
		ErrSetErrorCode(ERR_TOO_MANY_ELEMENTS);
		return ERR_TOO_MANY_ELEMENTS;
	}
	if ((iError = MscGetServerAddress(pszServer, pSvrAddr[*piPos])) < 0)
		return iError;
	(*piPos)++;

	return 0;
}

static long SvrThreadCntCTRL(ThreadConfig const *pThCfg)
{
	long lThreadCnt = 0;
	CTRLConfig *pCTRLCfg = (CTRLConfig *) ShbLock(pThCfg->hThShb);

	if (pCTRLCfg != NULL) {
		lThreadCnt = pCTRLCfg->lThreadCount;
		ShbUnlock(pThCfg->hThShb);
	}

	return lThreadCnt;
}

static void SvrCleanupCTRL(void)
{
	if (hCTRLThread != SYS_INVALID_THREAD) {
		/* Wait CTRL */
		SysWaitThread(hCTRLThread, SVR_EXIT_WAIT);

		/* Close CTRL Thread */
		SysCloseThread(hCTRLThread, 1);
	}

	ShbCloseBlock(hShbCTRL);

	for (; ThCfgCTRL.iNumSockFDs > 0; ThCfgCTRL.iNumSockFDs--)
		SysCloseSocket(ThCfgCTRL.SockFDs[ThCfgCTRL.iNumSockFDs - 1]);
}

static int SvrSetupCTRL(int iArgCount, char *pszArgs[])
{
	int iPort = STD_CTRL_PORT, iDisable = 0, iFamily = AF_INET;
	int iSessionTimeout = CTRL_SERVER_SESSION_TIMEOUT;
	long lMaxThreads = MAX_CTRL_THREADS;
	unsigned long ulFlags = 0;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hCTRLThread = SYS_INVALID_THREAD;

	ZeroData(ThCfgCTRL);
	ThCfgCTRL.pszName = CTRL_SERVER_NAME;
	ThCfgCTRL.pfThreadProc = CTRLClientThread;
	ThCfgCTRL.pfThreadCnt = SvrThreadCntCTRL;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'C'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 't':
			if (++i < iArgCount)
				iSessionTimeout = atoi(pszArgs[i]);
			break;

		case 'l':
			ulFlags |= CTRLF_LOG_ENABLED;
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgCTRL.SvrAddr,
						&ThCfgCTRL.iNumAddr,
						CountOf(ThCfgCTRL.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case 'X':
			if (++i < iArgCount)
				lMaxThreads = atol(pszArgs[i]);
			break;

		case '-':
			iDisable++;
			break;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}

	if ((hShbCTRL = ShbCreateBlock(sizeof(CTRLConfig))) == SHB_INVALID_HANDLE)
		return ErrGetErrorCode();

	CTRLConfig *pCTRLCfg = (CTRLConfig *) ShbLock(hShbCTRL);

	if (pCTRLCfg == NULL) {
		ErrorPush();
		ShbCloseBlock(hShbCTRL);
		return ErrorPop();
	}

	pCTRLCfg->ulFlags = ulFlags;
	pCTRLCfg->lThreadCount = 0;
	pCTRLCfg->lMaxThreads = lMaxThreads;
	pCTRLCfg->iSessionTimeout = iSessionTimeout;
	pCTRLCfg->iTimeout = STD_SERVER_TIMEOUT;

	ShbUnlock(hShbCTRL);
	if (iDisable)
		return 0;

	if (MscCreateServerSockets(ThCfgCTRL.iNumAddr, ThCfgCTRL.SvrAddr, iFamily,
				   iPort, CTRL_LISTEN_SIZE, ThCfgCTRL.SockFDs,
				   ThCfgCTRL.iNumSockFDs) < 0) {
		ShbCloseBlock(hShbCTRL);
		return ErrGetErrorCode();
	}

	ThCfgCTRL.hThShb = hShbCTRL;
	if ((hCTRLThread = SysCreateThread(MscServiceThread,
					   &ThCfgCTRL)) == SYS_INVALID_THREAD) {
		ErrorPush();
		ShbCloseBlock(hShbCTRL);
		for (; ThCfgCTRL.iNumSockFDs > 0; ThCfgCTRL.iNumSockFDs--)
			SysCloseSocket(ThCfgCTRL.SockFDs[ThCfgCTRL.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgCTRL);

	return 0;
}

static int SvrSetupCTRLS(int iArgCount, char *pszArgs[])
{
	int iPort = STD_CTRLS_PORT, iFamily = AF_INET;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hCTRLSThread = SYS_INVALID_THREAD;

	ZeroData(ThCfgCTRLS);
	ThCfgCTRLS.pszName = CTRLS_SERVER_NAME;
	ThCfgCTRLS.pfThreadProc = CTRLClientThread;
	ThCfgCTRLS.pfThreadCnt = SvrThreadCntCTRL;
	ThCfgCTRLS.ulFlags = THCF_USE_SSL;
	ThCfgCTRLS.hThShb = hShbCTRL;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'W'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgCTRLS.SvrAddr,
						&ThCfgCTRLS.iNumAddr,
						CountOf(ThCfgCTRLS.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case '-':
			return 0;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}
	if (MscCreateServerSockets(ThCfgCTRLS.iNumAddr, ThCfgCTRLS.SvrAddr, iFamily,
				   iPort, CTRL_LISTEN_SIZE, ThCfgCTRLS.SockFDs,
				   ThCfgCTRLS.iNumSockFDs) < 0)
		return ErrGetErrorCode();
	if ((hCTRLSThread = SysCreateThread(MscServiceThread,
					    &ThCfgCTRLS)) == SYS_INVALID_THREAD) {
		ErrorPush();
		for (; ThCfgCTRLS.iNumSockFDs > 0; ThCfgCTRLS.iNumSockFDs--)
			SysCloseSocket(ThCfgCTRLS.SockFDs[ThCfgCTRLS.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgCTRLS);

	return 0;
}

static void SvrCleanupCTRLS(void)
{
	if (hCTRLSThread != SYS_INVALID_THREAD) {
		/* Wait CTRLS Thread */
		SysWaitThread(hCTRLSThread, SVR_EXIT_WAIT);

		/* Close CTRLS Thread */
		SysCloseThread(hCTRLSThread, 1);
	}

	for (; ThCfgCTRLS.iNumSockFDs > 0; ThCfgCTRLS.iNumSockFDs--)
		SysCloseSocket(ThCfgCTRLS.SockFDs[ThCfgCTRLS.iNumSockFDs - 1]);
}

static long SvrThreadCntFING(ThreadConfig const *pThCfg)
{
	long lThreadCnt = 0;
	FINGConfig *pFINGCfg = (FINGConfig *) ShbLock(pThCfg->hThShb);

	if (pFINGCfg != NULL) {
		lThreadCnt = pFINGCfg->lThreadCount;
		ShbUnlock(pThCfg->hThShb);
	}

	return lThreadCnt;
}

static void SvrCleanupFING(void)
{
	if (hFINGThread != SYS_INVALID_THREAD) {
		/* Wait FINGER */
		SysWaitThread(hFINGThread, SVR_EXIT_WAIT);

		/* Close FINGER Thread */
		SysCloseThread(hFINGThread, 1);
	}

	ShbCloseBlock(hShbFING);

	for (; ThCfgFING.iNumSockFDs > 0; ThCfgFING.iNumSockFDs--)
		SysCloseSocket(ThCfgFING.SockFDs[ThCfgFING.iNumSockFDs - 1]);
}

static int SvrSetupFING(int iArgCount, char *pszArgs[])
{
	int iPort = STD_FINGER_PORT, iDisable = 0, iFamily = AF_INET;
	unsigned long ulFlags = 0;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hFINGThread = SYS_INVALID_THREAD;

	ZeroData(ThCfgFING);
	ThCfgFING.pszName = FING_SERVER_NAME;
	ThCfgFING.pfThreadProc = FINGClientThread;
	ThCfgFING.pfThreadCnt = SvrThreadCntFING;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'F'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 'l':
			ulFlags |= FINGF_LOG_ENABLED;
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgFING.SvrAddr,
						&ThCfgFING.iNumAddr,
						CountOf(ThCfgFING.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case '-':
			iDisable++;
			break;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}

	if ((hShbFING = ShbCreateBlock(sizeof(FINGConfig))) == SHB_INVALID_HANDLE)
		return ErrGetErrorCode();

	FINGConfig *pFINGCfg = (FINGConfig *) ShbLock(hShbFING);

	if (pFINGCfg == NULL) {
		ShbCloseBlock(hShbFING);
		return ErrGetErrorCode();
	}

	pFINGCfg->ulFlags = ulFlags;
	pFINGCfg->lThreadCount = 0;
	pFINGCfg->iTimeout = STD_SERVER_TIMEOUT;

	ShbUnlock(hShbFING);
	if (iDisable)
		return 0;

	if (MscCreateServerSockets(ThCfgFING.iNumAddr, ThCfgFING.SvrAddr, iFamily,
				   iPort, FING_LISTEN_SIZE, ThCfgFING.SockFDs,
				   ThCfgFING.iNumSockFDs) < 0) {
		ShbCloseBlock(hShbFING);
		return ErrGetErrorCode();
	}

	ThCfgFING.hThShb = hShbFING;
	if ((hFINGThread = SysCreateThread(MscServiceThread,
					   &ThCfgFING)) == SYS_INVALID_THREAD) {
		ErrorPush();
		ShbCloseBlock(hShbFING);
		for (; ThCfgFING.iNumSockFDs > 0; ThCfgFING.iNumSockFDs--)
			SysCloseSocket(ThCfgFING.SockFDs[ThCfgFING.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgFING);

	return 0;
}

static long SvrThreadCntPOP3(ThreadConfig const *pThCfg)
{
	long lThreadCnt = 0;
	POP3Config *pPOP3Cfg = (POP3Config *) ShbLock(pThCfg->hThShb);

	if (pPOP3Cfg != NULL) {
		lThreadCnt = pPOP3Cfg->lThreadCount;
		ShbUnlock(pThCfg->hThShb);
	}

	return lThreadCnt;
}

static void SvrCleanupPOP3(void)
{
	if (hPOP3Thread != SYS_INVALID_THREAD) {
		/* Wait POP3 Thread */
		SysWaitThread(hPOP3Thread, SVR_EXIT_WAIT);

		/* Close POP3 Thread */
		SysCloseThread(hPOP3Thread, 1);
	}

	ShbCloseBlock(hShbPOP3);

	for (; ThCfgPOP3.iNumSockFDs > 0; ThCfgPOP3.iNumSockFDs--)
		SysCloseSocket(ThCfgPOP3.SockFDs[ThCfgPOP3.iNumSockFDs - 1]);
}

static int SvrSetupPOP3(int iArgCount, char *pszArgs[])
{
	int iPort = STD_POP3_PORT, iDisable = 0, iFamily = AF_INET;
	int iSessionTimeout = STD_SERVER_SESSION_TIMEOUT;
	int iBadLoginWait = STD_POP3_BADLOGIN_WAIT;
	long lMaxThreads = MAX_POP3_THREADS;
	unsigned long ulFlags = 0;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hPOP3Thread = SYS_INVALID_THREAD;

	ZeroData(ThCfgPOP3);
	ThCfgPOP3.pszName = POP3_SERVER_NAME;
	ThCfgPOP3.pfThreadProc = POP3ClientThread;
	ThCfgPOP3.pfThreadCnt = SvrThreadCntPOP3;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'P'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 't':
			if (++i < iArgCount)
				iSessionTimeout = atoi(pszArgs[i]);
			break;

		case 'w':
			if (++i < iArgCount)
				iBadLoginWait = atoi(pszArgs[i]);
			break;

		case 'l':
			ulFlags |= POP3F_LOG_ENABLED;
			break;

		case 'h':
			ulFlags |= POP3F_HANG_ON_BADLOGIN;
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgPOP3.SvrAddr,
						&ThCfgPOP3.iNumAddr,
						CountOf(ThCfgPOP3.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case 'X':
			if (++i < iArgCount)
				lMaxThreads = atol(pszArgs[i]);
			break;

		case '-':
			iDisable++;
			break;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}

	if ((hShbPOP3 = ShbCreateBlock(sizeof(POP3Config))) == SHB_INVALID_HANDLE)
		return ErrGetErrorCode();

	POP3Config *pPOP3Cfg = (POP3Config *) ShbLock(hShbPOP3);

	if (pPOP3Cfg == NULL) {
		ShbCloseBlock(hShbPOP3);
		return ErrGetErrorCode();
	}

	pPOP3Cfg->ulFlags = ulFlags;
	pPOP3Cfg->lThreadCount = 0;
	pPOP3Cfg->lMaxThreads = lMaxThreads;
	pPOP3Cfg->iSessionTimeout = iSessionTimeout;
	pPOP3Cfg->iTimeout = STD_SERVER_TIMEOUT;
	pPOP3Cfg->iBadLoginWait = iBadLoginWait;

	ShbUnlock(hShbPOP3);
	if (iDisable)
		return 0;

	/* Remove POP3 lock files */
	UsrClearPop3LocksDir();

	if (MscCreateServerSockets(ThCfgPOP3.iNumAddr, ThCfgPOP3.SvrAddr, iFamily,
				   iPort, POP3_LISTEN_SIZE, ThCfgPOP3.SockFDs,
				   ThCfgPOP3.iNumSockFDs) < 0) {
		ShbCloseBlock(hShbPOP3);
		return ErrGetErrorCode();
	}

	ThCfgPOP3.hThShb = hShbPOP3;
	if ((hPOP3Thread = SysCreateThread(MscServiceThread,
					   &ThCfgPOP3)) == SYS_INVALID_THREAD) {
		ErrorPush();
		ShbCloseBlock(hShbPOP3);
		for (; ThCfgPOP3.iNumSockFDs > 0; ThCfgPOP3.iNumSockFDs--)
			SysCloseSocket(ThCfgPOP3.SockFDs[ThCfgPOP3.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgPOP3);

	return 0;
}

static int SvrSetupPOP3S(int iArgCount, char *pszArgs[])
{
	int iPort = STD_POP3S_PORT, iFamily = AF_INET;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hPOP3SThread = SYS_INVALID_THREAD;

	ZeroData(ThCfgPOP3S);
	ThCfgPOP3S.pszName = POP3S_SERVER_NAME;
	ThCfgPOP3S.pfThreadProc = POP3ClientThread;
	ThCfgPOP3S.pfThreadCnt = SvrThreadCntPOP3;
	ThCfgPOP3S.ulFlags = THCF_USE_SSL;
	ThCfgPOP3S.hThShb = hShbPOP3;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'B'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgPOP3S.SvrAddr,
						&ThCfgPOP3S.iNumAddr,
						CountOf(ThCfgPOP3S.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case '-':
			return 0;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}
	if (MscCreateServerSockets(ThCfgPOP3S.iNumAddr, ThCfgPOP3S.SvrAddr, iFamily,
				   iPort, POP3_LISTEN_SIZE, ThCfgPOP3S.SockFDs,
				   ThCfgPOP3S.iNumSockFDs) < 0)
		return ErrGetErrorCode();
	if ((hPOP3SThread = SysCreateThread(MscServiceThread,
					    &ThCfgPOP3S)) == SYS_INVALID_THREAD) {
		ErrorPush();
		for (; ThCfgPOP3S.iNumSockFDs > 0; ThCfgPOP3S.iNumSockFDs--)
			SysCloseSocket(ThCfgPOP3S.SockFDs[ThCfgPOP3S.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgPOP3S);

	return 0;
}

static void SvrCleanupPOP3S(void)
{
	if (hPOP3SThread != SYS_INVALID_THREAD) {
		/* Wait POP3S Thread */
		SysWaitThread(hPOP3SThread, SVR_EXIT_WAIT);

		/* Close POP3S Thread */
		SysCloseThread(hPOP3SThread, 1);
	}

	for (; ThCfgPOP3S.iNumSockFDs > 0; ThCfgPOP3S.iNumSockFDs--)
		SysCloseSocket(ThCfgPOP3S.SockFDs[ThCfgPOP3S.iNumSockFDs - 1]);
}

static long SvrThreadCntSMTP(ThreadConfig const *pThCfg)
{
	long lThreadCnt = 0;
	SMTPConfig *pSMTPCfg = (SMTPConfig *) ShbLock(pThCfg->hThShb);

	if (pSMTPCfg != NULL) {
		lThreadCnt = pSMTPCfg->lThreadCount;
		ShbUnlock(pThCfg->hThShb);
	}

	return lThreadCnt;
}

static void SvrCleanupSMTP(void)
{
	if (hSMTPThread != SYS_INVALID_THREAD) {
		/* Wait SMTP Thread */
		SysWaitThread(hSMTPThread, SVR_EXIT_WAIT);

		/* Close SMTP Thread */
		SysCloseThread(hSMTPThread, 1);
	}

	ShbCloseBlock(hShbSMTP);

	for (; ThCfgSMTP.iNumSockFDs > 0; ThCfgSMTP.iNumSockFDs--)
		SysCloseSocket(ThCfgSMTP.SockFDs[ThCfgSMTP.iNumSockFDs - 1]);
}

static int SvrSetupSMTP(int iArgCount, char *pszArgs[])
{
	int iPort = STD_SMTP_PORT, iDisable = 0, iFamily = AF_INET;
	int iSessionTimeout = STD_SERVER_SESSION_TIMEOUT;
	int iMaxRcpts = STD_SMTP_MAX_RCPTS;
	unsigned int uPopAuthExpireTime = STD_POP3AUTH_EXPIRE_TIME;
	long lMaxThreads = MAX_SMTP_THREADS;
	unsigned long ulFlags = 0;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hSMTPThread = SYS_INVALID_THREAD;

	ZeroData(ThCfgSMTP);
	ThCfgSMTP.pszName = SMTP_SERVER_NAME;
	ThCfgSMTP.pfThreadProc = SMTPClientThread;
	ThCfgSMTP.pfThreadCnt = SvrThreadCntSMTP;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'S'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 't':
			if (++i < iArgCount)
				iSessionTimeout = atoi(pszArgs[i]);
			break;

		case 'l':
			ulFlags |= SMTPF_LOG_ENABLED;
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgSMTP.SvrAddr,
						&ThCfgSMTP.iNumAddr,
						CountOf(ThCfgSMTP.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case 'X':
			if (++i < iArgCount)
				lMaxThreads = atol(pszArgs[i]);
			break;

		case 'r':
			if (++i < iArgCount)
				iMaxRcpts = atoi(pszArgs[i]);
			break;

		case 'e':
			if (++i < iArgCount)
				uPopAuthExpireTime = (unsigned int) atol(pszArgs[i]);
			break;

		case '-':
			iDisable++;
			break;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}

	if ((hShbSMTP = ShbCreateBlock(sizeof(SMTPConfig))) == SHB_INVALID_HANDLE)
		return ErrGetErrorCode();

	SMTPConfig *pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP);

	if (pSMTPCfg == NULL) {
		ShbCloseBlock(hShbSMTP);
		return ErrGetErrorCode();
	}

	pSMTPCfg->ulFlags = ulFlags;
	pSMTPCfg->lThreadCount = 0;
	pSMTPCfg->lMaxThreads = lMaxThreads;
	pSMTPCfg->iSessionTimeout = iSessionTimeout;
	pSMTPCfg->iTimeout = STD_SERVER_TIMEOUT;
	pSMTPCfg->iMaxRcpts = iMaxRcpts;
	pSMTPCfg->uPopAuthExpireTime = uPopAuthExpireTime;

	ShbUnlock(hShbSMTP);
	if (iDisable)
		return 0;

	if (MscCreateServerSockets(ThCfgSMTP.iNumAddr, ThCfgSMTP.SvrAddr, iFamily,
				   iPort, SMTP_LISTEN_SIZE, ThCfgSMTP.SockFDs,
				   ThCfgSMTP.iNumSockFDs) < 0) {
		ShbCloseBlock(hShbSMTP);
		return ErrGetErrorCode();
	}

	ThCfgSMTP.hThShb = hShbSMTP;
	if ((hSMTPThread = SysCreateThread(MscServiceThread,
					   &ThCfgSMTP)) == SYS_INVALID_THREAD) {
		ErrorPush();
		ShbCloseBlock(hShbSMTP);
		for (; ThCfgSMTP.iNumSockFDs > 0; ThCfgSMTP.iNumSockFDs--)
			SysCloseSocket(ThCfgSMTP.SockFDs[ThCfgSMTP.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgSMTP);

	return 0;
}

static int SvrSetupSMTPS(int iArgCount, char *pszArgs[])
{
	int iPort = STD_SMTPS_PORT, iFamily = AF_INET;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hSMTPSThread = SYS_INVALID_THREAD;

	ZeroData(ThCfgSMTPS);
	ThCfgSMTPS.pszName = SMTPS_SERVER_NAME;
	ThCfgSMTPS.pfThreadProc = SMTPClientThread;
	ThCfgSMTPS.pfThreadCnt = SvrThreadCntSMTP;
	ThCfgSMTPS.ulFlags = THCF_USE_SSL;
	ThCfgSMTPS.hThShb = hShbSMTP;
	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'X'))
			continue;

		switch (pszArgs[i][2]) {
		case 'p':
			if (++i < iArgCount)
				iPort = atoi(pszArgs[i]);
			break;

		case 'I':
			if (++i < iArgCount &&
			    SvrAddServerAddress(pszArgs[i], ThCfgSMTPS.SvrAddr,
						&ThCfgSMTPS.iNumAddr,
						CountOf(ThCfgSMTPS.SvrAddr)) < 0)
				return ErrGetErrorCode();
			break;

		case '-':
			return 0;

		case '6':
			iFamily = AF_INET6;
			break;
		}
	}
	if (MscCreateServerSockets(ThCfgSMTPS.iNumAddr, ThCfgSMTPS.SvrAddr, iFamily,
				   iPort, SMTP_LISTEN_SIZE, ThCfgSMTPS.SockFDs,
				   ThCfgSMTPS.iNumSockFDs) < 0)
		return ErrGetErrorCode();
	if ((hSMTPSThread = SysCreateThread(MscServiceThread,
					    &ThCfgSMTPS)) == SYS_INVALID_THREAD) {
		ErrorPush();
		for (; ThCfgSMTPS.iNumSockFDs > 0; ThCfgSMTPS.iNumSockFDs--)
			SysCloseSocket(ThCfgSMTPS.SockFDs[ThCfgSMTPS.iNumSockFDs - 1]);
		return ErrorPop();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__ThreadConfig, &ThCfgSMTPS);

	return 0;
}

static void SvrCleanupSMTPS(void)
{
	if (hSMTPSThread != SYS_INVALID_THREAD) {
		/* Wait SMTP Thread */
		SysWaitThread(hSMTPSThread, SVR_EXIT_WAIT);

		/* Close SMTP Thread */
		SysCloseThread(hSMTPSThread, 1);
	}

	for (; ThCfgSMTPS.iNumSockFDs > 0; ThCfgSMTPS.iNumSockFDs--)
		SysCloseSocket(ThCfgSMTPS.SockFDs[ThCfgSMTPS.iNumSockFDs - 1]);
}

static void SvrShutdown__SMAIL(void *pPrivate)
{
	SHB_HANDLE hShb = (SHB_HANDLE) pPrivate;
	SMAILConfig *pSMAILCfg = (SMAILConfig *) ShbLock(hShb);

	pSMAILCfg->ulFlags |= SMAILF_STOP_SERVER;

	ShbUnlock(hShb);
}

static int SvrSetupSMAIL(int iArgCount, char *pszArgs[])
{
	int i;
	int iRetryTimeout = STD_SMAIL_RETRY_TIMEOUT;
	int iRetryIncrRatio = STD_SMAIL_RETRY_INCR_RATIO;
	int iMaxRetry = STD_SMAIL_MAX_RETRY;
	unsigned long ulFlags = 0;

	iNumSMAILThreads = STD_SMAIL_THREADS;

	for (i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'Q'))
			continue;

		switch (pszArgs[i][2]) {
		case 'n':
			if (++i < iArgCount)
				iNumSMAILThreads = atoi(pszArgs[i]);

			iNumSMAILThreads = Min(MAX_SMAIL_THREADS, Max(1, iNumSMAILThreads));
			break;

		case 't':
			if (++i < iArgCount)
				iRetryTimeout = atoi(pszArgs[i]);
			break;

		case 'i':
			if (++i < iArgCount)
				iRetryIncrRatio = atoi(pszArgs[i]);
			break;

		case 'r':
			if (++i < iArgCount)
				iMaxRetry = atoi(pszArgs[i]);
			break;

		case 'l':
			ulFlags |= SMAILF_LOG_ENABLED;
			break;

		case 'T':
			if (++i < iArgCount)
				iFilterTimeout = atoi(pszArgs[i]);
			break;

		case 'g':
			bFilterLogEnabled = true;
			break;
		}
	}

	if ((hShbSMAIL = ShbCreateBlock(sizeof(SMAILConfig))) == SHB_INVALID_HANDLE)
		return ErrGetErrorCode();

	SMAILConfig *pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

	if (pSMAILCfg == NULL) {
		ErrorPush();
		ShbCloseBlock(hShbSMAIL);

		return ErrorPop();
	}

	pSMAILCfg->ulFlags = ulFlags;
	pSMAILCfg->lThreadCount = 0;

	ShbUnlock(hShbSMAIL);

	/* Initialize queue fs */
	char szSpoolDir[SYS_MAX_PATH] = "";

	SvrGetSpoolDir(szSpoolDir, sizeof(szSpoolDir));

	if ((hSpoolQueue = QueOpen(szSpoolDir, iMaxRetry, iRetryTimeout, iRetryIncrRatio,
				   iQueueSplitLevel)) == INVALID_QUEUE_HANDLE) {
		ErrorPush();
		ShbCloseBlock(hShbSMAIL);

		return ErrorPop();
	}
	/* Create mailer threads */
	for (i = 0; i < iNumSMAILThreads; i++)
		hSMAILThreads[i] = SysCreateThread(SMAILThreadProc, NULL);

	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__SMAIL, (void *) hShbSMAIL);

	return 0;
}

static void SvrCleanupSMAIL(void)
{
	/* Wait SMAIL Threads */
	int i;

	for (i = 0; i < iNumSMAILThreads; i++)
		SysWaitThread(hSMAILThreads[i], SVR_EXIT_WAIT);

	/* Close SMAIL Threads */
	for (i = 0; i < iNumSMAILThreads; i++)
		SysCloseThread(hSMAILThreads[i], 1);

	ShbCloseBlock(hShbSMAIL);

	/* Close the mail queue */
	QueClose(hSpoolQueue);
}

static void SvrShutdown__PSYNC(void *pPrivate)
{
	SHB_HANDLE hShb = (SHB_HANDLE) pPrivate;
	PSYNCConfig *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShb);

	pPSYNCCfg->ulFlags |= PSYNCF_STOP_SERVER;

	ShbUnlock(hShb);
}

static int SvrSetupPSYNC(int iArgCount, char *pszArgs[])
{
	int iSyncInterval = STD_PSYNC_INTERVAL, iDisable = 0;
	int iNumSyncThreads = STD_PSYNC_NUM_THREADS;
	unsigned long ulFlags = 0;

	/*
	 * Initialize the service thread handle to SYS_INVALID_THREAD so that
	 * we can detect the service being disabled in the cleanup function.
	 */
	hPSYNCThread = SYS_INVALID_THREAD;

	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'Y'))
			continue;

		switch (pszArgs[i][2]) {
		case 'i':
			if (++i < iArgCount)
				iSyncInterval = atoi(pszArgs[i]);
			break;

		case 't':
			if (++i < iArgCount)
				iNumSyncThreads = atoi(pszArgs[i]);

			iNumSyncThreads = Min(MAX_PSYNC_NUM_THREADS, Max(1, iNumSyncThreads));
			break;

		case 'l':
			ulFlags |= PSYNCF_LOG_ENABLED;
			break;

		case 'T':
			if (++i < iArgCount)
				iPOP3ClientTimeout = atoi(pszArgs[i]);
			break;

		case '-':
			iDisable++;
			break;
		}
	}

	if ((hSyncSem = SysCreateSemaphore(iNumSyncThreads,
					   SYS_DEFAULT_MAXCOUNT)) == SYS_INVALID_SEMAPHORE)
		return ErrGetErrorCode();

	if ((hShbPSYNC = ShbCreateBlock(sizeof(PSYNCConfig))) == SHB_INVALID_HANDLE) {
		ErrorPush();
		SysCloseSemaphore(hSyncSem);
		return ErrorPop();
	}

	PSYNCConfig *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

	if (pPSYNCCfg == NULL) {
		ErrorPush();
		ShbCloseBlock(hShbPSYNC);
		SysCloseSemaphore(hSyncSem);
		return ErrorPop();
	}

	pPSYNCCfg->ulFlags = ulFlags;
	pPSYNCCfg->lThreadCount = 0;
	pPSYNCCfg->iTimeout = STD_SERVER_TIMEOUT;
	pPSYNCCfg->iSyncInterval = iSyncInterval;
	pPSYNCCfg->iNumSyncThreads = iNumSyncThreads;

	ShbUnlock(hShbPSYNC);
	if (iDisable)
		return 0;

	/* Remove POP3 links lock files */
	GwLkClearLinkLocksDir();

	if ((hPSYNCThread = SysCreateThread(PSYNCThreadProc, NULL)) == SYS_INVALID_THREAD) {
		ShbCloseBlock(hShbPSYNC);
		SysCloseSemaphore(hSyncSem);

		return ErrGetErrorCode();
	}
	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__PSYNC, (void *) hShbPSYNC);

	return 0;
}

static void SvrCleanupPSYNC(void)
{
	if (hPSYNCThread != SYS_INVALID_THREAD) {
		/* Wait PSYNC Thread */
		SysWaitThread(hPSYNCThread, SVR_EXIT_WAIT);

		/* Close PSYNC Thread */
		SysCloseThread(hPSYNCThread, 1);
	}

	ShbCloseBlock(hShbPSYNC);
	SysCloseSemaphore(hSyncSem);
}

static void SvrShutdown__LMAIL(void *pPrivate)
{
	SHB_HANDLE hShb = (SHB_HANDLE) pPrivate;
	LMAILConfig *pLMAILCfg = (LMAILConfig *) ShbLock(hShb);

	pLMAILCfg->ulFlags |= LMAILF_STOP_SERVER;

	ShbUnlock(hShb);
}

static int SvrSetupLMAIL(int iArgCount, char *pszArgs[])
{
	int i;
	int iSleepTimeout = STD_LMAILTHREAD_SLEEP_TIME;
	unsigned long ulFlags = 0;

	iNumLMAILThreads = STD_LMAIL_THREADS;

	for (i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'L'))
			continue;

		switch (pszArgs[i][2]) {
		case 'n':
			if (++i < iArgCount)
				iNumLMAILThreads = atoi(pszArgs[i]);

			iNumLMAILThreads = Min(MAX_LMAIL_THREADS, Max(1, iNumLMAILThreads));
			break;

		case 'l':
			ulFlags |= LMAILF_LOG_ENABLED;
			break;

		case 't':
			if (++i < iArgCount)
				iSleepTimeout = atoi(pszArgs[i]);
			break;
		}
	}

	if ((hShbLMAIL = ShbCreateBlock(sizeof(LMAILConfig))) == SHB_INVALID_HANDLE)
		return ErrGetErrorCode();

	LMAILConfig *pLMAILCfg = (LMAILConfig *) ShbLock(hShbLMAIL);

	if (pLMAILCfg == NULL) {
		ErrorPush();
		ShbCloseBlock(hShbLMAIL);

		return ErrorPop();
	}

	pLMAILCfg->ulFlags = ulFlags;
	pLMAILCfg->iSleepTimeout = iSleepTimeout;
	pLMAILCfg->lNumThreads = iNumLMAILThreads;
	pLMAILCfg->lThreadCount = 0;

	ShbUnlock(hShbLMAIL);

	/* Create mailer threads */
	for (i = 0; i < iNumLMAILThreads; i++)
		hLMAILThreads[i] = SysCreateThread(LMAILThreadProc, NULL);

	/*
	 * Register the shutdown function.
	 */
	SvrAddShutdown(SvrShutdown__LMAIL, (void *) hShbLMAIL);

	return 0;
}

static void SvrCleanupLMAIL(void)
{
	/* Wait LMAIL Threads */
	int i;

	for (i = 0; i < iNumLMAILThreads; i++)
		SysWaitThread(hLMAILThreads[i], SVR_EXIT_WAIT);

	/* Close LMAIL Threads */
	for (i = 0; i < iNumLMAILThreads; i++)
		SysCloseThread(hLMAILThreads[i], 1);

	ShbCloseBlock(hShbLMAIL);
}

static int SvrSetup(int iArgCount, char *pszArgs[])
{
	StrSNCpy(szMailPath, SYS_BASE_FS_STR);

	char *pszValue = SysGetEnv(ENV_MAIN_PATH);

	if (pszValue != NULL) {
		if (strncmp(szMailPath, pszValue, strlen(szMailPath)) == 0)
			StrSNCpy(szMailPath, pszValue);
		else
			StrSNCat(szMailPath, pszValue);
		DelFinalSlash(szMailPath);

		SysFree(pszValue);
	}

	iNumShCtxs = 0;
	bServerDebug = false;

	int iSndBufSize = -1;
	int iRcvBufSize = -1;
	int iDnsCacheDirs = DNS_HASH_NUM_DIRS;

	for (int i = 0; i < iArgCount; i++) {
		if ((pszArgs[i][0] != '-') || (pszArgs[i][1] != 'M'))
			continue;

		switch (pszArgs[i][2]) {
		case 's':
			if (++i < iArgCount) {
				StrSNCpy(szMailPath, pszArgs[i]);
				DelFinalSlash(szMailPath);
			}
			break;

		case 'd':
			bServerDebug = true;
			break;

		case 'r':
			if (++i < iArgCount)
				iLogRotateHours = atoi(pszArgs[i]);
			break;

		case 'x':
			if (++i < iArgCount) {
				iQueueSplitLevel = atoi(pszArgs[i]);

				while (!IsPrimeNumber(iQueueSplitLevel))
					++iQueueSplitLevel;
			}
			break;

		case 'R':
			if (++i < iArgCount) {
				iRcvBufSize = atoi(pszArgs[i]);
				iRcvBufSize = NbrCeil(iRcvBufSize, 1024);
			}
			break;

		case 'S':
			if (++i < iArgCount) {
				iSndBufSize = atoi(pszArgs[i]);
				iSndBufSize = NbrCeil(iSndBufSize, 1024);
			}
			break;

		case 'M':
			iMailboxType = XMAIL_MAILDIR;
			break;

		case 'm':
			iMailboxType = XMAIL_MAILBOX;
			break;

		case 'D':
			if (++i < iArgCount)
				iDnsCacheDirs = atoi(pszArgs[i]);
			break;

		case '4':
			iAddrFamily = AF_INET;
			break;

		case '6':
			iAddrFamily = AF_INET6;
			break;

		case '5':
			iAddrFamily = SYS_INET46;
			break;

		case '7':
			iAddrFamily = SYS_INET64;
			break;
		}
	}

	if (strlen(szMailPath) == 0 || !SysExistDir(szMailPath)) {
		ErrSetErrorCode(ERR_CONF_PATH);
		return ERR_CONF_PATH;
	}
	AppendSlash(szMailPath);

	/* Setup library socket buffers */
	SysSetupSocketBuffers((iSndBufSize > 0) ? &iSndBufSize: NULL,
			      (iRcvBufSize > 0) ? &iRcvBufSize: NULL);

	/* Setup shutdown file name ( must be called before any shutdown function ) */
	sprintf(szShutdownFile, "%s%s", szMailPath, SVR_SHUTDOWN_FILE);

	/* Setup resource lockers */
	if (RLckInitLockers() < 0)
		return ErrGetErrorCode();

	/* Clear shutdown condition */
	SvrShutdownCleanup();

	/* Align table indexes */
	if (UsrCheckUsersIndexes() < 0 ||
	    UsrCheckAliasesIndexes() < 0 ||
	    ExAlCheckAliasIndexes() < 0 ||
	    MDomCheckDomainsIndexes() < 0 || ADomCheckDomainsIndexes() < 0) {
		ErrorPush();
		RLckCleanupLockers();

		return ErrorPop();
	}
	/* Initialize DNS cache */
	if (CDNS_Initialize(iDnsCacheDirs) < 0 ||
	    BSslInit() < 0) {
		ErrorPush();
		RLckCleanupLockers();

		return ErrorPop();
	}

	return 0;
}

static void SvrCleanup(void)
{
	/* Cleanup SSL support */
	BSslCleanup();

	/* Cleanup resource lockers */
	RLckCleanupLockers();

	/* Clear shutdown condition */
	SvrShutdownCleanup();
}

static void SvrBreakHandler(void)
{
	/* Set shutdown condition */
	SvrSetShutdown();
}

static char **SvrMergeArgs(int iArgs, char *pszArgs[], int &iArgsCount)
{
	int iCmdArgs = 0;
	char **ppszCmdArgs = NULL;
	char *pszCmdLine = SysGetEnv(ENV_CMD_LINE);

	if (pszCmdLine != NULL) {
		ppszCmdArgs = StrGetArgs(pszCmdLine, iCmdArgs);

		SysFree(pszCmdLine);
	}

	char **ppszMergeArgs = (char **) SysAlloc((iCmdArgs + iArgs + 1) * sizeof(char *));

	if (ppszMergeArgs == NULL) {
		if (ppszCmdArgs != NULL)
			StrFreeStrings(ppszCmdArgs);

		return NULL;
	}

	iArgsCount = 0;

	for (int i = 0; i < iArgs; i++, iArgsCount++)
		ppszMergeArgs[iArgsCount] = SysStrDup(pszArgs[i]);

	for (int j = 0; j < iCmdArgs; j++, iArgsCount++)
		ppszMergeArgs[iArgsCount] = SysStrDup(ppszCmdArgs[j]);

	ppszMergeArgs[iArgsCount] = NULL;

	if (ppszCmdArgs != NULL)
		StrFreeStrings(ppszCmdArgs);

	return ppszMergeArgs;
}

int SvrMain(int iArgCount, char *pszArgs[])
{
	int iError = -1;
	int iSvcI[SVC_MAX];

	if (SysInitLibrary() < 0) {
		ErrorPush();
		SysEventLog(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		return ErrorPop();
	}

	int iMergeArgsCount = 0;
	char **ppszMergeArgs = SvrMergeArgs(iArgCount, pszArgs, iMergeArgsCount);

	if (ppszMergeArgs == NULL) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		SysCleanupLibrary();
		return ErrorPop();
	}

	if (SvrSetup(iMergeArgsCount, ppszMergeArgs) < 0) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		StrFreeStrings(ppszMergeArgs);
		SysCleanupLibrary();
		return ErrorPop();
	}

	ArrayInit(iSvcI, -1);
	if ((iSvcI[SVC_SMAIL] = SvrSetupSMAIL(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_CTRL] = SvrSetupCTRL(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_CTRLS] = SvrSetupCTRLS(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_POP3] = SvrSetupPOP3(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_POP3S] = SvrSetupPOP3S(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_SMTP] = SvrSetupSMTP(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_SMTPS] = SvrSetupSMTPS(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_PSYNC] = SvrSetupPSYNC(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_FING] = SvrSetupFING(iMergeArgsCount, ppszMergeArgs)) < 0 ||
	    (iSvcI[SVC_LMAIL] = SvrSetupLMAIL(iMergeArgsCount, ppszMergeArgs)) < 0) {
		StrFreeStrings(ppszMergeArgs);
		goto ErrorExit;
	}
	StrFreeStrings(ppszMergeArgs);

	/* Set stop handler */
	SysSetBreakHandler(SvrBreakHandler);

	SysLogMessage(LOG_LEV_MESSAGE, APP_NAME_VERSION_STR " server started\n");

	/* Server main loop */
	for (; !SvrInShutdown(true);) {
		SysSleep(SERVER_SLEEP_TIMESLICE);

	}
	iError = 0;

ErrorExit:
	if (iError < 0) {
		iError = ErrGetErrorCode();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
	}
	/* Runs shutdown functions */
	SvrDoShutdown();

	/* Goodbye cleanups */
	if (iSvcI[SVC_LMAIL] == 0)
		SvrCleanupLMAIL();
	if (iSvcI[SVC_FING] == 0)
		SvrCleanupFING();
	if (iSvcI[SVC_PSYNC] == 0)
		SvrCleanupPSYNC();
	if (iSvcI[SVC_SMTPS] == 0)
		SvrCleanupSMTPS();
	if (iSvcI[SVC_SMTP] == 0)
		SvrCleanupSMTP();
	if (iSvcI[SVC_POP3S] == 0)
		SvrCleanupPOP3S();
	if (iSvcI[SVC_POP3] == 0)
		SvrCleanupPOP3();
	if (iSvcI[SVC_CTRLS] == 0)
		SvrCleanupCTRLS();
	if (iSvcI[SVC_CTRL] == 0)
		SvrCleanupCTRL();
	if (iSvcI[SVC_SMAIL] == 0)
		SvrCleanupSMAIL();

	SvrCleanup();

	SysLogMessage(LOG_LEV_MESSAGE, APP_NAME_VERSION_STR " server stopped\n");

	SysCleanupLibrary();

	return iError;
}

int SvrStopServer(bool bWait)
{
	/* Set shutdown condition */
	SvrSetShutdown();

	if (bWait) {
		int iWaitTime = 0;

		for (; SvrInShutdown(true); iWaitTime += SERVER_SLEEP_TIMESLICE)
			SysSleep(SERVER_SLEEP_TIMESLICE);
	}

	return 0;
}

bool SvrInShutdown(bool bForceCheck)
{
	time_t tNow = time(NULL);
	static time_t tLastCheck = 0;
	static bool bShutdown = false;

	if (bForceCheck || (tNow > (tLastCheck + SHUTDOWN_CHECK_TIME))) {
		tLastCheck = tNow;

		if (bServerShutdown)
			bShutdown = true;
		else if (SysExistFile(szShutdownFile)) {
			bServerShutdown = true;

			SysShutdownLibrary();

			bShutdown = true;
		} else
			bShutdown = false;
	}

	return bShutdown;
}

