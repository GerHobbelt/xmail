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
#include "BuffSock.h"
#include "Hash.h"
#include "SSLBind.h"
#include "SSLConfig.h"
#include "MD5.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MiscUtils.h"
#include "Maildir.h"
#include "POP3Svr.h"
#include "POP3GwLink.h"
#include "POP3Utils.h"
#include "SMTPUtils.h"
#include "MailSvr.h"

#define UPOP_IPMAP_FILE         "pop3.ipmap.tab"
#define POP3_IP_LOGFILE         ".ipconn"

#define POPF_MSG_DELETED        (1 << 0)
#define POPF_MSG_SENT           (1 << 1)
#define POPF_MSG_TOP            (1 << 2)

#define POPCHF_USE_APOP         (1 << 0)
#define POPCHF_FORCE_APOP       (1 << 1)
#define POPCHF_USE_STLS         (1 << 2)
#define POPCHF_FORCE_STLS       (1 << 3)
#define POPCHF_USE_POP3S        (1 << 4)
#define POPCHF_LEAVE_MSGS       (1 << 5)

#define STD_POP3_TIMEOUT        STD_SERVER_TIMEOUT

struct POP3ChannelCfg {
	unsigned long ulFlags;
	char *pszIFace;
};

struct POP3SyncMsg {
	SysListHead LLnk;
	HashNode HN;
	int iMsgSeq;
	char *pszMsgID;
	unsigned long ulSize;
};

struct POP3SyncChannel {
	char *pszRmtServer;
	char *pszRmtName;
	POP3ChannelCfg ChCfg;
	BSOCK_HANDLE hBSock;
	int iMsgCount;
	unsigned long ulMailboxSize;
	int iMsgSync;
	unsigned long ulSizeSync;
	POP3SyncMsg **ppSMsg;
	SysListHead SyncMList;
	SysListHead SeenMList;
};

struct POP3MsgData {
	LISTLINK LL;
	char szMsgName[SYS_MAX_PATH];
	unsigned long ulMsgSize;
	unsigned long ulFlags;
};

struct POP3SessionData {
	SYS_INET_ADDR PeerInfo;
	UserInfo *pUI;
	HSLIST hMessageList;
	POP3MsgData **ppMsgArray;
	int iMsgListed;
	int iMsgCount;
	unsigned long ulMBSize;
	int iLastAccessed;
	int iTimeout;
};

static int UPopMailFileNameFilter(const char *pszFileName);
static int UPopFillMessageList(const char *pszBasePath, const char *pszSubPath,
			       HSLIST &hMessageList, int &iMsgCount, unsigned long &ulMBSize);
static int UPopBuildMessageList(UserInfo *pUI, HSLIST &hMessageList,
				int *piMsgCount = NULL, unsigned long *pulMBSize = NULL);
static void UPopFreeMsgData(POP3MsgData *pPOPMD);
static void UPopFreeMessageList(HSLIST &hMessageList);
static unsigned long UPopMessagesSize(HSLIST &hMessageList);
static POP3MsgData *UPopMessageFromIndex(POP3SessionData *pPOPSD, int iMsgIndex);
static int UPopCheckPeerIP(UserInfo *pUI, SYS_INET_ADDR const &PeerInfo);
static int UPopUpdateMailbox(POP3SessionData *pPOPSD);
static int UPopCheckResponse(const char *pszResponse, char *pszMessage = NULL);
static int UPopGetResponse(BSOCK_HANDLE hBSock, char *pszResponse, int iMaxChars,
			   int iTimeout);
static char *UPopExtractServerTimeStamp(const char *pszResponse, char *pszTimeStamp,
					int iMaxTimeStamp);
static int UPopSendCommand(BSOCK_HANDLE hBSock, const char *pszCommand, char *pszResponse,
			   int iMaxChars, int iTimeout);
static int UPopDoClearTextAuth(BSOCK_HANDLE hBSock, const char *pszUsername,
			       const char *pszPassword, char *pszRespBuffer, int iMaxRespChars);
static int UPopDoAPOPAuth(BSOCK_HANDLE hBSock, const char *pszUsername,
			  const char *pszPassword, const char *pszTimeStamp,
			  char *pszRespBuffer, int iMaxRespChars);
static int UPopSwitchToTLS(BSOCK_HANDLE hBSock, const char *pszServer,
			   POP3ChannelCfg const *pChCfg);
static int UPopInitiateTLS(BSOCK_HANDLE hBSock, const char *pszServer, char *pszRespBuffer,
			   int iMaxRespChars, POP3ChannelCfg const *pChCfg);
static BSOCK_HANDLE UPopCreateChannel(const char *pszServer, const char *pszUsername,
				      const char *pszPassword, POP3ChannelCfg const *pChCfg);
static int UPopCloseChannel(BSOCK_HANDLE hBSock, int iHardClose = 0);
static int UPopGetMailboxStatus(BSOCK_HANDLE hBSock, int &iMsgCount,
				unsigned long &ulMailboxSize);
static int UPopRetrieveMessage(BSOCK_HANDLE hBSock, int iMsgIndex, const char *pszFileName,
			       unsigned long *pulMsgSize);
static int UPopDeleteMessage(BSOCK_HANDLE hBSock, int iMsgIndex);
static int UPopChanConfigAssign(void *pPrivate, const char *pszName, const char *pszValue);
static int UPopSetChanConfig(const char *pszAuthType, POP3ChannelCfg *pChCfg);
static void UPopFreeChanConfig(POP3ChannelCfg *pChCfg);
static POP3SyncMsg *UPopSChanMsgAlloc(int iMsgSeq, const char *pszMsgID);
static void UPopSChanMsgFree(POP3SyncMsg *pSMsg);
static int UPopSChanFilterSeen(POP3SyncChannel *pPSChan);
static int UPopSChanWriteSeenDB(POP3SyncChannel *pPSChan);
static int UPopSChanFillStatus(POP3SyncChannel *pPSChan);
static POP3SyncChannel *UPopSChanCreate(const char *pszRmtServer, const char *pszRmtName,
					const char *pszRmtPassword, const char *pszSyncCfg);
static void UPopSChanFree(POP3SyncChannel *pPSChan);
static int UPopGetIpLogFilePath(UserInfo *pUI, char *pszFilePath, int iMaxPath);

static int UPopMailFileNameFilter(const char *pszFileName)
{
	return (*pszFileName != '.') ? 1: 0;
}

int UPopGetMailboxSize(UserInfo *pUI, SYS_OFF_T &llMBSize, unsigned long &ulNumMessages)
{
	char szMBPath[SYS_MAX_PATH] = "";

	UsrGetMailboxPath(pUI, szMBPath, sizeof(szMBPath), 0);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szMBPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	llMBSize = 0;
	ulNumMessages = 0;
	if (MscGetDirectorySize(szMBPath, true, llMBSize, ulNumMessages,
				UPopMailFileNameFilter) < 0) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}
	RLckUnlockSH(hResLock);

	return 0;
}

int UPopCheckMailboxSize(UserInfo *pUI, SYS_OFF_T *pllAvailSpace)
{
	SYS_OFF_T llMBSize = 0, llProbeSize = (pllAvailSpace != NULL) ? *pllAvailSpace: 0;
	unsigned long ulNumMessages = 0;

	if (UPopGetMailboxSize(pUI, llMBSize, ulNumMessages) < 0)
		return ErrGetErrorCode();

	char *pszMaxMBSize = UsrGetUserInfoVar(pUI, "MaxMBSize");

	if (pszMaxMBSize != NULL) {
		SYS_OFF_T llMaxMBSize = (SYS_OFF_T) atol(pszMaxMBSize) * 1024;

		SysFree(pszMaxMBSize);
		if (llMaxMBSize && (llMBSize + llProbeSize >= llMaxMBSize)) {
			ErrSetErrorCode(ERR_MAILBOX_SIZE);
			return ERR_MAILBOX_SIZE;
		}
		if (pllAvailSpace != NULL)
			*pllAvailSpace = (llMaxMBSize ? llMaxMBSize - llMBSize:
					  (SYS_OFF_T) -1);
	} else if (pllAvailSpace != NULL) {
		*pllAvailSpace = ULONG_MAX;
	}

	return 0;
}

static int UPopFillMessageList(const char *pszBasePath, const char *pszSubPath,
			       HSLIST &hMessageList, int &iMsgCount, unsigned long &ulMBSize)
{
	/* Setup pathname to scan for messages */
	char szScanPath[SYS_MAX_PATH] = "";

	if (pszSubPath == NULL)
		StrSNCpy(szScanPath, pszBasePath);
	else
		SysSNPrintf(szScanPath, sizeof(szScanPath) - 1, "%s" SYS_SLASH_STR "%s",
			    pszBasePath, pszSubPath);

	char szFileName[SYS_MAX_PATH] = "";
	SYS_HANDLE hFind = SysFirstFile(szScanPath, szFileName, sizeof(szFileName));

	if (hFind != SYS_INVALID_HANDLE) {
		do {
			/* Skip directories and dot-files */
			if (SysIsDirectory(hFind) || IsDotFilename(szFileName))
				continue;

			POP3MsgData *pPOPMD = (POP3MsgData *) SysAlloc(sizeof(POP3MsgData));

			if (pPOPMD == NULL) {
				ErrorPush();
				SysFindClose(hFind);
				return ErrorPop();
			}
			/* Setup message entry fields */
			ListLinkInit(pPOPMD);

			if (pszSubPath == NULL)
				StrSNCpy(pPOPMD->szMsgName, szFileName);
			else
				SysSNPrintf(pPOPMD->szMsgName, sizeof(pPOPMD->szMsgName) - 1,
					    "%s" SYS_SLASH_STR "%s", pszSubPath, szFileName);

			pPOPMD->ulMsgSize = (unsigned long) SysGetSize(hFind);
			pPOPMD->ulFlags = 0;

			/* Insert entry in message list */
			ListAddTail(hMessageList, (PLISTLINK) pPOPMD);

			/* Update mailbox information */
			ulMBSize += pPOPMD->ulMsgSize;
			++iMsgCount;
		} while (SysNextFile(hFind, szFileName, sizeof(szFileName)));
		SysFindClose(hFind);
	}

	return 0;
}

static int UPopBuildMessageList(UserInfo *pUI, HSLIST &hMessageList,
				int *piMsgCount, unsigned long *pulMBSize)
{
	char szMBPath[SYS_MAX_PATH] = "";

	UsrGetMailboxPath(pUI, szMBPath, sizeof(szMBPath), 0);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMBPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* Initialize list and mailbox size information */
	ListInit(hMessageList);

	int iMsgCount = 0;
	unsigned long ulMBSize = 0;

	if (iMailboxType == XMAIL_MAILBOX) {
		if (UPopFillMessageList(szMBPath, NULL, hMessageList, iMsgCount, ulMBSize) < 0) {
			ErrorPush();
			UPopFreeMessageList(hMessageList);
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
	} else {
		int iScanCur = UsrGetUserInfoVarInt(pUI, "Pop3ScanCur", 0);

		if (UPopFillMessageList(szMBPath, "new", hMessageList, iMsgCount, ulMBSize) < 0 ||
		    (iScanCur > 0 &&
		     UPopFillMessageList(szMBPath, "cur", hMessageList, iMsgCount, ulMBSize) < 0)) {
			ErrorPush();
			UPopFreeMessageList(hMessageList);
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
	}
	RLckUnlockEX(hResLock);
	if (piMsgCount != NULL)
		*piMsgCount = iMsgCount;
	if (pulMBSize != NULL)
		*pulMBSize = ulMBSize;

	return 0;
}

static void UPopFreeMsgData(POP3MsgData *pPOPMD)
{
	SysFree(pPOPMD);

}

static void UPopFreeMessageList(HSLIST &hMessageList)
{
	POP3MsgData *pPOPMD;

	while ((pPOPMD = (POP3MsgData *) ListRemove(hMessageList)) != INVALID_SLIST_PTR)
		UPopFreeMsgData(pPOPMD);
}

static unsigned long UPopMessagesSize(HSLIST &hMessageList)
{
	unsigned long ulMBSize = 0;
	POP3MsgData *pPOPMD = (POP3MsgData *) ListFirst(hMessageList);

	for (; pPOPMD != INVALID_SLIST_PTR; pPOPMD = (POP3MsgData *)
		     ListNext(hMessageList, (PLISTLINK) pPOPMD))
		ulMBSize += pPOPMD->ulMsgSize;

	return ulMBSize;
}

static POP3MsgData *UPopMessageFromIndex(POP3SessionData *pPOPSD, int iMsgIndex)
{
	return (iMsgIndex >= 0 && iMsgIndex < pPOPSD->iMsgListed) ?
		pPOPSD->ppMsgArray[iMsgIndex]: NULL;

}

int UPopAuthenticateAPOP(const char *pszDomain, const char *pszUsrName,
			 const char *pszTimeStamp, const char *pszDigest)
{
	UserInfo *pUI = UsrGetUserByName(pszDomain, pszUsrName);

	if (pUI == NULL)
		return ErrGetErrorCode();

	int iAuthResult = MscMD5Authenticate(pUI->pszPassword, pszTimeStamp, pszDigest);

	UsrFreeUserInfo(pUI);

	return iAuthResult;
}

static int UPopCheckPeerIP(UserInfo *pUI, SYS_INET_ADDR const &PeerInfo)
{
	char szIPMapFile[SYS_MAX_PATH] = "";

	UsrGetUserPath(pUI, szIPMapFile, sizeof(szIPMapFile), 1);
	StrNCat(szIPMapFile, UPOP_IPMAP_FILE, sizeof(szIPMapFile));

	if (SysExistFile(szIPMapFile) && MscCheckAllowedIP(szIPMapFile, PeerInfo, true) < 0)
		return ErrGetErrorCode();

	return 0;
}

POP3_HANDLE UPopBuildSession(const char *pszDomain, const char *pszUsrName,
			     const char *pszUsrPass, SYS_INET_ADDR const *pPeerInfo)
{
	UserInfo *pUI = UsrGetUserByName(pszDomain, pszUsrName);

	if (pUI == NULL)
		return INVALID_POP3_HANDLE;

	/* Check if the account is enabled for POP3 sessions */
	if (!UsrGetUserInfoVarInt(pUI, "PopEnable", 1)) {
		UsrFreeUserInfo(pUI);

		ErrSetErrorCode(ERR_USER_DISABLED);
		return INVALID_POP3_HANDLE;
	}
	/* Check if peer is allowed to connect from its IP */
	if (pPeerInfo != NULL && UPopCheckPeerIP(pUI, *pPeerInfo) < 0) {
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}
	if (pszUsrPass != NULL && strcmp(pszUsrPass, pUI->pszPassword) != 0) {
		ErrSetErrorCode(ERR_INVALID_PASSWORD);
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}
	if (UsrPOP3Lock(pUI) < 0) {
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}

	POP3SessionData *pPOPSD = (POP3SessionData *) SysAlloc(sizeof(POP3SessionData));

	if (pPOPSD == NULL) {
		UsrPOP3Unlock(pUI);
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}

	pPOPSD->PeerInfo = *pPeerInfo;
	pPOPSD->pUI = pUI;
	pPOPSD->iLastAccessed = 0;
	pPOPSD->iTimeout = STD_POP3_TIMEOUT;

	if (UPopBuildMessageList(pUI, pPOPSD->hMessageList, &pPOPSD->iMsgCount,
				 &pPOPSD->ulMBSize) < 0) {
		SysFree(pPOPSD);
		UsrPOP3Unlock(pUI);
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}

	pPOPSD->iMsgListed = pPOPSD->iMsgCount;

	/* Build lookup array */
	if ((pPOPSD->ppMsgArray = (POP3MsgData **)
	     SysAlloc((pPOPSD->iMsgCount + 1) * sizeof(POP3MsgData *))) == NULL) {
		UPopFreeMessageList(pPOPSD->hMessageList);
		SysFree(pPOPSD);
		UsrPOP3Unlock(pUI);
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}

	POP3MsgData *pPOPMD = (POP3MsgData *) ListFirst(pPOPSD->hMessageList);

	for (int i = 0; i < pPOPSD->iMsgCount && pPOPMD != INVALID_SLIST_PTR;
	     pPOPMD = (POP3MsgData *) ListNext(pPOPSD->hMessageList, (PLISTLINK) pPOPMD), i++)
		pPOPSD->ppMsgArray[i] = pPOPMD;

	return (POP3_HANDLE) pPOPSD;
}

void UPopReleaseSession(POP3_HANDLE hPOPSession, int iUpdate)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	if (iUpdate)
		UPopUpdateMailbox(pPOPSD);
	UsrPOP3Unlock(pPOPSD->pUI);
	UsrFreeUserInfo(pPOPSD->pUI);
	UPopFreeMessageList(pPOPSD->hMessageList);
	SysFree(pPOPSD->ppMsgArray);
	SysFree(pPOPSD);
}

char *UPopGetUserInfoVar(POP3_HANDLE hPOPSession, const char *pszName, const char *pszDefault)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return UsrGetUserInfoVar(pPOPSD->pUI, pszName, pszDefault);
}

static int UPopUpdateMailbox(POP3SessionData *pPOPSD)
{
	char szMBPath[SYS_MAX_PATH] = "";

	UsrGetMailboxPath(pPOPSD->pUI, szMBPath, sizeof(szMBPath), 0);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMBPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	POP3MsgData *pPOPMD = (POP3MsgData *) ListFirst(pPOPSD->hMessageList);

	for (; pPOPMD != INVALID_SLIST_PTR; pPOPMD = (POP3MsgData *)
		     ListNext(pPOPSD->hMessageList, (PLISTLINK) pPOPMD)) {
		if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
			char szMsgPath[SYS_MAX_PATH] = "";

			SysSNPrintf(szMsgPath, sizeof(szMsgPath) - 1, "%s" SYS_SLASH_STR "%s",
				    szMBPath, pPOPMD->szMsgName);
			SysRemove(szMsgPath);
		}
	}
	RLckUnlockEX(hResLock);

	return 0;
}

int UPopGetSessionMsgCurrent(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return pPOPSD->iMsgCount;
}

int UPopGetSessionMsgTotal(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return pPOPSD->iMsgListed;
}

unsigned long UPopGetSessionMBSize(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return pPOPSD->ulMBSize;
}

int UPopGetSessionLastAccessed(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return pPOPSD->iLastAccessed;
}

int UPopGetMessageSize(POP3_HANDLE hPOPSession, int iMsgIndex, unsigned long &ulMessageSize)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	POP3MsgData *pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1);

	ulMessageSize = 0;
	if (pPOPMD == NULL) {
		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}
	ulMessageSize = pPOPMD->ulMsgSize;

	return 0;
}

int UPopGetMessageUIDL(POP3_HANDLE hPOPSession, int iMsgIndex, char *pszMessageUIDL,
		       int iSize)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	POP3MsgData *pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1);

	if (pPOPMD == NULL) {
		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}
	/* Extract message UIDL from mailbox file path */
	const char *pszFileName;

	if ((pszFileName = strrchr(pPOPMD->szMsgName, SYS_SLASH_CHAR)) != NULL)
		pszFileName++;
	else
		pszFileName = pPOPMD->szMsgName;

	StrNCpy(pszMessageUIDL, pszFileName, iSize);

	return 0;
}

int UPopDeleteMessage(POP3_HANDLE hPOPSession, int iMsgIndex)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	POP3MsgData *pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1);

	if (pPOPMD == NULL) {
		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}

	pPOPSD->ulMBSize -= pPOPMD->ulMsgSize;
	--pPOPSD->iMsgCount;
	pPOPMD->ulFlags |= POPF_MSG_DELETED;
	pPOPSD->iLastAccessed = iMsgIndex;

	return 0;
}

int UPopResetSession(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	POP3MsgData *pPOPMD = (POP3MsgData *) ListFirst(pPOPSD->hMessageList);

	for (; pPOPMD != INVALID_SLIST_PTR; pPOPMD = (POP3MsgData *)
		     ListNext(pPOPSD->hMessageList, (PLISTLINK) pPOPMD)) {
		if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
			pPOPSD->ulMBSize += pPOPMD->ulMsgSize;

			++pPOPSD->iMsgCount;
			pPOPMD->ulFlags &= ~(POPF_MSG_DELETED | POPF_MSG_SENT | POPF_MSG_TOP);
		}
	}
	pPOPSD->iLastAccessed = 0;

	return 0;
}

int UPopSendErrorResponse(BSOCK_HANDLE hBSock, int iErrorCode, int iTimeout)
{
	const char *pszError = ErrGetErrorString(iErrorCode);
	char *pszPOP3Error = (char *) SysAlloc(strlen(pszError) + 8);

	if (pszPOP3Error == NULL)
		return ErrGetErrorCode();

	sprintf(pszPOP3Error, "-ERR %s", pszError);
	if (BSckSendString(hBSock, pszPOP3Error, iTimeout) < 0) {
		SysFree(pszPOP3Error);
		return ErrGetErrorCode();
	}
	SysFree(pszPOP3Error);

	return 0;
}

static int UPopSendMessageFile(BSOCK_HANDLE hBSock, const char *pszFilePath,
			       int iTimeout)
{
	/*
	 * Send the message file to the remote POP3 client. If we are
	 * running on an OS with CRLF line termination, we can send the
	 * message as it is (since RFC wants it with CRLF).
	 * In the other case, we need to send by transforming LF to CRLF.
	 */
#ifdef SYS_CRLF_EOL
	return BSckSendFile(hBSock, pszFilePath, 0, -1, iTimeout);
#else
	return MscSendFileCRLF(pszFilePath, hBSock, iTimeout);
#endif
}

int UPopSessionSendMsg(POP3_HANDLE hPOPSession, int iMsgIndex, BSOCK_HANDLE hBSock)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	POP3MsgData *pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1);

	if (pPOPMD == NULL) {
		UPopSendErrorResponse(hBSock, ERR_MSG_NOT_IN_RANGE, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		UPopSendErrorResponse(hBSock, ERR_MSG_DELETED, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}

	char szMsgFilePath[SYS_MAX_PATH] = "";

	UsrGetMailboxPath(pPOPSD->pUI, szMsgFilePath, sizeof(szMsgFilePath), 1);

	StrNCat(szMsgFilePath, pPOPMD->szMsgName, sizeof(szMsgFilePath));

	char szResponse[256] = "";

	sprintf(szResponse, "+OK %lu bytes", pPOPMD->ulMsgSize);
	if (BSckSendString(hBSock, szResponse, pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	if (pPOPMD->ulMsgSize > 0 &&
	    UPopSendMessageFile(hBSock, szMsgFilePath,  pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	if (BSckSendString(hBSock, ".", pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	pPOPSD->iLastAccessed = iMsgIndex;
	pPOPMD->ulFlags |= POPF_MSG_SENT;

	return 0;
}

int UPopSessionTopMsg(POP3_HANDLE hPOPSession, int iMsgIndex, int iNumLines, BSOCK_HANDLE hBSock)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	POP3MsgData *pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1);

	if (pPOPMD == NULL) {
		UPopSendErrorResponse(hBSock, ERR_MSG_NOT_IN_RANGE, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		UPopSendErrorResponse(hBSock, ERR_MSG_DELETED, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}

	char szMsgFilePath[SYS_MAX_PATH] = "";

	UsrGetMailboxPath(pPOPSD->pUI, szMsgFilePath, sizeof(szMsgFilePath), 1);
	StrNCat(szMsgFilePath, pPOPMD->szMsgName, sizeof(szMsgFilePath));

	FILE *pMsgFile = fopen(szMsgFilePath, "rb");

	if (pMsgFile == NULL) {
		UPopSendErrorResponse(hBSock, ERR_FILE_OPEN, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_FILE_OPEN);
		return ERR_FILE_OPEN;
	}

	char szResponse[256] = "";

	sprintf(szResponse, "+OK message is %lu bytes", pPOPMD->ulMsgSize);
	if (BSckSendString(hBSock, szResponse, pPOPSD->iTimeout) < 0) {
		fclose(pMsgFile);
		return ErrGetErrorCode();
	}

	bool bSendingMsg = false;
	char szMsgLine[2048] = "";

	while (MscGetString(pMsgFile, szMsgLine, sizeof(szMsgLine) - 1) != NULL) {
		if (bSendingMsg && (--iNumLines < 0))
			break;

		if (BSckSendString(hBSock, szMsgLine, pPOPSD->iTimeout) < 0) {
			fclose(pMsgFile);
			return ErrGetErrorCode();
		}
		if (!bSendingMsg && (strlen(szMsgLine) == 0))
			bSendingMsg = true;
		if (SvrInShutdown()) {
			fclose(pMsgFile);
			ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
			return ERR_SERVER_SHUTDOWN;
		}
	}
	fclose(pMsgFile);

	if (BSckSendString(hBSock, ".", pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	pPOPSD->iLastAccessed = iMsgIndex;
	pPOPMD->ulFlags |= POPF_MSG_TOP;

	return 0;
}

int UPopSaveUserIP(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	char szIpFilePath[SYS_MAX_PATH] = "";

	UPopGetIpLogFilePath(pPOPSD->pUI, szIpFilePath, sizeof(szIpFilePath));

	char szIP[128] = "???.???.???.???";

	SysInetNToA(pPOPSD->PeerInfo, szIP, sizeof(szIP));

	FILE *pIpFile = fopen(szIpFilePath, "wt");

	if (pIpFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, szIpFilePath);
		return ERR_FILE_CREATE;
	}
	fprintf(pIpFile, "%s\n", szIP);
	fclose(pIpFile);

	return 0;
}

static int UPopCheckResponse(const char *pszResponse, char *pszMessage)
{
	if (strnicmp(pszResponse, "+OK", 3) == 0) {
		if (pszMessage != NULL)
			strcpy(pszMessage, pszResponse + ((pszResponse[3] == ' ') ? 4: 3));

		return 0;
	} else if (strnicmp(pszResponse, "-ERR", 4) == 0) {
		if (pszMessage != NULL)
			strcpy(pszMessage, pszResponse + ((pszResponse[4] == ' ') ? 5: 4));

		ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, pszResponse);
		return ERR_BAD_SERVER_RESPONSE;
	}

	ErrSetErrorCode(ERR_INVALID_POP3_RESPONSE, pszResponse);
	return ERR_INVALID_POP3_RESPONSE;
}

static int UPopGetResponse(BSOCK_HANDLE hBSock, char *pszResponse, int iMaxChars, int iTimeout)
{
	if (BSckGetString(hBSock, pszResponse, iMaxChars, iTimeout) == NULL)
		return ErrGetErrorCode();

	return UPopCheckResponse(pszResponse);
}

static char *UPopExtractServerTimeStamp(const char *pszResponse, char *pszTimeStamp,
					int iMaxTimeStamp)
{
	const char *pszStartTS = strchr(pszResponse, '<');
	const char *pszEndTS = strchr(pszResponse, '>');

	if (pszStartTS == NULL || pszEndTS == NULL || pszEndTS < pszStartTS)
		return NULL;

	int iLengthTS = (int) (pszEndTS - pszStartTS) + 1;

	iLengthTS = Min(iLengthTS, iMaxTimeStamp - 1);

	strncpy(pszTimeStamp, pszStartTS, iLengthTS);
	pszTimeStamp[iLengthTS] = '\0';

	return pszTimeStamp;
}

static int UPopSendCommand(BSOCK_HANDLE hBSock, const char *pszCommand, char *pszResponse,
			   int iMaxChars, int iTimeout)
{
	if (BSckSendString(hBSock, pszCommand, iTimeout) <= 0)
		return ErrGetErrorCode();
	if (UPopGetResponse(hBSock, pszResponse, iMaxChars, iTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopDoClearTextAuth(BSOCK_HANDLE hBSock, const char *pszUsername,
			       const char *pszPassword, char *pszRespBuffer, int iMaxRespChars)
{
	/* Send USER and read result */
	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "USER %s", pszUsername);
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	/* Send PASS and read result */
	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "PASS %s", pszPassword);
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopDoAPOPAuth(BSOCK_HANDLE hBSock, const char *pszUsername,
			  const char *pszPassword, const char *pszTimeStamp,
			  char *pszRespBuffer, int iMaxRespChars)
{
	/* Perform APOP authentication */
	char *pszHash = StrSprint("%s%s", pszTimeStamp, pszPassword);

	if (pszHash == NULL)
		return ErrGetErrorCode();

	char szMD5[128] = "";

	do_md5_string(pszHash, strlen(pszHash), szMD5);
	SysFree(pszHash);

	/* Send APOP and read result */
	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "APOP %s %s", pszUsername, szMD5);
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopSwitchToTLS(BSOCK_HANDLE hBSock, const char *pszServer,
			   POP3ChannelCfg const *pChCfg)
{
	int iError;
	SslServerBind SSLB;
	SslBindEnv SslE;

	if (CSslBindSetup(&SSLB) < 0)
		return ErrGetErrorCode();
	ZeroData(SslE);

	iError = BSslBindClient(hBSock, &SSLB, MscSslEnvCB, &SslE);

	CSslBindCleanup(&SSLB);
	/*
	 * We may want to add verify code here ...
	 */

	SysFree(SslE.pszIssuer);
	SysFree(SslE.pszSubject);

	return iError;
}

static int UPopInitiateTLS(BSOCK_HANDLE hBSock, const char *pszServer, char *pszRespBuffer,
			   int iMaxRespChars, POP3ChannelCfg const *pChCfg)
{

	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "STLS");
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return (pChCfg->ulFlags & POPCHF_FORCE_STLS) ? ErrGetErrorCode(): 0;

	return UPopSwitchToTLS(hBSock, pszServer, pChCfg);
}

static BSOCK_HANDLE UPopCreateChannel(const char *pszServer, const char *pszUsername,
				      const char *pszPassword, POP3ChannelCfg const *pChCfg)
{
	SYS_INET_ADDR SvrAddr;

	if (MscGetServerAddress(pszServer, SvrAddr, STD_POP3_PORT) < 0)
		return INVALID_BSOCK_HANDLE;

	SYS_SOCKET SockFD = SysCreateSocket(SysGetAddrFamily(SvrAddr), SOCK_STREAM, 0);

	if (SockFD == SYS_INVALID_SOCKET)
		return INVALID_BSOCK_HANDLE;

	/*
	 * Are we requested to bind to a specific interface to talk to this server?
	 */
	if (pChCfg->pszIFace != NULL) {
		SYS_INET_ADDR BndAddr;

		if (MscGetServerAddress(pChCfg->pszIFace, BndAddr, 0) < 0 ||
		    SysBindSocket(SockFD, &BndAddr) < 0) {
			SysCloseSocket(SockFD);
			return INVALID_BSOCK_HANDLE;
		}
	}
	if (SysConnect(SockFD, &SvrAddr, iPOP3ClientTimeout) < 0) {
		SysCloseSocket(SockFD);
		return INVALID_BSOCK_HANDLE;
	}

	BSOCK_HANDLE hBSock = BSckAttach(SockFD);

	if (hBSock == INVALID_BSOCK_HANDLE) {
		SysCloseSocket(SockFD);
		return INVALID_BSOCK_HANDLE;
	}
	/*
	 * Is this a full POP3S connection?
	 */
	if ((pChCfg->ulFlags & POPCHF_USE_POP3S) &&
	    UPopSwitchToTLS(hBSock, pszServer, pChCfg) < 0) {
		SysCloseSocket(SockFD);
		return INVALID_BSOCK_HANDLE;
	}

	/* Read welcome message */
	char szRTXBuffer[2048] = "";

	if (UPopGetResponse(hBSock, szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0) {
		UPopCloseChannel(hBSock);
		return INVALID_BSOCK_HANDLE;
	}
	/*
	 * Non TLS mode active and STLS required?
	 */
	if (strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) != 0 &&
	    (pChCfg->ulFlags & POPCHF_USE_STLS) &&
	    UPopInitiateTLS(hBSock, pszServer, szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    pChCfg) < 0) {
		UPopCloseChannel(hBSock);
		return INVALID_BSOCK_HANDLE;
	}

	/* Extract TimeStamp from server respose ( if any ) */
	char szTimeStamp[256] = "";

	if ((pChCfg->ulFlags & POPCHF_USE_APOP) == 0 ||
	    MscExtractServerTimeStamp(szRTXBuffer, szTimeStamp,
				      sizeof(szTimeStamp) - 1) == NULL) {
		/* Try clear text authentication */
		if (UPopDoClearTextAuth(hBSock, pszUsername, pszPassword, szRTXBuffer,
					sizeof(szRTXBuffer) - 1) < 0) {
			UPopCloseChannel(hBSock);
			return INVALID_BSOCK_HANDLE;
		}
	} else {
		/* Try APOP authentication first */
		int iApopAuthResult = UPopDoAPOPAuth(hBSock, pszUsername, pszPassword,
						     szTimeStamp, szRTXBuffer,
						     sizeof(szRTXBuffer) - 1);

		if (iApopAuthResult < 0) {
			if (iApopAuthResult != ERR_BAD_SERVER_RESPONSE ||
			    (pChCfg->ulFlags & POPCHF_FORCE_APOP)) {
				UPopCloseChannel(hBSock);
				return INVALID_BSOCK_HANDLE;
			}
			/* Try clear text authentication */
			if (UPopDoClearTextAuth(hBSock, pszUsername, pszPassword, szRTXBuffer,
						sizeof(szRTXBuffer) - 1) < 0) {
				UPopCloseChannel(hBSock);
				return INVALID_BSOCK_HANDLE;
			}
		}
	}

	return hBSock;
}

static int UPopCloseChannel(BSOCK_HANDLE hBSock, int iHardClose)
{
	if (!iHardClose) {
		/* Send QUIT and read result */
		char szRTXBuffer[2048] = "";

		if (UPopSendCommand(hBSock, "QUIT", szRTXBuffer, sizeof(szRTXBuffer) - 1,
				    iPOP3ClientTimeout) < 0) {
			BSckDetach(hBSock, 1);
			return ErrGetErrorCode();
		}
	}
	BSckDetach(hBSock, 1);

	return 0;
}

static int UPopGetMailboxStatus(BSOCK_HANDLE hBSock, int &iMsgCount, unsigned long &ulMailboxSize)
{
	char szRTXBuffer[2048] = "";

	if (UPopSendCommand(hBSock, "STAT", szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();
	if (sscanf(szRTXBuffer, "+OK %d %lu", &iMsgCount, &ulMailboxSize) != 2) {
		ErrSetErrorCode(ERR_INVALID_POP3_RESPONSE, szRTXBuffer);
		return ERR_INVALID_POP3_RESPONSE;
	}

	return 0;
}

static int UPopRetrieveMessage(BSOCK_HANDLE hBSock, int iMsgIndex, const char *pszFileName,
			       unsigned long *pulMsgSize)
{
	FILE *pMsgFile = fopen(pszFileName, "wb");

	if (pMsgFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE);
		return ERR_FILE_CREATE;
	}

	char szRTXBuffer[2048] = "";

	sprintf(szRTXBuffer, "RETR %d", iMsgIndex);
	if (UPopSendCommand(hBSock, szRTXBuffer, szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0) {
		fclose(pMsgFile);
		return ErrGetErrorCode();
	}

	int iLineLength = 0, iGotNL, iGotNLPrev = 1;
	unsigned long ulMsgSize = 0;

	for (;;) {
		if (BSckGetString(hBSock, szRTXBuffer, sizeof(szRTXBuffer) - 3,
				  iPOP3ClientTimeout, &iLineLength, &iGotNL) == NULL) {
			fclose(pMsgFile);

			ErrSetErrorCode(ERR_POP3_RETR_BROKEN);
			return ERR_POP3_RETR_BROKEN;
		}
		/* Check end of data condition */
		if (iGotNL && iGotNLPrev && strcmp(szRTXBuffer, ".") == 0)
			break;

		/* Correctly terminate the line */
		if (iGotNL)
			memcpy(szRTXBuffer + iLineLength, "\r\n", 3), iLineLength += 2;

		if (!fwrite(szRTXBuffer, iLineLength, 1, pMsgFile)) {
			fclose(pMsgFile);

			ErrSetErrorCode(ERR_FILE_WRITE, pszFileName);
			return ERR_FILE_WRITE;
		}
		ulMsgSize += (unsigned long) iLineLength;
		iGotNLPrev = iGotNL;
	}
	fclose(pMsgFile);
	if (pulMsgSize != NULL)
		*pulMsgSize = ulMsgSize;

	return 0;
}

static int UPopDeleteMessage(BSOCK_HANDLE hBSock, int iMsgIndex)
{
	char szRTXBuffer[2048] = "";

	sprintf(szRTXBuffer, "DELE %d", iMsgIndex);
	if (UPopSendCommand(hBSock, szRTXBuffer, szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopChanConfigAssign(void *pPrivate, const char *pszName, const char *pszValue)
{
	POP3ChannelCfg *pChCfg = (POP3ChannelCfg *) pPrivate;

	if (strcmp(pszName, "APOP") == 0)
		pChCfg->ulFlags |= POPCHF_USE_APOP;
	else if (strcmp(pszName, "FAPOP") == 0)
		pChCfg->ulFlags |= POPCHF_USE_APOP | POPCHF_FORCE_APOP;
	else if (strcmp(pszName, "STLS") == 0)
		pChCfg->ulFlags |= POPCHF_USE_STLS;
	else if (strcmp(pszName, "FSTLS") == 0)
		pChCfg->ulFlags |= POPCHF_USE_STLS | POPCHF_FORCE_STLS;
	else if (strcmp(pszName, "POP3S") == 0)
		pChCfg->ulFlags |= POPCHF_USE_POP3S;
	else if (strcmp(pszName, "Leave") == 0) {
		if (pszValue == NULL || atoi(pszValue) > 0)
			pChCfg->ulFlags |= POPCHF_LEAVE_MSGS;
	} else if (strcmp(pszName, "OutBind") == 0) {
		if (pszValue != NULL) {
			SysFree(pChCfg->pszIFace);
			pChCfg->pszIFace = SysStrDup(pszValue);
		}
	}

	return 0;
}

static int UPopSetChanConfig(const char *pszAuthType, POP3ChannelCfg *pChCfg)
{
	return MscParseOptions(pszAuthType, UPopChanConfigAssign, pChCfg);
}

static void UPopFreeChanConfig(POP3ChannelCfg *pChCfg)
{
	SysFree(pChCfg->pszIFace);
}

static POP3SyncMsg *UPopSChanMsgAlloc(int iMsgSeq, const char *pszMsgID)
{
	POP3SyncMsg *pSMsg;

	if ((pSMsg = (POP3SyncMsg *) SysAlloc(sizeof(POP3SyncMsg))) == NULL)
		return NULL;
	SYS_INIT_LIST_HEAD(&pSMsg->LLnk);
	HashInitNode(&pSMsg->HN);
	pSMsg->iMsgSeq = iMsgSeq;
	pSMsg->pszMsgID = (pszMsgID != NULL) ? SysStrDup(pszMsgID): NULL;

	return pSMsg;
}

static void UPopSChanMsgFree(POP3SyncMsg *pSMsg)
{
	SysFree(pSMsg->pszMsgID);
	SysFree(pSMsg);
}

/*
 * This function should be called only if the remote server supported UIDL
 * (AKA the "pszMsgID" member of the message MUST de different from NULL).
 */
static int UPopSChanFilterSeen(POP3SyncChannel *pPSChan)
{
	HASH_HANDLE hHash;
	SysListHead *pPos;
	POP3SyncMsg *pSMsg;
	HashNode *pHNode;

	if ((hHash = HashCreate(pPSChan->iMsgCount + 1)) == INVALID_HASH_HANDLE)
		return ErrGetErrorCode();
	for (pPos = SYS_LIST_FIRST(&pPSChan->SyncMList); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, &pPSChan->SyncMList)) {
		pSMsg = SYS_LIST_ENTRY(pPos, POP3SyncMsg, LLnk);

		DatumStrSet(&pSMsg->HN.Key, pSMsg->pszMsgID);
		if (HashAdd(hHash, &pSMsg->HN) < 0) {
			HashFree(hHash, NULL, NULL);
			return ErrGetErrorCode();
		}
	}

	/*
	 * Get the path of the UIDL seen-DB file ...
	 */
	char szMsgSyncFile[SYS_MAX_PATH];

	if (GwLkGetMsgSyncDbFile(pPSChan->pszRmtServer, pPSChan->pszRmtName,
				 szMsgSyncFile, sizeof(szMsgSyncFile) - 1) < 0) {
		HashFree(hHash, NULL, NULL);
		return ErrGetErrorCode();
	}

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szMsgSyncFile, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		HashFree(hHash, NULL, NULL);
		return ErrGetErrorCode();
	}

	FILE *pUFile = fopen(szMsgSyncFile, "rt");

	if (pUFile != NULL) {
		Datum Key;
		HashEnum HEnum;
		char szUIDL[512];

		while (MscFGets(szUIDL, sizeof(szUIDL) - 1, pUFile) != NULL) {
			DatumStrSet(&Key, szUIDL);
			if (HashGetFirst(hHash, &Key, &HEnum, &pHNode) == 0) {
				pSMsg = SYS_LIST_ENTRY(pHNode, POP3SyncMsg, HN);
				SYS_LIST_DEL(&pSMsg->LLnk);
				SYS_LIST_ADDT(&pSMsg->LLnk, &pPSChan->SeenMList);
				pPSChan->iMsgSync--;
				pPSChan->ulSizeSync -= pSMsg->ulSize;
			}
		}
		fclose(pUFile);
	}
	RLckUnlockSH(hResLock);
	HashFree(hHash, NULL, NULL);

	return 0;
}

static int UPopSChanWriteSeenDB(POP3SyncChannel *pPSChan)
{
	/*
	 * Get the path of the UIDL seen-DB file ...
	 */
	char szMsgSyncFile[SYS_MAX_PATH];

	if (GwLkGetMsgSyncDbFile(pPSChan->pszRmtServer, pPSChan->pszRmtName,
				 szMsgSyncFile, sizeof(szMsgSyncFile) - 1) < 0)
		return ErrGetErrorCode();

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMsgSyncFile, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pUFile = fopen(szMsgSyncFile, "wt");

	if (pUFile == NULL) {
		RLckUnlockEX(hResLock);
		ErrSetErrorCode(ERR_FILE_CREATE, szMsgSyncFile);
		return ERR_FILE_CREATE;
	}

	SysListHead *pPos;
	POP3SyncMsg *pSMsg;

	for (pPos = SYS_LIST_FIRST(&pPSChan->SeenMList); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, &pPSChan->SeenMList)) {
		pSMsg = SYS_LIST_ENTRY(pPos, POP3SyncMsg, LLnk);

		fprintf(pUFile, "%s\n", pSMsg->pszMsgID);
	}
	fclose(pUFile);
	RLckUnlockEX(hResLock);

	return 0;
}

static int UPopSChanFillStatus(POP3SyncChannel *pPSChan)
{
	int iLineLength, iMsgID, iHasUIDLs = 0;
	char *pszMsgID, *pszMsgUIDL, *pszMsgSize;
	POP3SyncMsg *pSMsg;
	char szRTXBuffer[2048] = "UIDL";

	if (UPopGetMailboxStatus(pPSChan->hBSock, pPSChan->iMsgCount,
				 pPSChan->ulMailboxSize) < 0)
		return ErrGetErrorCode();
	if ((pPSChan->ppSMsg = (POP3SyncMsg **)
	     SysAlloc((pPSChan->iMsgCount + 1) * sizeof(POP3SyncMsg *))) == NULL)
		return ErrGetErrorCode();
	/*
	 * Try to send UIDL, so that we can implement the leave-message-on-server
	 * feature, if requested ...
	 */
	if (UPopSendCommand(pPSChan->hBSock, szRTXBuffer, szRTXBuffer,
			    sizeof(szRTXBuffer) - 1, iPOP3ClientTimeout) == 0) {
		for (;;) {
			if (BSckGetString(pPSChan->hBSock, szRTXBuffer, sizeof(szRTXBuffer) - 1,
					  iPOP3ClientTimeout, &iLineLength) == NULL)
				return ErrGetErrorCode();
			/* Check end of data condition */
			if (strcmp(szRTXBuffer, ".") == 0)
				break;
			pszMsgID = szRTXBuffer;
			if (!isdigit(*pszMsgID) ||
			    (pszMsgUIDL = strchr(pszMsgID, ' ')) == NULL) {
				ErrSetErrorCode(ERR_INVALID_POP3_RESPONSE, szRTXBuffer);
				return ERR_INVALID_POP3_RESPONSE;
			}
			*pszMsgUIDL++ = '\0';
			if ((pSMsg = UPopSChanMsgAlloc(iMsgID = atoi(pszMsgID),
						       pszMsgUIDL)) == NULL)
				return ErrGetErrorCode();
			if (iMsgID >= 1 && iMsgID <= pPSChan->iMsgCount)
				pPSChan->ppSMsg[iMsgID - 1] = pSMsg;
			SYS_LIST_ADDT(&pSMsg->LLnk, &pPSChan->SyncMList);
			pPSChan->iMsgSync++;
		}
		iHasUIDLs++;
	} else {
		/*
		 * If UIDL is failing, we fill up the list by sequentials only.
		 */
		for (int i = 0; i < pPSChan->iMsgCount; i++) {
			if ((pSMsg = UPopSChanMsgAlloc(i + 1, NULL)) == NULL)
				return ErrGetErrorCode();
			pPSChan->ppSMsg[i] = pSMsg;
			SYS_LIST_ADDT(&pSMsg->LLnk, &pPSChan->SyncMList);
			pPSChan->iMsgSync++;
		}
	}

	/*
	 * Retrieve message list information ...
	 */
	strcpy(szRTXBuffer, "LIST");
	if (UPopSendCommand(pPSChan->hBSock, szRTXBuffer, szRTXBuffer,
			    sizeof(szRTXBuffer) - 1, iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();
	for (;;) {
		if (BSckGetString(pPSChan->hBSock, szRTXBuffer, sizeof(szRTXBuffer) - 1,
				  iPOP3ClientTimeout, &iLineLength) == NULL)
			return ErrGetErrorCode();
		/* Check end of data condition */
		if (strcmp(szRTXBuffer, ".") == 0)
			break;
		pszMsgID = szRTXBuffer;
		if (!isdigit(*pszMsgID) ||
		    (pszMsgSize = strchr(pszMsgID, ' ')) == NULL) {
			ErrSetErrorCode(ERR_INVALID_POP3_RESPONSE, szRTXBuffer);
			return ERR_INVALID_POP3_RESPONSE;
		}
		*pszMsgSize++ = '\0';
		if ((iMsgID = atoi(pszMsgID)) >= 1 && iMsgID <= pPSChan->iMsgCount &&
		    (pSMsg = pPSChan->ppSMsg[iMsgID - 1]) != NULL) {
			pSMsg->ulSize = atol(pszMsgSize);
			pPSChan->ulSizeSync += pSMsg->ulSize;
		}
	}

	/*
	 * If we need to fetch only the unseen ones, we need to rely to the
	 * UIDL database and create the download list accordingly. We do this
	 * only if the remote server supports UIDL of course.
	 */
	if (pPSChan->ChCfg.ulFlags & POPCHF_LEAVE_MSGS) {
		if (!iHasUIDLs) {
			ErrSetErrorCode(ERR_NOREMOTE_POP3_UIDL, pPSChan->pszRmtServer);
			return ERR_NOREMOTE_POP3_UIDL;
		}
		if (UPopSChanFilterSeen(pPSChan) < 0)
			return ErrGetErrorCode();
	}

	return 0;
}

static POP3SyncChannel *UPopSChanCreate(const char *pszRmtServer, const char *pszRmtName,
					const char *pszRmtPassword, const char *pszSyncCfg)
{
	POP3SyncChannel *pPSChan;

	if ((pPSChan = (POP3SyncChannel *) SysAlloc(sizeof(POP3SyncChannel))) == NULL)
		return NULL;
	SYS_INIT_LIST_HEAD(&pPSChan->SyncMList);
	SYS_INIT_LIST_HEAD(&pPSChan->SeenMList);
	if (UPopSetChanConfig(pszSyncCfg, &pPSChan->ChCfg) < 0) {
		SysFree(pPSChan);
		return NULL;
	}
	/* Connection to POP3 server */
	if ((pPSChan->hBSock = UPopCreateChannel(pszRmtServer, pszRmtName, pszRmtPassword,
						 &pPSChan->ChCfg)) == INVALID_BSOCK_HANDLE) {
		UPopFreeChanConfig(&pPSChan->ChCfg);
		SysFree(pPSChan);
		return NULL;
	}
	pPSChan->pszRmtServer = SysStrDup(pszRmtServer);
	pPSChan->pszRmtName = SysStrDup(pszRmtName);
	if (UPopSChanFillStatus(pPSChan) < 0) {
		UPopSChanFree(pPSChan);
		return NULL;
	}

	return pPSChan;
}

static void UPopSChanFree(POP3SyncChannel *pPSChan)
{
	SysListHead *pPos;
	POP3SyncMsg *pSMsg;

	if (pPSChan->ChCfg.ulFlags & POPCHF_LEAVE_MSGS)
		UPopSChanWriteSeenDB(pPSChan);
	while ((pPos = SYS_LIST_FIRST(&pPSChan->SyncMList)) != NULL) {
		pSMsg = SYS_LIST_ENTRY(pPos, POP3SyncMsg, LLnk);
		SYS_LIST_DEL(&pSMsg->LLnk);
		UPopSChanMsgFree(pSMsg);
	}
	while ((pPos = SYS_LIST_FIRST(&pPSChan->SeenMList)) != NULL) {
		pSMsg = SYS_LIST_ENTRY(pPos, POP3SyncMsg, LLnk);
		SYS_LIST_DEL(&pSMsg->LLnk);
		UPopSChanMsgFree(pSMsg);
	}
	SysFree(pPSChan->ppSMsg);
	UPopCloseChannel(pPSChan->hBSock);
	UPopFreeChanConfig(&pPSChan->ChCfg);
	SysFree(pPSChan->pszRmtServer);
	SysFree(pPSChan->pszRmtName);
	SysFree(pPSChan);
}

int UPopSyncRemoteLink(const char *pszSyncAddr, const char *pszRmtServer,
		       const char *pszRmtName, const char *pszRmtPassword,
		       MailSyncReport *pSRep, const char *pszSyncCfg,
		       const char *pszFetchHdrTags, const char *pszErrorAccount)
{
	POP3SyncChannel *pPSChan;

	if ((pPSChan = UPopSChanCreate(pszRmtServer, pszRmtName, pszRmtPassword,
				       pszSyncCfg)) == NULL)
		return ErrGetErrorCode();

	/* Initialize the report structure with current mailbox informations */
	pSRep->iMsgSync = 0;
	pSRep->iMsgErr = pPSChan->iMsgSync;
	pSRep->ulSizeSync = 0;
	pSRep->ulSizeErr = pPSChan->ulSizeSync;

	SysListHead *pPos;
	POP3SyncMsg *pSMsg;
	char szMsgFileName[SYS_MAX_PATH] = "";

	SysGetTmpFile(szMsgFileName);

	for (pPos = SYS_LIST_FIRST(&pPSChan->SyncMList); pPos != NULL;) {
		pSMsg = SYS_LIST_ENTRY(pPos, POP3SyncMsg, LLnk);
		pPos = SYS_LIST_NEXT(pPos, &pPSChan->SyncMList);

		/* Get the message */
		if (UPopRetrieveMessage(pPSChan->hBSock, pSMsg->iMsgSeq, szMsgFileName,
					NULL) < 0) {
			ErrorPush();
			CheckRemoveFile(szMsgFileName);
			UPopSChanFree(pPSChan);
			return ErrorPop();
		}
		/* Spool deliver fetched message */
		if (USmlDeliverFetchedMsg(pszSyncAddr, pszFetchHdrTags,
					  szMsgFileName) < 0) {
			/* If there's an error ( catch errors ) account try to deliver to this one */
			if (pszErrorAccount != NULL &&
			    USmlDeliverFetchedMsg(pszErrorAccount, NULL,
						  szMsgFileName) == 0) {
				/* Delete remote message only if successfully delivered */
				if ((pPSChan->ChCfg.ulFlags & POPCHF_LEAVE_MSGS) == 0)
					UPopDeleteMessage(pPSChan->hBSock, pSMsg->iMsgSeq);

				/* Add in Seen */
				SYS_LIST_DEL(&pSMsg->LLnk);
				SYS_LIST_ADDT(&pSMsg->LLnk, &pPSChan->SeenMList);
			}
		} else {
			/* Delete remote message only if successfully delivered */
			if ((pPSChan->ChCfg.ulFlags & POPCHF_LEAVE_MSGS) == 0)
				UPopDeleteMessage(pPSChan->hBSock, pSMsg->iMsgSeq);

			/* Add in Seen */
			SYS_LIST_DEL(&pSMsg->LLnk);
			SYS_LIST_ADDT(&pSMsg->LLnk, &pPSChan->SeenMList);

			pSRep->iMsgSync++;
			pSRep->ulSizeSync += pSMsg->ulSize;
		}
		SysRemove(szMsgFileName);

		if (SvrInShutdown()) {
			UPopSChanFree(pPSChan);

			ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
			return ERR_SERVER_SHUTDOWN;
		}
	}
	/* Disconnect from POP3 server */
	UPopSChanFree(pPSChan);

	pSRep->iMsgErr -= Min(pSRep->iMsgErr, pSRep->iMsgSync);
	pSRep->ulSizeErr -= Min(pSRep->ulSizeErr, pSRep->ulSizeSync);

	return 0;
}

static int UPopGetIpLogFilePath(UserInfo *pUI, char *pszFilePath, int iMaxPath)
{
	UsrGetUserPath(pUI, pszFilePath, iMaxPath, 1);
	StrNCat(pszFilePath, POP3_IP_LOGFILE, iMaxPath);

	return 0;
}

int UPopUserIpCheck(UserInfo *pUI, SYS_INET_ADDR const *pPeerInfo, unsigned int uExpireTime)
{
	char szIpFilePath[SYS_MAX_PATH] = "";

	UPopGetIpLogFilePath(pUI, szIpFilePath, sizeof(szIpFilePath));

	/* Load IP log file info and do expire check */
	SYS_FILE_INFO FI;

	if (SysGetFileInfo(szIpFilePath, FI) < 0 ||
	    (time_t) (FI.tMod + uExpireTime) < time(NULL)) {
		ErrSetErrorCode(ERR_NO_POP3_IP);
		return ERR_NO_POP3_IP;
	}
	/* Load IP from file */
	FILE *pIpFile = fopen(szIpFilePath, "rt");

	if (pIpFile == NULL) {
		ErrSetErrorCode(ERR_NO_POP3_IP);
		return ERR_NO_POP3_IP;
	}

	char szIP[128] = "";

	MscFGets(szIP, sizeof(szIP) - 1, pIpFile);

	fclose(pIpFile);

	/* Do IP matching */
	SYS_INET_ADDR CurrAddr;

	if (SysGetHostByName(szIP, SysGetAddrFamily(*pPeerInfo), CurrAddr) < 0 ||
	    !SysInetAddrMatch(*pPeerInfo, CurrAddr)) {
		ErrSetErrorCode(ERR_NO_POP3_IP);
		return ERR_NO_POP3_IP;
	}

	return 0;
}

int UPopGetLastLoginInfo(UserInfo *pUI, PopLastLoginInfo *pInfo)
{
	SYS_FILE_INFO FI;
	char szIpFilePath[SYS_MAX_PATH] = "";

	UPopGetIpLogFilePath(pUI, szIpFilePath, sizeof(szIpFilePath));
	if (SysGetFileInfo(szIpFilePath, FI) < 0)
		return ErrGetErrorCode();

	pInfo->LTime = FI.tMod;

	/* Load IP from file */
	FILE *pIpFile = fopen(szIpFilePath, "rt");

	if (pIpFile == NULL) {
		ErrSetErrorCode(ERR_NO_POP3_IP);
		return ERR_NO_POP3_IP;
	}

	char szIP[128] = "";

	MscFGets(szIP, sizeof(szIP) - 1, pIpFile);
	fclose(pIpFile);

	return SysGetHostByName(szIP, -1, pInfo->Address);
}

