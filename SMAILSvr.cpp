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
#include "MiscUtils.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MailDomains.h"
#include "AliasDomain.h"
#include "SMTPUtils.h"
#include "ExtAliases.h"
#include "UsrMailList.h"
#include "Filter.h"
#include "DNS.h"
#include "MailConfig.h"
#include "SMAILSvr.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define SMAIL_WAITMSG_TIMEOUT       2000
#define CUSTOM_PROC_LINE_MAX        1024
#define SMAIL_EXTERNAL_EXIT_BREAK   16
#define SMAIL_STOP_PROCESSING       3111965L

struct MacroSubstCtx {
	SPLF_HANDLE hFSpool;
	FileSection FSect;
};

static int SMAILThreadCountAdd(long lCount, SHB_HANDLE hShbSMAIL, SMAILConfig *pSMAILCfg = NULL);
static int SMAILLogEnabled(SHB_HANDLE hShbSMAIL, SMAILConfig *pSMAILCfg = NULL);

static SMAILConfig *SMAILGetConfigCopy(SHB_HANDLE hShbSMAIL)
{
	SMAILConfig *pLMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

	if (pLMAILCfg == NULL)
		return NULL;

	SMAILConfig *pNewLMAILCfg = (SMAILConfig *) SysAlloc(sizeof(SMAILConfig));

	if (pNewLMAILCfg != NULL)
		memcpy(pNewLMAILCfg, pLMAILCfg, sizeof(SMAILConfig));
	ShbUnlock(hShbSMAIL);

	return pNewLMAILCfg;
}

static int SMAILThreadCountAdd(long lCount, SHB_HANDLE hShbSMAIL, SMAILConfig *pSMAILCfg)
{
	int iDoUnlock = 0;

	if (pSMAILCfg == NULL) {
		if ((pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}
	pSMAILCfg->lThreadCount += lCount;
	if (iDoUnlock)
		ShbUnlock(hShbSMAIL);

	return 0;
}

static int SMAILLogEnabled(SHB_HANDLE hShbSMAIL, SMAILConfig *pSMAILCfg)
{
	int iDoUnlock = 0;

	if (pSMAILCfg == NULL) {
		if ((pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}

	unsigned long ulFlags = pSMAILCfg->ulFlags;

	if (iDoUnlock)
		ShbUnlock(hShbSMAIL);

	return (ulFlags & SMAILF_LOG_ENABLED) ? 1: 0;
}

static int SMAILHandleResendNotify(SVRCFG_HANDLE hSvrConfig, QUEUE_HANDLE hQueue,
				   QMSG_HANDLE hMessage, SPLF_HANDLE hFSpool)
{
	/* Check if it's time to notify about a failed delivery attempt */
	int iRetryCount = QueGetTryCount(hMessage);
	char szNotifyPattern[128];

	SvrConfigVar("NotifyTryPattern", szNotifyPattern, sizeof(szNotifyPattern) - 1,
		     hSvrConfig, "");

	for (char *pszTry = szNotifyPattern; pszTry != NULL; ++pszTry) {
		if (isdigit(*pszTry) && atoi(pszTry) == iRetryCount)
			break;
		if ((pszTry = strchr(pszTry, ',')) == NULL)
			return 0;
	}

	/* Build the notification text and send the message */
	time_t tLastTry = QueGetLastTryTime(hMessage);
	time_t tNextTry = QueGetMessageNextOp(hQueue, hMessage);
	char szTimeLast[128];
	char szTimeNext[128];

	MscGetTimeStr(szTimeLast, sizeof(szTimeLast) - 1, tLastTry);
	MscGetTimeStr(szTimeNext, sizeof(szTimeNext) - 1, tNextTry);

	char *pszText =
		StrSprint("** This is a temporary error and you do not have to resend the message\r\n"
			  "** The system tried to send the message at      : %s\r\n"
			  "** The current number of delivery attempts is   : %d\r\n"
			  "** The system will try to resend the message at : %s\r\n",
			  szTimeLast, iRetryCount, szTimeNext);

	if (pszText == NULL)
		return ErrGetErrorCode();

	int iNotifyResult = QueUtNotifyTempErrDelivery(hQueue, hMessage, hFSpool,
						       NULL, pszText, NULL);

	SysFree(pszText);

	return iNotifyResult;
}

static int SMAILMailingListExplode(UserInfo *pUI, SPLF_HANDLE hFSpool)
{
	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);
	char const *const *ppszRcpt = USmlGetRcptTo(hFSpool);

	char szDestUser[MAX_ADDR_NAME];
	char szDestDomain[MAX_ADDR_NAME];

	if (USmtpSplitEmailAddr(ppszRcpt[0], szDestUser, szDestDomain) < 0)
		return ErrGetErrorCode();

	/* Get Mailing List Sender address. If this account variable does not exist */
	/* the sender will be the "real" message sender */
	char *pszMLSender = UsrGetUserInfoVar(pUI, "ListSender");

	/* Check if the use of the Reply-To: is requested */
	int iUseReplyTo = UsrGetUserInfoVarInt(pUI, "UseReplyTo", 1);

	USRML_HANDLE hUsersDB = UsrMLOpenDB(pUI);

	if (hUsersDB == INVALID_USRML_HANDLE) {
		ErrorPush();
		SysFree(pszMLSender);
		return ErrorPop();
	}
	/* Mailing list scan */
	MLUserInfo *pMLUI = UsrMLGetFirstUser(hUsersDB);

	for (; pMLUI != NULL; pMLUI = UsrMLGetNextUser(hUsersDB)) {
		if (strchr(pMLUI->pszPerms, 'R') != NULL) {
			/* Get message handle */
			QMSG_HANDLE hMessage = QueCreateMessage(hSpoolQueue);

			if (hMessage == INVALID_QMSG_HANDLE) {
				ErrorPush();
				SysFree(pszMLSender);
				UsrMLFreeUser(pMLUI);
				UsrMLCloseDB(hUsersDB);
				return ErrorPop();
			}

			char szQueueFilePath[SYS_MAX_PATH];

			QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

			/* Create spool file. If "pszMLSender" is NULL the original sender is kept */
			if (USmlCreateSpoolFile(hFSpool, pszMLSender, pMLUI->pszAddress,
						szQueueFilePath, (iUseReplyTo != 0) ? "Reply-To" : "",
						ppszRcpt[0], NULL) < 0) {
				ErrorPush();
				QueCleanupMessage(hSpoolQueue, hMessage);
				QueCloseMessage(hSpoolQueue, hMessage);
				SysFree(pszMLSender);
				UsrMLFreeUser(pMLUI);
				UsrMLCloseDB(hUsersDB);
				return ErrorPop();
			}
			/* Transfer file to the spool */
			if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
				ErrorPush();
				QueCleanupMessage(hSpoolQueue, hMessage);
				QueCloseMessage(hSpoolQueue, hMessage);
				SysFree(pszMLSender);
				UsrMLFreeUser(pMLUI);
				UsrMLCloseDB(hUsersDB);
				return ErrorPop();
			}
		}
		UsrMLFreeUser(pMLUI);
	}
	UsrMLCloseDB(hUsersDB);
	SysFree(pszMLSender);

	return 0;
}

static int SMAILRemoteMsgSMTPSend(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
				  SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				  char const *pszDestDomain, SMTPError *pSMTPE)
{
	/* Apply filters ... */
	if (FilFilterMessage(hFSpool, hQueue, hMessage, FILTER_MODE_OUTBOUND, NULL) < 0)
		return ErrGetErrorCode();

	int iError;
	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
	char const *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
	char const *pszSpoolFile = USmlGetSpoolFile(hFSpool);
	char const *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);
	char const *pszMailFrom = USmlMailFrom(hFSpool);
	char const *pszSendMailFrom = USmlSendMailFrom(hFSpool);
	char const *pszRcptTo = USmlRcptTo(hFSpool);
	char const *pszSendRcptTo = USmlSendRcptTo(hFSpool);
	char const *pszRelayDomain = USmlGetRelayDomain(hFSpool);
	FileSection FSect;

	/* This function retrieve the spool file message section and sync the content. */
	/* This is necessary before sending the file */
	if (USmlGetMsgFileSection(hFSpool, FSect) < 0)
		return ErrGetErrorCode();

	/* Get HELO domain */
	char szHeloDomain[MAX_HOST_NAME];

	SvrConfigVar("HeloDomain", szHeloDomain, sizeof(szHeloDomain) - 1, hSvrConfig, "");

	char const *pszHeloDomain = IsEmptyString(szHeloDomain) ? NULL: szHeloDomain;

#if 1
	SysLogMessage(LOG_LEV_MESSAGE,
		"SMAIL SMTP-Send: DEST = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
				      pszDestDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);
#endif
#if 0
	ErrSetErrorCode(ERR_BAD_SERVER_ADDR);
	return ErrGetErrorCode();
#endif

	/* If it's a relayed message use the associated relay */
	if (pszRelayDomain != NULL) {
		SysLogMessage(LOG_LEV_MESSAGE,
			      "SMAIL SMTP-Send DEST = \"%s\" CMX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
			      pszDestDomain, pszRelayDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

		USmtpCleanupError(pSMTPE);

		if (USmtpMailRmtDeliver(hSvrConfig, pszRelayDomain, pszHeloDomain, pszSendMailFrom,
					pszSendRcptTo, &FSect, pSMTPE) < 0) {
			ErrorPush();

			char szSmtpError[512];

			USmtpGetSMTPError(pSMTPE, szSmtpError, sizeof(szSmtpError));

			ErrLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send CMX = \"%s\" SMTP = \"%s\" "
				      "From = \"%s\" To = \"%s\" Failed !\n"
				      "%s = \"%s\"\n"
				      "%s = \"%s\"\n", pszRelayDomain, pszSMTPDomain, pszMailFrom,
				      pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError,
				      SMTP_SERVER_VARNAME, USmtpGetErrorServer(pSMTPE));

			QueUtErrLogMessage(hQueue, hMessage,
					   "SMAIL SMTP-Send CMX = \"%s\" SMTP = \"%s\" "
					   "From = \"%s\" To = \"%s\" Failed !\n"
					   "%s = \"%s\"\n"
					   "%s = \"%s\"\n", pszRelayDomain, pszSMTPDomain,
					   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
					   szSmtpError, SMTP_SERVER_VARNAME,
					   USmtpGetErrorServer(pSMTPE));

			return ErrorPop();
		}
		if (SMAILLogEnabled(hShbSMAIL)) {
			char szRmtMsgID[256];

			USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(pSMTPE), szRmtMsgID,
					     sizeof(szRmtMsgID));
			USmlLogMessage(hFSpool, "SMTP", szRmtMsgID, pszRelayDomain);
		}

		return 0;
	}
	/* Check the existance of direct SMTP forwarders */
	SMTPGateway **ppszFwdGws = USmtpGetFwdGateways(hSvrConfig, pszDestDomain);

	if (ppszFwdGws != NULL) {
		/*
		 * By initializing this to zero makes XMail to discharge all mail
		 * for domains that have an empty forwarders list
		 */
		iError = 0;
		for (int i = 0; ppszFwdGws[i] != NULL; i++) {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send DEST = \"%s\" FWD = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
				      pszDestDomain, ppszFwdGws[i]->pszHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

			USmtpCleanupError(pSMTPE);

			if ((iError = USmtpSendMail(ppszFwdGws[i], pszHeloDomain, pszSendMailFrom,
						    pszSendRcptTo, &FSect, pSMTPE)) == 0) {
				/* Log Mailer operation */
				if (SMAILLogEnabled(hShbSMAIL)) {
					char szRmtMsgID[256];

					USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(pSMTPE), szRmtMsgID,
							     sizeof(szRmtMsgID));
					USmlLogMessage(hFSpool, "FWD", szRmtMsgID, ppszFwdGws[i]->pszHost);
				}
				break;
			}

			char szSmtpError[512];

			USmtpGetSMTPError(pSMTPE, szSmtpError, sizeof(szSmtpError));

			ErrLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send FWD = \"%s\" SMTP = \"%s\" "
				      "From = \"%s\" To = \"%s\" Failed !\n"
				      "%s = \"%s\"\n"
				      "%s = \"%s\"\n", ppszFwdGws[i]->pszHost, pszSMTPDomain, pszMailFrom,
				      pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError,
				      SMTP_SERVER_VARNAME, USmtpGetErrorServer(pSMTPE));

			QueUtErrLogMessage(hQueue, hMessage,
					   "SMAIL SMTP-Send FWD = \"%s\" SMTP = \"%s\" "
					   "From = \"%s\" To = \"%s\" Failed !\n"
					   "%s = \"%s\"\n"
					   "%s = \"%s\"\n", ppszFwdGws[i]->pszHost, pszSMTPDomain,
					   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
					   szSmtpError, SMTP_SERVER_VARNAME,
					   USmtpGetErrorServer(pSMTPE));

			if (USmtpIsFatalError(pSMTPE))
				break;
		}
		USmtpFreeGateways(ppszFwdGws);

		return iError;
	}
	/* Try to get custom mail exchangers or DNS mail exchangers and if both tests */
	/* fails try direct */
	SMTPGateway **ppMXGWs = USmtpGetMailExchangers(hSvrConfig, pszDestDomain);

	if (ppMXGWs == NULL) {
		/* If the destination domain is an IP-domain do direct deliver */
		char szIP[64];

		if (MscIsIPDomain(pszDestDomain, szIP, sizeof(szIP))) {
			SysLogMessage(LOG_LEV_MESSAGE, "Direct IP delivery for \"%s\".\n", szIP);

			SysLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send DEST = \"%s\" IP = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
				      pszDestDomain, szIP, pszSMTPDomain, pszMailFrom, pszRcptTo);

			USmtpCleanupError(pSMTPE);

			if (USmtpMailRmtDeliver(hSvrConfig, szIP, pszHeloDomain, pszSendMailFrom, pszSendRcptTo,
						&FSect, pSMTPE) < 0) {
				ErrorPush();

				char szSmtpError[512];

				USmtpGetSMTPError(pSMTPE, szSmtpError, sizeof(szSmtpError));

				ErrLogMessage(LOG_LEV_MESSAGE,
					      "SMAIL SMTP-Send IP = \"%s\" SMTP = \"%s\" "
					      "From = \"%s\" To = \"%s\" Failed !\n"
					      "%s = \"%s\"\n"
					      "%s = \"%s\"\n", szIP, pszSMTPDomain, pszMailFrom,
					      pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError,
					      SMTP_SERVER_VARNAME, USmtpGetErrorServer(pSMTPE));

				QueUtErrLogMessage(hQueue, hMessage,
						   "SMAIL SMTP-Send IP = \"%s\" SMTP = \"%s\" "
						   "From = \"%s\" To = \"%s\" Failed !\n"
						   "%s = \"%s\"\n"
						   "%s = \"%s\"\n", szIP, pszSMTPDomain,
						   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
						   szSmtpError, SMTP_SERVER_VARNAME,
						   USmtpGetErrorServer(pSMTPE));

				return ErrorPop();
			}
			/* Log Mailer operation */
			if (SMAILLogEnabled(hShbSMAIL)) {
				char szRmtMsgID[256];

				USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(pSMTPE), szRmtMsgID,
						     sizeof(szRmtMsgID));
				USmlLogMessage(hFSpool, "SMTP", szRmtMsgID, szIP);
			}

			return 0;
		}

		/* Do DNS MX lookup and send to mail exchangers */
		char szDomainMXHost[MAX_HOST_NAME];
		MXS_HANDLE hMXSHandle = USmtpGetMXFirst(hSvrConfig, pszDestDomain,
							szDomainMXHost);

		if (hMXSHandle != INVALID_MXS_HANDLE) {
			iError = 0;
			do {
				SysLogMessage(LOG_LEV_MESSAGE,
					      "SMAIL SMTP-Send DEST = \"%s\" MX = \"%s\" SMTP = \"%s\" "
					      "From = \"%s\" To = \"%s\"\n",
					      pszDestDomain, szDomainMXHost, pszSMTPDomain, pszMailFrom,
					      pszRcptTo);

				USmtpCleanupError(pSMTPE);

				if ((iError = USmtpMailRmtDeliver(hSvrConfig, szDomainMXHost,
								  pszHeloDomain,
								  pszSendMailFrom,
								  pszSendRcptTo, &FSect,
								  pSMTPE)) == 0) {
					/* Log Mailer operation */
					if (SMAILLogEnabled(hShbSMAIL)) {
						char szRmtMsgID[256];

						USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(pSMTPE),
								     szRmtMsgID,
								     sizeof(szRmtMsgID));
						USmlLogMessage(hFSpool, "SMTP", szRmtMsgID,
							       szDomainMXHost);
					}
					break;
				}

				char szSmtpError[512];

				USmtpGetSMTPError(pSMTPE, szSmtpError, sizeof(szSmtpError));

				ErrLogMessage(LOG_LEV_MESSAGE,
					      "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" "
					      "From = \"%s\" To = \"%s\" Failed !\n"
					      "%s = \"%s\"\n"
					      "%s = \"%s\"\n", szDomainMXHost, pszSMTPDomain,
					      pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
					      szSmtpError, SMTP_SERVER_VARNAME,
					      USmtpGetErrorServer(pSMTPE));

				QueUtErrLogMessage(hQueue, hMessage,
						   "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" "
						   "From = \"%s\" To = \"%s\" Failed !\n"
						   "%s = \"%s\"\n"
						   "%s = \"%s\"\n", szDomainMXHost, pszSMTPDomain,
						   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
						   szSmtpError, SMTP_SERVER_VARNAME,
						   USmtpGetErrorServer(pSMTPE));

				if (USmtpIsFatalError(pSMTPE))
					break;
			} while (USmtpGetMXNext(hMXSHandle, szDomainMXHost) == 0);
			USmtpMXSClose(hMXSHandle);

			if (iError < 0)
				return iError;
		} else {
			iError = ErrGetErrorCode();

			/*
			 * If the target domain does not exist at all (or it has a
			 * misconfigured DNS), bounce soon without trying the A record.
			 * It's pointless engaging in retry policies when the recipient
			 * domain does not exist.
			 */
			if (DNS_FatalError(iError)) {
				ErrorPush();

				/* No account inside the handled domain */
				char szBounceMsg[512];

				SysSNPrintf(szBounceMsg, sizeof(szBounceMsg) - 1,
					    "Recipient domain \"%s\" does not exist "
					    "(or it has a misconfigured DNS)", pszDestDomain);

				SysLogMessage(LOG_LEV_MESSAGE,
					      "SMAIL SMTP-Send NXD/EDNS = \"%s\" SMTP = \"%s\" "
					      "From = \"%s\" To = \"%s\" Failed!\n",
					      pszDestDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);
				QueUtErrLogMessage(hQueue, hMessage, "%s\n", szBounceMsg);
				QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool, szBounceMsg, NULL,
							   true);

				return ErrorPop();
			}

			/*
			 * Fall back to A record delivery only if the domain has
			 * no MX records.
			 */
			if (iError != ERR_DNS_NOTFOUND) {
				ErrLogMessage(LOG_LEV_MESSAGE,
					      "SMAIL SMTP-Send EDNS = \"%s\" SMTP = \"%s\" "
					      "From = \"%s\" To = \"%s\" Failed !\n"
					      "%s = \"%s\"\n"
					      "%s = \"%s\"\n", pszDestDomain, pszSMTPDomain,
					      pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
					      ErrGetErrorString(iError), SMTP_SERVER_VARNAME,
					      pszDestDomain);

				QueUtErrLogMessage(hQueue, hMessage,
						   "SMAIL SMTP-Send EDNS = \"%s\" SMTP = \"%s\" "
						   "From = \"%s\" To = \"%s\" Failed !\n"
						   "%s = \"%s\"\n"
						   "%s = \"%s\"\n", pszDestDomain, pszSMTPDomain,
						   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
						   ErrGetErrorString(iError), SMTP_SERVER_VARNAME,
						   pszDestDomain);

				return ErrorSet(iError);
			}

			/* MX records for destination domain not found, try direct ! */
			SysLogMessage(LOG_LEV_MESSAGE,
				      "MX records for domain \"%s\" not found, trying direct.\n",
				      pszDestDomain);

			SysLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send FF = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
				      pszDestDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

			USmtpCleanupError(pSMTPE);

			if ((iError = USmtpMailRmtDeliver(hSvrConfig, pszDestDomain,
							  pszHeloDomain, pszSendMailFrom,
							  pszSendRcptTo, &FSect, pSMTPE)) < 0) {
				ErrorPush();

				/*
				 * If the A record of the recipient host does not exist, set the
				 * fatal SMTP error so that the caller can properly bounce the
				 * message.
				 */
				if (iError == ERR_DNS_NOTFOUND || iError == ERR_BAD_SERVER_ADDR)
					USmtpSetError(pSMTPE, SMTP_FATAL_ERROR,
						      ErrGetErrorString(iError), pszDestDomain);

				char szSmtpError[512];

				USmtpGetSMTPError(pSMTPE, szSmtpError, sizeof(szSmtpError));

				ErrLogMessage(LOG_LEV_MESSAGE,
					      "SMAIL SMTP-Send FF = \"%s\" SMTP = \"%s\" "
					      "From = \"%s\" To = \"%s\" Failed !\n"
					      "%s = \"%s\"\n"
					      "%s = \"%s\"\n", pszDestDomain, pszSMTPDomain,
					      pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
					      szSmtpError, SMTP_SERVER_VARNAME,
					      USmtpGetErrorServer(pSMTPE));

				QueUtErrLogMessage(hQueue, hMessage,
						   "SMAIL SMTP-Send FF = \"%s\" SMTP = \"%s\" "
						   "From = \"%s\" To = \"%s\" Failed !\n"
						   "%s = \"%s\"\n"
						   "%s = \"%s\"\n", pszDestDomain, pszSMTPDomain,
						   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
						   szSmtpError, SMTP_SERVER_VARNAME,
						   USmtpGetErrorServer(pSMTPE));

				return ErrorPop();
			}
			/* Log Mailer operation */
			if (SMAILLogEnabled(hShbSMAIL)) {
				char szRmtMsgID[256];

				USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(pSMTPE),
						     szRmtMsgID, sizeof(szRmtMsgID));
				USmlLogMessage(hFSpool, "SMTP", szRmtMsgID, pszDestDomain);
			}
		}
	} else {
		iError = 0;
		for (int i = 0; ppMXGWs[i] != NULL; i++) {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send DEST = \"%s\" MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
				      pszDestDomain, ppMXGWs[i]->pszHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

			USmtpCleanupError(pSMTPE);

			if ((iError = USmtpSendMail(ppMXGWs[i], pszHeloDomain, pszSendMailFrom,
						    pszSendRcptTo, &FSect, pSMTPE)) == 0) {
				/* Log Mailer operation */
				if (SMAILLogEnabled(hShbSMAIL)) {
					char szRmtMsgID[256];

					USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(pSMTPE),
							     szRmtMsgID, sizeof(szRmtMsgID));
					USmlLogMessage(hFSpool, "SMTP", szRmtMsgID,
						       ppMXGWs[i]->pszHost);
				}
				break;
			}

			char szSmtpError[512];

			USmtpGetSMTPError(pSMTPE, szSmtpError, sizeof(szSmtpError));

			ErrLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" "
				      "From = \"%s\" To = \"%s\" Failed !\n"
				      "%s = \"%s\"\n"
				      "%s = \"%s\"\n", ppMXGWs[i]->pszHost, pszSMTPDomain, pszMailFrom,
				      pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError,
				      SMTP_SERVER_VARNAME, USmtpGetErrorServer(pSMTPE));

			QueUtErrLogMessage(hQueue, hMessage,
					   "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" "
					   "From = \"%s\" To = \"%s\" Failed !\n"
					   "%s = \"%s\"\n"
					   "%s = \"%s\"\n", ppMXGWs[i]->pszHost, pszSMTPDomain,
					   pszMailFrom, pszRcptTo, SMTP_ERROR_VARNAME,
					   szSmtpError, SMTP_SERVER_VARNAME,
					   USmtpGetErrorServer(pSMTPE));

			if (USmtpIsFatalError(pSMTPE))
				break;
		}
		USmtpFreeGateways(ppMXGWs);

		if (iError < 0)
			return iError;
	}

	return 0;
}

static char *SMAILMacroLkupProc(void *pPrivate, char const *pszName, int iSize)
{
	MacroSubstCtx *pMSC = (MacroSubstCtx *) pPrivate;

	if (MemMatch(pszName, iSize, "FROM", 4)) {
		char const *const *ppszFrom = USmlGetMailFrom(pMSC->hFSpool);
		int iFromDomains = StrStringsCount(ppszFrom);

		return SysStrDup((iFromDomains > 0) ? ppszFrom[iFromDomains - 1] : "");
	} else if (MemMatch(pszName, iSize, "RCPT", 4)) {
		char const *const *ppszRcpt = USmlGetRcptTo(pMSC->hFSpool);
		int iRcptDomains = StrStringsCount(ppszRcpt);

		return SysStrDup((iRcptDomains > 0) ? ppszRcpt[iRcptDomains - 1] : "");
	} else if (MemMatch(pszName, iSize, "FILE", 4)) {

		return SysStrDup(pMSC->FSect.szFilePath);
	} else if (MemMatch(pszName, iSize, "MSGID", 5)) {

		return SysStrDup(USmlGetSpoolFile(pMSC->hFSpool));
	} else if (MemMatch(pszName, iSize, "MSGREF", 6)) {

		return SysStrDup(USmlGetSmtpMessageID(pMSC->hFSpool));
	} else if (MemMatch(pszName, iSize, "LOCALADDR", 9)) {
		char const *const *ppszInfo = USmlGetInfo(pMSC->hFSpool);

		return SysStrDup(ppszInfo[smiServerAddr]);
	} else if (MemMatch(pszName, iSize, "REMOTEADDR", 10)) {
		char const *const *ppszInfo = USmlGetInfo(pMSC->hFSpool);

		return SysStrDup(ppszInfo[smiClientAddr]);
	} else if (MemMatch(pszName, iSize, "TMPFILE", 7)) {
		char szTmpFile[SYS_MAX_PATH];

		MscSafeGetTmpFile(szTmpFile, sizeof(szTmpFile));
		if (MscCopyFile(szTmpFile, pMSC->FSect.szFilePath) < 0) {
			CheckRemoveFile(szTmpFile);
			return NULL;
		}

		return SysStrDup(szTmpFile);
	} else if (MemMatch(pszName, iSize, "USERAUTH", 8)) {
		char szAuthName[MAX_ADDR_NAME] = "-";

		USmlMessageAuth(pMSC->hFSpool, szAuthName, sizeof(szAuthName) - 1);

		return SysStrDup(szAuthName);
	}

	return SysStrDup("");
}

static int SMAILCmdMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool)
{
	MacroSubstCtx MSC;

	MSC.hFSpool = hFSpool;
	/*
	 * This function retrieve the spool file message section and sync the content.
	 * This is necessary before passing the file name to external programs.
	 */
	if (USmlGetMsgFileSection(hFSpool, MSC.FSect) < 0)
		return ErrGetErrorCode();

	return MscReplaceTokens(ppszCmdTokens, SMAILMacroLkupProc, &MSC);
}

static int SMAILCmd_external(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			     char const *pszDestDomain, char **ppszCmdTokens, int iNumTokens,
			     SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	/* Apply filters ... */
	if (FilFilterMessage(hFSpool, hQueue, hMessage, FILTER_MODE_INBOUND) < 0)
		return ErrGetErrorCode();

	if (iNumTokens < 5) {
		ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
		return ERR_BAD_DOMAIN_PROC_CMD_SYNTAX;
	}

	int iPriority = atoi(ppszCmdTokens[1]);
	int iWaitTimeout = atoi(ppszCmdTokens[2]) * 1000;
	int iExitStatus = 0;

	if (SysExec(ppszCmdTokens[3], &ppszCmdTokens[3], iWaitTimeout, iPriority,
		    &iExitStatus) < 0) {
		ErrorPush();

		char const *pszMailFrom = USmlMailFrom(hFSpool);
		char const *pszRcptTo = USmlRcptTo(hFSpool);

		ErrLogMessage(LOG_LEV_MESSAGE,
			      "SMAIL EXTRN-Send Prg = \"%s\" Domain = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
			      ppszCmdTokens[3], pszDestDomain, pszMailFrom, pszRcptTo);

		QueUtErrLogMessage(hQueue, hMessage,
				   "SMAIL EXTRN-Send Prg = \"%s\" Domain = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
				   ppszCmdTokens[3], pszDestDomain, pszMailFrom, pszRcptTo);

		return ErrorPop();
	}
	/* Log Mailer operation */
	if (SMAILLogEnabled(hShbSMAIL))
		USmlLogMessage(hFSpool, "EXTRN", NULL, ppszCmdTokens[3]);

	return (iExitStatus == SMAIL_EXTERNAL_EXIT_BREAK) ? SMAIL_STOP_PROCESSING: 0;
}

static int SMAILCmd_filter(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			   char const *pszDestDomain, char **ppszCmdTokens, int iNumTokens,
			   SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	if (iNumTokens < 5) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}

	int iPriority = atoi(ppszCmdTokens[1]);
	int iWaitTimeout = atoi(ppszCmdTokens[2]) * 1000;
	int iExitStatus = 0;

	if (SysExec(ppszCmdTokens[3], &ppszCmdTokens[3], iWaitTimeout, iPriority,
		    &iExitStatus) < 0) {
		ErrorPush();

		char const *pszMailFrom = USmlMailFrom(hFSpool);
		char const *pszRcptTo = USmlRcptTo(hFSpool);

		ErrLogMessage(LOG_LEV_MESSAGE,
			      "USMAIL FILTER Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
			      ppszCmdTokens[3], pszMailFrom, pszRcptTo);

		QueUtErrLogMessage(hQueue, hMessage,
				   "USMAIL FILTER Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
				   ppszCmdTokens[3], pszMailFrom, pszRcptTo);

		return ErrorPop();
	}

	/* Log Mailer operation */
	if (SMAILLogEnabled(hShbSMAIL))
		USmlLogMessage(hFSpool, "FILTER", NULL, ppszCmdTokens[3]);

	/* Separate code from flags */
	int iExitFlags = iExitStatus & FILTER_FLAGS_MASK;

	iExitStatus &= ~FILTER_FLAGS_MASK;

	if (iExitStatus == FILTER_OUT_EXITCODE ||
	    iExitStatus == FILTER_OUT_NN_EXITCODE ||
	    iExitStatus == FILTER_OUT_NNF_EXITCODE) {
		/* Filter out message */
		char *pszRejMsg = FilGetFilterRejMessage(USmlGetSpoolFilePath(hFSpool));

		if (iExitStatus == FILTER_OUT_EXITCODE)
			QueUtNotifyPermErrDelivery(hQueue, hMessage, NULL,
						   (pszRejMsg != NULL) ? pszRejMsg :
						   ErrGetErrorString(ERR_FILTERED_MESSAGE),
						   NULL, true);
		else if (iExitStatus == FILTER_OUT_NN_EXITCODE)
			QueCleanupMessage(hQueue, hMessage,
					  !QueUtRemoveSpoolErrors());
		else
			QueCleanupMessage(hQueue, hMessage, false);
		SysFree(pszRejMsg);

		ErrSetErrorCode(ERR_FILTERED_MESSAGE);
		return ERR_FILTERED_MESSAGE;
	} else if (iExitStatus == FILTER_MODIFY_EXITCODE) {
		/* Filter modified the message, we need to reload the spool handle */
		if (USmlReloadHandle(hFSpool) < 0) {
			ErrorPush();

			char const *pszMailFrom = USmlMailFrom(hFSpool);
			char const *pszRcptTo = USmlRcptTo(hFSpool);

			SysLogMessage(LOG_LEV_MESSAGE,
				      "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
				      pszMailFrom, pszRcptTo,
				      ppszCmdTokens[3]);

			QueUtErrLogMessage(hQueue, hMessage,
					   "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
					   pszMailFrom, pszRcptTo,
					   ppszCmdTokens[3]);

			QueCleanupMessage(hQueue, hMessage, true);

			return ErrorPop();
		}
	}

	return (iExitFlags & FILTER_FLAGS_BREAK) ? SMAIL_STOP_PROCESSING: 0;
}

static int SMAILCmd_smtp(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			 char const *pszDestDomain, char **ppszCmdTokens, int iNumTokens,
			 SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	if (iNumTokens != 1) {
		ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
		return ERR_BAD_DOMAIN_PROC_CMD_SYNTAX;
	}

	SMTPError SMTPE;

	USmtpInitError(&SMTPE);

	if (SMAILRemoteMsgSMTPSend(hSvrConfig, hShbSMAIL, hFSpool, hQueue, hMessage,
				   pszDestDomain, &SMTPE) < 0) {
		/* If we get an SMTP fatal error We must return <0 , otherwise >0 to give */
		/* XMail to ability to resume the command */
		int iReturnCode = USmtpIsFatalError(&SMTPE) ?
			ErrGetErrorCode(): -ErrGetErrorCode();

		/* If a permanent SMTP error has been detected, then notify the message sender */
		if (USmtpIsFatalError(&SMTPE))
			QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool,
						   USmtpGetErrorMessage(&SMTPE),
						   USmtpGetErrorServer(&SMTPE), false);

		USmtpCleanupError(&SMTPE);

		return iReturnCode;
	}
	USmtpCleanupError(&SMTPE);

	return 0;
}

static int SMAILCmd_smtprelay(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			      char const *pszDestDomain, char **ppszCmdTokens, int iNumTokens,
			      SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	/* Apply filters ... */
	if (FilFilterMessage(hFSpool, hQueue, hMessage, FILTER_MODE_OUTBOUND) < 0)
		return ErrGetErrorCode();

	if (iNumTokens < 2) {
		ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
		return ERR_BAD_DOMAIN_PROC_CMD_SYNTAX;
	}

	char **ppszRelays = NULL;

	if (ppszCmdTokens[1][0] == '#') {
		if ((ppszRelays = StrTokenize(ppszCmdTokens[1] + 1, ";")) != NULL)
			MscRandomizeStringsOrder(ppszRelays);
	} else
		ppszRelays = StrTokenize(ppszCmdTokens[1], ";");

	if (ppszRelays == NULL)
		return ErrGetErrorCode();

	SMTPGateway **ppGws = USmtpGetCfgGateways(hSvrConfig, ppszRelays,
						  iNumTokens > 2 ? ppszCmdTokens[2]: NULL);

	StrFreeStrings(ppszRelays);
	if (ppGws == NULL)
		return ErrGetErrorCode();

	/* This function retrieve the spool file message section and sync the content. */
	/* This is necessary before sending the file */
	FileSection FSect;

	if (USmlGetMsgFileSection(hFSpool, FSect) < 0) {
		ErrorPush();
		USmtpFreeGateways(ppGws);
		return ErrorPop();
	}
	/* Get spool file infos */
	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
	char const *pszMailFrom = USmlMailFrom(hFSpool);
	char const *pszRcptTo = USmlRcptTo(hFSpool);
	char const *pszSendMailFrom = USmlSendMailFrom(hFSpool);
	char const *pszSendRcptTo = USmlSendRcptTo(hFSpool);
	char const *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);

	/* Get HELO domain */
	char szHeloDomain[MAX_HOST_NAME];

	SvrConfigVar("HeloDomain", szHeloDomain, sizeof(szHeloDomain) - 1, hSvrConfig, "");

	char const *pszHeloDomain = IsEmptyString(szHeloDomain) ? NULL: szHeloDomain;

	SMTPError SMTPE;

	USmtpInitError(&SMTPE);

	/* By initializing this to zero makes XMail to discharge all mail for domains */
	/* that have an empty relay list */
	int iReturnCode = 0;

	for (int i = 0; ppGws[i] != NULL && iReturnCode >= 0; i++) {
		SysLogMessage(LOG_LEV_MESSAGE,
			      "SMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
			      ppGws[i]->pszHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

		USmtpCleanupError(&SMTPE);

		if (USmtpSendMail(ppGws[i], pszHeloDomain, pszSendMailFrom,
				  pszSendRcptTo, &FSect, &SMTPE) == 0) {
			/* Log Mailer operation */
			if (SMAILLogEnabled(hShbSMAIL)) {
				char szRmtMsgID[256];

				USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(&SMTPE),
						     szRmtMsgID,
						     sizeof(szRmtMsgID));
				USmlLogMessage(hFSpool, "RLYS", szRmtMsgID,
					       ppGws[i]->pszHost);
			}
			USmtpCleanupError(&SMTPE);
			USmtpFreeGateways(ppGws);

			return 0;
		}

		int iErrorCode = ErrGetErrorCode();
		char szSmtpError[512];

		USmtpGetSMTPError(&SMTPE, szSmtpError, sizeof(szSmtpError));

		ErrLogMessage(LOG_LEV_MESSAGE,
			      "SMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n"
			      "%s = \"%s\"\n"
			      "%s = \"%s\"\n", ppGws[i]->pszHost, pszSMTPDomain, pszMailFrom,
			      pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError, SMTP_SERVER_VARNAME,
			      USmtpGetErrorServer(&SMTPE));

		QueUtErrLogMessage(hQueue, hMessage,
				   "SMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n"
				   "%s = \"%s\"\n"
				   "%s = \"%s\"\n", ppGws[i]->pszHost, pszSMTPDomain, pszMailFrom,
				   pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError,
				   SMTP_SERVER_VARNAME, USmtpGetErrorServer(&SMTPE));

		/* If a permanent SMTP error has been detected, then notify the message sender */
		if (USmtpIsFatalError(&SMTPE))
			QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool,
						   USmtpGetErrorMessage(&SMTPE),
						   USmtpGetErrorServer(&SMTPE), false);

		iReturnCode = USmtpIsFatalError(&SMTPE) ? iErrorCode: -iErrorCode;
	}
	USmtpCleanupError(&SMTPE);
	USmtpFreeGateways(ppGws);

	return iReturnCode;
}

static int SMAILCmd_redirect(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			     char const *pszDestDomain, char **ppszCmdTokens, int iNumTokens,
			     SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	if (iNumTokens < 2) {
		ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
		return ERR_BAD_DOMAIN_PROC_CMD_SYNTAX;
	}

	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);
	char const *const *ppszRcpt = USmlGetRcptTo(hFSpool);

	int iFromDomains = StrStringsCount(ppszFrom);
	int iRcptDomains = StrStringsCount(ppszRcpt);

	char szLocalUser[MAX_ADDR_NAME], szLocalDomain[MAX_ADDR_NAME];

	if (iRcptDomains < 1 ||
	    USmtpSplitEmailAddr(ppszRcpt[iRcptDomains - 1], szLocalUser,
				szLocalDomain) < 0)
		return ErrGetErrorCode();

	for (int i = 1; ppszCmdTokens[i] != NULL; i++) {
		/* Get message handle */
		QMSG_HANDLE hRedirMessage = QueCreateMessage(hSpoolQueue);
		char szQueueFilePath[SYS_MAX_PATH], szAliasAddr[MAX_ADDR_NAME];

		if (hRedirMessage == INVALID_QMSG_HANDLE)
			return ErrGetErrorCode();
		QueGetFilePath(hSpoolQueue, hRedirMessage, szQueueFilePath);
		if (strchr(ppszCmdTokens[i], '@') == NULL)
			SysSNPrintf(szAliasAddr, sizeof(szAliasAddr) - 1, "%s@%s",
				    szLocalUser, ppszCmdTokens[i]);
		else
			StrSNCpy(szAliasAddr, ppszCmdTokens[i]);

		if (USmlCreateSpoolFile(hFSpool, NULL, szAliasAddr, szQueueFilePath,
					NULL) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hRedirMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
	}

	return 0;
}

static int SMAILCmd_lredirect(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			      char const *pszDestDomain, char **ppszCmdTokens, int iNumTokens,
			      SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	if (iNumTokens < 2) {
		ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
		return ERR_BAD_DOMAIN_PROC_CMD_SYNTAX;
	}

	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);
	char const *const *ppszRcpt = USmlGetRcptTo(hFSpool);

	int iFromDomains = StrStringsCount(ppszFrom);
	int iRcptDomains = StrStringsCount(ppszRcpt);

	char szLocalUser[MAX_ADDR_NAME], szLocalDomain[MAX_ADDR_NAME];

	if (iRcptDomains < 1 ||
	    USmtpSplitEmailAddr(ppszRcpt[iRcptDomains - 1], szLocalUser,
				szLocalDomain) < 0)
		return ErrGetErrorCode();

	for (int i = 1; ppszCmdTokens[i] != NULL; i++) {
		/* Get message handle */
		QMSG_HANDLE hRedirMessage = QueCreateMessage(hSpoolQueue);
		char szQueueFilePath[SYS_MAX_PATH], szAliasAddr[MAX_ADDR_NAME];

		if (hRedirMessage == INVALID_QMSG_HANDLE)
			return ErrGetErrorCode();
		QueGetFilePath(hSpoolQueue, hRedirMessage, szQueueFilePath);
		if (strchr(ppszCmdTokens[i], '@') == NULL)
			SysSNPrintf(szAliasAddr, sizeof(szAliasAddr) - 1, "%s@%s",
				    szLocalUser, ppszCmdTokens[i]);
		else
			StrSNCpy(szAliasAddr, ppszCmdTokens[i]);

		if (USmlCreateSpoolFile(hFSpool, ppszRcpt[iRcptDomains - 1], szAliasAddr,
					szQueueFilePath, NULL) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hRedirMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
	}

	return 0;
}

static int SMAILCustomProcessMessage(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
				     SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
				     QMSG_HANDLE hMessage, char const *pszDestDomain,
				     char const *pszDestUser, char const *pszCustFilePath)
{
	FILE *pCPFile = fopen(pszCustFilePath, "rt");

	if (pCPFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszCustFilePath);
		return ERR_FILE_OPEN;
	}
	/* Create pushback command file */
	char szTmpFile[SYS_MAX_PATH];

	UsrGetTmpFile(NULL, szTmpFile, sizeof(szTmpFile));

	FILE *pPushBFile = fopen(szTmpFile, "wt");

	if (pPushBFile == NULL) {
		fclose(pCPFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile);
		return ERR_FILE_CREATE;
	}

	int iPushBackCmds = 0;
	char szCmdLine[CUSTOM_PROC_LINE_MAX];

	while (MscGetConfigLine(szCmdLine, sizeof(szCmdLine) - 1, pCPFile) != NULL) {
		char **ppszCmdTokens = StrGetTabLineStrings(szCmdLine);

		if (ppszCmdTokens == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszCmdTokens);

		if (iFieldsCount > 0) {
			/* Do command line macro substitution */
			SMAILCmdMacroSubstitutes(ppszCmdTokens, hFSpool);

			int iCmdResult = 0;

			if (stricmp(ppszCmdTokens[0], "external") == 0)
				iCmdResult =
					SMAILCmd_external(hSvrConfig, hShbSMAIL, pszDestDomain,
							  ppszCmdTokens, iFieldsCount, hFSpool,
							  hQueue, hMessage);
			else if (stricmp(ppszCmdTokens[0], "filter") == 0)
				iCmdResult = SMAILCmd_filter(hSvrConfig, hShbSMAIL, pszDestDomain,
							     ppszCmdTokens, iFieldsCount, hFSpool,
							     hQueue, hMessage);
			else if (stricmp(ppszCmdTokens[0], "smtp") == 0)
				iCmdResult = SMAILCmd_smtp(hSvrConfig, hShbSMAIL, pszDestDomain,
							   ppszCmdTokens, iFieldsCount, hFSpool,
							   hQueue, hMessage);
			else if (stricmp(ppszCmdTokens[0], "smtprelay") == 0)
				iCmdResult =
					SMAILCmd_smtprelay(hSvrConfig, hShbSMAIL, pszDestDomain,
							   ppszCmdTokens, iFieldsCount, hFSpool,
							   hQueue, hMessage);
			else if (stricmp(ppszCmdTokens[0], "redirect") == 0)
				iCmdResult =
					SMAILCmd_redirect(hSvrConfig, hShbSMAIL, pszDestDomain,
							  ppszCmdTokens, iFieldsCount, hFSpool,
							  hQueue, hMessage);
			else if (stricmp(ppszCmdTokens[0], "lredirect") == 0)
				iCmdResult =
					SMAILCmd_lredirect(hSvrConfig, hShbSMAIL, pszDestDomain,
							   ppszCmdTokens, iFieldsCount, hFSpool,
							   hQueue, hMessage);
			else {
				SysLogMessage(LOG_LEV_ERROR,
					      "Invalid command \"%s\" in file \"%s\"\n",
					      ppszCmdTokens[0], pszCustFilePath);

			}

			/* Check for the stop-processing error code */
			if (iCmdResult == SMAIL_STOP_PROCESSING) {
				StrFreeStrings(ppszCmdTokens);
				break;
			}
			/* Test if we must save a failed command */
			/* <0 = Error ; ==0 = Success ; >0 = Transient error ( save the command ) */
			if (iCmdResult > 0) {
				fprintf(pPushBFile, "%s\n", szCmdLine);

				++iPushBackCmds;
			}
			/* An error code might result if filters blocked the message. If this is the */
			/* case QueCheckMessage() will return error and we MUST stop processing */
			if (iCmdResult < 0 &&
			    QueCheckMessage(hQueue, hMessage) < 0) {
				ErrorPush();
				StrFreeStrings(ppszCmdTokens);
				fclose(pPushBFile);
				fclose(pCPFile);
				SysRemove(szTmpFile);
				return ErrorPop();
			}
		}
		StrFreeStrings(ppszCmdTokens);
	}
	fclose(pPushBFile);
	fclose(pCPFile);
	SysRemove(pszCustFilePath);

	if (iPushBackCmds > 0) {
		/* If commands left out of processing, push them into the custom file */
		if (MscMoveFile(szTmpFile, pszCustFilePath) < 0)
			return ErrGetErrorCode();

		ErrSetErrorCode(ERR_INCOMPLETE_PROCESSING);
		return ERR_INCOMPLETE_PROCESSING;
	}
	SysRemove(szTmpFile);

	return 0;
}

static int SMAILHandleRemoteUserMessage(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
					SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
					QMSG_HANDLE hMessage, char const *pszDestDomain,
					char const *pszDestUser)
{
	/* Try domain custom processing */
	char szCustFilePath[SYS_MAX_PATH];

	if (USmlGetDomainMsgCustomFile(hFSpool, hQueue, hMessage, pszDestDomain,
				       szCustFilePath) == 0)
		return SMAILCustomProcessMessage(hSvrConfig, hShbSMAIL, hFSpool,
						 hQueue, hMessage, pszDestDomain,
						 pszDestUser, szCustFilePath);

	/* Fall down to use standard SMTP delivery */
	SMTPError SMTPE;

	USmtpInitError(&SMTPE);

	if (SMAILRemoteMsgSMTPSend(hSvrConfig, hShbSMAIL, hFSpool, hQueue, hMessage,
				   pszDestDomain, &SMTPE) < 0) {
		ErrorPush();
		/*
		 * If a permanent SMTP error has been detected, then notify the message
		 * sender and remove the spool file
		 */
		if (USmtpIsFatalError(&SMTPE))
			QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool,
						   USmtpGetErrorMessage(&SMTPE),
						   USmtpGetErrorServer(&SMTPE), true);

		USmtpCleanupError(&SMTPE);

		return ErrorPop();
	}
	USmtpCleanupError(&SMTPE);

	return 0;
}

static int SMAILProcessFile(SVRCFG_HANDLE hSvrConfig, SHB_HANDLE hShbSMAIL,
			    SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	char const *const *ppszRcpt = USmlGetRcptTo(hFSpool);
	int iRcptDomains = StrStringsCount(ppszRcpt);

	char szDestUser[MAX_ADDR_NAME], szDestDomain[MAX_ADDR_NAME];
	char szAliasFilePath[SYS_MAX_PATH];

	if (iRcptDomains < 1) {
		ErrSetErrorCode(ERR_BAD_EMAIL_ADDR);
		return ERR_BAD_EMAIL_ADDR;
	}
	/*
	 * We can have two cases here. The recipient is a simple one, or it has an
	 * explicit routing (@dom1,@dom2:usr@dom).
	 */
	if (iRcptDomains == 1) {
		if (USmtpSplitEmailAddr(ppszRcpt[0], szDestUser, szDestDomain) < 0)
			return ErrGetErrorCode();
	} else {
		char const *pszRtHost = ppszRcpt[0] + 1;

		/*
		 * If we get here, the first entry in ppszRcpt is a route host,
		 * in the form of '@HOST'.
		 */
		if (USmlValidHost(pszRtHost, pszRtHost + strlen(pszRtHost)) < 0)
			return ErrGetErrorCode();
		StrNCpy(szDestDomain, pszRtHost, sizeof(szDestDomain));
	}

	/*
	 * Resolve alias domain alias domain. This needs to be used when
	 * looking for cmdalias handling, in order to avoid replicating
	 * then over aliased domains.
	 */
	char const *pszRealDomain = szDestDomain;
	char szADomain[MAX_HOST_NAME];

	if (ADomLookupDomain(szDestDomain, szADomain, true))
		pszRealDomain = szADomain;

	/* Is the target handled with cmdalias ? */
	if (USmlGetCmdAliasCustomFile(hFSpool, hQueue, hMessage, pszRealDomain,
				      szDestUser, szAliasFilePath) == 0) {
		/* Do cmd alias processing */
		if (SMAILCustomProcessMessage(hSvrConfig, hShbSMAIL, hFSpool, hQueue, hMessage,
					      pszRealDomain, szDestUser, szAliasFilePath) < 0)
			return ErrGetErrorCode();
	} else if (MDomIsHandledDomain(szDestDomain) == 0) {
		UserInfo *pUI = UsrGetUserByNameOrAlias(szDestDomain, szDestUser);

		if (pUI != NULL) {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "SMAIL local SMTP = \"%s\" From = <%s> To = <%s>\n",
				      USmlGetSMTPDomain(hFSpool), USmlMailFrom(hFSpool),
				      ppszRcpt[0]);

			if (UsrGetUserType(pUI) == usrTypeUser) {
				/* Local user case */
				LocalMailProcConfig LMPC;

				ZeroData(LMPC);
				LMPC.ulFlags = SMAILLogEnabled(hShbSMAIL) ? LMPCF_LOG_ENABLED: 0;

				if (USmlProcessLocalUserMessage(hSvrConfig, pUI, hFSpool, hQueue,
								hMessage, LMPC) < 0) {
					ErrorPush();
					UsrFreeUserInfo(pUI);
					return ErrorPop();
				}
			} else {
				/* Apply filters ... */
				if (FilFilterMessage(hFSpool, hQueue, hMessage,
						     FILTER_MODE_INBOUND) < 0) {
					ErrorPush();
					UsrFreeUserInfo(pUI);
					return ErrorPop();
				}
				/* Local mailing list case */
				if (SMAILMailingListExplode(pUI, hFSpool) < 0) {
					ErrorPush();
					UsrFreeUserInfo(pUI);
					return ErrorPop();
				}
			}
			UsrFreeUserInfo(pUI);
		} else {
			ErrorPush();
			/* No account inside the handled domain */
			char szBounceMsg[512];

			SysSNPrintf(szBounceMsg, sizeof(szBounceMsg) - 1,
				    "Unknown user \"%s\" in domain \"%s\"", szDestUser,
				    szDestDomain);

			QueUtErrLogMessage(hQueue, hMessage, "%s\n", szBounceMsg);

			QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool, szBounceMsg, NULL,
						   true);

			return ErrorPop();
		}
	} else {
		/* Remote user case ( or custom domain user ) */
		if (SMAILHandleRemoteUserMessage(hSvrConfig, hShbSMAIL, hFSpool,
						 hQueue, hMessage, szDestDomain, szDestUser) < 0)
			return ErrGetErrorCode();
	}

	return 0;
}

static int SMAILTryProcessMessage(SVRCFG_HANDLE hSvrConfig, QUEUE_HANDLE hQueue,
				  QMSG_HANDLE hMessage, SHB_HANDLE hShbSMAIL,
				  SMAILConfig *pSMAILCfg)
{
	/* Create the handle to manage the queue file */
	char szMessFilePath[SYS_MAX_PATH];

	QueGetFilePath(hQueue, hMessage, szMessFilePath);

	SPLF_HANDLE hFSpool = USmlCreateHandle(szMessFilePath);

	if (hFSpool == INVALID_SPLF_HANDLE) {
		ErrorPush();
		ErrLogMessage(LOG_LEV_ERROR,
			      "Unable to load spool file \"%s\"\n"
			      "%s = \"%s\"\n",
			      szMessFilePath, SMTP_ERROR_VARNAME, "554 Error loading spool file");
		QueUtErrLogMessage(hQueue, hMessage,
				   "Unable to load spool file \"%s\"\n"
				   "%s = \"%s\"\n",
				   szMessFilePath, SMTP_ERROR_VARNAME,
				   "554 Error loading spool file");

		QueUtCleanupNotifyRoot(hQueue, hMessage, INVALID_SPLF_HANDLE,
				       ErrGetErrorString(ErrorFetch()));
		QueCloseMessage(hQueue, hMessage);

		return ErrorPop();
	}
	/* Check for mail loops */
	if (USmlMailLoopCheck(hFSpool, hSvrConfig) < 0) {
		ErrorPush();

		/* Notify root and remove the message */
		char const *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);

		ErrLogMessage(LOG_LEV_ERROR,
			      "Message <%s> blocked by mail loop check !\n"
			      "%s = \"%s\"\n",
			      pszSmtpMessageID, SMTP_ERROR_VARNAME,
			      "554 Message blocked by mail loop check");
		QueUtErrLogMessage(hQueue, hMessage,
				   "Message <%s> blocked by mail loop check !\n"
				   "%s = \"%s\"\n",
				   pszSmtpMessageID, SMTP_ERROR_VARNAME,
				   "554 Message blocked by mail loop check");

		QueUtCleanupNotifyRoot(hQueue, hMessage, hFSpool,
				       ErrGetErrorString(ErrorFetch()));
		USmlCloseHandle(hFSpool);
		QueCloseMessage(hQueue, hMessage);

		return ErrorPop();
	}
	/* Process queue file */
	if (SMAILProcessFile(hSvrConfig, hShbSMAIL, hFSpool, hQueue, hMessage) < 0) {
		ErrorPush();

		/* Resend the message if it's not been cleaned up */
		if (QueCheckMessage(hQueue, hMessage) == 0) {
			USmlSyncChanges(hFSpool);

			/* Handle resend notifications */
			SMAILHandleResendNotify(hSvrConfig, hQueue, hMessage, hFSpool);

			/* Resend the message */
			QueUtResendMessage(hQueue, hMessage, hFSpool);

		} else
			QueCloseMessage(hQueue, hMessage);

		USmlCloseHandle(hFSpool);

		return ErrorPop();
	}
	USmlCloseHandle(hFSpool);

	/* Cleanup message */
	QueCleanupMessage(hQueue, hMessage);
	QueCloseMessage(hQueue, hMessage);

	return 0;
}

static int SMAILTryProcessSpool(SHB_HANDLE hShbSMAIL)
{
	SMAILConfig *pSMAILCfg = SMAILGetConfigCopy(hShbSMAIL);

	if (pSMAILCfg == NULL) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		return ErrorPop();
	}
	/* Get queue file to process */
	QMSG_HANDLE hMessage = QueExtractMessage(hSpoolQueue, SMAIL_WAITMSG_TIMEOUT);

	if (hMessage != INVALID_QMSG_HANDLE) {
		/* Get configuration handle */
		SVRCFG_HANDLE hSvrConfig = SvrGetConfigHandle();

		if (hSvrConfig == INVALID_SVRCFG_HANDLE) {
			ErrorPush();
			ErrLogMessage(LOG_LEV_ERROR,
				      "Unable to load server configuration file\n"
				      "%s = \"%s\"\n", SMTP_ERROR_VARNAME,
				      "417 Unable to load server configuration file");
			QueUtErrLogMessage(hSpoolQueue, hMessage,
					   "Unable to load server configuration file\n"
					   "%s = \"%s\"\n", SMTP_ERROR_VARNAME,
					   "417 Unable to load server configuration file");

			QueUtResendMessage(hSpoolQueue, hMessage, NULL);

			SysFree(pSMAILCfg);
			return ErrorPop();
		}
		/* Process queue file */
		SMAILTryProcessMessage(hSvrConfig, hSpoolQueue, hMessage, hShbSMAIL, pSMAILCfg);

		SvrReleaseConfigHandle(hSvrConfig);
	}
	SysFree(pSMAILCfg);

	return 0;
}

unsigned int SMAILThreadProc(void *pThreadData)
{
	SMAILConfig *pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

	if (pSMAILCfg == NULL) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		return ErrorPop();
	}
	/* Get thread id */
	long lThreadId = pSMAILCfg->lThreadCount;

	/* Increase thread count */
	SMAILThreadCountAdd(+1, hShbSMAIL, pSMAILCfg);

	ShbUnlock(hShbSMAIL);

	SysLogMessage(LOG_LEV_MESSAGE, "SMAIL thread [%02ld] started\n", lThreadId);

	for (;;) {
		/* Check shutdown condition */
		pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

		if (pSMAILCfg == NULL ||
		    pSMAILCfg->ulFlags & SMAILF_STOP_SERVER) {
			SysLogMessage(LOG_LEV_MESSAGE, "SMAIL thread [%02ld] exiting\n",
				      lThreadId);

			if (pSMAILCfg != NULL)
				ShbUnlock(hShbSMAIL);
			break;
		}
		ShbUnlock(hShbSMAIL);

		/* Process spool files */
		SMAILTryProcessSpool(hShbSMAIL);
	}

	/* Decrease thread count */
	SMAILThreadCountAdd(-1, hShbSMAIL);

	SysLogMessage(LOG_LEV_MESSAGE, "SMAIL thread [%02ld] stopped\n", lThreadId);

	return 0;
}

