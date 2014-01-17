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
#include "BuffSock.h"
#include "SSLBind.h"
#include "ResLocks.h"
#include "StrUtils.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MiscUtils.h"
#include "SSLConfig.h"
#include "Base64Enc.h"
#include "MD5.h"
#include "UsrMailList.h"
#include "SMTPSvr.h"
#include "SMTPUtils.h"
#include "MailDomains.h"
#include "AliasDomain.h"
#include "POP3Utils.h"
#include "Filter.h"
#include "MailConfig.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define SMTP_MAX_LINE_SIZE      2048
#define STD_SMTP_TIMEOUT        30000
#define SMTP_IPMAP_FILE         "smtp.ipmap.tab"
#define SMTP_LOG_FILE           "smtp"
#define SMTP_POST_RCPT_FILTER   "post-rcpt"
#define SMTP_PRE_DATA_FILTER    "pre-data"
#define SMTP_POST_DATA_FILTER   "post-data"
#define SMTP_FILTER_REJECT_CODE 3
#define PLAIN_AUTH_PARAM_SIZE   1024
#define LOGIN_AUTH_USERNAME     "Username:"
#define LOGIN_AUTH_PASSWORD     "Password:"
#define SVR_SMTP_AUTH_FILE      "smtpauth.tab"
#define SVR_SMTPAUTH_LINE_MAX   512
#define SVR_SMTP_EXTAUTH_FILE   "smtpextauth.tab"
#define SVR_SMTP_EXTAUTH_LINE_MAX   1024
#define SVR_SMTP_EXTAUTH_TIMEOUT    60000
#define SVR_SMTP_EXTAUTH_SUCCESS    0

#define SMTPF_RELAY_ENABLED     (1 << 0)
#define SMTPF_MAIL_LOCKED       (1 << 1)
#define SMTPF_MAIL_UNLOCKED     (1 << 2)
#define SMTPF_AUTHENTICATED     (1 << 3)
#define SMTPF_VRFY_ENABLED      (1 << 4)
#define SMTPF_MAPPED_IP         (1 << 5)
#define SMTPF_NORDNS_IP         (1 << 6)
#define SMTPF_ETRN_ENABLED      (1 << 7)
#define SMTPF_NOEMIT_AUTH       (1 << 8)
#define SMTPF_WHITE_LISTED      (1 << 9)
#define SMTPF_BLOCKED_IP        (1 << 10)
#define SMTPF_IPMAPPED_IP       (1 << 11)
#define SMTPF_SNDRCHECK_BYPASS  (1 << 12)
#define SMTPF_WANT_TLS          (1 << 13)
#define SMTPF_EASE_TLS          (1 << 14)

#define SMTPF_STATIC_MASK       (SMTPF_MAPPED_IP | SMTPF_NORDNS_IP | SMTPF_WHITE_LISTED | \
				 SMTPF_SNDRCHECK_BYPASS | SMTPF_BLOCKED_IP | SMTPF_IPMAPPED_IP)
#define SMTPF_AUTH_MASK         (SMTPF_RELAY_ENABLED | SMTPF_MAIL_UNLOCKED | SMTPF_AUTHENTICATED | \
				 SMTPF_VRFY_ENABLED | SMTPF_ETRN_ENABLED | SMTPF_EASE_TLS)
#define SMTPF_RESET_MASK        (SMTPF_AUTH_MASK | SMTPF_STATIC_MASK)

#define SMTP_FILTER_FL_BREAK    (1 << 4)
#define SMTP_FILTER_FL_MASK     SMTP_FILTER_FL_BREAK


enum SMTPStates {
	stateInit = 0,
	stateHelo,
	stateAuthenticated,
	stateMail,
	stateRcpt,

	stateExit
};

struct SMTPSession {
	int iSMTPState;
	ThreadConfig const *pThCfg;
	SMTPConfig *pSMTPCfg;
	SVRCFG_HANDLE hSvrConfig;
	SYS_INET_ADDR PeerInfo;
	SYS_INET_ADDR SockInfo;
	int iCmdDelay;
	unsigned long ulMaxMsgSize;
	char szSvrFQDN[MAX_ADDR_NAME];
	char szSvrDomain[MAX_ADDR_NAME];
	char szClientFQDN[MAX_ADDR_NAME];
	char szClientDomain[MAX_ADDR_NAME];
	char szDestDomain[MAX_ADDR_NAME];
	char szLogonUser[128];
	char szMsgFile[SYS_MAX_PATH];
	FILE *pMsgFile;
	char *pszFrom;
	char *pszRcpt;
	char *pszSendRcpt;
	char *pszRealRcpt;
	int iRcptCount;
	int iErrorsCount;
	int iErrorsMax;
	SYS_UINT64 ullMessageID;
	char szMessageID[128];
	char szTimeStamp[256];
	unsigned long ulSetupFlags;
	unsigned long ulFlags;
	char *pszCustMsg;
	char szRejMapName[256];
	char *pszNoTLSAuths;
};

enum SmtpAuthFields {
	smtpaUsername = 0,
	smtpaPassword,
	smtpaPerms,

	smtpaMax
};

struct ExtAuthMacroSubstCtx {
	char const *pszAuthType;
	char const *pszUsername;
	char const *pszPassword;
	char const *pszChallenge;
	char const *pszDigest;
	char const *pszRespFile;
};

static int SMTPLogEnabled(SHB_HANDLE hShbSMTP, SMTPConfig *pSMTPCfg = NULL);
static int SMTPThreadCountAdd(long lCount, SHB_HANDLE hShbSMTP, SMTPConfig *pSMTPCfg = NULL);


static char const *SMTPGetFilterExtname(char const *pszFiltID)
{
	static struct Filter_ID_Name {
		char const *pszFiltID;
		char const *pszName;
	} const FiltIdNames[] = {
		{ SMTP_POST_RCPT_FILTER, "RCPT" },
		{ SMTP_PRE_DATA_FILTER, "PREDATA" },
		{ SMTP_POST_DATA_FILTER, "POSTDATA" },
	};
	int i;

	for (i = 0; i < CountOf(FiltIdNames); i++)
		if (strcmp(FiltIdNames[i].pszFiltID, pszFiltID) == 0)
			return FiltIdNames[i].pszName;

	return "UNKNOWN";
}

static SMTPConfig *SMTPGetConfigCopy(SHB_HANDLE hShbSMTP)
{
	SMTPConfig *pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP);

	if (pSMTPCfg == NULL)
		return NULL;

	SMTPConfig *pSMTPCfgCopy = (SMTPConfig *) SysAlloc(sizeof(SMTPConfig));

	if (pSMTPCfgCopy != NULL)
		memcpy(pSMTPCfgCopy, pSMTPCfg, sizeof(SMTPConfig));

	ShbUnlock(hShbSMTP);

	return pSMTPCfgCopy;
}

static int SMTPLogEnabled(SHB_HANDLE hShbSMTP, SMTPConfig *pSMTPCfg)
{
	int iDoUnlock = 0;

	if (pSMTPCfg == NULL) {
		if ((pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}

	unsigned long ulFlags = pSMTPCfg->ulFlags;

	if (iDoUnlock)
		ShbUnlock(hShbSMTP);

	return (ulFlags & SMTPF_LOG_ENABLED) ? 1 : 0;
}

static int SMTPCheckPeerIP(const SYS_INET_ADDR &PeerAddr)
{
	char szIPMapFile[SYS_MAX_PATH] = "";

	CfgGetRootPath(szIPMapFile, sizeof(szIPMapFile));
	StrNCat(szIPMapFile, SMTP_IPMAP_FILE, sizeof(szIPMapFile));

	if (SysExistFile(szIPMapFile) &&
	    MscCheckAllowedIP(szIPMapFile, PeerAddr, true) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int SMTPThreadCountAdd(long lCount, SHB_HANDLE hShbSMTP, SMTPConfig *pSMTPCfg)
{
	int iDoUnlock = 0;

	if (pSMTPCfg == NULL) {
		if ((pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}
	if ((pSMTPCfg->lThreadCount + lCount) > pSMTPCfg->lMaxThreads) {
		if (iDoUnlock)
			ShbUnlock(hShbSMTP);

		ErrSetErrorCode(ERR_SERVER_BUSY);
		return ERR_SERVER_BUSY;
	}
	pSMTPCfg->lThreadCount += lCount;
	if (iDoUnlock)
		ShbUnlock(hShbSMTP);

	return 0;
}

static int SMTPCheckSysResources(SVRCFG_HANDLE hSvrConfig)
{
	/* Check disk space */
	int iMinValue = SvrGetConfigInt("SmtpMinDiskSpace", -1, hSvrConfig);

	if ((iMinValue > 0) && (SvrCheckDiskSpace(1024 * (unsigned long) iMinValue) < 0))
		return ErrGetErrorCode();

	/* Check virtual memory */
	if (((iMinValue = SvrGetConfigInt("SmtpMinVirtMemSpace", -1, hSvrConfig)) > 0) &&
	    (SvrCheckVirtMemSpace(1024 * (unsigned long) iMinValue) < 0))
		return ErrGetErrorCode();

	return 0;
}

static int SMTPEnumIPPropsCB(void *pPrivate, char const *pszName, char const *pszVal)
{
	SMTPSession *pSMTPS = (SMTPSession *) pPrivate;

	if (strcmp(pszName, "WhiteList") == 0) {
		if (pszVal == NULL || atoi(pszVal))
			pSMTPS->ulFlags |= SMTPF_WHITE_LISTED;
	} else if (strcmp(pszName, "NoAuth") == 0) {
		if (pszVal == NULL || atoi(pszVal))
			pSMTPS->ulFlags |= SMTPF_MAIL_UNLOCKED;
	} else if (strcmp(pszName, "SenderDomainCheck") == 0) {
		if (pszVal != NULL && !atoi(pszVal))
			pSMTPS->ulFlags |= SMTPF_SNDRCHECK_BYPASS;
	} else if (strcmp(pszName, "EaseTLS") == 0) {
		if (pszVal == NULL || atoi(pszVal))
			pSMTPS->ulSetupFlags &= ~SMTPF_WANT_TLS;
	} else if (strcmp(pszName, "EnableVRFY") == 0) {
		if (pszVal == NULL || atoi(pszVal))
			pSMTPS->ulFlags |= SMTPF_VRFY_ENABLED;
	} else if (strcmp(pszName, "EnableETRN") == 0) {
		if (pszVal == NULL || atoi(pszVal))
			pSMTPS->ulFlags |= SMTPF_ETRN_ENABLED;
	}

	return 0;
}

static int SMTPApplyIPProps(SMTPSession &SMTPS)
{
	return SvrEnumProtoProps("smtp", &SMTPS.PeerInfo,
				 IsEmptyString(SMTPS.szClientFQDN) ? NULL: SMTPS.szClientFQDN,
				 SMTPEnumIPPropsCB, &SMTPS);
}

static int SMTPLogSession(SMTPSession &SMTPS, char const *pszSender,
			  char const *pszRecipient, char const *pszStatus,
			  unsigned long ulMsgSize)
{
	char szTime[256] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1);

	RLCK_HANDLE hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR SMTP_LOG_FILE);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	char szIP[128] = "???.???.???.???";

	MscFileLog(SMTP_LOG_FILE, "\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%lu\""
		   "\t\"%s\""
		   "\n", SMTPS.szSvrFQDN, SMTPS.szSvrDomain,
		   SysInetNToA(SMTPS.PeerInfo, szIP, sizeof(szIP)), szTime,
		   SMTPS.szClientDomain, SMTPS.szDestDomain, pszSender, pszRecipient,
		   SMTPS.szMessageID, pszStatus, SMTPS.szLogonUser, ulMsgSize,
		   SMTPS.szClientFQDN);

	RLckUnlockEX(hResLock);

	return 0;
}

static int SMTPCheckMapsList(SYS_INET_ADDR const &PeerInfo, char const *pszMapList,
			     char *pszMapName, int iMaxMapName, int &iMapCode)
{
	for (;;) {
		char const *pszColon = strchr(pszMapList, ':');

		if (pszColon == NULL)
			break;

		int iRetCode = atoi(pszColon + 1);
		int iMapLength = Min((int) (pszColon - pszMapList), MAX_HOST_NAME - 1);
		char szMapName[MAX_HOST_NAME] = "";

		strncpy(szMapName, pszMapList, iMapLength);
		szMapName[iMapLength] = '\0';

		if (USmtpDnsMapsContained(PeerInfo, szMapName)) {
			char szIP[128] = "???.???.???.???";
			char szMapSpec[MAX_HOST_NAME + 128] = "";

			if (pszMapName != NULL)
				StrNCpy(pszMapName, szMapName, iMaxMapName);
			iMapCode = iRetCode;

			SysInetNToA(PeerInfo, szIP, sizeof(szIP));
			SysSNPrintf(szMapSpec, sizeof(szMapSpec) - 1, "%s:%s",
				    szMapName, szIP);

			ErrSetErrorCode(ERR_MAPS_CONTAINED, szMapSpec);
			return ERR_MAPS_CONTAINED;
		}
		if ((pszMapList = strchr(pszColon, ',')) == NULL)
			break;
		++pszMapList;
	}

	return 0;
}

static int SMTPDoIPBasedInit(SMTPSession &SMTPS, char *&pszSMTPError)
{
	int iErrorCode;

	/* Check using the "smtp.ipmap.tab" file */
	if ((iErrorCode = SMTPCheckPeerIP(SMTPS.PeerInfo)) < 0) {
		int iMapCode = SvrGetConfigInt("SMTP-IpMapDropCode", 1, SMTPS.hSvrConfig);

		if (iMapCode > 0) {
			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, "", "", "SNDRIP=EIPBAN", 0);

			pszSMTPError = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpMsgIPBan");

			ErrSetErrorCode(iErrorCode);
			return iErrorCode;
		} else if (iMapCode == 0) {
			SMTPS.ulFlags |= SMTPF_IPMAPPED_IP;
		} else
			SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, Abs(iMapCode));
	}

	/* Check if SMTP client is in "spammers.tab" file */
	char *pszChkInfo = NULL;

	if ((iErrorCode = USmtpSpammerCheck(SMTPS.PeerInfo, pszChkInfo)) < 0) {
		int iMapCode = 1;

		if (pszChkInfo != NULL) {
			char szMapCode[32] = "";

			if (StrParamGet(pszChkInfo, "code", szMapCode, sizeof(szMapCode) - 1))
				iMapCode = atoi(szMapCode);

			SysFree(pszChkInfo);
		}

		if (iMapCode > 0) {
			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, "", "", "SNDRIP=EIPSPAM", 0);

			pszSMTPError = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpMsgIPBanSpammers");

			ErrSetErrorCode(iErrorCode);
			return iErrorCode;
		} else if (iMapCode == 0) {
			SMTPS.ulFlags |= SMTPF_BLOCKED_IP;
		} else
			SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, Abs(iMapCode));
	}

	/* Custom maps checking */
	char *pszMapsList = SvrGetConfigVar(SMTPS.hSvrConfig, "CustMapsList");

	if (pszMapsList != NULL) {
		int iMapCode = 0;
		char *pszCfgError = NULL;

		if (SMTPCheckMapsList(SMTPS.PeerInfo, pszMapsList, SMTPS.szRejMapName,
				      sizeof(SMTPS.szRejMapName) - 1, iMapCode) < 0) {
			if (iMapCode == 1) {
				ErrorPush();

				if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg)) {
					char *pszError =
						StrSprint("SNDRIP=EIPMAP (%s)", SMTPS.szRejMapName);

					SMTPLogSession(SMTPS, "", "",
						       (pszError !=
							NULL) ? pszError : "SNDRIP=EIPMAP", 0);
					SysFree(pszError);
				}
				if ((pszCfgError =
				     SvrGetConfigVar(SMTPS.hSvrConfig,
						     "SmtpMsgIPBanMaps")) != NULL) {
					pszSMTPError =
						StrSprint("%s (%s)", pszCfgError, SMTPS.szRejMapName);
					SysFree(pszCfgError);
				} else
					pszSMTPError =
						StrSprint
						("550 Denied due inclusion of your IP inside (%s)",
						 SMTPS.szRejMapName);
				SysFree(pszMapsList);
				return ErrorPop();
			}
			if (iMapCode == 0)
				SMTPS.ulFlags |= SMTPF_MAPPED_IP;
			else
				SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, Abs(iMapCode));
		}
		SysFree(pszMapsList);
	}
	/* RDNS client check */
	int iCheckValue = SvrGetConfigInt("SMTP-RDNSCheck", 0, SMTPS.hSvrConfig);

	if (iCheckValue != 0 &&
	    SysGetHostByAddr(SMTPS.PeerInfo, SMTPS.szClientFQDN, sizeof(SMTPS.szClientFQDN)) < 0) {
		if (iCheckValue > 0)
			SMTPS.ulFlags |= SMTPF_NORDNS_IP;
		else
			SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, -iCheckValue);
	}

	return 0;
}

static int SMTPSvrCfgOptionsAssign(void *pPrivate, char const *pszName, char const *pszValue)
{
	SMTPSession *pSMTPS = (SMTPSession *) pPrivate;

	if (strcmp(pszName, "mail-auth") == 0 ||
	    strcmp(pszName, "MailAuth") == 0) {
		if (pszValue == NULL || atoi(pszValue))
			pSMTPS->ulSetupFlags |= SMTPF_MAIL_LOCKED;
	} else if (strcmp(pszName, "WantTLS") == 0) {
		if (pszValue == NULL || atoi(pszValue))
			pSMTPS->ulSetupFlags |= SMTPF_WANT_TLS;
	}

	return 0;
}

static int SMTPLoadConfig(SMTPSession &SMTPS, char const *pszSvrConfig)
{
	return MscParseOptions(pszSvrConfig, SMTPSvrCfgOptionsAssign, &SMTPS);
}

static int SMTPApplyPerms(SMTPSession &SMTPS, char const *pszPerms)
{
	for (int i = 0; pszPerms[i] != '\0'; i++) {
		switch (pszPerms[i]) {
		case 'M':
			SMTPS.ulFlags |= SMTPF_MAIL_UNLOCKED;
			break;

		case 'R':
			SMTPS.ulFlags |= SMTPF_RELAY_ENABLED;
			break;

		case 'V':
			SMTPS.ulFlags |= SMTPF_VRFY_ENABLED;
			break;

		case 'T':
			SMTPS.ulFlags |= SMTPF_ETRN_ENABLED;
			break;

		case 'Z':
			SMTPS.ulMaxMsgSize = 0;
			break;

		case 'S':
			SMTPS.ulFlags |= SMTPF_EASE_TLS;
			break;
		}
	}

	/* Clear bad ip mask and command delay */
	SMTPS.ulFlags &= ~(SMTPF_MAPPED_IP | SMTPF_NORDNS_IP | SMTPF_BLOCKED_IP | SMTPF_IPMAPPED_IP);

	SMTPS.iCmdDelay = 0;

	return 0;
}

static int SMTPInitSession(ThreadConfig const *pThCfg, BSOCK_HANDLE hBSock,
			   SMTPSession &SMTPS, char *&pszSMTPError)
{
	ZeroData(SMTPS);
	SMTPS.iSMTPState = stateInit;
	SMTPS.pThCfg = pThCfg;
	SMTPS.hSvrConfig = INVALID_SVRCFG_HANDLE;
	SMTPS.pSMTPCfg = NULL;
	SMTPS.pMsgFile = NULL;
	SMTPS.pszFrom = NULL;
	SMTPS.pszRcpt = NULL;
	SMTPS.pszSendRcpt = NULL;
	SMTPS.pszRealRcpt = NULL;
	SMTPS.iRcptCount = 0;
	SMTPS.iErrorsCount = 0;
	SMTPS.iErrorsMax = 0;
	SMTPS.iCmdDelay = 0;
	SMTPS.ulMaxMsgSize = 0;
	SetEmptyString(SMTPS.szMessageID);
	SetEmptyString(SMTPS.szDestDomain);
	SetEmptyString(SMTPS.szClientFQDN);
	SetEmptyString(SMTPS.szClientDomain);
	SetEmptyString(SMTPS.szLogonUser);
	SetEmptyString(SMTPS.szRejMapName);
	SMTPS.ulFlags = 0;
	SMTPS.ulSetupFlags = 0;
	SMTPS.pszCustMsg = NULL;
	SMTPS.pszNoTLSAuths = NULL;

	MscSafeGetTmpFile(SMTPS.szMsgFile, sizeof(SMTPS.szMsgFile));

	if ((SMTPS.hSvrConfig = SvrGetConfigHandle()) == INVALID_SVRCFG_HANDLE)
		return ErrGetErrorCode();

	if (SMTPCheckSysResources(SMTPS.hSvrConfig) < 0 ||
	    SysGetPeerInfo(BSckGetAttachedSocket(hBSock), SMTPS.PeerInfo) < 0 ||
	    SysGetSockInfo(BSckGetAttachedSocket(hBSock), SMTPS.SockInfo) < 0 ||
	    SMTPApplyIPProps(SMTPS) < 0) {
		ErrorPush();
		SvrReleaseConfigHandle(SMTPS.hSvrConfig);
		return ErrorPop();
	}
	/* If the remote IP is not white-listed, do all IP based checks */
	if ((SMTPS.ulFlags & SMTPF_WHITE_LISTED) == 0 &&
	    SMTPDoIPBasedInit(SMTPS, pszSMTPError) < 0) {
		ErrorPush();
		SvrReleaseConfigHandle(SMTPS.hSvrConfig);
		return ErrorPop();
	}

	/* Get maximum errors count allowed in an SMTP session */
	SMTPS.iErrorsMax = SvrGetConfigInt("SMTP-MaxErrors", 0, SMTPS.hSvrConfig);

	/* Setup SMTP domain */
	char *pszSvrDomain = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpServerDomain");
	char szIP[128] = "???.???.???.???";

	if (pszSvrDomain != NULL) {
		StrSNCpy(SMTPS.szSvrDomain, pszSvrDomain);
		StrSNCpy(SMTPS.szSvrFQDN, pszSvrDomain);
		SysFree(pszSvrDomain);
	} else {
		if (SysGetHostByAddr(SMTPS.SockInfo, SMTPS.szSvrFQDN, sizeof(SMTPS.szSvrFQDN)) < 0)
			StrSNCpy(SMTPS.szSvrFQDN, SysInetNToA(SMTPS.SockInfo, szIP,
							      sizeof(szIP)));
		else {
			/* Try to get a valid domain from the FQDN */
			if (MDomGetClientDomain(SMTPS.szSvrFQDN, SMTPS.szSvrDomain,
						sizeof(SMTPS.szSvrDomain) - 1) < 0)
				StrSNCpy(SMTPS.szSvrDomain, SMTPS.szSvrFQDN);
		}

		/* Last attempt, try fetch the "RootDomain" variable ... */
		if (IsEmptyString(SMTPS.szSvrDomain)) {
			if ((pszSvrDomain = SvrGetConfigVar(SMTPS.hSvrConfig,
							    "RootDomain")) == NULL) {
				SvrReleaseConfigHandle(SMTPS.hSvrConfig);
				ErrSetErrorCode(ERR_NO_DOMAIN);
				return ERR_NO_DOMAIN;
			}
			StrSNCpy(SMTPS.szSvrDomain, pszSvrDomain);
			SysFree(pszSvrDomain);
		}
	}

	/* Create timestamp */
	sprintf(SMTPS.szTimeStamp, "<%lu.%lu@%s>",
		(unsigned long) time(NULL), SysGetCurrentThreadId(), SMTPS.szSvrDomain);

	if ((SMTPS.pSMTPCfg = SMTPGetConfigCopy(hShbSMTP)) == NULL) {
		ErrorPush();
		SvrReleaseConfigHandle(SMTPS.hSvrConfig);
		return ErrorPop();
	}
	/* Get maximum accepted message size */
	SMTPS.ulMaxMsgSize = 1024 * (unsigned long) SvrGetConfigInt("MaxMessageSize",
								    0, SMTPS.hSvrConfig);

	/* Check if the emission of "X-Auth-User:" is diabled */
	if (SvrGetConfigInt("DisableEmitAuthUser", 0, SMTPS.hSvrConfig))
		SMTPS.ulSetupFlags |= SMTPF_NOEMIT_AUTH;

	/* Try to load specific configuration */
	char szConfigName[128] = "";

	SysInetNToA(SMTPS.SockInfo, szIP, sizeof(szIP));
	SysSNPrintf(szConfigName, sizeof(szConfigName), "SmtpConfig-%s,%d",
		    szIP, SysGetAddrPort(SMTPS.SockInfo));

	char *pszSvrConfig = SvrGetConfigVar(SMTPS.hSvrConfig, szConfigName);

	if (pszSvrConfig == NULL) {
		SysSNPrintf(szConfigName, sizeof(szConfigName), "SmtpConfig-%s", szIP);
		if ((pszSvrConfig = SvrGetConfigVar(SMTPS.hSvrConfig, szConfigName)) == NULL)
			pszSvrConfig = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpConfig");
	}
	if (pszSvrConfig != NULL) {
		SMTPLoadConfig(SMTPS, pszSvrConfig);
		SysFree(pszSvrConfig);
	}

	SMTPS.ulFlags |= SMTPS.ulSetupFlags;

	/* Get custom message to append to the SMTP response */
	SMTPS.pszCustMsg = SvrGetConfigVar(SMTPS.hSvrConfig, "CustomSMTPMessage");

	/* Get custom message to append to the SMTP response */
	SMTPS.pszNoTLSAuths = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpNoTLSAuths");

	return 0;
}

static void SMTPClearSession(SMTPSession &SMTPS)
{
	if (SMTPS.pMsgFile != NULL)
		fclose(SMTPS.pMsgFile), SMTPS.pMsgFile = NULL;
	SysRemove(SMTPS.szMsgFile);

	if (SMTPS.hSvrConfig != INVALID_SVRCFG_HANDLE)
		SvrReleaseConfigHandle(SMTPS.hSvrConfig), SMTPS.hSvrConfig =
			INVALID_SVRCFG_HANDLE;

	SysFreeNullify(SMTPS.pSMTPCfg);
	SysFreeNullify(SMTPS.pszFrom);
	SysFreeNullify(SMTPS.pszRcpt);
	SysFreeNullify(SMTPS.pszSendRcpt);
	SysFreeNullify(SMTPS.pszRealRcpt);
	SysFreeNullify(SMTPS.pszCustMsg);
	SysFreeNullify(SMTPS.pszNoTLSAuths);
}

static void SMTPResetSession(SMTPSession &SMTPS)
{
	SMTPS.ulFlags = SMTPS.ulSetupFlags | (SMTPS.ulFlags & SMTPF_RESET_MASK);
	SMTPS.ullMessageID = 0;
	SMTPS.iRcptCount = 0;
	SetEmptyString(SMTPS.szMessageID);

	if (SMTPS.pMsgFile != NULL)
		fclose(SMTPS.pMsgFile), SMTPS.pMsgFile = NULL;
	SysRemove(SMTPS.szMsgFile);

	SetEmptyString(SMTPS.szDestDomain);
	SysFreeNullify(SMTPS.pszFrom);
	SysFreeNullify(SMTPS.pszRcpt);
	SysFreeNullify(SMTPS.pszSendRcpt);
	SysFreeNullify(SMTPS.pszRealRcpt);

	SMTPS.iSMTPState = (SMTPS.ulFlags & SMTPF_AUTHENTICATED) ? stateAuthenticated:
		Min(SMTPS.iSMTPState, stateHelo);
}

static void SMTPFullResetSession(SMTPSession &SMTPS)
{
	SMTPS.ulFlags = 0;
	SMTPS.iSMTPState = stateInit;
	SMTPResetSession(SMTPS);
}

static int SMTPGetUserSmtpPerms(UserInfo *pUI, SVRCFG_HANDLE hSvrConfig, char *pszPerms,
				int iMaxPerms)
{
	char *pszUserPerms = UsrGetUserInfoVar(pUI, "SmtpPerms");

	if (pszUserPerms != NULL) {
		StrNCpy(pszPerms, pszUserPerms, iMaxPerms);
		SysFree(pszUserPerms);
	} else {
		/* Match found, get the default permissions */
		char *pszDefultPerms = SvrGetConfigVar(hSvrConfig,
						       "DefaultSmtpPerms", "MRVZ");

		if (pszDefultPerms != NULL) {
			StrNCpy(pszPerms, pszDefultPerms, iMaxPerms);
			SysFree(pszDefultPerms);
		} else
			SetEmptyString(pszPerms);
	}

	return 0;
}

static int SMTPApplyUserConfig(SMTPSession &SMTPS, UserInfo *pUI)
{
	/* Retrieve and apply permissions */
	char *pszValue = NULL;
	char szPerms[128] = "";

	if (SMTPGetUserSmtpPerms(pUI, SMTPS.hSvrConfig, szPerms, sizeof(szPerms) - 1) < 0)
		return ErrGetErrorCode();

	SMTPApplyPerms(SMTPS, szPerms);

	/* Check "MaxMessageSize" override */
	if ((pszValue = UsrGetUserInfoVar(pUI, "MaxMessageSize")) != NULL) {
		SMTPS.ulMaxMsgSize = 1024 * (unsigned long) atol(pszValue);

		SysFree(pszValue);
	}
	/* Check if the emission of "X-Auth-User:" is diabled */
	if ((pszValue = UsrGetUserInfoVar(pUI, "DisableEmitAuthUser")) != NULL) {
		if (atoi(pszValue))
			SMTPS.ulFlags |= SMTPF_NOEMIT_AUTH;
		else
			SMTPS.ulFlags &= ~SMTPF_NOEMIT_AUTH;
		SysFree(pszValue);
	}

	return 0;
}

static int SMTPSendError(BSOCK_HANDLE hBSock, SMTPSession &SMTPS, char const *pszFormat, ...)
{
	char *pszBuffer = NULL;

	StrVSprint(pszBuffer, pszFormat, pszFormat);

	if (pszBuffer == NULL)
		return ErrGetErrorCode();

	if (SMTPS.pszCustMsg == NULL) {
		if (BSckSendString(hBSock, pszBuffer, SMTPS.pSMTPCfg->iTimeout) < 0) {
			ErrorPush();
			SysFree(pszBuffer);
			return ErrorPop();
		}
	} else {
		if (BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
				    "%s - %s", pszBuffer, SMTPS.pszCustMsg) < 0) {
			ErrorPush();
			SysFree(pszBuffer);
			return ErrorPop();
		}
	}
	SysFree(pszBuffer);

	/*
	 * Increase the number of errors we encountered in this session. If the
	 * maximum number of allowed errors is not zero, and the current number of
	 * errors exceed the maximum number, we set the state to 'exit' so that the
	 * SMTP connection will be dropped.
	 */
	SMTPS.iErrorsCount++;
	if (SMTPS.iErrorsMax > 0 && SMTPS.iErrorsCount >= SMTPS.iErrorsMax) {
		char szIP[128] = "";

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom != NULL ? SMTPS.pszFrom: "",
				       "", "SMTP=EERRS", 0);

		SysLogMessage(LOG_LEV_MESSAGE, "SMTP forced exit (too many errors: %d) [%s]\n",
			      SMTPS.iErrorsCount, SysInetNToA(SMTPS.PeerInfo, szIP, sizeof(szIP)));

		SMTPS.iSMTPState = stateExit;
	}

	return 0;
}

static int SMTPTryPopAuthIpCheck(SMTPSession &SMTPS, char const *pszUser, char const *pszDomain)
{
	/* Load user info */
	UserInfo *pUI = UsrGetUserByNameOrAlias(pszDomain, pszUser);

	if (pUI == NULL)
		return ErrGetErrorCode();

	/* Perform IP file checking */
	if (UPopUserIpCheck(pUI, &SMTPS.PeerInfo, SMTPS.pSMTPCfg->uPopAuthExpireTime) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}
	/* Apply user configuration */
	if (SMTPApplyUserConfig(SMTPS, pUI) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}
	/* If the user did not authenticate, set the logon user token */
	if (IsEmptyString(SMTPS.szLogonUser))
		UsrGetAddress(pUI, SMTPS.szLogonUser);

	UsrFreeUserInfo(pUI);

	return 0;
}

static int SMTPCheckMailParams(char const *pszCommand, char **ppszRetDomains,
			       SMTPSession &SMTPS, char *&pszSMTPError)
{
	char const *pszParams = strrchr(pszCommand, '>');

	if (pszParams == NULL)
		pszParams = pszCommand;

	/* Check the SIZE parameter */
	if (SMTPS.ulMaxMsgSize != 0) {
		char const *pszSize = pszParams;

		while ((pszSize = StrIStr(pszSize, " SIZE=")) != NULL) {
			pszSize += CStringSize(" SIZE=");

			if (isdigit(*pszSize) &&
			    SMTPS.ulMaxMsgSize < (unsigned long) atol(pszSize)) {
				if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
					SMTPLogSession(SMTPS,
						       (ppszRetDomains[0] !=
							NULL) ? ppszRetDomains[0] : "", "",
						       "SIZE=EBIG",
						       (unsigned long) atol(pszSize));

				pszSMTPError =
					SysStrDup("552 Message exceeds fixed maximum message size");

				ErrSetErrorCode(ERR_MESSAGE_SIZE);
				return ERR_MESSAGE_SIZE;
			}
		}
	}

	return 0;
}

static int SMTPCheckReturnPath(char const *pszCommand, char **ppszRetDomains,
			       SMTPSession &SMTPS, char *&pszSMTPError)
{
	int iDomainCount;
	char szMailerUser[MAX_ADDR_NAME] = "";
	char szMailerDomain[MAX_ADDR_NAME] = "";

	if ((iDomainCount = StrStringsCount(ppszRetDomains)) == 0) {
		if (!SvrTestConfigFlag("AllowNullSender", true, SMTPS.hSvrConfig)) {
			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, "", "", "SNDR=EEMPTY", 0);

			pszSMTPError = SysStrDup("501 Syntax error in return path");

			ErrSetErrorCode(ERR_BAD_RETURN_PATH);
			return ERR_BAD_RETURN_PATH;
		}
		SysFree(SMTPS.pszFrom);

		SMTPS.pszFrom = SysStrDup("");

		return 0;
	}
	if (USmtpSplitEmailAddr(ppszRetDomains[0], szMailerUser, szMailerDomain) < 0) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, ppszRetDomains[0], "", "SNDR=ESYNTAX", 0);

		pszSMTPError = SysStrDup("501 Syntax error in return path");

		return ErrorPop();
	}
	/* Check mailer domain for DNS/MX entries */
	if (!(SMTPS.ulFlags & SMTPF_SNDRCHECK_BYPASS) &&
	    SvrTestConfigFlag("CheckMailerDomain", false, SMTPS.hSvrConfig) &&
	    USmtpCheckMailDomain(SMTPS.hSvrConfig, szMailerDomain) < 0) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, ppszRetDomains[0], "", "SNDR=ENODNS", 0);

		pszSMTPError = SysStrDup("505 Your domain has no DNS/MX entries");

		return ErrorPop();
	}
	/* Check if mail come from a spammer address ( spam-address.tab ) */
	if (USmtpSpamAddressCheck(ppszRetDomains[0]) < 0) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, ppszRetDomains[0], "", "SNDR=ESPAM", 0);

		if ((pszSMTPError =
		     SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpMsgIPBanSpamAddress")) == NULL)
			pszSMTPError = SysStrDup("504 You are registered as spammer");

		return ErrorPop();
	}
	/* Check SMTP after POP3 authentication */
	if (SvrTestConfigFlag("EnableAuthSMTP-POP3", true, SMTPS.hSvrConfig))
		SMTPTryPopAuthIpCheck(SMTPS, szMailerUser, szMailerDomain);

	/* Check extended mail from parameters */
	if (SMTPCheckMailParams(pszCommand, ppszRetDomains, SMTPS, pszSMTPError) < 0)
		return ErrGetErrorCode();

	/* Setup From string */
	SysFree(SMTPS.pszFrom);
	SMTPS.pszFrom = SysStrDup(ppszRetDomains[0]);

	return 0;
}

static int SMTPAddMessageInfo(SMTPSession &SMTPS)
{
	return USmtpAddMessageInfo(SMTPS.pMsgFile, SMTPS.szClientDomain, SMTPS.PeerInfo,
				   SMTPS.szSvrDomain, SMTPS.SockInfo, SMTP_SERVER_NAME);
}

static int SMTPHandleCmd_MAIL(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	if (SMTPS.iSMTPState != stateHelo && SMTPS.iSMTPState != stateAuthenticated) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "503 Bad sequence of commands");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}
	/* Do we need to be in TLS mode for this session? */
	if ((SMTPS.ulFlags & SMTPF_WANT_TLS) && !(SMTPS.ulFlags & SMTPF_EASE_TLS) &&
	    strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) != 0) {

		SMTPSendError(hBSock, SMTPS,
			      "503 Bad sequence of commands (TLS needed for this session)");

		ErrSetErrorCode(ERR_TLS_MODE_REQUIRED);
		return ERR_TLS_MODE_REQUIRED;
	}

	/* Split the RETURN PATH */
	char **ppszRetDomains = USmtpGetPathStrings(pszCommand);

	if (ppszRetDomains == NULL) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "501 Syntax error in parameters or arguments: (%d)",
			      ErrorFetch());
		return ErrorPop();
	}
	/* Check RETURN PATH */
	char *pszSMTPError = NULL;

	if (SMTPCheckReturnPath(pszCommand, ppszRetDomains, SMTPS, pszSMTPError) < 0) {
		ErrorPush();
		StrFreeStrings(ppszRetDomains);
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "%s", pszSMTPError);
		SysFree(pszSMTPError);
		return ErrorPop();
	}
	StrFreeStrings(ppszRetDomains);

	/* If the incoming IP is "mapped" stop here */
	if (SMTPS.ulFlags & (SMTPF_MAPPED_IP | SMTPF_NORDNS_IP | SMTPF_BLOCKED_IP)) {
		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg)) {
			if (SMTPS.ulFlags & SMTPF_MAPPED_IP) {
				char *pszError =
					StrSprint("SNDRIP=EIPMAP (%s)", SMTPS.szRejMapName);

				SMTPLogSession(SMTPS, SMTPS.pszFrom, "",
					       (pszError != NULL) ? pszError : "SNDRIP=EIPMAP",
					       0);

				SysFree(pszError);
			} else if (SMTPS.ulFlags & SMTPF_NORDNS_IP) {
				SMTPLogSession(SMTPS, SMTPS.pszFrom, "", "SNDRIP=ERDNS", 0);
			} else
				SMTPLogSession(SMTPS, SMTPS.pszFrom, "", "SNDRIP=EIPSPAM", 0);
		}
		if (SMTPS.ulFlags & SMTPF_BLOCKED_IP)
			pszSMTPError = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpMsgIPBanSpammers");

		SMTPResetSession(SMTPS);

		if (pszSMTPError == NULL)
			SMTPSendError(hBSock, SMTPS, "551 Server access forbidden by your IP");
		else {
			SMTPSendError(hBSock, SMTPS, "%s", pszSMTPError);
			SysFree(pszSMTPError);
		}

		ErrSetErrorCode(ERR_SMTP_USE_FORBIDDEN);
		return ERR_SMTP_USE_FORBIDDEN;
	}
	/* If MAIL command is locked stop here */
	if ((SMTPS.ulFlags & SMTPF_MAIL_LOCKED) && !(SMTPS.ulFlags & SMTPF_MAIL_UNLOCKED)) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "551 Server use forbidden");

		ErrSetErrorCode(ERR_SMTP_USE_FORBIDDEN);
		return ERR_SMTP_USE_FORBIDDEN;
	}
	/* Prepare mail file */
	if ((SMTPS.pMsgFile = fopen(SMTPS.szMsgFile, "w+b")) == NULL) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ERR_FILE_CREATE);

		ErrSetErrorCode(ERR_FILE_CREATE, SMTPS.szMsgFile);
		return ERR_FILE_CREATE;
	}
	/* Write message infos ( 1st row of the smtp-mail file ) */
	if (SMTPAddMessageInfo(SMTPS) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());
		return ErrorPop();
	}
	/* Write domain ( 2nd row of the smtp-mail file ) */
	if (StrWriteCRLFString(SMTPS.pMsgFile, SMTPS.szSvrDomain) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());
		return ErrorPop();
	}
	/* Get SMTP message ID and write it to file ( 3th row of the smtp-mail file ) */
	if (SvrGetMessageID(&SMTPS.ullMessageID) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());
		return ErrorPop();
	}

	sprintf(SMTPS.szMessageID, "S" SYS_LLX_FMT, SMTPS.ullMessageID);

	if (StrWriteCRLFString(SMTPS.pMsgFile, SMTPS.szMessageID) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());
		return ErrorPop();
	}
	/* Write MAIL FROM ( 4th row of the smtp-mail file ) */
	if (StrWriteCRLFString(SMTPS.pMsgFile, pszCommand) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());
		return ErrorPop();
	}

	BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

	SMTPS.iSMTPState = stateMail;

	return 0;
}

static int SMTPCheckRelayCapability(SMTPSession &SMTPS, char const *pszDestDomain)
{
	/* OK if enabled ( authentication ) */
	if (SMTPS.ulFlags & SMTPF_RELAY_ENABLED)
		return 0;

	/* OK if destination domain is a custom-handled domain */
	if (USmlCustomizedDomain(pszDestDomain) == 0)
		return 0;

	/* IP based relay check */
	return USmtpIsAllowedRelay(SMTPS.PeerInfo, SMTPS.hSvrConfig);
}

static int SMTPGetFilterFile(char const *pszFiltID, char *pszFileName, int iMaxName)
{
	char szMailRoot[SYS_MAX_PATH] = "";

	CfgGetRootPath(szMailRoot, sizeof(szMailRoot));
	SysSNPrintf(pszFileName, iMaxName - 1, "%sfilters.%s.tab", szMailRoot, pszFiltID);

	return SysExistFile(pszFileName);
}

static char *SMTPMacroLkupProc(void *pPrivate, char const *pszName, int iSize)
{
	SMTPSession *pSMTPS = (SMTPSession *) pPrivate;

	if (MemMatch(pszName, iSize, "FROM", 4)) {

		return SysStrDup(pSMTPS->pszFrom != NULL ? pSMTPS->pszFrom: "-");
	} else if (MemMatch(pszName, iSize, "CRCPT", 5)) {

		return SysStrDup(pSMTPS->pszRcpt != NULL ? pSMTPS->pszRcpt: "-");
	} else if (MemMatch(pszName, iSize, "RRCPT", 5)) {

		return SysStrDup(pSMTPS->pszRealRcpt != NULL ?
				 pSMTPS->pszRealRcpt: pSMTPS->pszRcpt != NULL ?
				 pSMTPS->pszRcpt: "-");
	} else if (MemMatch(pszName, iSize, "FILE", 4)) {

		return SysStrDup(pSMTPS->szMsgFile);
	} else if (MemMatch(pszName, iSize, "LOCALADDR", 9)) {
		char szAddr[128] = "";

		MscGetAddrString(pSMTPS->SockInfo, szAddr, sizeof(szAddr) - 1);
		return SysStrDup(szAddr);
	} else if (MemMatch(pszName, iSize, "REMOTEADDR", 10)) {
		char szAddr[128] = "";

		MscGetAddrString(pSMTPS->PeerInfo, szAddr, sizeof(szAddr) - 1);
		return SysStrDup(szAddr);
	} else if (MemMatch(pszName, iSize, "MSGREF", 6)) {

		return SysStrDup(pSMTPS->szMessageID);
	} else if (MemMatch(pszName, iSize, "USERAUTH", 8)) {

		return SysStrDup(IsEmptyString(pSMTPS->szLogonUser) ?
				 "-": pSMTPS->szLogonUser);
	}

	return SysStrDup("");
}

static int SMTPFilterMacroSubstitutes(char **ppszCmdTokens, SMTPSession &SMTPS)
{
	return MscReplaceTokens(ppszCmdTokens, SMTPMacroLkupProc, &SMTPS);
}

static char *SMTPGetFilterRejMessage(char const *pszMsgFilePath)
{
	FILE *pFile;
	char szRejFilePath[SYS_MAX_PATH] = "";
	char szRejMsg[512] = "";

	SysSNPrintf(szRejFilePath, sizeof(szRejFilePath) - 1, "%s.rej", pszMsgFilePath);
	if ((pFile = fopen(szRejFilePath, "rb")) == NULL)
		return NULL;

	MscFGets(szRejMsg, sizeof(szRejMsg) - 1, pFile);

	fclose(pFile);
	SysRemove(szRejFilePath);

	return SysStrDup(szRejMsg);
}

static int SMTPLogFilter(SMTPSession &SMTPS, char const *const *ppszExec, int iExecResult,
			 int iExitCode, char const *pszType, char const *pszInfo)
{
	FilterLogInfo FLI;

	ZeroData(FLI);
	FLI.pszSender = SMTPS.pszFrom != NULL ? SMTPS.pszFrom: "";
	/*
	 * In case we have multiple recipients, it is misleading to log only the
	 * latest (or current) one, so we log "*" instead. But for post-RCPT
	 * filters, it makes sense to log the current recipient for each filter
	 * execution.
	 */
	if (SMTPS.pszRcpt != NULL)
		FLI.pszRecipient = (SMTPS.iRcptCount == 1 ||
				    strcmp(pszType, SMTP_POST_RCPT_FILTER) == 0) ? SMTPS.pszRcpt: "*";
	else
		FLI.pszRecipient = "";
	FLI.LocalAddr = SMTPS.SockInfo;
	FLI.RemoteAddr = SMTPS.PeerInfo;
	FLI.ppszExec = ppszExec;
	FLI.iExecResult = iExecResult;
	FLI.iExitCode = iExitCode;
	FLI.pszType = pszType;
	FLI.pszInfo = pszInfo != NULL ? pszInfo: "";

	return FilLogFilter(&FLI);
}

static int SMTPPreFilterExec(SMTPSession &SMTPS, FilterExecCtx *pFCtx,
			     FilterTokens *pToks, char **ppszPEError)
{
	pFCtx->pToks = pToks;
	pFCtx->pszAuthName = SMTPS.szLogonUser;
	pFCtx->ulFlags = (SMTPS.ulFlags & SMTPF_WHITE_LISTED) ? FILTER_XFL_WHITELISTED: 0;
	pFCtx->iTimeout = iFilterTimeout;

	return FilExecPreParse(pFCtx, ppszPEError);
}

static int SMTPRunFilters(SMTPSession &SMTPS, char const *pszFilterPath, char const *pszType,
			  char *&pszError)
{
	/* Share lock the filter file */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(pszFilterPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* This should not happen but if it happens we let the message pass through */
	FILE *pFiltFile = fopen(pszFilterPath, "rt");

	if (pFiltFile == NULL) {
		RLckUnlockSH(hResLock);
		return 0;
	}

	/* Filter this message */
	int iFieldsCount, iExitCode, iExitFlags, iExecResult;
	char szFiltLine[1024] = "";

	while (MscGetConfigLine(szFiltLine, sizeof(szFiltLine) - 1, pFiltFile) != NULL) {
		char **ppszCmdTokens = StrGetTabLineStrings(szFiltLine);

		if (ppszCmdTokens == NULL)
			continue;

		if ((iFieldsCount = StrStringsCount(ppszCmdTokens)) < 1) {
			StrFreeStrings(ppszCmdTokens);
			continue;
		}

		/* Perform pre-exec filtering (like exec exclude if authenticated, ...) */
		char *pszPEError = NULL;
		FilterExecCtx FCtx;
		FilterTokens Toks;

		ZeroData(FCtx);
		Toks.ppszCmdTokens = ppszCmdTokens;
		Toks.iTokenCount = iFieldsCount;

		if (SMTPPreFilterExec(SMTPS, &FCtx, &Toks, &pszPEError) < 0) {
			if (bFilterLogEnabled)
				SMTPLogFilter(SMTPS, Toks.ppszCmdTokens, -1,
					      -1, pszType, pszPEError);
			SysFree(pszPEError);
			StrFreeStrings(ppszCmdTokens);
			continue;
		}

		/* Do filter line macro substitution */
		SMTPFilterMacroSubstitutes(Toks.ppszCmdTokens, SMTPS);

		iExitCode = -1;
		iExitFlags = 0;
		iExecResult = SysExec(Toks.ppszCmdTokens[0], &Toks.ppszCmdTokens[0],
				      FCtx.iTimeout, SYS_PRIORITY_NORMAL, &iExitCode);

		/* Log filter execution, if enabled */
		if (bFilterLogEnabled)
			SMTPLogFilter(SMTPS, Toks.ppszCmdTokens, iExecResult,
				      iExitCode, pszType, NULL);

		if (iExecResult == 0) {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "SMTP filter run: Filter = \"%s\" Retcode = %d\n",
				      Toks.ppszCmdTokens[0], iExitCode);

			iExitFlags = iExitCode & SMTP_FILTER_FL_MASK;
			iExitCode &= ~SMTP_FILTER_FL_MASK;

			if (iExitCode == SMTP_FILTER_REJECT_CODE) {
				StrFreeStrings(ppszCmdTokens);
				fclose(pFiltFile);
				RLckUnlockSH(hResLock);

				char szLogLine[128];

				SysSNPrintf(szLogLine, sizeof(szLogLine) - 1,
					    "%s=EFILTER", SMTPGetFilterExtname(pszType));

				if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
					SMTPLogSession(SMTPS, SMTPS.pszFrom, SMTPS.pszRcpt,
						       szLogLine, 0);

				pszError = SMTPGetFilterRejMessage(SMTPS.szMsgFile);

				ErrSetErrorCode(ERR_FILTERED_MESSAGE);
				return ERR_FILTERED_MESSAGE;
			}
		} else {
			SysLogMessage(LOG_LEV_ERROR,
				      "SMTP filter error (%d): Filter = \"%s\"\n",
				      iExecResult, Toks.ppszCmdTokens[0]);
		}
		StrFreeStrings(ppszCmdTokens);

		if (iExitFlags & SMTP_FILTER_FL_BREAK)
			break;
	}
	fclose(pFiltFile);
	RLckUnlockSH(hResLock);

	return 0;
}

static int SMTPFilterMessage(SMTPSession &SMTPS, char const *pszFiltID, char *&pszError)
{
	int iReOpen = 0;
	char szFilterFile[SYS_MAX_PATH] = "";

	if (!SMTPGetFilterFile(pszFiltID, szFilterFile, sizeof(szFilterFile) - 1))
		return 0;

	if (SMTPS.pMsgFile != NULL) {
		if (fclose(SMTPS.pMsgFile)) {
			SMTPS.pMsgFile = NULL;
			ErrSetErrorCode(ERR_FILE_WRITE, SMTPS.szMsgFile);
			return ERR_FILE_WRITE;
		}
		SMTPS.pMsgFile = NULL;
		iReOpen++;
	}

	int iError = SMTPRunFilters(SMTPS, szFilterFile, pszFiltID, pszError);

	if (iReOpen) {
		if ((SMTPS.pMsgFile = fopen(SMTPS.szMsgFile, "r+b")) == NULL) {
			ErrSetErrorCode(ERR_FILE_OPEN, SMTPS.szMsgFile);

			/*
			 * We failed to re-open our own file. Cannot proceed w/out
			 * a session reset!
			 */
			SMTPResetSession(SMTPS);
			return ERR_FILE_OPEN;
		}
		fseek(SMTPS.pMsgFile, 0, SEEK_END);
	}

	return iError;
}

static int SMTPCheckForwardPath(char **ppszFwdDomains, SMTPSession &SMTPS,
				char *&pszSMTPError)
{
	int iDomainCount;
	char szDestUser[MAX_ADDR_NAME] = "";
	char szDestDomain[MAX_ADDR_NAME] = "";
	char szRealUser[MAX_ADDR_NAME] = "";

	if ((iDomainCount = StrStringsCount(ppszFwdDomains)) == 0) {
		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom, "", "RCPT=ESYNTAX", 0);

		pszSMTPError = SysStrDup("501 Syntax error in forward path");

		ErrSetErrorCode(ERR_BAD_FORWARD_PATH);
		return ERR_BAD_FORWARD_PATH;
	}
	if (USmtpSplitEmailAddr(ppszFwdDomains[iDomainCount - 1], szDestUser,
				szDestDomain) < 0) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ESYNTAX",
				       0);

		pszSMTPError = SysStrDup("501 Syntax error in forward path");

		return ErrorPop();
	}
	if (iDomainCount == 1) {
		/*
		 * Resolve alias domain alias domain. This needs to be used when
		 * looking for cmdalias handling, in order to avoid replicating
		 * then over aliased domains.
		 */
		char const *pszRealDomain = szDestDomain;
		char szADomain[MAX_HOST_NAME] = "";

		if (ADomLookupDomain(szDestDomain, szADomain, true))
			pszRealDomain = szADomain;

		if (USmlIsCmdAliasAccount(pszRealDomain, szDestUser) == 0) {
			/* The recipient is handled with cmdaliases */

		} else if (MDomIsHandledDomain(szDestDomain) == 0) {
			/* Check user existance */
			UserInfo *pUI = UsrGetUserByNameOrAlias(szDestDomain, szDestUser);

			if (pUI != NULL) {
				/* Check if the account is enabled for receiving */
				if (!UsrGetUserInfoVarInt(pUI, "ReceiveEnable", 1)) {
					UsrFreeUserInfo(pUI);

					if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
						SMTPLogSession(SMTPS, SMTPS.pszFrom,
							       ppszFwdDomains[0], "RCPT=EDSBL",
							       0);

					pszSMTPError = StrSprint("550 Account disabled <%s@%s>",
								 szDestUser, szDestDomain);

					ErrSetErrorCode(ERR_USER_DISABLED);
					return ERR_USER_DISABLED;
				}

				if (UsrGetUserType(pUI) == usrTypeUser) {
					/* Target is a normal user */

					/* Check user mailbox size */
					if (UPopCheckMailboxSize(pUI) < 0) {
						ErrorPush();
						UsrFreeUserInfo(pUI);

						if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
							SMTPLogSession(SMTPS, SMTPS.pszFrom,
								       ppszFwdDomains[0],
								       "RCPT=EFULL", 0);

						pszSMTPError = StrSprint("552 Requested mail action aborted: exceeded storage allocation - <%s@%s>",
									 szDestUser, szDestDomain);

						return ErrorPop();
					}
				} else {
					/* Target is a mailing list */

					/* Check if client can post to this mailing list */
					if (UsrMLCheckUserPost(pUI, SMTPS.pszFrom,
							       IsEmptyString(SMTPS.
									     szLogonUser) ? NULL :
							       SMTPS.szLogonUser) < 0) {
						ErrorPush();
						UsrFreeUserInfo(pUI);

						if (SMTPLogEnabled
						    (SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
							SMTPLogSession(SMTPS, SMTPS.pszFrom,
								       ppszFwdDomains[0],
								       "RCPT=EACCESS", 0);

						pszSMTPError = StrSprint("557 Access denied <%s@%s> for user <%s>",
									 szDestUser, szDestDomain, SMTPS.pszFrom);

						return ErrorPop();
					}
				}
				/* Extract the real user address */
				UsrGetAddress(pUI, szRealUser);

				UsrFreeUserInfo(pUI);
			} else {
				/* Recipient domain is local but no account is found inside the standard */
				/* users/aliases database and the account is not handled with cmdaliases. */
				/* It's pretty much time to report a recipient error */
				if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
					SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0],
						       "RCPT=EAVAIL", 0);

				pszSMTPError = StrSprint("550 Mailbox unavailable <%s@%s>",
							 szDestUser, szDestDomain);

				ErrSetErrorCode(ERR_USER_NOT_LOCAL);
				return ERR_USER_NOT_LOCAL;
			}
		} else {
			/* Check relay permission */
			if (SMTPCheckRelayCapability(SMTPS, szDestDomain) < 0) {
				ErrorPush();

				if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
					SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0],
						       "RCPT=ERELAY", 0);

				pszSMTPError = SysStrDup("550 Relay denied");

				return ErrorPop();
			}
		}
	} else {
		/* Check relay permission */
		if (SMTPCheckRelayCapability(SMTPS, szDestDomain) < 0) {
			ErrorPush();

			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0],
					       "RCPT=ERELAY", 0);

			pszSMTPError = SysStrDup("550 Relay denied");

			return ErrorPop();
		}
	}

	/* Retrieve destination domain */
	if (USmtpSplitEmailAddr(ppszFwdDomains[0], NULL, SMTPS.szDestDomain) < 0) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ESYNTAX",
				       0);

		pszSMTPError = SysStrDup("501 Syntax error in forward path");

		return ErrorPop();
	}
	/*
	 * Setup SendRcpt string (it'll be used to build "RCPT TO:<>" line into
	 * the message file)
	 */
	SysFree(SMTPS.pszSendRcpt);

	if ((SMTPS.pszSendRcpt = USmtpBuildRcptPath(ppszFwdDomains, SMTPS.hSvrConfig)) == NULL) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ESYNTAX",
				       0);

		pszSMTPError = SysStrDup("501 Syntax error in forward path");

		return ErrorPop();
	}
	/*
	 * Setup Rcpt string. This needs to be done before filter execution,
	 * since the CRCPT macro substitution needs that information.
	 */
	SysFree(SMTPS.pszRcpt);
	SMTPS.pszRcpt = SysStrDup(ppszFwdDomains[0]);

	/* Setup the Real Rcpt string */
	if (!IsEmptyString(szRealUser))
		SMTPS.pszRealRcpt = SysStrDup(szRealUser);

	/*
	 * Call the post-rcpt filter.
	 */
	pszSMTPError = NULL;
	if (SMTPFilterMessage(SMTPS, SMTP_POST_RCPT_FILTER, pszSMTPError) < 0) {
		ErrorPush();
		if (pszSMTPError == NULL)
			pszSMTPError = SysStrDup("550 Recipient rejected");
		return ErrorPop();
	}

	return 0;
}

static char **SMTPGetForwardPath(char const *pszCommand, SMTPSession &SMTPS)
{
	int iCount;
	char **ppszFwdDomains = USmtpGetPathStrings(pszCommand);

	if (ppszFwdDomains != NULL && (iCount = StrStringsCount(ppszFwdDomains)) > 0) {
		char *pszRename = NULL;

		if (stricmp(ppszFwdDomains[iCount - 1], "postmaster") == 0)
			pszRename = SvrGetConfigVar(SMTPS.hSvrConfig, "PostMaster");

		if (pszRename != NULL) {
			SysFree(ppszFwdDomains[iCount - 1]);
			ppszFwdDomains[iCount - 1] = pszRename;
		}
	}

	return ppszFwdDomains;
}

static int SMTPHandleCmd_RCPT(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	if ((SMTPS.iSMTPState != stateMail) && (SMTPS.iSMTPState != stateRcpt)) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "503 Bad sequence of commands");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}
	/* Check recipients count */
	if (SMTPS.iRcptCount >= SMTPS.pSMTPCfg->iMaxRcpts) {
		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom, "", "RCPT=ENBR", 0);

		SMTPSendError(hBSock, SMTPS, "552 Too many recipients");

		ErrSetErrorCode(ERR_SMTP_TOO_MANY_RECIPIENTS);
		return ERR_SMTP_TOO_MANY_RECIPIENTS;
	}

	char **ppszFwdDomains = SMTPGetForwardPath(pszCommand, SMTPS);

	if (ppszFwdDomains == NULL) {
		ErrorPush();

		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, SMTPS.pszFrom, "", "RCPT=ESYNTAX", 0);

		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "501 Syntax error in parameters or arguments: (%d)",
			      ErrorFetch());
		return ErrorPop();
	}
	/* Check FORWARD PATH */
	char *pszSMTPError = NULL;

	if (SMTPCheckForwardPath(ppszFwdDomains, SMTPS, pszSMTPError) < 0) {
		ErrorPush();
		StrFreeStrings(ppszFwdDomains);

		SMTPSendError(hBSock, SMTPS, "%s", pszSMTPError);
		SysFree(pszSMTPError);
		return ErrorPop();
	}
	StrFreeStrings(ppszFwdDomains);

	/* Log SMTP session */
	if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
		SMTPLogSession(SMTPS, SMTPS.pszFrom, SMTPS.pszRcpt, "RCPT=OK", 0);

	/* Write RCPT TO ( 5th[,...] row(s) of the smtp-mail file ) */
	fprintf(SMTPS.pMsgFile, "RCPT TO:<%s> {ra=%s}\r\n", SMTPS.pszSendRcpt,
		(SMTPS.pszRealRcpt != NULL) ? SMTPS.pszRealRcpt: SMTPS.pszSendRcpt);

	BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

	++SMTPS.iRcptCount;

	SMTPS.iSMTPState = stateRcpt;

	return 0;
}

static int SMTPAddReceived(int iType, char const *pszAuth, char const *const *ppszMsgInfo,
			   char const *pszMailFrom, char const *pszRcptTo,
			   char const *pszMessageID, FILE *pMailFile)
{
	char *pszReceived = USmtpGetReceived(iType, pszAuth, ppszMsgInfo, pszMailFrom, pszRcptTo,
					     pszMessageID);

	if (pszReceived == NULL)
		return ErrGetErrorCode();

	/* Write "Received:" tag */
	fputs(pszReceived, pMailFile);

	SysFree(pszReceived);

	return 0;
}

static char *SMTPTrimRcptLine(char *pszRcptLn)
{
	char *pszTrim = strchr(pszRcptLn, '>');

	if (pszTrim != NULL)
		pszTrim[1] = '\0';

	return pszRcptLn;
}

static int SMTPSubmitPackedFile(SMTPSession &SMTPS, char const *pszPkgFile)
{
	FILE *pPkgFile = fopen(pszPkgFile, "rb");

	if (pPkgFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN);
		return ERR_FILE_OPEN;
	}

	char szSpoolLine[MAX_SPOOL_LINE] = "";

	while (MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL &&
	       strncmp(szSpoolLine, SPOOL_FILE_DATA_START,
		       CStringSize(SPOOL_FILE_DATA_START)) != 0);

	if (strncmp(szSpoolLine, SPOOL_FILE_DATA_START, CStringSize(SPOOL_FILE_DATA_START)) != 0) {
		fclose(pPkgFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Get the offset at which the message data begin and rewind the file */
	SYS_OFF_T llMsgOffset = Sys_ftell(pPkgFile);

	rewind(pPkgFile);

	/* Read SMTP message info ( 1st row of the smtp-mail file ) */
	char **ppszMsgInfo = NULL;

	if (MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL ||
	    (ppszMsgInfo = StrTokenize(szSpoolLine, ";")) == NULL ||
	    StrStringsCount(ppszMsgInfo) < smsgiMax) {
		if (ppszMsgInfo != NULL)
			StrFreeStrings(ppszMsgInfo);
		fclose(pPkgFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read SMTP domain ( 2nd row of the smtp-mail file ) */
	char szSMTPDomain[256] = "";

	if (MscGetString(pPkgFile, szSMTPDomain, sizeof(szSMTPDomain) - 1) == NULL) {
		StrFreeStrings(ppszMsgInfo);
		fclose(pPkgFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read message ID ( 3th row of the smtp-mail file ) */
	char szMessageID[128] = "";

	if (MscGetString(pPkgFile, szMessageID, sizeof(szMessageID) - 1) == NULL) {
		StrFreeStrings(ppszMsgInfo);
		fclose(pPkgFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read "MAIL FROM:" ( 4th row of the smtp-mail file ) */
	char szMailFrom[MAX_SPOOL_LINE] = "";

	if (MscGetString(pPkgFile, szMailFrom, sizeof(szMailFrom) - 1) == NULL ||
	    StrINComp(szMailFrom, MAIL_FROM_STR) != 0) {
		StrFreeStrings(ppszMsgInfo);
		fclose(pPkgFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Get the Received: header type to emit */
	int iReceivedType = SvrGetConfigInt("ReceivedHdrType", RECEIVED_TYPE_STD,
					    SMTPS.hSvrConfig);

	/* Read "RCPT TO:" ( 5th[,...] row(s) of the smtp-mail file ) */
	while (MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL &&
	       StrINComp(szSpoolLine, RCPT_TO_STR) == 0) {
		/* Cleanup the RCPT line from extra info */
		SMTPTrimRcptLine(szSpoolLine);

		/* Get message handle */
		QMSG_HANDLE hMessage = QueCreateMessage(hSpoolQueue);

		if (hMessage == INVALID_QMSG_HANDLE) {
			ErrorPush();
			StrFreeStrings(ppszMsgInfo);
			fclose(pPkgFile);
			return ErrorPop();
		}

		char szQueueFilePath[SYS_MAX_PATH] = "";

		QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

		FILE *pSpoolFile = fopen(szQueueFilePath, "wb");

		if (pSpoolFile == NULL) {
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszMsgInfo);
			fclose(pPkgFile);
			ErrSetErrorCode(ERR_FILE_CREATE);
			return ERR_FILE_CREATE;
		}
		/* Write info line */
		USmtpWriteInfoLine(pSpoolFile, ppszMsgInfo[smsgiClientAddr],
				   ppszMsgInfo[smsgiServerAddr], ppszMsgInfo[smsgiTime]);

		/* Write SMTP domain */
		fprintf(pSpoolFile, "%s\r\n", szSMTPDomain);

		/* Write message ID */
		fprintf(pSpoolFile, "%s\r\n", szMessageID);

		/* Write "MAIL FROM:" */
		fprintf(pSpoolFile, "%s\r\n", szMailFrom);

		/* Write "RCPT TO:" */
		fprintf(pSpoolFile, "%s\r\n", szSpoolLine);

		/* Write SPOOL_FILE_DATA_START */
		fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

		/* Write "X-AuthUser:" tag */
		if (!IsEmptyString(SMTPS.szLogonUser) &&
		    !(SMTPS.ulFlags & SMTPF_NOEMIT_AUTH))
			fprintf(pSpoolFile, "X-AuthUser: %s\r\n", SMTPS.szLogonUser);

		/* Write "Received:" tag */
		SMTPAddReceived(iReceivedType,
				IsEmptyString(SMTPS.szLogonUser) ? NULL: SMTPS.szLogonUser,
				ppszMsgInfo, szMailFrom, szSpoolLine, szMessageID,
				pSpoolFile);

		/* Write mail data, saving and restoring the current file pointer */
		SYS_OFF_T llCurrOffset = Sys_ftell(pPkgFile);

		if (MscCopyFile(pSpoolFile, pPkgFile, llMsgOffset, (SYS_OFF_T) -1) < 0) {
			ErrorPush();
			fclose(pSpoolFile);
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszMsgInfo);
			fclose(pPkgFile);
			return ErrorPop();
		}
		if (SysFileSync(pSpoolFile) < 0) {
			ErrorPush();
			fclose(pSpoolFile);
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszMsgInfo);
			fclose(pPkgFile);
			return ErrorPop();
		}
		if (fclose(pSpoolFile)) {
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszMsgInfo);
			fclose(pPkgFile);
			ErrSetErrorCode(ERR_FILE_WRITE, szQueueFilePath);
			return ERR_FILE_WRITE;
		}
		Sys_fseek(pPkgFile, llCurrOffset, SEEK_SET);

		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszMsgInfo);
			fclose(pPkgFile);
			return ErrorPop();
		}
	}
	StrFreeStrings(ppszMsgInfo);
	fclose(pPkgFile);

	return 0;
}

static int SMTPHandleCmd_DATA(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	char *pszError;

	if (SMTPS.iSMTPState != stateRcpt) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "503 Bad sequence of commands");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}

	/* Run the pre-DATA filter */
	pszError = NULL;
	if (SMTPFilterMessage(SMTPS, SMTP_PRE_DATA_FILTER, pszError) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		if (pszError != NULL) {
			SMTPSendError(hBSock, SMTPS, "%s", pszError);
			SysFree(pszError);
		} else
			BSckSendString(hBSock, "554 Transaction failed",
				       SMTPS.pSMTPCfg->iTimeout);
		return ErrorPop();
	}

	/* Write data begin marker */
	if (StrWriteCRLFString(SMTPS.pMsgFile, SPOOL_FILE_DATA_START) < 0) {
		ErrorPush();
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());
		return ErrorPop();
	}

	BSckSendString(hBSock, "354 Start mail input; end with <CRLF>.<CRLF>",
		       SMTPS.pSMTPCfg->iTimeout);

	/* Write data */
	int iError = 0, iLineLength, iGotNL, iGotNLPrev = 1;
	unsigned long ulMessageSize = 0;
	unsigned long ulMaxMsgSize = SMTPS.ulMaxMsgSize;
	char const *pszSmtpError = NULL;
	char szBuffer[SMTP_MAX_LINE_SIZE + 4];

	for (;;) {
		if (BSckGetString(hBSock, szBuffer, sizeof(szBuffer) - 3,
				  SMTPS.pSMTPCfg->iTimeout, &iLineLength, &iGotNL) == NULL) {
			/*
			 * At this point, either we got a timeout or the remote
			 * peer dropped the connection. We can safely set the
			 * exist status here because, in the first case the link
			 * will be out of sync, and in the second case the link
			 * is dead anyway.
			 */
			SMTPS.iSMTPState = stateExit;

			return ErrGetErrorCode();
		}
		/* Check end of data condition */
		if (iGotNL && iGotNLPrev && (strcmp(szBuffer, ".") == 0))
			break;

		/* Correctly terminate the line */
		if (iGotNL)
			memcpy(szBuffer + iLineLength, "\r\n", 3), iLineLength += 2;

		if (iError == 0) {
			/* Write data on disk */
			if (!fwrite(szBuffer, iLineLength, 1, SMTPS.pMsgFile)) {
				ErrSetErrorCode(iError = ERR_FILE_WRITE, SMTPS.szMsgFile);

			}
		}
		ulMessageSize += (unsigned long) iLineLength;

		/* Check the message size */
		if ((ulMaxMsgSize != 0) && (ulMaxMsgSize < ulMessageSize)) {
			pszSmtpError = "552 Message exceeds fixed maximum message size";

			ErrSetErrorCode(iError = ERR_MESSAGE_SIZE);
		}
		if (SvrInShutdown()) {
			SMTPResetSession(SMTPS);

			ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
			return ERR_SERVER_SHUTDOWN;
		}
		iGotNLPrev = iGotNL;
	}

	/* Check fclose() return value coz data might be buffered and fail to flush */
	if (fclose(SMTPS.pMsgFile))
		ErrSetErrorCode(iError = ERR_FILE_WRITE, SMTPS.szMsgFile);
	SMTPS.pMsgFile = NULL;

	if (iError == 0) {
		/* Run the post-DATA filter */
		pszError = NULL;
		if (SMTPFilterMessage(SMTPS, SMTP_POST_DATA_FILTER, pszError) < 0) {
			ErrorPush();
			SMTPResetSession(SMTPS);

			if (pszError != NULL) {
				SMTPSendError(hBSock, SMTPS, "%s", pszError);
				SysFree(pszError);
			} else
				BSckSendString(hBSock, "554 Transaction failed",
					       SMTPS.pSMTPCfg->iTimeout);
			return ErrorPop();
		}

		/* Transfer spool file */
		if ((iError = SMTPSubmitPackedFile(SMTPS, SMTPS.szMsgFile)) < 0) {
			SMTPResetSession(SMTPS);

			SMTPSendError(hBSock, SMTPS,
				      "451 Requested action aborted: (%d) local error in processing",
				      ErrGetErrorCode());
		} else {
			/* Log the message receive */
			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, SMTPS.pszFrom, SMTPS.pszRcpt, "RECV=OK",
					       ulMessageSize);

			/* Send the ack only when everything is OK */
			BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "250 OK <%s>",
					SMTPS.szMessageID);

			SMTPResetSession(SMTPS);
		}
	} else {
		SMTPResetSession(SMTPS);

		/* Notify the client the error condition */
		if (pszSmtpError == NULL)
			SMTPSendError(hBSock, SMTPS,
				      "451 Requested action aborted: (%d) local error in processing",
				      ErrGetErrorCode());
		else
			SMTPSendError(hBSock, SMTPS, "%s", pszSmtpError);
	}

	return iError;
}

static int SMTPHandleCmd_HELO(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	if (SMTPS.iSMTPState != stateInit && SMTPS.iSMTPState != stateHelo) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "503 Bad sequence of commands");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}

	char **ppszTokens = StrTokenize(pszCommand, " ");

	if (ppszTokens == NULL || StrStringsCount(ppszTokens) != 2) {
		if (ppszTokens != NULL)
			StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");
		return -1;
	}
	StrSNCpy(SMTPS.szClientDomain, ppszTokens[1]);
	StrFreeStrings(ppszTokens);

	char *pszDomain = SvrGetConfigVar(SMTPS.hSvrConfig, "RootDomain");

	if (pszDomain == NULL) {
		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ERR_NO_ROOT_DOMAIN_VAR);

		ErrSetErrorCode(ERR_NO_ROOT_DOMAIN_VAR);
		return ERR_NO_ROOT_DOMAIN_VAR;
	}

	BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "250 %s", pszDomain);

	SysFree(pszDomain);

	SMTPS.iSMTPState = stateHelo;

	return 0;
}

static int SMTPAddAuth(char **ppszAuths, int iNumAuths, int *piAuthCnt, char const *pszAuth,
		       char const *pszNoTLSAuths, int iLinkSSL)
{
	if (*piAuthCnt < iNumAuths &&
	    (iLinkSSL || pszNoTLSAuths == NULL ||
	     StrLimIStr(pszNoTLSAuths, pszAuth, ",") != NULL)) {
		if ((ppszAuths[*piAuthCnt] = SysStrDup(pszAuth)) != NULL)
			(*piAuthCnt)++;
	}

	return *piAuthCnt;
}

static char *SMTPGetAuthFilePath(char *pszFilePath, int iMaxPath)
{
	CfgGetRootPath(pszFilePath, iMaxPath);

	StrNCat(pszFilePath, SVR_SMTP_AUTH_FILE, iMaxPath);

	return pszFilePath;
}

static char *SMTPGetExtAuthFilePath(char *pszFilePath, int iMaxPath)
{
	CfgGetRootPath(pszFilePath, iMaxPath);

	StrNCat(pszFilePath, SVR_SMTP_EXTAUTH_FILE, iMaxPath);

	return pszFilePath;
}

static int SMTPListAuths(DynString *pDS, SMTPSession &SMTPS, int iLinkSSL)
{
	char szExtAuthFilePath[SYS_MAX_PATH] = "";

	SMTPGetExtAuthFilePath(szExtAuthFilePath, sizeof(szExtAuthFilePath));

	int iExtAuthCnt = 0, iNumAuths = 0;
	char *pszAuths[16];
	FILE *pExtAuthFile = fopen(szExtAuthFilePath, "rt");

	if (pExtAuthFile != NULL) {
		char szExtAuthLine[SVR_SMTP_EXTAUTH_LINE_MAX] = "";

		while (MscGetConfigLine(szExtAuthLine, sizeof(szExtAuthLine) - 1,
					pExtAuthFile) != NULL) {
			char **ppszStrings = StrGetTabLineStrings(szExtAuthLine);

			if (ppszStrings == NULL)
				continue;
			if (StrStringsCount(ppszStrings) > 1) {
				SMTPAddAuth(pszAuths, CountOf(pszAuths), &iNumAuths,
					    ppszStrings[0], SMTPS.pszNoTLSAuths,
					    iLinkSSL);
				iExtAuthCnt++;
			}
			StrFreeStrings(ppszStrings);
		}
		fclose(pExtAuthFile);
	}
	/*
	 * The logic is this: If the user has declared an external authentication
	 * source, we need to let him declare which AUTH methods are supported by
	 * his external source. For example, the CRAM-MD5 authentication method
	 * requires the password to be known to the external authentication binary,
	 * and many authentication APIs do not support the clear text password to
	 * be exported. So in those case the user must not advertise CRAM-MD5.
	 * If no externally handled authentications are declared, we advertise the
	 * internally handled ones.
	 */
	if (iExtAuthCnt == 0) {
		SMTPAddAuth(pszAuths, CountOf(pszAuths), &iNumAuths,
			    "LOGIN", SMTPS.pszNoTLSAuths, iLinkSSL);
		SMTPAddAuth(pszAuths, CountOf(pszAuths), &iNumAuths,
			    "PLAIN", SMTPS.pszNoTLSAuths, iLinkSSL);
		SMTPAddAuth(pszAuths, CountOf(pszAuths), &iNumAuths,
			    "CRAM-MD5", SMTPS.pszNoTLSAuths, iLinkSSL);
	}
	if (iNumAuths > 0) {
		int i;

		StrDynAdd(pDS, "250 AUTH");
		for (i = 0; i < iNumAuths; i++) {
			StrDynAdd(pDS, " ");
			StrDynAdd(pDS, pszAuths[i]);
			SysFree(pszAuths[i]);
		}
		StrDynAdd(pDS, "\r\n");
	}

	return 0;
}

static int SMTPSendMultilineResponse(BSOCK_HANDLE hBSock, int iTimeout, char const *pszResp)
{
	int iError;
	char *pszDResp, *pszPtr, *pszPrev, *pszTmp;

	if ((pszDResp = SysStrDup(pszResp)) == NULL)
		return ErrGetErrorCode();
	for (pszPrev = pszPtr = pszDResp; (pszTmp = strchr(pszPtr, '\n')) != NULL;
	     pszPrev = pszPtr, pszPtr = pszTmp + 1)
		pszPtr[3] = '-';
	pszPrev[3] = ' ';

	iError = BSckSendData(hBSock, pszDResp, strlen(pszDResp), iTimeout);

	SysFree(pszDResp);

	return iError;
}

static int SMTPHandleCmd_EHLO(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	if (SMTPS.iSMTPState != stateInit && SMTPS.iSMTPState != stateHelo) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "503 Bad sequence of commands");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}

	char **ppszTokens = StrTokenize(pszCommand, " ");

	if (ppszTokens == NULL || StrStringsCount(ppszTokens) != 2) {
		if (ppszTokens != NULL)
			StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");
		return -1;
	}
	StrSNCpy(SMTPS.szClientDomain, ppszTokens[1]);
	StrFreeStrings(ppszTokens);

	/* Create response */
	DynString DynS;

	if (StrDynInit(&DynS) < 0) {
		ErrorPush();

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());

		return ErrorPop();
	}
	/* Get root domain */
	char *pszDomain = SvrGetConfigVar(SMTPS.hSvrConfig, "RootDomain");

	if (pszDomain == NULL) {
		StrDynFree(&DynS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ERR_NO_ROOT_DOMAIN_VAR);

		ErrSetErrorCode(ERR_NO_ROOT_DOMAIN_VAR);
		return ERR_NO_ROOT_DOMAIN_VAR;
	}
	/* Build EHLO response file ( domain + auths ) */
	StrDynPrint(&DynS, "250 %s\r\n", pszDomain);
	SysFree(pszDomain);

	/* Emit extended SMTP command and internal auths */
	int iLinkSSL = strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) == 0;

	StrDynAdd(&DynS,
		  "250 VRFY\r\n"
		  "250 ETRN\r\n"
		  "250 8BITMIME\r\n"
		  "250 PIPELINING\r\n");

	/* Emit external authentication methods */
	SMTPListAuths(&DynS, SMTPS, iLinkSSL);

	/* Emit maximum message size ( if set ) */
	if (SMTPS.ulMaxMsgSize != 0)
		StrDynPrint(&DynS, "250 SIZE %lu\r\n", SMTPS.ulMaxMsgSize);
	else
		StrDynAdd(&DynS, "250 SIZE\r\n");
	if (!iLinkSSL && SvrTestConfigFlag("EnableSMTP-TLS", true, SMTPS.hSvrConfig))
		StrDynAdd(&DynS, "250 STARTTLS\r\n");

	/* Send EHLO response file */
	if (SMTPSendMultilineResponse(hBSock, SMTPS.pSMTPCfg->iTimeout,
				      StrDynGet(&DynS)) < 0) {
		ErrorPush();
		StrDynFree(&DynS);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());

		return ErrorPop();
	}
	StrDynFree(&DynS);

	SMTPS.iSMTPState = stateHelo;

	return 0;
}

static int SMTPSslEnvCB(void *pPrivate, int iID, void const *pData)
{
	SMTPSession *pSMTPS = (SMTPSession *) pPrivate;

	/*
	 * Empty for now ...
	 */


	return 0;
}

static int SMTPHandleCmd_STARTTLS(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{

	if (strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) == 0) {
		/*
		 * Client is trying to run another STARTTLS after a successful one.
		 * Not possible ...
		 */
		SMTPSendError(hBSock, SMTPS, "454 TLS not available due to temporary reason");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}
	if (!SvrTestConfigFlag("EnableSMTP-TLS", true, SMTPS.hSvrConfig)) {
		SMTPSendError(hBSock, SMTPS, "454 TLS not available");

		ErrSetErrorCode(ERR_SSL_DISABLED);
		return ERR_SSL_DISABLED;
	}

	int iError;
	SslServerBind SSLB;

	if (CSslBindSetup(&SSLB) < 0) {
		ErrorPush();
		SMTPSendError(hBSock, SMTPS, "454 TLS not available due to temporary reason");
		return ErrorPop();
	}

	BSckSendString(hBSock, "220 Ready to start TLS", SMTPS.pSMTPCfg->iTimeout);

	iError = BSslBindServer(hBSock, &SSLB, SMTPSslEnvCB, &SMTPS);

	CSslBindCleanup(&SSLB);
	if (iError < 0) {
		/*
		 * At this point, we have no other option than terminating the connection.
		 * We already sent to the client the ACK to start the SSL negotiation,
		 * and quitting here would likely leave garbage data on the link.
		 * Log and quit ...
		 */
		char szIP[128] = "";

		ErrorPush();
		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, "", "", "SMTP=ESSL", 0);
		SysLogMessage(LOG_LEV_MESSAGE, "SMTP failed to STARTTLS (%d) [%s]\n",
			      iError, SysInetNToA(SMTPS.PeerInfo, szIP, sizeof(szIP)));
		SMTPS.iSMTPState = stateExit;

		return ErrorPop();
	}
	/*
	 * STARTTLS requires the session to be fully reset (RFC3207).
	 */
	SMTPFullResetSession(SMTPS);

	return 0;
}

static char *SMTPExtAuthMacroLkupProc(void *pPrivate, char const *pszName, int iSize)
{
	ExtAuthMacroSubstCtx *pASX = (ExtAuthMacroSubstCtx *) pPrivate;

	if (MemMatch(pszName, iSize, "USER", 4)) {

		return SysStrDup(pASX->pszUsername != NULL ? pASX->pszUsername: "-");
	} else if (MemMatch(pszName, iSize, "PASS", 4)) {

		return SysStrDup(pASX->pszPassword != NULL ? pASX->pszPassword: "-");
	} else if (MemMatch(pszName, iSize, "CHALL", 5)) {

		return SysStrDup(pASX->pszChallenge != NULL ? pASX->pszChallenge: "-");
	} else if (MemMatch(pszName, iSize, "DGEST", 5)) {

		return SysStrDup(pASX->pszDigest != NULL ? pASX->pszDigest: "-");
	} else if (MemMatch(pszName, iSize, "RFILE", 5)) {

		return SysStrDup(pASX->pszRespFile);
	} else if (MemMatch(pszName, iSize, "AUTH", 4)) {

		return SysStrDup(pASX->pszAuthType);
	}

	return SysStrDup("");
}

static int SMTPExternalAuthSubstitute(char **ppszAuthTokens, char const *pszAuthType,
				      char const *pszUsername, char const *pszPassword,
				      char const *pszChallenge, char const *pszDigest,
				      char const *pszRespFile)
{
	ExtAuthMacroSubstCtx ASX;

	ZeroData(ASX);
	ASX.pszAuthType = pszAuthType;
	ASX.pszUsername = pszUsername;
	ASX.pszPassword = pszPassword;
	ASX.pszChallenge = pszChallenge;
	ASX.pszDigest = pszDigest;
	ASX.pszRespFile = pszRespFile;

	return MscReplaceTokens(ppszAuthTokens, SMTPExtAuthMacroLkupProc, &ASX);
}

static int SMTPAssignExtPerms(void *pPrivate, char const *pszName, char const *pszVal)
{
	SMTPSession *pSMTPS = (SMTPSession *) pPrivate;

	if (strcmp(pszName, "Perms") == 0) {
		SMTPApplyPerms(*pSMTPS, pszVal);
	}

	return 0;
}

static int SMTPExternalAuthenticate(BSOCK_HANDLE hBSock, SMTPSession &SMTPS,
				    char **ppszAuthTokens, char const *pszAuthType,
				    char const *pszUsername, char const *pszPassword,
				    char const *pszChallenge, char const *pszDigest)
{
	char szRespFile[SYS_MAX_PATH] = "";

	MscSafeGetTmpFile(szRespFile, sizeof(szRespFile));

	/* Do macro substitution */
	SMTPExternalAuthSubstitute(ppszAuthTokens, pszAuthType, pszUsername, pszPassword,
				   pszChallenge, pszDigest, szRespFile);

	/* Call external program to compute the response */
	int iExitCode = -1;

	if (SysExec(ppszAuthTokens[1], &ppszAuthTokens[1], SVR_SMTP_EXTAUTH_TIMEOUT,
		    SYS_PRIORITY_NORMAL, &iExitCode) < 0) {
		ErrorPush();
		SysRemove(szRespFile);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());

		return ErrorPop();
	}
	if (iExitCode != SVR_SMTP_EXTAUTH_SUCCESS) {
		SMTPSendError(hBSock, SMTPS, "503 Authentication failed");

		ErrSetErrorCode(ERR_BAD_EXTRNPRG_EXITCODE);
		return ERR_BAD_EXTRNPRG_EXITCODE;
	}

	/*
	 * Load externally supplied credentials ...
	 */
	unsigned long ulFileSize;
	void *pRData = MscLoadFile(szRespFile, &ulFileSize);

	if (pRData != NULL) {
		SysRemove(szRespFile);
		/*
		 * The MscLoadFile() function zero-terminate the loaded content,
		 * so we can safely call MscParseOptions() since the "pRData" will
		 * be a C-string.
		 */
		MscParseOptions((char *) pRData, SMTPAssignExtPerms, &SMTPS);
		SysFree(pRData);
	}

	return 0;
}

static char **SMTPGetAuthExternal(SMTPSession &SMTPS, char const *pszAuthType)
{
	char szExtAuthFilePath[SYS_MAX_PATH] = "";

	SMTPGetExtAuthFilePath(szExtAuthFilePath, sizeof(szExtAuthFilePath));

	FILE *pExtAuthFile = fopen(szExtAuthFilePath, "rt");

	if (pExtAuthFile == NULL)
		return NULL;

	char szExtAuthLine[SVR_SMTP_EXTAUTH_LINE_MAX] = "";

	while (MscGetConfigLine(szExtAuthLine, sizeof(szExtAuthLine) - 1,
				pExtAuthFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szExtAuthLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount > 1 && stricmp(ppszStrings[0], pszAuthType) == 0) {
			fclose(pExtAuthFile);

			return ppszStrings;
		}
		StrFreeStrings(ppszStrings);
	}
	fclose(pExtAuthFile);

	return NULL;
}

static int SMTPTryExtAuth(BSOCK_HANDLE hBSock, SMTPSession &SMTPS, char const *pszAuthType,
			  char const *pszUsername, char const *pszPassword, char const *pszChallenge,
			  char const *pszDigest)
{
	char **ppszAuthTokens;

	if ((ppszAuthTokens = SMTPGetAuthExternal(SMTPS, pszAuthType)) == NULL)
		return 0;

	int iAuthResult = SMTPExternalAuthenticate(hBSock, SMTPS, ppszAuthTokens, pszAuthType,
						   pszUsername, pszPassword, pszChallenge, pszDigest);

	StrFreeStrings(ppszAuthTokens);

	return iAuthResult < 0 ? iAuthResult: 1;
}

static int SMTPTryApplyLocalAuth(SMTPSession &SMTPS, char const *pszUsername,
				 char const *pszPassword)
{
	/* First try to lookup  mailusers.tab */
	char szAccountUser[MAX_ADDR_NAME] = "";
	char szAccountDomain[MAX_HOST_NAME] = "";

	if (StrSplitString(pszUsername, POP3_USER_SPLITTERS, szAccountUser, sizeof(szAccountUser),
			   szAccountDomain, sizeof(szAccountDomain)) < 0)
		return ErrGetErrorCode();

	UserInfo *pUI = UsrGetUserByName(szAccountDomain, szAccountUser);

	if (pUI != NULL) {
		if (strcmp(pUI->pszPassword, pszPassword) == 0) {
			/* Apply user configuration */
			if (SMTPApplyUserConfig(SMTPS, pUI) < 0) {
				ErrorPush();
				UsrFreeUserInfo(pUI);
				return ErrorPop();
			}
			UsrFreeUserInfo(pUI);

			return 0;
		}
		UsrFreeUserInfo(pUI);
	}

	ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
	return ERR_SMTP_AUTH_FAILED;
}

static int SMTPTryApplyUsrPwdAuth(SMTPSession &SMTPS, char const *pszUsername,
				  char const *pszPassword)
{
	char szAuthFilePath[SYS_MAX_PATH] = "";

	SMTPGetAuthFilePath(szAuthFilePath, sizeof(szAuthFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szAuthFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pAuthFile = fopen(szAuthFilePath, "rt");

	if (pAuthFile == NULL) {
		RLckUnlockSH(hResLock);
		ErrSetErrorCode(ERR_FILE_OPEN, szAuthFilePath);
		return ERR_FILE_OPEN;
	}

	char szAuthLine[SVR_SMTPAUTH_LINE_MAX] = "";

	while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAuthLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= smtpaMax &&
		    strcmp(ppszStrings[smtpaUsername], pszUsername) == 0 &&
		    strcmp(ppszStrings[smtpaPassword], pszPassword) == 0) {
			/* Apply user perms to SMTP config */
			SMTPApplyPerms(SMTPS, ppszStrings[smtpaPerms]);

			StrFreeStrings(ppszStrings);
			fclose(pAuthFile);
			RLckUnlockSH(hResLock);

			return 0;
		}
		StrFreeStrings(ppszStrings);
	}
	fclose(pAuthFile);

	RLckUnlockSH(hResLock);

	ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
	return ERR_SMTP_AUTH_FAILED;
}

static int SMTPDoAuthPlain(BSOCK_HANDLE hBSock, SMTPSession &SMTPS, char const *pszAuthParam)
{
	if (pszAuthParam == NULL || IsEmptyString(pszAuthParam)) {
		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");

		ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
		return ERR_BAD_SMTP_CMD_SYNTAX;
	}

	int iDec64Length;
	char szClientAuth[PLAIN_AUTH_PARAM_SIZE] = "";

	ZeroData(szClientAuth);
	iDec64Length = sizeof(szClientAuth);
	if (Base64Decode(pszAuthParam, strlen(pszAuthParam), szClientAuth, &iDec64Length) != 0) {
		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");

		ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
		return ERR_BAD_SMTP_CMD_SYNTAX;
	}

	/* Extract plain auth params (unused + 0 + username + 0 + password) */
	int iError;
	char *pszUsername = szClientAuth + strlen(szClientAuth) + 1;
	char *pszPassword = pszUsername + strlen(pszUsername) + 1;

	/* Validate client response */
	if ((iError = SMTPTryExtAuth(hBSock, SMTPS, "PLAIN", pszUsername,
				     pszPassword, NULL, NULL)) < 0)
		return ErrGetErrorCode();
	else if (iError == 0) {
		if (SMTPTryApplyLocalAuth(SMTPS, pszUsername, pszPassword) < 0 &&
		    SMTPTryApplyUsrPwdAuth(SMTPS, pszUsername, pszPassword) < 0) {
			ErrorPush();

			SMTPSendError(hBSock, SMTPS, "503 Authentication failed");

			return ErrorPop();
		}
	}
	/* Set the logon user */
	StrSNCpy(SMTPS.szLogonUser, pszUsername);

	SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
	SMTPS.iSMTPState = stateAuthenticated;

	BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

	return 0;
}

static int SMTPDoAuthLogin(BSOCK_HANDLE hBSock, SMTPSession &SMTPS, char const *pszAuthParam)
{
	/* Emit encoded64 username request */
	int iEnc64Length;
	char szUsername[512] = "";

	if (pszAuthParam == NULL || IsEmptyString(pszAuthParam)) {
		iEnc64Length = sizeof(szUsername) - 1;
		Base64Encode(LOGIN_AUTH_USERNAME, strlen(LOGIN_AUTH_USERNAME),
			     szUsername, &iEnc64Length);
		if (BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s",
				    szUsername) < 0 ||
		    BSckGetString(hBSock, szUsername, sizeof(szUsername) - 1,
				  SMTPS.pSMTPCfg->iTimeout) == NULL)
			return ErrGetErrorCode();

		pszAuthParam = szUsername;
	}

	/* Emit encoded64 password request */
	char szPassword[512] = "";

	iEnc64Length = sizeof(szPassword) - 1;
	Base64Encode(LOGIN_AUTH_PASSWORD, strlen(LOGIN_AUTH_PASSWORD), szPassword, &iEnc64Length);

	if (BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s", szPassword) < 0 ||
	    BSckGetString(hBSock, szPassword, sizeof(szPassword) - 1,
			  SMTPS.pSMTPCfg->iTimeout) == NULL)
		return ErrGetErrorCode();

	/* Decode (base64) username */
	int iDec64Length;
	char szDecodeBuffer[512] = "";

	iDec64Length = sizeof(szDecodeBuffer);
	if (Base64Decode(pszAuthParam, strlen(pszAuthParam), szDecodeBuffer,
			 &iDec64Length) != 0) {
		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");

		ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
		return ERR_BAD_SMTP_CMD_SYNTAX;
	}
	StrSNCpy(szUsername, szDecodeBuffer);

	/* Decode (base64) password */
	iDec64Length = sizeof(szDecodeBuffer);
	if (Base64Decode(szPassword, strlen(szPassword), szDecodeBuffer, &iDec64Length) != 0) {
		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");

		ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
		return ERR_BAD_SMTP_CMD_SYNTAX;
	}
	StrSNCpy(szPassword, szDecodeBuffer);

	/* Validate client response */
	int iError;

	if ((iError = SMTPTryExtAuth(hBSock, SMTPS, "LOGIN", szUsername,
				     szPassword, NULL, NULL)) < 0)
		return ErrGetErrorCode();
	else if (iError == 0) {
		if (SMTPTryApplyLocalAuth(SMTPS, szUsername, szPassword) < 0 &&
		    SMTPTryApplyUsrPwdAuth(SMTPS, szUsername, szPassword) < 0) {
			ErrorPush();

			SMTPSendError(hBSock, SMTPS, "503 Authentication failed");

			return ErrorPop();
		}
	}
	/* Set the logon user */
	StrSNCpy(SMTPS.szLogonUser, szUsername);

	SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
	SMTPS.iSMTPState = stateAuthenticated;

	BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

	return 0;
}

static int SMTPTryApplyLocalCMD5Auth(SMTPSession &SMTPS, char const *pszChallenge,
				     char const *pszUsername, char const *pszDigest)
{
	/* First try to lookup  mailusers.tab */
	char szAccountUser[MAX_ADDR_NAME] = "";
	char szAccountDomain[MAX_HOST_NAME] = "";

	if (StrSplitString(pszUsername, POP3_USER_SPLITTERS, szAccountUser, sizeof(szAccountUser),
			   szAccountDomain, sizeof(szAccountDomain)) < 0)
		return ErrGetErrorCode();

	UserInfo *pUI = UsrGetUserByName(szAccountDomain, szAccountUser);

	if (pUI != NULL) {
		/* Compute MD5 response ( secret , challenge , digest ) */
		char szCurrDigest[512] = "";

		if (MscCramMD5(pUI->pszPassword, pszChallenge, szCurrDigest) < 0) {
			UsrFreeUserInfo(pUI);

			return ErrGetErrorCode();
		}
		if (stricmp(szCurrDigest, pszDigest) == 0) {
			/* Apply user configuration */
			if (SMTPApplyUserConfig(SMTPS, pUI) < 0) {
				ErrorPush();
				UsrFreeUserInfo(pUI);
				return ErrorPop();
			}
			UsrFreeUserInfo(pUI);

			return 0;
		}
		UsrFreeUserInfo(pUI);
	}

	ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
	return ERR_SMTP_AUTH_FAILED;
}

static int SMTPTryApplyCMD5Auth(SMTPSession &SMTPS, char const *pszChallenge,
				char const *pszUsername, char const *pszDigest)
{
	char szAuthFilePath[SYS_MAX_PATH] = "";

	SMTPGetAuthFilePath(szAuthFilePath, sizeof(szAuthFilePath));

	FILE *pAuthFile = fopen(szAuthFilePath, "rt");

	if (pAuthFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, szAuthFilePath);
		return ERR_FILE_OPEN;
	}

	char szAuthLine[SVR_SMTPAUTH_LINE_MAX] = "";

	while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAuthLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if (iFieldsCount >= smtpaMax &&
		    strcmp(ppszStrings[smtpaUsername], pszUsername) == 0) {
			char szCurrDigest[512] = "";

			/* Compute MD5 response ( secret , challenge , digest ) */
			if (MscCramMD5(ppszStrings[smtpaPassword], pszChallenge,
				       szCurrDigest) < 0) {
				StrFreeStrings(ppszStrings);
				fclose(pAuthFile);

				return ErrGetErrorCode();
			}
			if (stricmp(szCurrDigest, pszDigest) == 0) {
				/* Apply user perms to SMTP config */
				SMTPApplyPerms(SMTPS, ppszStrings[smtpaPerms]);

				StrFreeStrings(ppszStrings);
				fclose(pAuthFile);

				return 0;
			}
		}
		StrFreeStrings(ppszStrings);
	}
	fclose(pAuthFile);

	ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
	return ERR_SMTP_AUTH_FAILED;
}

static int SMTPDoAuthCramMD5(BSOCK_HANDLE hBSock, SMTPSession &SMTPS, char const *pszAuthParam)
{
	/* Emit encoded64 challenge and get client response */
	int iEnc64Length;
	char szChallenge[512] = "";

	iEnc64Length = sizeof(szChallenge) - 1;
	Base64Encode(SMTPS.szTimeStamp, strlen(SMTPS.szTimeStamp), szChallenge, &iEnc64Length);

	if (BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s", szChallenge) < 0 ||
	    BSckGetString(hBSock, szChallenge, sizeof(szChallenge) - 1,
			  SMTPS.pSMTPCfg->iTimeout) == NULL)
		return ErrGetErrorCode();

	/* Decode ( base64 ) client response */
	int iDec64Length;
	char szClientResp[512] = "";

	iDec64Length = sizeof(szClientResp);
	if (Base64Decode(szChallenge, strlen(szChallenge), szClientResp, &iDec64Length) != 0) {
		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");

		ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
		return ERR_BAD_SMTP_CMD_SYNTAX;
	}
	/* Extract the username and client digest */
	char *pszUsername = szClientResp;
	char *pszDigest = strchr(szClientResp, ' ');

	if (pszDigest == NULL) {
		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");

		ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
		return ERR_BAD_SMTP_CMD_SYNTAX;
	}
	*pszDigest++ = '\0';

	/* Validate client response */
	int iError;

	if ((iError = SMTPTryExtAuth(hBSock, SMTPS, "CRAM-MD5", pszUsername,
				     NULL, SMTPS.szTimeStamp, pszDigest)) < 0)
		return ErrGetErrorCode();
	else if (iError == 0) {
		if (SMTPTryApplyLocalCMD5Auth(SMTPS, SMTPS.szTimeStamp, pszUsername,
					      pszDigest) < 0 &&
		    SMTPTryApplyCMD5Auth(SMTPS, SMTPS.szTimeStamp, pszUsername, pszDigest) < 0) {
			ErrorPush();

			SMTPSendError(hBSock, SMTPS, "503 Authentication failed");

			return ErrorPop();
		}
	}
	/* Set the logon user */
	StrSNCpy(SMTPS.szLogonUser, pszUsername);

	SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
	SMTPS.iSMTPState = stateAuthenticated;

	BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

	return 0;
}

static int SMTPHandleCmd_AUTH(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	if (SMTPS.iSMTPState != stateHelo) {
		SMTPResetSession(SMTPS);

		SMTPSendError(hBSock, SMTPS, "503 Bad sequence of commands");

		ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
		return ERR_SMTP_BAD_CMD_SEQUENCE;
	}

	int iTokensCount;
	char **ppszTokens = StrTokenize(pszCommand, " ");

	if (ppszTokens == NULL || (iTokensCount = StrStringsCount(ppszTokens)) < 2) {
		if (ppszTokens != NULL)
			StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");
		return -1;
	}
	/* Decode AUTH command params */
	char szAuthType[128] = "";
	char szAuthParam[PLAIN_AUTH_PARAM_SIZE] = "";

	StrSNCpy(szAuthType, ppszTokens[1]);
	if (iTokensCount > 2)
		StrSNCpy(szAuthParam, ppszTokens[2]);
	StrFreeStrings(ppszTokens);

	/*
	 * Check if the client sent an AUTH type that is not allowed in non-TLS
	 * mode.
	 */
	if (SMTPS.pszNoTLSAuths != NULL &&
	    strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) != 0 &&
	    StrLimIStr(SMTPS.pszNoTLSAuths, szAuthType, ",") == NULL) {
		SMTPSendError(hBSock, SMTPS, "504 Unrecognized authentication type");
		ErrSetErrorCode(ERR_UNKNOWN_SMTP_AUTH);
		return ERR_UNKNOWN_SMTP_AUTH;
	}

	/* Handle authentication methods */
	if (stricmp(szAuthType, "PLAIN") == 0) {
		if (SMTPDoAuthPlain(hBSock, SMTPS, szAuthParam) < 0) {
			ErrorPush();

			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=PLAIN", 0);

			return ErrorPop();
		}
	} else if (stricmp(szAuthType, "LOGIN") == 0) {
		if (SMTPDoAuthLogin(hBSock, SMTPS, szAuthParam) < 0) {
			ErrorPush();

			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=LOGIN", 0);

			return ErrorPop();
		}
	} else if (stricmp(szAuthType, "CRAM-MD5") == 0) {
		if (SMTPDoAuthCramMD5(hBSock, SMTPS, szAuthParam) < 0) {
			ErrorPush();

			if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
				SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=CRAM-MD5", 0);

			return ErrorPop();
		}
	} else {
		SMTPSendError(hBSock, SMTPS, "504 Unrecognized authentication type");
		ErrSetErrorCode(ERR_UNKNOWN_SMTP_AUTH);
		return ERR_UNKNOWN_SMTP_AUTH;
	}

	return 0;
}

static int SMTPHandleCmd_RSET(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	SMTPResetSession(SMTPS);

	BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

	return 0;
}

static int SMTPHandleCmd_NOOP(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

	return 0;
}

static int SMTPHandleCmd_HELP(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	char const *pszSTLS = "";

	if (strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) != 0 &&
	    SvrTestConfigFlag("EnableSMTP-TLS", true, SMTPS.hSvrConfig))
		pszSTLS = " STARTTLS";

	BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
			"214-HELO EHLO MAIL RCPT DATA AUTH%s\r\n"
			"214-RSET VRFY ETRN NOOP HELP QUIT\r\n"
			"214 For more information please visit : %s", pszSTLS, APP_URL);

	return 0;
}

static int SMTPHandleCmd_QUIT(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	SMTPS.iSMTPState = stateExit;

	BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
			"221 %s service closing transmission channel", SMTP_SERVER_NAME);

	return 0;
}

static int SMTPHandleCmd_VRFY(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	/* Check if VRFY is enabled */
	if ((SMTPS.ulFlags & SMTPF_VRFY_ENABLED) == 0 &&
	    !SvrTestConfigFlag("AllowSmtpVRFY", false, SMTPS.hSvrConfig)) {
		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, "", "", "VRFY=EACCESS", 0);

		SMTPSendError(hBSock, SMTPS, "252 Argument not checked");
		return -1;
	}

	char **ppszTokens = StrTokenize(pszCommand, " ");

	if ((ppszTokens == NULL) || (StrStringsCount(ppszTokens) != 2)) {
		if (ppszTokens != NULL)
			StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");
		return -1;
	}

	char szVrfyUser[MAX_ADDR_NAME] = "";
	char szVrfyDomain[MAX_ADDR_NAME] = "";

	if (USmtpSplitEmailAddr(ppszTokens[1], szVrfyUser, szVrfyDomain) < 0) {
		ErrorPush();
		StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");
		return ErrorPop();
	}

	StrFreeStrings(ppszTokens);

	UserInfo *pUI = UsrGetUserByNameOrAlias(szVrfyDomain, szVrfyUser);

	if (pUI != NULL) {
		char *pszRealName = UsrGetUserInfoVar(pUI, "RealName", "Unknown");

		BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
				"250 %s <%s@%s>", pszRealName, pUI->pszName, pUI->pszDomain);

		SysFree(pszRealName);
		UsrFreeUserInfo(pUI);
	} else {
		if (USmlIsCmdAliasAccount(szVrfyDomain, szVrfyUser) < 0) {
			SMTPSendError(hBSock, SMTPS, "550 String does not match anything");

			ErrSetErrorCode(ERR_USER_NOT_LOCAL);
			return ERR_USER_NOT_LOCAL;
		}

		BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
				"250 Local account <%s@%s>", szVrfyUser, szVrfyDomain);
	}

	return 0;
}

static int SMTPHandleCmd_ETRN(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	/* Check if ETRN is enabled */
	if ((SMTPS.ulFlags & SMTPF_ETRN_ENABLED) == 0 &&
	    !SvrTestConfigFlag("AllowSmtpETRN", false, SMTPS.hSvrConfig)) {
		if (SMTPLogEnabled(SMTPS.pThCfg->hThShb, SMTPS.pSMTPCfg))
			SMTPLogSession(SMTPS, "", "", "ETRN=EACCESS", 0);

		SMTPSendError(hBSock, SMTPS, "501 Command not accepted");
		return -1;
	}

	char **ppszTokens = StrTokenize(pszCommand, " ");

	if (ppszTokens == NULL || StrStringsCount(ppszTokens) != 2) {
		if (ppszTokens != NULL)
			StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS, "501 Syntax error in parameters or arguments");
		return -1;
	}
	/* Do a matched flush of the rsnd arena */
	if (QueFlushRsndArena(hSpoolQueue, ppszTokens[1]) < 0) {
		ErrorPush();
		StrFreeStrings(ppszTokens);

		SMTPSendError(hBSock, SMTPS,
			      "451 Requested action aborted: (%d) local error in processing",
			      ErrorFetch());

		return ErrorPop();
	}
	BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
			"250 Queueing for '%s' has been started", ppszTokens[1]);
	StrFreeStrings(ppszTokens);

	return 0;
}

static int SMTPHandleCommand(char const *pszCommand, BSOCK_HANDLE hBSock, SMTPSession &SMTPS)
{
	/* Delay protection against massive spammers */
	if (SMTPS.iCmdDelay > 0)
		SysSleep(SMTPS.iCmdDelay);

	/* Command parsing and processing */
	int iError = -1;

	if (StrINComp(pszCommand, MAIL_FROM_STR) == 0)
		iError = SMTPHandleCmd_MAIL(pszCommand, hBSock, SMTPS);
	else if (StrINComp(pszCommand, RCPT_TO_STR) == 0)
		iError = SMTPHandleCmd_RCPT(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "DATA"))
		iError = SMTPHandleCmd_DATA(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "HELO"))
		iError = SMTPHandleCmd_HELO(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "EHLO"))
		iError = SMTPHandleCmd_EHLO(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "STARTTLS"))
		iError = SMTPHandleCmd_STARTTLS(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "AUTH"))
		iError = SMTPHandleCmd_AUTH(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "RSET"))
		iError = SMTPHandleCmd_RSET(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "VRFY"))
		iError = SMTPHandleCmd_VRFY(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "ETRN"))
		iError = SMTPHandleCmd_ETRN(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "NOOP"))
		iError = SMTPHandleCmd_NOOP(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "HELP"))
		iError = SMTPHandleCmd_HELP(pszCommand, hBSock, SMTPS);
	else if (StrCmdMatch(pszCommand, "QUIT"))
		iError = SMTPHandleCmd_QUIT(pszCommand, hBSock, SMTPS);
	else
		SMTPSendError(hBSock, SMTPS, "500 Syntax error, command unrecognized");

	return iError;
}

static int SMTPHandleSession(ThreadConfig const *pThCfg, BSOCK_HANDLE hBSock)
{
	/* Session structure declaration and init */
	char *pszSMTPError = NULL;
	SMTPSession SMTPS;

	if (SMTPInitSession(pThCfg, hBSock, SMTPS, pszSMTPError) < 0) {
		ErrorPush();
		if (pszSMTPError != NULL) {
			BSckSendString(hBSock, pszSMTPError, STD_SMTP_TIMEOUT);

			SysFree(pszSMTPError);
		} else
			BSckVSendString(hBSock, STD_SMTP_TIMEOUT,
					"421 %s service not available (%d), closing transmission channel",
					SMTP_SERVER_NAME, ErrorFetch());

		return ErrorPop();
	}

	char szIP[128] = "???.???.???.???";

	SysLogMessage(LOG_LEV_MESSAGE, "SMTP client connection from [%s]\n",
		      SysInetNToA(SMTPS.PeerInfo, szIP, sizeof(szIP)));

	/* Send welcome message */
	char szTime[256] = "";

	MscGetTimeStr(szTime, sizeof(szTime) - 1);

	if (BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
			    "220 %s %s %s service ready; %s", SMTPS.szSvrDomain,
			    SMTPS.szTimeStamp, SMTP_SERVER_NAME, szTime) < 0) {
		ErrorPush();
		SMTPClearSession(SMTPS);
		return ErrorPop();
	}
	/* Command loop */
	char szCommand[1024] = "";

	while (!SvrInShutdown() && SMTPS.iSMTPState != stateExit &&
	       BSckGetString(hBSock, szCommand, sizeof(szCommand) - 1,
			     SMTPS.pSMTPCfg->iSessionTimeout) != NULL &&
	       MscCmdStringCheck(szCommand) == 0) {
		if (pThCfg->ulFlags & THCF_SHUTDOWN)
			break;

		/* Handle command */
		SMTPHandleCommand(szCommand, hBSock, SMTPS);
	}

	SysLogMessage(LOG_LEV_MESSAGE, "SMTP client exit [%s]\n",
		      SysInetNToA(SMTPS.PeerInfo, szIP, sizeof(szIP)));

	SMTPClearSession(SMTPS);

	return 0;
}

unsigned int SMTPClientThread(void *pThreadData)
{
	ThreadCreateCtx *pThCtx = (ThreadCreateCtx *) pThreadData;

	/* Link socket to the bufferer */
	BSOCK_HANDLE hBSock = BSckAttach(pThCtx->SockFD);

	if (hBSock == INVALID_BSOCK_HANDLE) {
		ErrorPush();
		SysCloseSocket(pThCtx->SockFD);
		SysFree(pThCtx);
		return ErrorPop();
	}

	/*
	 * Do we need to switch to TLS?
	 */
	if (pThCtx->pThCfg->ulFlags & THCF_USE_SSL) {
		int iError;
		SslServerBind SSLB;
		SslBindEnv SslE;

		if (CSslBindSetup(&SSLB) < 0) {
			ErrorPush();
			BSckDetach(hBSock, 1);
			SysFree(pThCtx);
			return ErrorPop();
		}
		ZeroData(SslE);

		iError = BSslBindServer(hBSock, &SSLB, MscSslEnvCB, &SslE);

		CSslBindCleanup(&SSLB);
		if (iError < 0) {
			ErrorPush();
			BSckDetach(hBSock, 1);
			SysFree(pThCtx);
			return ErrorPop();
		}
		/*
		 * We may want to add verify code here ...
		 */

		SysFree(SslE.pszIssuer);
		SysFree(SslE.pszSubject);
	}

	/* Increase threads count */
	if (SMTPThreadCountAdd(+1, pThCtx->pThCfg->hThShb) < 0) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s (SMTP thread count)\n",
			      ErrGetErrorString(ErrorFetch()));
		BSckVSendString(hBSock, STD_SMTP_TIMEOUT, "421 %s - %s",
				SMTP_SERVER_NAME, ErrGetErrorString(ErrorFetch()));

		BSckDetach(hBSock, 1);
		SysFree(pThCtx);
		return ErrorPop();
	}

	/* Handle client session */
	SMTPHandleSession(pThCtx->pThCfg, hBSock);

	/* Decrease threads count */
	SMTPThreadCountAdd(-1, pThCtx->pThCfg->hThShb);

	/* Unlink socket from the bufferer and close it */
	BSckDetach(hBSock, 1);
	SysFree(pThCtx);

	return 0;
}

