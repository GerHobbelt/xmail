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
#include "SList.h"
#include "BuffSock.h"
#include "ResLocks.h"
#include "MiscUtils.h"
#include "MailConfig.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "POP3Svr.h"
#include "POP3Utils.h"
#include "MessQueue.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "POP3GwLink.h"
#include "PSYNCSvr.h"

#define PSYNC_LOG_FILE              "psync"
#define PSYNC_TRIGGER_FILE          ".psync-trigger"
#define PSYNC_WAIT_SLEEP            2
#define MAX_CLIENTS_WAIT            300
#define PSYNC_WAKEUP_TIME           2
#define PSYNC_SERVER_NAME           "[" APP_NAME_VERSION_STR " PSYNC Server]"

struct PSYNCThreadData {
	PSYNCConfig *pPSYNCCfg;
	POP3Link *pPopLnk;
};

static int PSYNCThreadCountAdd(long lCount, SHB_HANDLE hShbPSYNC, PSYNCConfig *pPSYNCCfg = NULL);

static bool PSYNCNeedSync(void)
{
	char szTriggerPath[SYS_MAX_PATH] = "";

	CfgGetRootPath(szTriggerPath, sizeof(szTriggerPath));

	StrNCat(szTriggerPath, PSYNC_TRIGGER_FILE, sizeof(szTriggerPath));

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at config file: '%s'\n", szTriggerPath);

	/* Check for the presence of the trigger file */
	if (!SysExistFile(szTriggerPath))
		return false;

	SysRemove(szTriggerPath);

	return true;
}

static PSYNCConfig *PSYNCGetConfigCopy(SHB_HANDLE hShbPSYNC)
{
	PSYNCConfig *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

	if (pPSYNCCfg == NULL)
		return NULL;

	PSYNCConfig *pNewPSYNCCfg = (PSYNCConfig *) SysAlloc(sizeof(PSYNCConfig));

	if (pNewPSYNCCfg != NULL)
		memcpy(pNewPSYNCCfg, pPSYNCCfg, sizeof(PSYNCConfig));

	ShbUnlock(hShbPSYNC);

	return pNewPSYNCCfg;
}

static int PSYNCThreadCountAdd(long lCount, SHB_HANDLE hShbPSYNC, PSYNCConfig *pPSYNCCfg)
{
	int iDoUnlock = 0;

	if (pPSYNCCfg == NULL) {
		if ((pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC)) == NULL)
			return ErrGetErrorCode();

		++iDoUnlock;
	}

	pPSYNCCfg->lThreadCount += lCount;

	if (iDoUnlock)
		ShbUnlock(hShbPSYNC);

	return 0;
}

static int PSYNCTimeToStop(SHB_HANDLE hShbPSYNC)
{
	PSYNCConfig *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

	if (pPSYNCCfg == NULL)
		return 1;

	int iTimeToStop = (pPSYNCCfg->ulFlags & PSYNCF_STOP_SERVER) ? 1 : 0;

	ShbUnlock(hShbPSYNC);

	return iTimeToStop;
}

static int PSYNCThreadNotifyExit(void)
{
	SysReleaseSemaphore(hSyncSem, 1);

	return 0;
}

static int PSYNCLogEnabled(PSYNCConfig *pPSYNCCfg)
{
	return (pPSYNCCfg->ulFlags & PSYNCF_LOG_ENABLED) ? 1 : 0;
}

static int PSYNCLogSession(POP3Link const *pPopLnk, MailSyncReport const *pSRep,
			   char const *pszStatus)
{
	char szTime[256] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1);

	RLCK_HANDLE hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR PSYNC_LOG_FILE);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	MscFileLog(PSYNC_LOG_FILE,
		   "\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%d\""
		   "\t\"" SYS_OFFT_FMT "\""
		   "\t\"%d\""
		   "\t\"" SYS_OFFT_FMT "\""
		   "\n", szTime, pPopLnk->pszDomain, pPopLnk->pszName,
		   pPopLnk->pszRmtDomain, pPopLnk->pszRmtName, pPopLnk->pszAuthType, pszStatus,
		   pSRep->iMsgSync, pSRep->llSizeSync, pSRep->iMsgErr, pSRep->llSizeErr);

	RLckUnlockEX(hResLock);

	return 0;
}

unsigned int PSYNCThreadSyncProc(void *pThreadData)
{
	PSYNCThreadData *pSTD = (PSYNCThreadData *) pThreadData;
	POP3Link *pPopLnk = pSTD->pPopLnk;
	PSYNCConfig *pPSYNCCfg = pSTD->pPSYNCCfg;

	SysFree(pSTD);

	SysLogMessage(LOG_LEV_MESSAGE, "[PSYNC] entry\n");

	/* Get configuration handle */
	SVRCFG_HANDLE hSvrConfig = SvrGetConfigHandle();

	if (hSvrConfig == INVALID_SVRCFG_HANDLE) {
		ErrorPush();
		SysLogMessage(LOG_LEV_MESSAGE, "%s\n", ErrGetErrorString(ErrorFetch()));

		GwLkFreePOP3Link(pPopLnk);
		SysFree(pPSYNCCfg);
		/* Notify thread exit semaphore */
		PSYNCThreadNotifyExit();
		return ErrorPop();
	}
	/* Get the error account for email that the server is not able to deliver coz */
	/* it does not find information about where it has to deliver */
	char szErrorAccount[MAX_ADDR_NAME] = "";

	SvrConfigVar("Pop3SyncErrorAccount", szErrorAccount, sizeof(szErrorAccount) - 1,
		     hSvrConfig, "");

	char const *pszErrorAccount = (IsEmptyString(szErrorAccount)) ? NULL : szErrorAccount;

	/* Get headers tags that must be checked to extract recipients */
	char szFetchHdrTags[256] = "";

	SvrConfigVar("FetchHdrTags", szFetchHdrTags, sizeof(szFetchHdrTags) - 1,
		     hSvrConfig, "+X-Deliver-To,+Received,To,Cc");

	/* Lock the link */
	if (GwLkLinkLock(pPopLnk) < 0) {
		ErrorPush();
		SysLogMessage(LOG_LEV_MESSAGE, "%s\n", ErrGetErrorString(ErrorFetch()));

		SvrReleaseConfigHandle(hSvrConfig);
		GwLkFreePOP3Link(pPopLnk);
		SysFree(pPSYNCCfg);
		/* Notify thread exit semaphore */
		PSYNCThreadNotifyExit();
		return ErrorPop();
	}
	/* Increase threads count */
	PSYNCThreadCountAdd(+1, hShbPSYNC);

	/* Sync for real internal account ? */
	MailSyncReport SRep;

	if (GwLkLocalDomain(pPopLnk)) {
		/* Verify user credentials */
		UserInfo *pUI = UsrGetUserByName(pPopLnk->pszDomain, pPopLnk->pszName);

		if (pUI != NULL) {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "[PSYNC] User = \"%s\" - Domain = \"%s\"\n",
				      pPopLnk->pszName, pPopLnk->pszDomain);

			/* Sync */
			char szUserAddress[MAX_ADDR_NAME] = "";

			UsrGetAddress(pUI, szUserAddress);

			if (UPopSyncRemoteLink(szUserAddress, pPopLnk->pszRmtDomain,
					       pPopLnk->pszRmtName, pPopLnk->pszRmtPassword, &SRep,
					       pPopLnk->pszAuthType, szFetchHdrTags,
					       pszErrorAccount) < 0) {
				ErrLogMessage(LOG_LEV_MESSAGE,
					      "[PSYNC] User = \"%s\" - Domain = \"%s\" Failed !\n",
					      pPopLnk->pszName, pPopLnk->pszDomain);

				ZeroData(SRep);
				if (PSYNCLogEnabled(pPSYNCCfg))
					PSYNCLogSession(pPopLnk, &SRep, "SYNC=EFAIL");
			} else {
				if (PSYNCLogEnabled(pPSYNCCfg))
					PSYNCLogSession(pPopLnk, &SRep, "SYNC=OK");
			}
			UsrFreeUserInfo(pUI);
		} else {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "[PSYNC] User = \"%s\" - Domain = \"%s\" Failed !\n"
				      "Error = %s\n", pPopLnk->pszName, pPopLnk->pszDomain,
				      ErrGetErrorString());

			ZeroData(SRep);
			if (PSYNCLogEnabled(pPSYNCCfg))
				PSYNCLogSession(pPopLnk, &SRep, "SYNC=ENOUSER");
		}
	} else if (GwLkMasqueradeDomain(pPopLnk)) {
		SysLogMessage(LOG_LEV_MESSAGE,
			      "[PSYNC/MASQ] MasqDomain = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\"\n",
			      pPopLnk->pszDomain + 1, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);

		/* Sync ( "pszDomain" == "?" + masq-domain or "pszDomain" == "&" + add-domain ) */
		if (UPopSyncRemoteLink(pPopLnk->pszDomain, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName,
				       pPopLnk->pszRmtPassword, &SRep, pPopLnk->pszAuthType,
				       szFetchHdrTags, pszErrorAccount) < 0) {
			ErrLogMessage(LOG_LEV_MESSAGE,
				      "[PSYNC/MASQ] MasqDomain = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\" Failed !\n",
				      pPopLnk->pszDomain + 1, pPopLnk->pszRmtDomain,
				      pPopLnk->pszRmtName);

			ZeroData(SRep);
			if (PSYNCLogEnabled(pPSYNCCfg))
				PSYNCLogSession(pPopLnk, &SRep, "SYNC=EFAIL");
		} else {
			if (PSYNCLogEnabled(pPSYNCCfg))
				PSYNCLogSession(pPopLnk, &SRep, "SYNC=OK");
		}
	} else {
		char szSyncAddress[MAX_ADDR_NAME] = "";

		SysSNPrintf(szSyncAddress, sizeof(szSyncAddress) - 1, "%s%s", pPopLnk->pszName,
			    pPopLnk->pszDomain);

		SysLogMessage(LOG_LEV_MESSAGE,
			      "[PSYNC/EXT] Account = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\"\n",
			      szSyncAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);

		/* Sync ( "pszDomain" == "@" + domain ) */
		if (UPopSyncRemoteLink(szSyncAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName,
				       pPopLnk->pszRmtPassword, &SRep, pPopLnk->pszAuthType, szFetchHdrTags,
				       pszErrorAccount) < 0) {
			ErrLogMessage(LOG_LEV_MESSAGE,
				      "[PSYNC/EXT] Account = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\" Failed !\n",
				      szSyncAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);

			ZeroData(SRep);
			if (PSYNCLogEnabled(pPSYNCCfg))
				PSYNCLogSession(pPopLnk, &SRep, "SYNC=EFAIL");
		} else {
			if (PSYNCLogEnabled(pPSYNCCfg))
				PSYNCLogSession(pPopLnk, &SRep, "SYNC=OK");
		}
	}

	/* Decrease threads count */
	PSYNCThreadCountAdd(-1, hShbPSYNC);

	GwLkLinkUnlock(pPopLnk);
	SvrReleaseConfigHandle(hSvrConfig);
	GwLkFreePOP3Link(pPopLnk);
	SysFree(pPSYNCCfg);

	/* Notify thread exit semaphore */
	PSYNCThreadNotifyExit();

	SysLogMessage(LOG_LEV_MESSAGE, "[PSYNC] exit\n");

	return 0;
}

static SYS_THREAD PSYNCCreateSyncThread(SHB_HANDLE hShbPSYNC, POP3Link *pPopLnk)
{
	PSYNCThreadData *pSTD = (PSYNCThreadData *) SysAlloc(sizeof(PSYNCThreadData));

	if (pSTD == NULL)
		return SYS_INVALID_THREAD;
	if ((pSTD->pPSYNCCfg = PSYNCGetConfigCopy(hShbPSYNC)) == NULL) {
		SysFree(pSTD);
		return SYS_INVALID_THREAD;
	}
	pSTD->pPopLnk = pPopLnk;

	SYS_THREAD hThread = SysCreateThread(PSYNCThreadSyncProc, pSTD);

	if (hThread == SYS_INVALID_THREAD) {
		SysFree(pSTD->pPSYNCCfg);
		SysFree(pSTD);
		return SYS_INVALID_THREAD;
	}

	return hThread;
}

static int PSYNCStartTransfer(SHB_HANDLE hShbPSYNC, PSYNCConfig *pPSYNCCfg)
{
	GWLKF_HANDLE hLinksDB = GwLkOpenDB();

	if (hLinksDB == INVALID_GWLKF_HANDLE) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString(ErrorFetch()));
		return ErrorPop();
	}

	POP3Link *pPopLnk = GwLkGetFirstUser(hLinksDB);

	for (; pPopLnk != NULL; pPopLnk = GwLkGetNextUser(hLinksDB)) {
		/* Check if link is enabled */
		if (GwLkCheckEnabled(pPopLnk) < 0) {
			GwLkFreePOP3Link(pPopLnk);
			continue;
		}
		if (SysWaitSemaphore(hSyncSem, SYS_INFINITE_TIMEOUT) < 0 ||
		    PSYNCTimeToStop(hShbPSYNC)) {
			GwLkFreePOP3Link(pPopLnk);
			break;
		}

		SYS_THREAD hClientThread = PSYNCCreateSyncThread(hShbPSYNC, pPopLnk);

		if (hClientThread == SYS_INVALID_THREAD) {
			ErrorPush();
			GwLkFreePOP3Link(pPopLnk);
			SysReleaseSemaphore(hSyncSem, 1);
			GwLkCloseDB(hLinksDB);
			return ErrorPop();
		}
		SysCloseThread(hClientThread, 0);
	}

	GwLkCloseDB(hLinksDB);

	return 0;
}

unsigned int PSYNCThreadProc(void *pThreadData)
{
	SysLogMessage(LOG_LEV_MESSAGE, "%s started\n", PSYNC_SERVER_NAME);

	int iElapsedTime = INT_MAX - 2 * PSYNC_WAKEUP_TIME; /* [i_a] start the first PSYNC immediately after starting the xmail server itself */

	for (;;) {
		SysSleep(PSYNC_WAKEUP_TIME);

		iElapsedTime += PSYNC_WAKEUP_TIME;

		PSYNCConfig *pPSYNCCfg = PSYNCGetConfigCopy(hShbPSYNC);

		if (pPSYNCCfg == NULL)
			break;
		if (pPSYNCCfg->ulFlags & PSYNCF_STOP_SERVER) {
			SysFree(pPSYNCCfg);
			break;
		}

		if ((pPSYNCCfg->iSyncInterval == 0 || iElapsedTime < pPSYNCCfg->iSyncInterval)
		    && !PSYNCNeedSync()) {
			SysFree(pPSYNCCfg);
			continue;
		}
		iElapsedTime = 0;

		PSYNCStartTransfer(hShbPSYNC, pPSYNCCfg);

		SysFree(pPSYNCCfg);
	}

	/* Wait for client completion */
	for (int iTotalWait = 0; (iTotalWait < MAX_CLIENTS_WAIT);
	     iTotalWait += PSYNC_WAIT_SLEEP) {
		PSYNCConfig *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

		if (pPSYNCCfg == NULL)
			break;

		long lThreadCount = pPSYNCCfg->lThreadCount;

		ShbUnlock(hShbPSYNC);

		if (lThreadCount == 0)
			break;

		SysSleep(PSYNC_WAIT_SLEEP);
	}

	SysLogMessage(LOG_LEV_MESSAGE, "%s stopped\n", PSYNC_SERVER_NAME);

	return 0;
}

