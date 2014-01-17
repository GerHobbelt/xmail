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
	SYS_OFF_T llSize;
};

struct POP3SyncChannel {
	char *pszRmtServer;
	char *pszRmtName;
	POP3ChannelCfg ChCfg;
	BSOCK_HANDLE hBSock;
	int iMsgCount;
	SYS_OFF_T llMailboxSize;
	int iMsgSync;
	SYS_OFF_T llSizeSync;
	POP3SyncMsg **ppSMsg;
	SysListHead SyncMList;
	SysListHead SeenMList;
};

struct POP3MsgData {
	SysListHead LLnk;
	char szMsgName[SYS_MAX_PATH];
	SYS_OFF_T llMsgSize;
	unsigned long ulFlags;
};

struct POP3SessionData {
	SYS_INET_ADDR PeerInfo;
	UserInfo *pUI;
	SysListHead MessageList;
	POP3MsgData **ppMsgArray;
	int iMsgListed;
	int iMsgCount;
	SYS_OFF_T llMBSize;
	int iLastAccessed;
	int iTimeout;
};

/*
static int UPopMailFileNameFilter(const char *pszFileName);
static int UPopFillMessageList(const char *pszBasePath, const char *pszSubPath,
			       HSLIST &hMessageList, int &iMsgCount, unsigned long &ulMBSize);
static int UPopBuildMessageList(UserInfo *pUI, SysListHead *pMsgList,
				int *piMsgCount = NULL, SYS_OFF_T *pllMBSize = NULL);
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
*/
static int UPopCloseChannel(BSOCK_HANDLE hBSock, int iHardClose = 0);
/*
static int UPopGetMailboxStatus(BSOCK_HANDLE hBSock, int &iMsgCount,
				unsigned long &ulMailboxSize);
static int UPopRetrieveMessage(BSOCK_HANDLE hBSock, int iMsgIndex, char const *pszFileName,
			       SYS_OFF_T *pllMsgSize);
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
*/

static int UPopMailFileNameFilter(char const *pszFileName)
{
	return (*pszFileName != '.') ? 1: 0;
}

int UPopGetMailboxSize(UserInfo *pUI, SYS_OFF_T &llMBSize, unsigned long &ulNumMessages)
{
	char szMBPath[SYS_MAX_PATH];

	UsrGetMailboxPath(pUI, szMBPath, sizeof(szMBPath), 0);

	char szResLock[SYS_MAX_PATH];
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
		SYS_OFF_T llMaxMBSize = Sys_atoi64(pszMaxMBSize) * 1024;

		SysFree(pszMaxMBSize);
		if (llMaxMBSize != 0 && (llMBSize + llProbeSize >= llMaxMBSize)) {
			ErrSetErrorCode(ERR_MAILBOX_SIZE);
			return ERR_MAILBOX_SIZE;
		}
		if (pllAvailSpace != NULL)
			*pllAvailSpace = (llMaxMBSize != 0 ? llMaxMBSize - llMBSize:
					  MaxSignedType(SYS_OFF_T));
	} else if (pllAvailSpace != NULL)
		*pllAvailSpace = MaxSignedType(SYS_OFF_T);

	return 0;
}

static int UPopFillMessageList(char const *pszBasePath, char const *pszSubPath,
			       SysListHead *pMsgList, int &iMsgCount, SYS_OFF_T &llMBSize)
{
	SYS_HANDLE hFind;
	char szScanPath[SYS_MAX_PATH], szFileName[SYS_MAX_PATH];

	if (pszSubPath == NULL)
		StrSNCpy(szScanPath, pszBasePath);
	else
		SysSNPrintf(szScanPath, sizeof(szScanPath) - 1, "%s" SYS_SLASH_STR "%s",
			    pszBasePath, pszSubPath);

	if ((hFind = SysFirstFile(szScanPath, szFileName,
				  sizeof(szFileName))) != SYS_INVALID_HANDLE) {
		do {
			POP3MsgData *pPOPMD;

			if (SysIsDirectory(hFind) ||
			    !UPopMailFileNameFilter(szFileName))
				continue;
			if ((pPOPMD = (POP3MsgData *)
			     SysAlloc(sizeof(POP3MsgData))) == NULL) {
				ErrorPush();
				SysFindClose(hFind);
				return ErrorPop();
			}
			if (pszSubPath == NULL)
				StrSNCpy(pPOPMD->szMsgName, szFileName);
			else
				SysSNPrintf(pPOPMD->szMsgName, sizeof(pPOPMD->szMsgName) - 1,
					    "%s" SYS_SLASH_STR "%s", pszSubPath, szFileName);

			pPOPMD->llMsgSize = SysGetSize(hFind);
			pPOPMD->ulFlags = 0;

			/* Insert entry in message list */
			SYS_LIST_ADDT(&pPOPMD->LLnk, pMsgList);

			/* Update mailbox information */
			llMBSize += pPOPMD->llMsgSize;
			++iMsgCount;
		} while (SysNextFile(hFind, szFileName, sizeof(szFileName)));
		SysFindClose(hFind);
	}

	return 0;
}

static void UPopFreeMsgData(POP3MsgData *pPOPMD)
{
	SysFree(pPOPMD);
}

static void UPopFreeMessageList(SysListHead *pMsgList)
{
	SysListHead *pPos;
	POP3MsgData *pPOPMD;

	while ((pPos = SYS_LIST_FIRST(pMsgList)) != NULL) {
		pPOPMD = SYS_LIST_ENTRY(pPos, POP3MsgData, LLnk);
		SYS_LIST_DEL(pPos);
		UPopFreeMsgData(pPOPMD);
	}
}

static int UPopBuildMessageList(UserInfo *pUI, SysListHead *pMsgList,
				int *piMsgCount, SYS_OFF_T *pllMBSize)
{
	char szMBPath[SYS_MAX_PATH];

	UsrGetMailboxPath(pUI, szMBPath, sizeof(szMBPath), 0);

	char szResLock[SYS_MAX_PATH];
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMBPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	SYS_INIT_LIST_HEAD(pMsgList);

	int iMsgCount = 0;
	SYS_OFF_T llMBSize = 0;

	if (iMailboxType == XMAIL_MAILBOX) {
		if (UPopFillMessageList(szMBPath, NULL, pMsgList, iMsgCount,
					llMBSize) < 0) {
			ErrorPush();
			UPopFreeMessageList(pMsgList);
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
	} else {
		int iScanCur = UsrGetUserInfoVarInt(pUI, "Pop3ScanCur", 0);

		if (UPopFillMessageList(szMBPath, "new", pMsgList, iMsgCount,
					llMBSize) < 0 ||
		    (iScanCur > 0 &&
		     UPopFillMessageList(szMBPath, "cur", pMsgList, iMsgCount,
					 llMBSize) < 0)) {
			ErrorPush();
			UPopFreeMessageList(pMsgList);
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
	}
	RLckUnlockEX(hResLock);
	if (piMsgCount != NULL)
		*piMsgCount = iMsgCount;
	if (pllMBSize != NULL)
		*pllMBSize = llMBSize;

	return 0;
}

static SYS_OFF_T UPopMessagesSize(SysListHead *pMsgList)
{
	SYS_OFF_T llMBSize = 0;
	SysListHead *pPos;
	POP3MsgData *pPOPMD;

	SYS_LIST_FOR_EACH(pPos, pMsgList) {
		pPOPMD = SYS_LIST_ENTRY(pPos, POP3MsgData, LLnk);
		llMBSize += pPOPMD->llMsgSize;
	}

	return llMBSize;
}

static POP3MsgData *UPopMessageFromIndex(POP3SessionData *pPOPSD, int iMsgIndex)
{
	return (iMsgIndex >= 0 && iMsgIndex < pPOPSD->iMsgListed) ?
		pPOPSD->ppMsgArray[iMsgIndex]: NULL;

}

int UPopAuthenticateAPOP(char const *pszDomain, char const *pszUsrName,
			 char const *pszTimeStamp, char const *pszDigest)
{
	UserInfo *pUI = UsrGetUserByName(pszDomain, pszUsrName);

	if (pUI == NULL)
		return ErrGetErrorCode();

	int iAuthResult = MscMD5Authenticate(pUI->pszPassword, pszTimeStamp,
					     pszDigest);

	UsrFreeUserInfo(pUI);

	return iAuthResult;
}

static int UPopCheckPeerIP(UserInfo *pUI, SYS_INET_ADDR const &PeerInfo)
{
	char szIPMapFile[SYS_MAX_PATH];

	UsrGetUserPath(pUI, szIPMapFile, sizeof(szIPMapFile), 1);
	StrNCat(szIPMapFile, UPOP_IPMAP_FILE, sizeof(szIPMapFile));

	if (SysExistFile(szIPMapFile) && MscCheckAllowedIP(szIPMapFile, PeerInfo, true) < 0)
		return ErrGetErrorCode();

	return 0;
}

POP3_HANDLE UPopBuildSession(char const *pszDomain, char const *pszUsrName,
			     char const *pszUsrPass, SYS_INET_ADDR const *pPeerInfo)
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
		UsrFreeUserInfo(pUI);
		ErrSetErrorCode(ERR_INVALID_PASSWORD);
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

	if (UPopBuildMessageList(pUI, &pPOPSD->MessageList, &pPOPSD->iMsgCount,
				 &pPOPSD->llMBSize) < 0) {
		SysFree(pPOPSD);
		UsrPOP3Unlock(pUI);
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}

	pPOPSD->iMsgListed = pPOPSD->iMsgCount;

	/* Build lookup array */
	if ((pPOPSD->ppMsgArray = (POP3MsgData **)
	     SysAlloc((pPOPSD->iMsgCount + 1) * sizeof(POP3MsgData *))) == NULL) {
		UPopFreeMessageList(&pPOPSD->MessageList);
		SysFree(pPOPSD);
		UsrPOP3Unlock(pUI);
		UsrFreeUserInfo(pUI);
		return INVALID_POP3_HANDLE;
	}

	int i = 0;
	SysListHead *pPos;
	POP3MsgData *pPOPMD;

	SYS_LIST_FOR_EACH(pPos, &pPOPSD->MessageList) {
		pPOPMD = SYS_LIST_ENTRY(pPos, POP3MsgData, LLnk);
		pPOPSD->ppMsgArray[i++] = pPOPMD;
	}

	return (POP3_HANDLE) pPOPSD;
}

static int UPopUpdateMailbox(POP3SessionData *pPOPSD)
{
	SysListHead *pPos;
	POP3MsgData *pPOPMD;
	char szMBPath[SYS_MAX_PATH];

	UsrGetMailboxPath(pPOPSD->pUI, szMBPath, sizeof(szMBPath), 0);

	char szResLock[SYS_MAX_PATH];
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szMBPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();
	SYS_LIST_FOR_EACH(pPos, &pPOPSD->MessageList) {
		pPOPMD = SYS_LIST_ENTRY(pPos, POP3MsgData, LLnk);
		if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
			char szMsgPath[SYS_MAX_PATH];

			SysSNPrintf(szMsgPath, sizeof(szMsgPath) - 1, "%s" SYS_SLASH_STR "%s",
				    szMBPath, pPOPMD->szMsgName);
			SysRemove(szMsgPath);
		}
	}
	RLckUnlockEX(hResLock);

	return 0;
}

void UPopReleaseSession(POP3_HANDLE hPOPSession, int iUpdate)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	if (iUpdate)
		UPopUpdateMailbox(pPOPSD);
	UsrPOP3Unlock(pPOPSD->pUI);
	UsrFreeUserInfo(pPOPSD->pUI);
	UPopFreeMessageList(&pPOPSD->MessageList);
	SysFree(pPOPSD->ppMsgArray);
	SysFree(pPOPSD);
}

char *UPopGetUserInfoVar(POP3_HANDLE hPOPSession, char const *pszName,
			 char const *pszDefault)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return UsrGetUserInfoVar(pPOPSD->pUI, pszName, pszDefault);
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

SYS_OFF_T UPopGetSessionMBSize(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return pPOPSD->llMBSize;
}

int UPopGetSessionLastAccessed(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;

	return pPOPSD->iLastAccessed;
}

int UPopGetMessageSize(POP3_HANDLE hPOPSession, int iMsgIndex,
		       SYS_OFF_T &llMessageSize)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	POP3MsgData *pPOPMD;

	llMessageSize = 0;
	if ((pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1)) == NULL) {
		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}
	llMessageSize = pPOPMD->llMsgSize;

	return 0;
}

int UPopGetMessageUIDL(POP3_HANDLE hPOPSession, int iMsgIndex, char *pszMessageUIDL,
		       int iSize)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	POP3MsgData *pPOPMD;
	char const *pszFileName;

	if ((pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1)) == NULL) {
		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}
	/* Extract message UIDL from mailbox file path */

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

	pPOPSD->llMBSize -= pPOPMD->llMsgSize;
	--pPOPSD->iMsgCount;
	pPOPMD->ulFlags |= POPF_MSG_DELETED;
	pPOPSD->iLastAccessed = iMsgIndex;

	return 0;
}

int UPopResetSession(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	SysListHead *pPos;
	POP3MsgData *pPOPMD;

	SYS_LIST_FOR_EACH(pPos, &pPOPSD->MessageList) {
		pPOPMD = SYS_LIST_ENTRY(pPos, POP3MsgData, LLnk);
		if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
			pPOPSD->llMBSize += pPOPMD->llMsgSize;

			++pPOPSD->iMsgCount;
			pPOPMD->ulFlags &= ~(POPF_MSG_DELETED | POPF_MSG_SENT | POPF_MSG_TOP);
		}
	}
	pPOPSD->iLastAccessed = 0;

	return 0;
}

int UPopSendErrorResponse(BSOCK_HANDLE hBSock, int iErrorCode, int iTimeout)
{
	char const *pszError = ErrGetErrorString(iErrorCode);
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

static int UPopSendMessageFile(BSOCK_HANDLE hBSock, char const *pszFilePath,
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
	POP3MsgData *pPOPMD;
	char szMsgFilePath[SYS_MAX_PATH], szResponse[256];

	if ((pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1)) == NULL) {
		UPopSendErrorResponse(hBSock, ERR_MSG_NOT_IN_RANGE, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		UPopSendErrorResponse(hBSock, ERR_MSG_DELETED, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}

	UsrGetMailboxPath(pPOPSD->pUI, szMsgFilePath, sizeof(szMsgFilePath), 1);
	StrNCat(szMsgFilePath, pPOPMD->szMsgName, sizeof(szMsgFilePath));

	SysSNPrintf(szResponse, sizeof(szResponse) - 1,
		    "+OK " SYS_OFFT_FMT " bytes", pPOPMD->llMsgSize);
	if (BSckSendString(hBSock, szResponse, pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	if (pPOPMD->llMsgSize > 0 &&
	    UPopSendMessageFile(hBSock, szMsgFilePath,  pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	if (BSckSendString(hBSock, ".", pPOPSD->iTimeout) < 0)
		return ErrGetErrorCode();

	pPOPSD->iLastAccessed = iMsgIndex;
	pPOPMD->ulFlags |= POPF_MSG_SENT;

	return 0;
}

int UPopSessionTopMsg(POP3_HANDLE hPOPSession, int iMsgIndex, int iNumLines,
		      BSOCK_HANDLE hBSock)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	POP3MsgData *pPOPMD;
	FILE *pMsgFile;
	char szMsgFilePath[SYS_MAX_PATH], szResponse[256];

	if ((pPOPMD = UPopMessageFromIndex(pPOPSD, iMsgIndex - 1)) == NULL) {
		UPopSendErrorResponse(hBSock, ERR_MSG_NOT_IN_RANGE, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_NOT_IN_RANGE);
		return ERR_MSG_NOT_IN_RANGE;
	}
	if (pPOPMD->ulFlags & POPF_MSG_DELETED) {
		UPopSendErrorResponse(hBSock, ERR_MSG_DELETED, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_MSG_DELETED);
		return ERR_MSG_DELETED;
	}

	UsrGetMailboxPath(pPOPSD->pUI, szMsgFilePath, sizeof(szMsgFilePath), 1);
	StrNCat(szMsgFilePath, pPOPMD->szMsgName, sizeof(szMsgFilePath));

	pMsgFile = fopen(szMsgFilePath, "rb");

	if (pMsgFile == NULL) {
		UPopSendErrorResponse(hBSock, ERR_FILE_OPEN, pPOPSD->iTimeout);

		ErrSetErrorCode(ERR_FILE_OPEN, szMsgFilePath); /* [i_a] */
		return ERR_FILE_OPEN;
	}

	SysSNPrintf(szResponse, sizeof(szResponse) - 1,
		    "+OK message is " SYS_OFFT_FMT " bytes", pPOPMD->llMsgSize);
	if (BSckSendString(hBSock, szResponse, pPOPSD->iTimeout) < 0) {
		fclose(pMsgFile);
		return ErrGetErrorCode();
	}

	bool bSendingMsg = false;
	char szMsgLine[2048];

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

static int UPopGetIpLogFilePath(UserInfo *pUI, char *pszFilePath, int iMaxPath)
{
	UsrGetUserPath(pUI, pszFilePath, iMaxPath, 1);
	StrNCat(pszFilePath, POP3_IP_LOGFILE, iMaxPath);

	return 0;
}

int UPopSaveUserIP(POP3_HANDLE hPOPSession)
{
	POP3SessionData *pPOPSD = (POP3SessionData *) hPOPSession;
	char szIpFilePath[SYS_MAX_PATH];

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

static int UPopCheckResponse(char const *pszResponse, char *pszMessage = NULL)
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

static int UPopGetResponse(BSOCK_HANDLE hBSock, char *pszResponse, int iMaxChars,
			   int iTimeout)
{
	if (BSckGetString(hBSock, pszResponse, iMaxChars, iTimeout) == NULL)
		return ErrGetErrorCode();

	return UPopCheckResponse(pszResponse);
}

static char *UPopExtractServerTimeStamp(char const *pszResponse, char *pszTimeStamp,
					int iMaxTimeStamp)
{
	char const *pszStartTS = strchr(pszResponse, '<');
	char const *pszEndTS = strchr(pszResponse, '>');

	if (pszStartTS == NULL || pszEndTS == NULL || pszEndTS < pszStartTS)
		return NULL;

	int iSize = (int) (pszEndTS - pszStartTS) + 1;

	iSize = Min(iSize, iMaxTimeStamp - 1);

	strncpy(pszTimeStamp, pszStartTS, iSize);
	pszTimeStamp[iSize] = '\0';

	return pszTimeStamp;
}

static int UPopSendCommand(BSOCK_HANDLE hBSock, char const *pszCommand, char *pszResponse,
			   int iMaxChars, int iTimeout)
{
	if (BSckSendString(hBSock, pszCommand, iTimeout) <= 0)
		return ErrGetErrorCode();
	if (UPopGetResponse(hBSock, pszResponse, iMaxChars, iTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopDoClearTextAuth(BSOCK_HANDLE hBSock, char const *pszUsername,
			       char const *pszPassword, char *pszRespBuffer,
			       int iMaxRespChars)
{
	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "USER %s", pszUsername);
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "PASS %s", pszPassword);
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopDoAPOPAuth(BSOCK_HANDLE hBSock, char const *pszUsername,
			  char const *pszPassword, char const *pszTimeStamp,
			  char *pszRespBuffer, int iMaxRespChars)
{
	char *pszHash;
	char szMD5[128];

	if ((pszHash = StrSprint("%s%s", pszTimeStamp, pszPassword)) == NULL)
		return ErrGetErrorCode();
	do_md5_string(pszHash, strlen(pszHash), szMD5);
	SysFree(pszHash);

	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "APOP %s %s", pszUsername, szMD5);
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopSwitchToTLS(BSOCK_HANDLE hBSock, char const *pszServer,
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

static int UPopInitiateTLS(BSOCK_HANDLE hBSock, char const *pszServer, char *pszRespBuffer,
			   int iMaxRespChars, POP3ChannelCfg const *pChCfg)
{
	SysSNPrintf(pszRespBuffer, iMaxRespChars - 1, "STLS");
	if (UPopSendCommand(hBSock, pszRespBuffer, pszRespBuffer, iMaxRespChars,
			    iPOP3ClientTimeout) < 0)
		return (pChCfg->ulFlags & POPCHF_FORCE_STLS) ? ErrGetErrorCode(): 0;

	return UPopSwitchToTLS(hBSock, pszServer, pChCfg);
}

static BSOCK_HANDLE UPopCreateChannel(char const *pszServer, char const *pszUsername,
				      char const *pszPassword, POP3ChannelCfg const *pChCfg)
{
	SYS_INET_ADDR SvrAddr;

	ZeroData(SvrAddr); /* [i_a] */
	
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

		ZeroData(BndAddr); /* [i_a] */
	
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
	char szRTXBuffer[2048];

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

	/* Extract TimeStamp from server respose (if any) */
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
		int iResult = UPopDoAPOPAuth(hBSock, pszUsername, pszPassword,
					     szTimeStamp, szRTXBuffer,
					     sizeof(szRTXBuffer) - 1);

		if (iResult < 0) {
			if (iResult != ERR_BAD_SERVER_RESPONSE ||
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
		char szRTXBuffer[2048];

		if (UPopSendCommand(hBSock, "QUIT", szRTXBuffer, sizeof(szRTXBuffer) - 1,
				    iPOP3ClientTimeout) < 0) {
			BSckDetach(hBSock, 1);
			return ErrGetErrorCode();
		}
	}
	BSckDetach(hBSock, 1);

	return 0;
}

static int UPopGetMailboxStatus(BSOCK_HANDLE hBSock, int &iMsgCount,
				SYS_OFF_T &llMailboxSize)
{
	char szRTXBuffer[2048];

	if (UPopSendCommand(hBSock, "STAT", szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();
	if (sscanf(szRTXBuffer, "+OK %d " SYS_OFFT_FMT, &iMsgCount,
		   &llMailboxSize) != 2) {
		ErrSetErrorCode(ERR_INVALID_POP3_RESPONSE, szRTXBuffer);
		return ERR_INVALID_POP3_RESPONSE;
	}

	return 0;
}

static int UPopRetrieveMessage(BSOCK_HANDLE hBSock, int iMsgIndex, char const *pszFileName,
			       SYS_OFF_T *pllMsgSize)
{
	FILE *pMsgFile = fopen(pszFileName, "wb");

	if (pMsgFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszFileName); /* [i_a] */
		return ERR_FILE_CREATE;
	}

	char szRTXBuffer[2048];

	sprintf(szRTXBuffer, "RETR %d", iMsgIndex);
	if (UPopSendCommand(hBSock, szRTXBuffer, szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0) {
		fclose(pMsgFile);
		return ErrGetErrorCode();
	}

	int iLineLength = 0, iGotNL, iGotNLPrev = 1;
	SYS_OFF_T llMsgSize = 0;

	for (;;) {
		if (BSckGetString(hBSock, szRTXBuffer, sizeof(szRTXBuffer) - 3,
				  iPOP3ClientTimeout, &iLineLength, &iGotNL) == NULL) {
			fclose(pMsgFile);

			ErrSetErrorCode(ERR_POP3_RETR_BROKEN);
			return ERR_POP3_RETR_BROKEN;
		}
		if (iGotNL && iGotNLPrev && strcmp(szRTXBuffer, ".") == 0)
			break;
		if (iGotNL)
			memcpy(szRTXBuffer + iLineLength, "\r\n", 3), iLineLength += 2;
		if (!fwrite(szRTXBuffer, iLineLength, 1, pMsgFile)) {
			fclose(pMsgFile);

			ErrSetErrorCode(ERR_FILE_WRITE, pszFileName);
			return ERR_FILE_WRITE;
		}
		llMsgSize += iLineLength;
		iGotNLPrev = iGotNL;
	}
	fclose(pMsgFile);
	if (pllMsgSize != NULL)
		*pllMsgSize = llMsgSize;

	return 0;
}

static int UPopDeleteMessage(BSOCK_HANDLE hBSock, int iMsgIndex)
{
	char szRTXBuffer[2048];

	sprintf(szRTXBuffer, "DELE %d", iMsgIndex);
	if (UPopSendCommand(hBSock, szRTXBuffer, szRTXBuffer, sizeof(szRTXBuffer) - 1,
			    iPOP3ClientTimeout) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int UPopChanConfigAssign(void *pPrivate, char const *pszName, char const *pszValue)
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

static int UPopSetChanConfig(char const *pszAuthType, POP3ChannelCfg *pChCfg)
{
	return MscParseOptions(pszAuthType, UPopChanConfigAssign, pChCfg);
}

static void UPopFreeChanConfig(POP3ChannelCfg *pChCfg)
{
	SysFree(pChCfg->pszIFace);
}

static POP3SyncMsg *UPopSChanMsgAlloc(int iMsgSeq, char const *pszMsgID)
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
	HashOps HOps;

	ZeroData(HOps);
	HOps.pGetHashVal = MscStringHashCB;
	HOps.pCompare = MscStringCompareCB;
	if ((hHash = HashCreate(&HOps, pPSChan->iMsgCount + 1)) == INVALID_HASH_HANDLE)
		return ErrGetErrorCode();
	for (pPos = SYS_LIST_FIRST(&pPSChan->SyncMList); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, &pPSChan->SyncMList)) {
		pSMsg = SYS_LIST_ENTRY(pPos, POP3SyncMsg, LLnk);

		pSMsg->HN.Key.pData = pSMsg->pszMsgID;
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

	char szResLock[SYS_MAX_PATH];
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szMsgSyncFile, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		HashFree(hHash, NULL, NULL);
		return ErrGetErrorCode();
	}

	FILE *pUFile = fopen(szMsgSyncFile, "rt");

	if (pUFile != NULL) {
		HashDatum Key;
		HashEnum HEnum;
		char szUIDL[512];

		while (MscFGets(szUIDL, sizeof(szUIDL) - 1, pUFile) != NULL) {
			Key.pData = szUIDL;
			if (HashGetFirst(hHash, &Key, &HEnum, &pHNode) == 0) {
				pSMsg = SYS_LIST_ENTRY(pHNode, POP3SyncMsg, HN);
				SYS_LIST_DEL(&pSMsg->LLnk);
				SYS_LIST_ADDT(&pSMsg->LLnk, &pPSChan->SeenMList);
				pPSChan->iMsgSync--;
				pPSChan->llSizeSync -= pSMsg->llSize;
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

	char szResLock[SYS_MAX_PATH];
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
				 pPSChan->llMailboxSize) < 0)
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
			pSMsg->llSize = Sys_atoi64(pszMsgSize);
			pPSChan->llSizeSync += pSMsg->llSize;
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

static POP3SyncChannel *UPopSChanCreate(char const *pszRmtServer, char const *pszRmtName,
					char const *pszRmtPassword, char const *pszSyncCfg)
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

int UPopSyncRemoteLink(char const *pszSyncAddr, char const *pszRmtServer,
		       char const *pszRmtName, char const *pszRmtPassword,
		       MailSyncReport *pSRep, char const *pszSyncCfg,
		       char const *pszFetchHdrTags, char const *pszErrorAccount)
{
	POP3SyncChannel *pPSChan;

	if ((pPSChan = UPopSChanCreate(pszRmtServer, pszRmtName, pszRmtPassword,
				       pszSyncCfg)) == NULL)
		return ErrGetErrorCode();

	/* Initialize the report structure with current mailbox informations */
	pSRep->iMsgSync = 0;
	pSRep->iMsgErr = pPSChan->iMsgSync;
	pSRep->llSizeSync = 0;
	pSRep->llSizeErr = pPSChan->llSizeSync;

	SysListHead *pPos;
	POP3SyncMsg *pSMsg;
	char szMsgFileName[SYS_MAX_PATH];

	MscSafeGetTmpFile(szMsgFileName, sizeof(szMsgFileName));

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
			pSRep->llSizeSync += pSMsg->llSize;
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
	pSRep->llSizeErr -= Min(pSRep->llSizeErr, pSRep->llSizeSync);

	return 0;
}

int UPopUserIpCheck(UserInfo *pUI, SYS_INET_ADDR const *pPeerInfo,
		    unsigned int uExpireTime)
{
	char szIpFilePath[SYS_MAX_PATH];

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
		ErrSetErrorCode(ERR_NO_POP3_IP, szIpFilePath);
		return ERR_NO_POP3_IP;
	}

	char szIP[128] = "";

	MscFGets(szIP, sizeof(szIP) - 1, pIpFile);

	fclose(pIpFile);

	/* Do IP matching */
	SYS_INET_ADDR CurrAddr;

	ZeroData(CurrAddr); /* [i_a] */

	// if (SysGetHostByName(szIP, SYS_INET46 /* SysGetAddrFamily(CurrAddr) */, CurrAddr) < 0 || 
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
	char szIpFilePath[SYS_MAX_PATH];

	UPopGetIpLogFilePath(pUI, szIpFilePath, sizeof(szIpFilePath));
	if (SysGetFileInfo(szIpFilePath, FI) < 0)
		return ErrGetErrorCode();

	pInfo->LTime = FI.tMod;

	/* Load IP from file */
	FILE *pIpFile = fopen(szIpFilePath, "rt");

	if (pIpFile == NULL) {
		ErrSetErrorCode(ERR_NO_POP3_IP, szIpFilePath);
		return ERR_NO_POP3_IP;
	}

	char szIP[128] = "";

	MscFGets(szIP, sizeof(szIP) - 1, pIpFile);
	fclose(pIpFile);

	return SysGetHostByName(szIP, -1, pInfo->Address);
}

