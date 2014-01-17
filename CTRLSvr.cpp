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
#include "SList.h"
#include "BuffSock.h"
#include "SSLBind.h"
#include "SSLConfig.h"
#include "ResLocks.h"
#include "MiscUtils.h"
#include "MD5.h"
#include "MailConfig.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "StrUtils.h"
#include "POP3Utils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "UsrMailList.h"
#include "POP3GwLink.h"
#include "MailDomains.h"
#include "AliasDomain.h"
#include "ExtAliases.h"
#include "SMTPUtils.h"
#include "MailConfig.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "CTRLSvr.h"

#define CTRL_ACCOUNTS_FILE      "ctrlaccounts.tab"
#define CTRL_ACCOUNTS_LINE_MAX  512
#define STD_CTRL_TIMEOUT        45
#define CTRL_IPMAP_FILE         "ctrl.ipmap.tab"
#define CTRL_LOG_FILE           "ctrl"
#define CTRL_MAX_LINE_SIZE      4096
#define CTRL_QUIT_CMD_EXIT      1
#define CTRL_LISTFOLLOW_RESULT  100
#define CTRL_WAITDATA_RESULT    101
#define CTRL_VAR_DROP_VALUE     ".|rm"
#define CTRL_TLS_INIT_STR       "#!TLS"

enum CtrlAccountsFileds {
	accUsername = 0,
	accPassword,

	accMax
};

static CTRLConfig *CTRLGetConfigCopy(SHB_HANDLE hShbCTRL);
static int CTRLLogEnabled(SHB_HANDLE hShbCTRL, CTRLConfig *pCTRLCfg = NULL);
static int CTRLCheckPeerIP(SYS_SOCKET SockFD);
static int CTRLLogSession(char const *pszUsername, char const *pszPassword,
			  SYS_INET_ADDR const &PeerInfo, int iStatus);
static int CTRLThreadCountAdd(long lCount, SHB_HANDLE hShbCTRL, CTRLConfig *pCTRLCfg = NULL);
static int CTRLSendCmdResult(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, int iErrorCode);
static int CTRLSendCmdResult(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, int iErrorCode,
			     char const *pszMessage);
static int CTRLVSendCmdResult(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, int iErrorCode,
			      char const *pszFormat, ...);
static int CTRLSendCmdResult(BSOCK_HANDLE hBSock, int iErrorCode, char const *pszMessage,
			     int iTimeout);
static char *CTRLGetAccountsFilePath(char *pszAccFilePath, int iMaxPath);
static int CTRLAccountCheck(CTRLConfig *pCTRLCfg, char const *pszUsername,
			    char const *pszPassword, char const *pszTimeStamp);
static int CTRLSslEnvCB(void *pPrivate, int iID, void const *pData);
static int CTRLLogin(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		     char const *pszTimeStamp, SYS_INET_ADDR const &PeerInfo);
static int CTRLHandleSession(ThreadConfig const *pThCfg, BSOCK_HANDLE hBSock,
			     SYS_INET_ADDR const &PeerInfo);
static int CTRLProcessCommand(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, char const *pszCommand);
static int CTRLDo_useradd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			  char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_userdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			  char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_userpasswd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_aliasadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_aliasdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_aliaslist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_exaliasadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_exaliasdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_exaliaslist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			      char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_uservars(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_uservarsset(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			      char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_userlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_usergetmproc(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			       char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_usersetmproc(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			       char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_userauth(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_userstat(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_mluseradd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_mluserdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_mluserlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_domainadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_domaindel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_domainlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_custdomget(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_custdomset(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_custdomlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			      char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_noop(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		       char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_quit(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		       char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_poplnkadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_poplnkdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_poplnklist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_poplnkenable(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			       char const *const *ppszTokens, int iTokensCount);
static int CTRLCheckRelativePath(char const *pszPath);
static int CTRLDo_filelist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_cfgfileget(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_cfgfileset(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_frozlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_frozsubmit(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_frozdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			  char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_frozgetlog(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_frozgetmsg(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_etrn(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		       char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_aliasdomainadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
				 char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_aliasdomaindel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
				 char const *const *ppszTokens, int iTokensCount);
static int CTRLDo_aliasdomainlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
				  char const *const *ppszTokens, int iTokensCount);

static CTRLConfig *CTRLGetConfigCopy(SHB_HANDLE hShbCTRL)
{
	CTRLConfig *pCTRLCfg = (CTRLConfig *) ShbLock(hShbCTRL);

	if (pCTRLCfg == NULL)
		return NULL;

	CTRLConfig *pCTRLCfgCopy = (CTRLConfig *) SysAlloc(sizeof(CTRLConfig));

	if (pCTRLCfgCopy != NULL)
		memcpy(pCTRLCfgCopy, pCTRLCfg, sizeof(CTRLConfig));

	ShbUnlock(hShbCTRL);

	return pCTRLCfgCopy;
}

static int CTRLLogEnabled(SHB_HANDLE hShbCTRL, CTRLConfig *pCTRLCfg)
{
	int iDoUnlock = 0;

	if (pCTRLCfg == NULL) {
		if ((pCTRLCfg = (CTRLConfig *) ShbLock(hShbCTRL)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}

	unsigned long ulFlags = pCTRLCfg->ulFlags;

	if (iDoUnlock)
		ShbUnlock(hShbCTRL);

	return (ulFlags & CTRLF_LOG_ENABLED) ? 1: 0;
}

static int CTRLCheckPeerIP(SYS_SOCKET SockFD)
{
	char szIPMapFile[SYS_MAX_PATH] = "";

	CfgGetRootPath(szIPMapFile, sizeof(szIPMapFile));
	StrSNCat(szIPMapFile, CTRL_IPMAP_FILE);

	if (SysExistFile(szIPMapFile)) {
		SYS_INET_ADDR PeerInfo;

		if (SysGetPeerInfo(SockFD, PeerInfo) < 0)
			return ErrGetErrorCode();

		if (MscCheckAllowedIP(szIPMapFile, PeerInfo, true) < 0)
			return ErrGetErrorCode();
	}

	return 0;
}

static int CTRLLogSession(char const *pszUsername, char const *pszPassword,
			  SYS_INET_ADDR const &PeerInfo, int iStatus)
{
	char szTime[256] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1);

	RLCK_HANDLE hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR CTRL_LOG_FILE);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	char szIP[128] = "???.???.???.???";

	MscFileLog(CTRL_LOG_FILE, "\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\n", SysInetNToA(PeerInfo, szIP, sizeof(szIP)), pszUsername, pszPassword,
		   szTime, (iStatus == 0) ? "REQ": ((iStatus > 0) ? "AUTH": "FAIL"));

	RLckUnlockEX(hResLock);

	return 0;
}

static int CTRLThreadCountAdd(long lCount, SHB_HANDLE hShbCTRL, CTRLConfig *pCTRLCfg)
{
	int iDoUnlock = 0;

	if (pCTRLCfg == NULL) {
		if ((pCTRLCfg = (CTRLConfig *) ShbLock(hShbCTRL)) == NULL)
			return ErrGetErrorCode();

		++iDoUnlock;
	}
	if ((pCTRLCfg->lThreadCount + lCount) > pCTRLCfg->lMaxThreads) {
		if (iDoUnlock)
			ShbUnlock(hShbCTRL);

		ErrSetErrorCode(ERR_SERVER_BUSY);
		return ERR_SERVER_BUSY;
	}
	pCTRLCfg->lThreadCount += lCount;
	if (iDoUnlock)
		ShbUnlock(hShbCTRL);

	return 0;
}

unsigned int CTRLClientThread(void *pThreadData)
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
			SysCloseSocket(pThCtx->SockFD);
			SysFree(pThCtx);
			return ErrorPop();
		}
		ZeroData(SslE);

		iError = BSslBindServer(hBSock, &SSLB, MscSslEnvCB, &SslE);

		CSslBindCleanup(&SSLB);
		if (iError < 0) {
			ErrorPush();
			SysCloseSocket(pThCtx->SockFD);
			SysFree(pThCtx);
			return ErrorPop();
		}
		/*
		 * We may want to add verify code here ...
		 */

		SysFree(SslE.pszIssuer);
		SysFree(SslE.pszSubject);
	}
	/* Check IP permission */
	if (CTRLCheckPeerIP(pThCtx->SockFD) < 0) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s (CTRL check peer IP)\n",
			      ErrGetErrorString(ErrorFetch()));
		CTRLSendCmdResult(hBSock, ErrorFetch(), ErrGetErrorString(), STD_CTRL_TIMEOUT);
		BSckDetach(hBSock, 1);
		SysFree(pThCtx);
		return ErrorPop();
	}
	/* Increase threads count */
	if (CTRLThreadCountAdd(+1, pThCtx->pThCfg->hThShb) < 0) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s (CTRL thread count)\n",
			      ErrGetErrorString(ErrorFetch()));
		CTRLSendCmdResult(hBSock, ErrorFetch(), ErrGetErrorString(), STD_CTRL_TIMEOUT);
		BSckDetach(hBSock, 1);
		SysFree(pThCtx);
		return ErrorPop();
	}
	/* Get client socket information */
	SYS_INET_ADDR PeerInfo;

	if (SysGetPeerInfo(pThCtx->SockFD, PeerInfo) < 0) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		CTRLThreadCountAdd(-1, pThCtx->pThCfg->hThShb);
		BSckDetach(hBSock, 1);
		SysFree(pThCtx);
		return ErrorPop();
	}

	char szIP[128] = "???.???.???.???";

	SysLogMessage(LOG_LEV_MESSAGE, "CTRL client connection from [%s]\n",
		      SysInetNToA(PeerInfo, szIP, sizeof(szIP)));

	/* Handle client session */
	CTRLHandleSession(pThCtx->pThCfg, hBSock, PeerInfo);

	SysLogMessage(LOG_LEV_MESSAGE, "CTRL client exit [%s]\n",
		      SysInetNToA(PeerInfo, szIP, sizeof(szIP)));

	/* Decrease threads count */
	CTRLThreadCountAdd(-1, pThCtx->pThCfg->hThShb);

	/* Unlink socket from the bufferer and close it */
	BSckDetach(hBSock, 1);
	SysFree(pThCtx);

	return 0;
}

static int CTRLSendCmdResult(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, int iErrorCode)
{
	return (CTRLSendCmdResult(pCTRLCfg, hBSock, iErrorCode,
				  (iErrorCode >= 0) ? "OK": ErrGetErrorString(iErrorCode)));

}

static int CTRLSendCmdResult(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, int iErrorCode,
			     char const *pszMessage)
{
	return CTRLSendCmdResult(hBSock, iErrorCode, pszMessage, pCTRLCfg->iTimeout);
}

static int CTRLVSendCmdResult(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, int iErrorCode,
			      char const *pszFormat, ...)
{
	char *pszMessage = NULL;

	StrVSprint(pszMessage, pszFormat, pszFormat);

	if (pszMessage == NULL)
		return ErrGetErrorCode();

	int iSendResult = CTRLSendCmdResult(hBSock, iErrorCode, pszMessage, pCTRLCfg->iTimeout);

	SysFree(pszMessage);

	return iSendResult;
}

static int CTRLSendCmdResult(BSOCK_HANDLE hBSock, int iErrorCode, char const *pszMessage,
			     int iTimeout)
{
	int iSendResult;

	if (iErrorCode >= 0)
		iSendResult = BSckVSendString(hBSock, iTimeout, "+%05d %s", iErrorCode,
					      pszMessage);
	else
		iSendResult = BSckVSendString(hBSock, iTimeout, "-%05d %s", -iErrorCode,
					      pszMessage);

	return iSendResult;
}

static char *CTRLGetAccountsFilePath(char *pszAccFilePath, int iMaxPath)
{
	CfgGetRootPath(pszAccFilePath, iMaxPath);

	StrNCat(pszAccFilePath, CTRL_ACCOUNTS_FILE, iMaxPath);

	return pszAccFilePath;
}

static int CTRLAccountCheck(CTRLConfig *pCTRLCfg, char const *pszUsername,
			    char const *pszPassword, char const *pszTimeStamp)
{
	char szAccFilePath[SYS_MAX_PATH] = "";

	CTRLGetAccountsFilePath(szAccFilePath, sizeof(szAccFilePath));

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szAccFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pAccountsFile = fopen(szAccFilePath, "rt");

	if (pAccountsFile == NULL) {
		RLckUnlockSH(hResLock);

		ErrSetErrorCode(ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND);
		return ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND;
	}

	char szAccountsLine[CTRL_ACCOUNTS_LINE_MAX] = "";

	while (MscFGets(szAccountsLine, sizeof(szAccountsLine) - 1, pAccountsFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szAccountsLine);

		if (ppszStrings == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszStrings);

		if ((iFieldsCount >= accMax) &&
		    (stricmp(pszUsername, ppszStrings[accUsername]) == 0)) {

			char szClearPassword[256] = "";

			StrDeCrypt(ppszStrings[accPassword], szClearPassword);

			StrFreeStrings(ppszStrings);
			fclose(pAccountsFile);
			RLckUnlockSH(hResLock);

			/* Check for MD5 authentication ( # as first char of password ) */
			if (*pszPassword == '#') {
				if (MscMD5Authenticate
				    (szClearPassword, pszTimeStamp, pszPassword + 1) < 0) {
					ErrSetErrorCode(ERR_BAD_CTRL_LOGIN);
					return ERR_BAD_CTRL_LOGIN;
				}
			} else if (strcmp(szClearPassword, pszPassword) != 0) {
				ErrSetErrorCode(ERR_BAD_CTRL_LOGIN);
				return ERR_BAD_CTRL_LOGIN;
			}

			return 0;
		}
		StrFreeStrings(ppszStrings);
	}
	fclose(pAccountsFile);

	RLckUnlockSH(hResLock);

	ErrSetErrorCode(ERR_BAD_CTRL_LOGIN);
	return ERR_BAD_CTRL_LOGIN;
}

static int CTRLSslEnvCB(void *pPrivate, int iID, void const *pData)
{
	SslBindEnv *pSslE = (SslBindEnv *) pPrivate;

	/*
	 * Empty for now ...
	 */


	return 0;
}

static int CTRLLogin(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		     char const *pszTimeStamp, SYS_INET_ADDR const &PeerInfo)
{
	char szLogin[256] = "";

	if (BSckGetString(hBSock, szLogin, sizeof(szLogin) - 1, pCTRLCfg->iTimeout) == NULL ||
	    MscCmdStringCheck(szLogin) < 0)
		return ErrGetErrorCode();

	if (strcmp(szLogin, CTRL_TLS_INIT_STR) == 0) {
		int iError;
		SslServerBind SSLB;
		SslBindEnv SslE;

		if (strcmp(BSckBioName(hBSock), BSSL_BIO_NAME) == 0) {
			CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_SSL_ALREADY_ACTIVE);
			ErrSetErrorCode(ERR_SSL_ALREADY_ACTIVE);
			return ERR_SSL_ALREADY_ACTIVE;
		} else if (!SvrTestConfigFlag("EnableCTRL-TLS", true)) {
			CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_SSL_DISABLED);
			ErrSetErrorCode(ERR_SSL_DISABLED);
			return ERR_SSL_DISABLED;
		}
		if (CSslBindSetup(&SSLB) < 0) {
			ErrorPush();
			CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
			return ErrorPop();
		}
		ZeroData(SslE);

		CTRLSendCmdResult(pCTRLCfg, hBSock, 0, "Ready to start TLS mode");

		iError = BSslBindServer(hBSock, &SSLB, CTRLSslEnvCB, &SslE);

		CSslBindCleanup(&SSLB);
		if (iError < 0)
			return iError;

		SysFree(SslE.pszIssuer);
		SysFree(SslE.pszSubject);
		if (BSckGetString(hBSock, szLogin, sizeof(szLogin) - 1,
				  pCTRLCfg->iTimeout) == NULL ||
		    MscCmdStringCheck(szLogin) < 0)
			return ErrGetErrorCode();
	}

	char **ppszTokens = StrGetTabLineStrings(szLogin);

	if (ppszTokens == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	int iTokensCount = StrStringsCount(ppszTokens);

	if (iTokensCount != 2) {
		StrFreeStrings(ppszTokens);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_LOGIN);
		ErrSetErrorCode(ERR_BAD_CTRL_LOGIN);
		return ERR_BAD_CTRL_LOGIN;
	}
	/* Log CTRL login request */
	if (CTRLLogEnabled(SHB_INVALID_HANDLE, pCTRLCfg))
		CTRLLogSession(ppszTokens[0], ppszTokens[1], PeerInfo, 0);

	/* Check user and password */
	if (CTRLAccountCheck(pCTRLCfg, ppszTokens[0], ppszTokens[1], pszTimeStamp) < 0) {
		ErrorPush();
		/* Log CTRL login failure */
		if (CTRLLogEnabled(SHB_INVALID_HANDLE, pCTRLCfg))
			CTRLLogSession(ppszTokens[0], ppszTokens[1], PeerInfo, -1);

		StrFreeStrings(ppszTokens);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	/* Log CTRL login authentication */
	if (CTRLLogEnabled(SHB_INVALID_HANDLE, pCTRLCfg))
		CTRLLogSession(ppszTokens[0], ppszTokens[1], PeerInfo, +1);

	StrFreeStrings(ppszTokens);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLHandleSession(ThreadConfig const *pThCfg, BSOCK_HANDLE hBSock,
			     SYS_INET_ADDR const &PeerInfo)
{
	CTRLConfig *pCTRLCfg = CTRLGetConfigCopy(pThCfg->hThShb);

	if (pCTRLCfg == NULL)
		return ErrGetErrorCode();

	int iSessionTimeout = pCTRLCfg->iSessionTimeout, iTimeout = pCTRLCfg->iTimeout;

	/* Build TimeStamp string */
	SYS_INET_ADDR SockInfo;
	char szTimeStamp[256] = "";

	SysGetSockInfo(BSckGetAttachedSocket(hBSock), SockInfo);

	char szIP[128] = "???.???.???.???";

	sprintf(szTimeStamp, "<%lu.%lu@%s>",
		(unsigned long) time(NULL), SysGetCurrentThreadId(),
		SysInetNToA(SockInfo, szIP, sizeof(szIP)));

	/* Welcome */
	char szTime[256] = "";

	MscGetTimeStr(szTime, sizeof(szTime) - 1);

	CTRLVSendCmdResult(pCTRLCfg, hBSock, 0, "%s %s CTRL Server; %s",
			   szTimeStamp, APP_NAME_VERSION_STR, szTime);

	/* User login */
	if (CTRLLogin(pCTRLCfg, hBSock, szTimeStamp, PeerInfo) < 0) {
		ErrorPush();
		SysFree(pCTRLCfg);
		return ErrorPop();
	}

	/* Command loop */
	char szCommand[CTRL_MAX_LINE_SIZE] = "";

	while (!SvrInShutdown() &&
	       BSckGetString(hBSock, szCommand, sizeof(szCommand) - 1, iSessionTimeout) != NULL &&
	       MscCmdStringCheck(szCommand) == 0) {
		if (pThCfg->ulFlags & THCF_SHUTDOWN)
			break;

		/* Process client command */
		if (CTRLProcessCommand(pCTRLCfg, hBSock, szCommand) == CTRL_QUIT_CMD_EXIT)
			break;
	}
	SysFree(pCTRLCfg);

	return 0;
}

static int CTRLProcessCommand(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock, char const *pszCommand)
{
	char **ppszTokens = StrGetTabLineStrings(pszCommand);

	if (ppszTokens == NULL) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	int iTokensCount = StrStringsCount(ppszTokens);

	if (iTokensCount < 1) {
		StrFreeStrings(ppszTokens);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	int iCmdResult = -1;

	if (stricmp(ppszTokens[0], "useradd") == 0)
		iCmdResult = CTRLDo_useradd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "userdel") == 0)
		iCmdResult = CTRLDo_userdel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "userpasswd") == 0)
		iCmdResult = CTRLDo_userpasswd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "uservars") == 0)
		iCmdResult = CTRLDo_uservars(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "uservarsset") == 0)
		iCmdResult = CTRLDo_uservarsset(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "userlist") == 0)
		iCmdResult = CTRLDo_userlist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "usergetmproc") == 0)
		iCmdResult = CTRLDo_usergetmproc(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "usersetmproc") == 0)
		iCmdResult = CTRLDo_usersetmproc(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "userauth") == 0)
		iCmdResult = CTRLDo_userauth(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "userstat") == 0)
		iCmdResult = CTRLDo_userstat(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "aliasadd") == 0)
		iCmdResult = CTRLDo_aliasadd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "aliasdel") == 0)
		iCmdResult = CTRLDo_aliasdel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "aliaslist") == 0)
		iCmdResult = CTRLDo_aliaslist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "exaliasadd") == 0)
		iCmdResult = CTRLDo_exaliasadd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "exaliasdel") == 0)
		iCmdResult = CTRLDo_exaliasdel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "exaliaslist") == 0)
		iCmdResult = CTRLDo_exaliaslist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "mluseradd") == 0)
		iCmdResult = CTRLDo_mluseradd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "mluserdel") == 0)
		iCmdResult = CTRLDo_mluserdel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "mluserlist") == 0)
		iCmdResult = CTRLDo_mluserlist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "domainadd") == 0)
		iCmdResult = CTRLDo_domainadd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "domaindel") == 0)
		iCmdResult = CTRLDo_domaindel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "domainlist") == 0)
		iCmdResult = CTRLDo_domainlist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "custdomget") == 0)
		iCmdResult = CTRLDo_custdomget(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "custdomset") == 0)
		iCmdResult = CTRLDo_custdomset(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "custdomlist") == 0)
		iCmdResult = CTRLDo_custdomlist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "poplnkadd") == 0)
		iCmdResult = CTRLDo_poplnkadd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "poplnkdel") == 0)
		iCmdResult = CTRLDo_poplnkdel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "poplnklist") == 0)
		iCmdResult = CTRLDo_poplnklist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "poplnkenable") == 0)
		iCmdResult = CTRLDo_poplnkenable(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "filelist") == 0)
		iCmdResult = CTRLDo_filelist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "cfgfileget") == 0)
		iCmdResult = CTRLDo_cfgfileget(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "cfgfileset") == 0)
		iCmdResult = CTRLDo_cfgfileset(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "frozlist") == 0)
		iCmdResult = CTRLDo_frozlist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "frozsubmit") == 0)
		iCmdResult = CTRLDo_frozsubmit(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "frozdel") == 0)
		iCmdResult = CTRLDo_frozdel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "frozgetlog") == 0)
		iCmdResult = CTRLDo_frozgetlog(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "frozgetmsg") == 0)
		iCmdResult = CTRLDo_frozgetmsg(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "aliasdomainadd") == 0)
		iCmdResult = CTRLDo_aliasdomainadd(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "aliasdomaindel") == 0)
		iCmdResult = CTRLDo_aliasdomaindel(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "aliasdomainlist") == 0)
		iCmdResult = CTRLDo_aliasdomainlist(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "etrn") == 0)
		iCmdResult = CTRLDo_etrn(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "noop") == 0)
		iCmdResult = CTRLDo_noop(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else if (stricmp(ppszTokens[0], "quit") == 0)
		iCmdResult = CTRLDo_quit(pCTRLCfg, hBSock, ppszTokens, iTokensCount);
	else {
		StrFreeStrings(ppszTokens);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	StrFreeStrings(ppszTokens);

	return iCmdResult;
}

static int CTRLDo_useradd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			  char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 5) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	if (USmtpCheckDomainPart(ppszTokens[1]) < 0 ||
	    USmtpCheckAddressPart(ppszTokens[2]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UserInfo *pUI = UsrCreateDefaultUser(ppszTokens[1], ppszTokens[2], ppszTokens[3],
					     (ppszTokens[4][0] == 'M') ? usrTypeML: usrTypeUser);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	if (UsrAddUser(pUI) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}

	UsrFreeUserInfo(pUI);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_userdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			  char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	if (UsrRemoveUser(ppszTokens[1], ppszTokens[2], 0) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_userpasswd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	/* Check real user account existence */
	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Set account password and do modify */
	SysFree(pUI->pszPassword);
	pUI->pszPassword = SysStrDup(ppszTokens[3]);

	if (UsrModifyUser(pUI) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	UsrFreeUserInfo(pUI);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_aliasadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Check real user account existence */
	char szAccountName[MAX_ADDR_NAME] = "";
	char szAccountDomain[MAX_ADDR_NAME] = "";

	if (USmtpSplitEmailAddr(ppszTokens[3], szAccountName, szAccountDomain) < 0) {
		StrSNCpy(szAccountName, ppszTokens[3]);
		StrSNCpy(szAccountDomain, ppszTokens[1]);
	}

	UserInfo *pUI = UsrGetUserByName(szAccountDomain, szAccountName);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	UsrFreeUserInfo(pUI);

	/* Check if we're overlapping an existing users with the new alias */
	if ((pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2])) != NULL) {
		UsrFreeUserInfo(pUI);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_USER_EXIST);

		ErrSetErrorCode(ERR_USER_EXIST);
		return ERR_USER_EXIST;
	}

	AliasInfo *pAI = UsrAllocAlias(ppszTokens[1], ppszTokens[2], ppszTokens[3]);

	if (pAI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	if (UsrAddAlias(pAI) < 0) {
		ErrorPush();
		UsrFreeAlias(pAI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UsrFreeAlias(pAI);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_aliasdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (UsrRemoveAlias(ppszTokens[1], ppszTokens[2]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_aliaslist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount > 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	char const *pszDomain = (iTokensCount > 1) ? ppszTokens[1]: NULL;
	char const *pszAlias = (iTokensCount > 2) ? ppszTokens[2]: NULL;
	char const *pszName = (iTokensCount > 3) ? ppszTokens[3]: NULL;

	ALSF_HANDLE hAliasDB = UsrAliasOpenDB();

	if (hAliasDB == INVALID_ALSF_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	AliasInfo *pAI = UsrAliasGetFirst(hAliasDB);

	if (pAI != NULL) {
		do {
			if (((pszDomain == NULL) || StrIWildMatch(pAI->pszDomain, pszDomain)) &&
			    ((pszAlias == NULL) || StrIWildMatch(pAI->pszAlias, pszAlias)) &&
			    ((pszName == NULL) || StrIWildMatch(pAI->pszName, pszName))) {
				char szAliasLine[1024] = "";

				sprintf(szAliasLine,
					"\"%s\"\t"
					"\"%s\"\t"
					"\"%s\"", pAI->pszDomain, pAI->pszAlias, pAI->pszName);

				if (BSckSendString(hBSock, szAliasLine, pCTRLCfg->iTimeout) < 0) {
					ErrorPush();
					UsrFreeAlias(pAI);
					UsrAliasCloseDB(hAliasDB);
					return ErrorPop();
				}
			}

			UsrFreeAlias(pAI);

		} while ((pAI = UsrAliasGetNext(hAliasDB)) != NULL);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	UsrAliasCloseDB(hAliasDB);

	return 0;
}

static int CTRLDo_exaliasadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	/* Split local and remote accounts addresses */
	char szLocName[MAX_ADDR_NAME] = "";
	char szLocDomain[MAX_ADDR_NAME] = "";
	char szRmtName[MAX_ADDR_NAME] = "";
	char szRmtDomain[MAX_ADDR_NAME] = "";

	if ((USmtpSplitEmailAddr(ppszTokens[1], szLocName, szLocDomain) < 0) ||
	    (USmtpSplitEmailAddr(ppszTokens[2], szRmtName, szRmtDomain) < 0)) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	/* Build the external alias structure */
	ExtAlias *pEA = ExAlAllocAlias();

	if (pEA == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	pEA->pszRmtDomain = SysStrDup(szRmtDomain);
	pEA->pszRmtName = SysStrDup(szRmtName);
	pEA->pszDomain = SysStrDup(szLocDomain);
	pEA->pszName = SysStrDup(szLocName);

	if (ExAlAddAlias(pEA) < 0) {
		ErrorPush();
		ExAlFreeAlias(pEA);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	ExAlFreeAlias(pEA);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_exaliasdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	/* Split local and remote accounts addresses */
	char szRmtName[MAX_ADDR_NAME] = "";
	char szRmtDomain[MAX_ADDR_NAME] = "";

	if (USmtpSplitEmailAddr(ppszTokens[1], szRmtName, szRmtDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	/* Build the external alias structure */
	ExtAlias *pEA = ExAlAllocAlias();

	if (pEA == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	pEA->pszRmtDomain = SysStrDup(szRmtDomain);
	pEA->pszRmtName = SysStrDup(szRmtName);

	if (ExAlRemoveAlias(pEA) < 0) {
		ErrorPush();
		ExAlFreeAlias(pEA);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	ExAlFreeAlias(pEA);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_exaliaslist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			      char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount > 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	/* Split local and remote accounts addresses */
	char const *pszLocNameMatch = "*";
	char const *pszLocDomainMatch = "*";
	char const *pszRmtNameMatch = "*";
	char const *pszRmtDomainMatch = "*";
	char szLocName[MAX_ADDR_NAME] = "";
	char szLocDomain[MAX_ADDR_NAME] = "";
	char szRmtName[MAX_ADDR_NAME] = "";
	char szRmtDomain[MAX_ADDR_NAME] = "";

	if (iTokensCount > 1) {
		if (USmtpSplitEmailAddr(ppszTokens[1], szLocName, szLocDomain) < 0) {
			ErrorPush();
			CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
			return ErrorPop();
		}
		pszLocNameMatch = szLocName;
		pszLocDomainMatch = szLocDomain;

		if (iTokensCount > 2) {
			if (USmtpSplitEmailAddr(ppszTokens[2], szRmtName, szRmtDomain) < 0) {
				ErrorPush();
				CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
				return ErrorPop();
			}
			pszRmtNameMatch = szRmtName;
			pszRmtDomainMatch = szRmtDomain;
		}
	}

	EXAL_HANDLE hLinksDB = ExAlOpenDB();

	if (hLinksDB == INVALID_EXAL_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	ExtAlias *pEA;

	for (pEA = ExAlGetFirstAlias(hLinksDB); pEA != NULL;
	     pEA = ExAlGetNextAlias(hLinksDB)) {
		if (StrIWildMatch(pEA->pszRmtDomain, pszRmtDomainMatch) &&
		    StrIWildMatch(pEA->pszRmtName, pszRmtNameMatch) &&
		    StrIWildMatch(pEA->pszDomain, pszLocDomainMatch) &&
		    StrIWildMatch(pEA->pszName, pszLocNameMatch)) {
			char szAliasLine[1024] = "";

			sprintf(szAliasLine,
				"\"%s\"\t"
				"\"%s\"\t"
				"\"%s\"\t"
				"\"%s\"", pEA->pszRmtDomain, pEA->pszRmtName,
				pEA->pszDomain, pEA->pszName);

			if (BSckSendString(hBSock, szAliasLine, pCTRLCfg->iTimeout) < 0) {
				ErrorPush();
				ExAlFreeAlias(pEA);
				ExAlCloseDB(hLinksDB);
				return ErrorPop();
			}
		}

		ExAlFreeAlias(pEA);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	ExAlCloseDB(hLinksDB);

	return 0;
}

static int CTRLDo_uservars(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	char const *pszDomain = ppszTokens[1];
	char const *pszName = ppszTokens[2];

	if (MDomIsHandledDomain(pszDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UserInfo *pUI = UsrGetUserByName(pszDomain, pszName);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	char **ppszVars = UsrGetProfileVars(pUI);

	if (ppszVars == NULL) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	for (int ii = 0; ppszVars[ii] != NULL; ii++) {
		char *pszVar = UsrGetUserInfoVar(pUI, ppszVars[ii]);

		if (pszVar != NULL) {
			if (BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"%s\"\t\"%s\"",
					    ppszVars[ii], pszVar) < 0) {
				ErrorPush();
				SysFree(pszVar);
				StrFreeStrings(ppszVars);
				UsrFreeUserInfo(pUI);
				return ErrorPop();
			}
			SysFree(pszVar);
		}
	}
	StrFreeStrings(ppszVars);
	UsrFreeUserInfo(pUI);

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	return 0;
}

static int CTRLDo_uservarsset(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			      char const *const *ppszTokens, int iTokensCount)
{
	if ((iTokensCount < 5) || (((iTokensCount - 3) % 2) != 0)) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	char const *pszDomain = ppszTokens[1];
	char const *pszName = ppszTokens[2];

	if (MDomIsHandledDomain(pszDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UserInfo *pUI = UsrGetUserByName(pszDomain, pszName);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	for (int ii = 3; ii < (iTokensCount - 1); ii += 2) {
		/* Check if the variable deletion is requested */
		if (strcmp(ppszTokens[ii + 1], CTRL_VAR_DROP_VALUE) == 0)
			UsrDelUserInfoVar(pUI, ppszTokens[ii]);
		else {
			/* Set user variable */
			if (UsrSetUserInfoVar(pUI, ppszTokens[ii], ppszTokens[ii + 1]) < 0) {
				ErrorPush();
				UsrFreeUserInfo(pUI);
				CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
				return ErrorPop();
			}
		}
	}
	UsrFlushUserVars(pUI);
	UsrFreeUserInfo(pUI);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_userlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount > 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	char const *pszDomain = (iTokensCount > 1) ? ppszTokens[1]: NULL;
	char const *pszName = (iTokensCount > 2) ? ppszTokens[2]: NULL;

	USRF_HANDLE hUsersDB = UsrOpenDB();

	if (hUsersDB == INVALID_USRF_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	UserInfo *pUI = UsrGetFirstUser(hUsersDB, 0);

	if (pUI != NULL) {
		do {
			if (((pszDomain == NULL) || StrIWildMatch(pUI->pszDomain, pszDomain)) &&
			    ((pszName == NULL) || StrIWildMatch(pUI->pszName, pszName))) {
				char szUserLine[1024] = "";

				SysSNPrintf(szUserLine, sizeof(szUserLine),
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"",
					    pUI->pszDomain, pUI->pszName, pUI->pszPassword,
					    pUI->pszType);
				if (BSckSendString(hBSock, szUserLine, pCTRLCfg->iTimeout) < 0) {
					ErrorPush();
					UsrFreeUserInfo(pUI);
					UsrCloseDB(hUsersDB);
					return ErrorPop();
				}
			}

			UsrFreeUserInfo(pUI);

		} while ((pUI = UsrGetNextUser(hUsersDB, 0)) != NULL);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	UsrCloseDB(hUsersDB);

	return 0;
}

static int CTRLDo_usergetmproc(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			       char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	unsigned long ulGMPFlags = 0;

	if (iTokensCount >= 4) {
		char const *pszFlag;

		for (pszFlag = ppszTokens[3]; *pszFlag != '\0'; pszFlag++)
			switch (*pszFlag) {
			case 'u':
			case 'U':
				ulGMPFlags |= GMPROC_USER;
				break;
			case 'd':
			case 'D':
				ulGMPFlags |= GMPROC_DOMAIN;
				break;
			}
	} else
		ulGMPFlags = GMPROC_DOMAIN | GMPROC_USER;

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	/* Check real user account existence */
	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Exist user custom message processing ? */
	char szMPFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szMPFile);
	if (UsrGetMailProcessFile(pUI, szMPFile, ulGMPFlags) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Send mailproc file */
	if (MscSendTextFile(szMPFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		SysRemove(szMPFile);
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}
	SysRemove(szMPFile);
	UsrFreeUserInfo(pUI);

	return 0;
}

static int CTRLDo_usersetmproc(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			       char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	int iWhichMP = GMPROC_USER;

	if (iTokensCount >= 4 &&
	    (ppszTokens[3][0] == 'd' || ppszTokens[3][0] == 'D'))
		iWhichMP = GMPROC_DOMAIN;

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	/* Check real user account existence */
	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_WAITDATA_RESULT);

	/* Read user data in file */
	char szMPFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szMPFile);

	if (MscRecvTextFile(szMPFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		CheckRemoveFile(szMPFile);
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Get file info for size checking */
	SYS_FILE_INFO FI;

	if (SysGetFileInfo(szMPFile, FI) < 0) {
		ErrorPush();
		CheckRemoveFile(szMPFile);
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Set mailproc file ( or delete it if size == 0 ) */
	if (UsrSetMailProcessFile(pUI, (FI.llSize != 0) ? szMPFile: NULL,
				  iWhichMP) < 0) {
		ErrorPush();
		SysRemove(szMPFile);
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	SysRemove(szMPFile);
	UsrFreeUserInfo(pUI);

	return 0;
}

static int CTRLDo_userauth(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Check real user account existence */
	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Check password */
	if (strcmp(pUI->pszPassword, ppszTokens[3]) != 0) {
		UsrFreeUserInfo(pUI);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_INVALID_PASSWORD);
		ErrSetErrorCode(ERR_INVALID_PASSWORD);
		return ERR_INVALID_PASSWORD;
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	UsrFreeUserInfo(pUI);

	return 0;
}

static int CTRLDo_userstat(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Check real user account existence */
	char szRealAddress[MAX_ADDR_NAME] = "";
	UserInfo *pUI = UsrGetUserByNameOrAlias(ppszTokens[1], ppszTokens[2],
						szRealAddress);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Get mailbox infos */
	SYS_OFF_T llMBSize = 0;
	unsigned long ulNumMessages = 0;

	if (UPopGetMailboxSize(pUI, llMBSize, ulNumMessages) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	time_t LTime = time(NULL);
	PopLastLoginInfo LoginInfo;
	char szIPAddr[128] = "0.0.0.0";
	char szLoginTime[128] = "";

	if (UPopGetLastLoginInfo(pUI, &LoginInfo) == 0) {
		SysInetNToA(LoginInfo.Address, szIPAddr, sizeof(szIPAddr));
		LTime = LoginInfo.LTime;
	}
	MscGetTimeStr(szLoginTime, sizeof(szLoginTime) - 1, LTime);

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	if (BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"RealAddress\"\t\"%s\"",
			    szRealAddress) < 0 ||
	    BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"MailboxSize\"\t\"" SYS_OFFT_FMT "u\"",
			    llMBSize) < 0 ||
	    BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"MailboxMessages\"\t\"%lu\"",
			    ulNumMessages) < 0 ||
	    BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"LastLoginTimeDate\"\t\"%s\"",
			    szLoginTime) < 0 ||
	    BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"LastLoginIP\"\t\"%s\"",
			    szIPAddr) < 0) {
		ErrorPush();
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}
	UsrFreeUserInfo(pUI);

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	return 0;
}

static int CTRLDo_mluseradd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	if (USmtpCheckAddress(ppszTokens[3]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	if (UsrGetUserType(pUI) != usrTypeML) {
		UsrFreeUserInfo(pUI);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_USER_NOT_MAILINGLIST);
		ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
		return ERR_USER_NOT_MAILINGLIST;
	}

	char const *pszPerms = (iTokensCount > 4) ? ppszTokens[4]: DEFAULT_MLUSER_PERMS;
	MLUserInfo *pMLUI = UsrMLAllocDefault(ppszTokens[3], pszPerms);

	if (pMLUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}

	if (UsrMLAddUser(pUI, pMLUI) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		UsrMLFreeUser(pMLUI);
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}

	UsrMLFreeUser(pMLUI);

	UsrFreeUserInfo(pUI);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_mluserdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	if (UsrGetUserType(pUI) != usrTypeML) {
		UsrFreeUserInfo(pUI);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_USER_NOT_MAILINGLIST);
		ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
		return ERR_USER_NOT_MAILINGLIST;
	}

	if (UsrMLRemoveUser(pUI, ppszTokens[3]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}

	UsrFreeUserInfo(pUI);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_mluserlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (MDomIsHandledDomain(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	UserInfo *pUI = UsrGetUserByName(ppszTokens[1], ppszTokens[2]);

	if (pUI == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	if (UsrGetUserType(pUI) != usrTypeML) {
		UsrFreeUserInfo(pUI);

		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_USER_NOT_MAILINGLIST);
		ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
		return ERR_USER_NOT_MAILINGLIST;
	}

	USRML_HANDLE hUsersDB = UsrMLOpenDB(pUI);

	if (hUsersDB == INVALID_USRML_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		UsrFreeUserInfo(pUI);
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Mailing list scan */
	MLUserInfo *pMLUI = UsrMLGetFirstUser(hUsersDB);

	for (; pMLUI != NULL; pMLUI = UsrMLGetNextUser(hUsersDB)) {
		char szUserLine[512] = "";

		sprintf(szUserLine, "\"%s\"\t\"%s\"", pMLUI->pszAddress, pMLUI->pszPerms);

		if (BSckSendString(hBSock, szUserLine, pCTRLCfg->iTimeout) < 0) {
			ErrorPush();
			UsrMLFreeUser(pMLUI);
			UsrFreeUserInfo(pUI);
			UsrMLCloseDB(hUsersDB);
			return ErrorPop();
		}

		UsrMLFreeUser(pMLUI);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	UsrFreeUserInfo(pUI);
	UsrMLCloseDB(hUsersDB);

	return 0;
}

static int CTRLDo_domainadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	if (USmtpCheckDomainPart(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	char szDomain[MAX_HOST_NAME] = "";

	StrSNCpy(szDomain, ppszTokens[1]);
	StrLower(szDomain);

	if (MDomIsHandledDomain(szDomain) == 0) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_ALREADY_EXIST);

		ErrSetErrorCode(ERR_ALREADY_EXIST);
		return ERR_ALREADY_EXIST;
	}
	if (MDomAddDomain(szDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_domaindel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	char szDomain[MAX_HOST_NAME] = "";

	StrSNCpy(szDomain, ppszTokens[1]);
	StrLower(szDomain);

	if (MDomRemoveDomain(szDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_domainlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	DOMLS_HANDLE hDomainsDB = MDomOpenDB();

	if (hDomainsDB == INVALID_DOMLS_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	char const *pszDomain = MDomGetFirstDomain(hDomainsDB);

	if (pszDomain != NULL) {
		do {
			if ((iTokensCount < 2) || StrStringsRIWMatch(&ppszTokens[1], pszDomain)) {
				char szDomainLine[512] = "";

				sprintf(szDomainLine, "\"%s\"", pszDomain);

				if (BSckSendString(hBSock, szDomainLine, pCTRLCfg->iTimeout) < 0) {
					ErrorPush();
					MDomCloseDB(hDomainsDB);
					return ErrorPop();
				}
			}
		} while ((pszDomain = MDomGetNextDomain(hDomainsDB)) != NULL);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	MDomCloseDB(hDomainsDB);

	return 0;
}

static int CTRLDo_custdomget(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Try to get custom domain file ( if exist ) */
	char szCustDomainFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szCustDomainFile);

	if (USmlGetCustomDomainFile(ppszTokens[1], szCustDomainFile) < 0) {
		ErrorPush();
		CheckRemoveFile(szCustDomainFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Send custom domain file */
	if (MscSendTextFile(szCustDomainFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		SysRemove(szCustDomainFile);
		return ErrorPop();
	}

	SysRemove(szCustDomainFile);

	return 0;
}

static int CTRLDo_custdomset(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	if (USmtpCheckDomainPart(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_WAITDATA_RESULT);

	/* Read user data in file */
	char szCustDomainFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szCustDomainFile);

	if (MscRecvTextFile(szCustDomainFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		CheckRemoveFile(szCustDomainFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Get file info for size checking */
	SYS_FILE_INFO FI;

	if (SysGetFileInfo(szCustDomainFile, FI) < 0) {
		ErrorPush();
		CheckRemoveFile(szCustDomainFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Set custom domain file ( or delete it if size == 0 ) */
	if (USmlSetCustomDomainFile(ppszTokens[1],
				    (FI.llSize != 0) ? szCustDomainFile: NULL) < 0) {
		ErrorPush();
		SysRemove(szCustDomainFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	SysRemove(szCustDomainFile);

	return 0;
}

static int CTRLDo_custdomlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			      char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 1) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	char szCustomPath[SYS_MAX_PATH] = "";

	USmlGetDomainCustomDir(szCustomPath, sizeof(szCustomPath), 0);

	char szCustFileName[SYS_MAX_PATH] = "";
	FSCAN_HANDLE hFileScan = MscFirstFile(szCustomPath, 0, szCustFileName,
					      sizeof(szCustFileName));

	if (hFileScan != INVALID_FSCAN_HANDLE) {
		do {
			char szCustDomain[SYS_MAX_PATH] = "";

			MscSplitPath(szCustFileName, NULL, 0, szCustDomain, sizeof(szCustDomain),
				     NULL, 0);
			if (BSckVSendString(hBSock, pCTRLCfg->iTimeout, "\"%s\"",
					    szCustDomain) < 0) {
				ErrorPush();
				MscCloseFindFile(hFileScan);
				return ErrorPop();
			}
		} while (MscNextFile(hFileScan, szCustFileName, sizeof(szCustFileName)));
		MscCloseFindFile(hFileScan);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	return 0;
}

static int CTRLDo_noop(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		       char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 1) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_quit(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		       char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 1) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return CTRL_QUIT_CMD_EXIT;
}

static int CTRLDo_poplnkadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 7) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	POP3Link *pPopLnk = GwLkAllocLink(ppszTokens[1], ppszTokens[2],
					  ppszTokens[3], ppszTokens[4], ppszTokens[5],
					  ppszTokens[6]);

	if (pPopLnk == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	if (GwLkAddLink(pPopLnk) < 0) {
		ErrorPush();
		GwLkFreePOP3Link(pPopLnk);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	GwLkFreePOP3Link(pPopLnk);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_poplnkdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			    char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 5) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	POP3Link *pPopLnk = GwLkAllocLink(ppszTokens[1], ppszTokens[2],
					  ppszTokens[3], ppszTokens[4], NULL, NULL);

	if (pPopLnk == NULL) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	if (GwLkRemoveLink(pPopLnk) < 0) {
		ErrorPush();
		GwLkFreePOP3Link(pPopLnk);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	GwLkFreePOP3Link(pPopLnk);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_poplnklist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount > 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	char const *pszDomain = (iTokensCount > 1) ? ppszTokens[1]: NULL;
	char const *pszName = (iTokensCount > 2) ? ppszTokens[2]: NULL;

	GWLKF_HANDLE hLinksDB = GwLkOpenDB();

	if (hLinksDB == INVALID_GWLKF_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	POP3Link *pPopLnk = GwLkGetFirstUser(hLinksDB);

	if (pPopLnk != NULL) {
		do {
			if (((pszDomain == NULL) || (stricmp(pPopLnk->pszDomain, pszDomain) == 0))
			    && ((pszName == NULL) || (stricmp(pPopLnk->pszName, pszName) == 0))) {
				char const *pszEnable =
					(GwLkCheckEnabled(pPopLnk) == 0) ? "ON": "OFF";
				char szLinkLine[2048] = "";

				SysSNPrintf(szLinkLine, sizeof(szLinkLine),
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"\t"
					    "\"%s\"",
					    pPopLnk->pszDomain, pPopLnk->pszName,
					    pPopLnk->pszRmtDomain, pPopLnk->pszRmtName,
					    pPopLnk->pszRmtPassword, pPopLnk->pszAuthType, pszEnable);
				if (BSckSendString(hBSock, szLinkLine, pCTRLCfg->iTimeout) < 0) {
					ErrorPush();
					GwLkFreePOP3Link(pPopLnk);
					GwLkCloseDB(hLinksDB);
					return ErrorPop();
				}
			}
			GwLkFreePOP3Link(pPopLnk);
		} while ((pPopLnk = GwLkGetNextUser(hLinksDB)) != NULL);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	GwLkCloseDB(hLinksDB);

	return 0;
}

static int CTRLDo_poplnkenable(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			       char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}

	bool bEnable = (atoi(ppszTokens[1])) ? true: false;
	char const *pszDomain = ppszTokens[2];
	char const *pszName = ppszTokens[3];
	char const *pszRmtDomain = (iTokensCount > 4) ? ppszTokens[4]: NULL;
	char const *pszRmtName = (iTokensCount > 5) ? ppszTokens[5]: NULL;

	if (GwLkEnable(pszDomain, pszName, pszRmtDomain, pszRmtName, bEnable) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLCheckRelativePath(char const *pszPath)
{
	/* Check 101 tricky path */
	if (strstr(pszPath, "..") != NULL) {
		ErrSetErrorCode(ERR_BAD_RELATIVE_PATH);
		return ERR_BAD_RELATIVE_PATH;
	}

	return 0;
}

static int CTRLDo_filelist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Check relative path syntax */
	if (CTRLCheckRelativePath(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	/* Setup listing file path */
	char szRelativePath[SYS_MAX_PATH] = "";
	char szFullPath[SYS_MAX_PATH] = "";

	StrSNCpy(szRelativePath, ppszTokens[1]);
	MscTranslatePath(szRelativePath);

	CfgGetFullPath(szRelativePath, szFullPath, sizeof(szFullPath));
	DelFinalSlash(szFullPath);

	/* Check directory existance */
	if (!SysExistDir(szFullPath)) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_LISTDIR_NOT_FOUND);
		ErrSetErrorCode(ERR_LISTDIR_NOT_FOUND);
		return ERR_LISTDIR_NOT_FOUND;
	}
	/* Send command continue response */
	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* List files */
	char szFileName[SYS_MAX_PATH] = "";
	FSCAN_HANDLE hFileScan = MscFirstFile(szFullPath, 0, szFileName,
					      sizeof(szFileName));

	if (hFileScan != INVALID_FSCAN_HANDLE) {
		do {
			if (!SYS_IS_VALID_FILENAME(szFileName) ||
			    !StrWildMatch(szFileName, ppszTokens[2]))
				continue;

			SYS_FILE_INFO FI;
			char szFilePath[SYS_MAX_PATH] = "";

			SysSNPrintf(szFilePath, sizeof(szFilePath) - 1, "%s%s%s",
				    szFullPath, SYS_SLASH_STR, szFileName);
			if (SysGetFileInfo(szFilePath, FI) == 0) {
				if (BSckVSendString(hBSock, pCTRLCfg->iTimeout,
						    "\"%s\"\t\"" SYS_OFFT_FMT "u\"",
						    szFileName, FI.llSize) < 0) {
					ErrorPush();
					MscCloseFindFile(hFileScan);
					return ErrorPop();
				}
			}
		} while (MscNextFile(hFileScan, szFileName, sizeof(szFileName)));
		MscCloseFindFile(hFileScan);
	}

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	return 0;
}

static int CTRLDo_cfgfileget(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Check relative path syntax */
	if (CTRLCheckRelativePath(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	/* Setup client target file path */
	char szRelativePath[SYS_MAX_PATH] = "";
	char szFullPath[SYS_MAX_PATH] = "";

	StrSNCpy(szRelativePath, ppszTokens[1]);
	MscTranslatePath(szRelativePath);

	CfgGetFullPath(szRelativePath, szFullPath, sizeof(szFullPath));
	DelFinalSlash(szFullPath);

	/* Share lock client target file */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szFullPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	/* Get a file snapshot */
	char szRequestedFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szRequestedFile);

	if (MscCopyFile(szRequestedFile, szFullPath) < 0) {
		ErrorPush();
		CheckRemoveFile(szRequestedFile);
		RLckUnlockSH(hResLock);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	RLckUnlockSH(hResLock);

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Send client target file */
	if (MscSendTextFile(szRequestedFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		SysRemove(szRequestedFile);
		return ErrorPop();
	}

	SysRemove(szRequestedFile);

	return 0;
}

static int CTRLDo_cfgfileset(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Check relative path syntax */
	if (CTRLCheckRelativePath(ppszTokens[1]) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	/* Setup client target file path */
	char szRelativePath[SYS_MAX_PATH] = "";
	char szFullPath[SYS_MAX_PATH] = "";

	StrSNCpy(szRelativePath, ppszTokens[1]);
	MscTranslatePath(szRelativePath);

	CfgGetFullPath(szRelativePath, szFullPath, sizeof(szFullPath));
	DelFinalSlash(szFullPath);

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_WAITDATA_RESULT);

	/* Read user data in file */
	char szClientFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szClientFile);

	if (MscRecvTextFile(szClientFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		CheckRemoveFile(szClientFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Get file info for size checking */
	SYS_FILE_INFO FI;

	if (SysGetFileInfo(szClientFile, FI) < 0) {
		ErrorPush();
		CheckRemoveFile(szClientFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}
	/* Exclusive lock client target file */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szFullPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE) {
		ErrorPush();
		SysRemove(szClientFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	if (FI.llSize != 0) {
		if (MscCopyFile(szFullPath, szClientFile) < 0) {
			ErrorPush();
			RLckUnlockEX(hResLock);
			SysRemove(szClientFile);
			CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
			return ErrorPop();
		}
	} else
		SysRemove(szFullPath);

	RLckUnlockEX(hResLock);

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	SysRemove(szClientFile);

	return 0;
}

static int CTRLDo_frozlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			   char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 1) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Build frozen list file */
	char szListFile[SYS_MAX_PATH] = "";

	SysGetTmpFile(szListFile);

	if (QueUtGetFrozenList(hSpoolQueue, szListFile) < 0) {
		ErrorPush();
		CheckRemoveFile(szListFile);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Send client target file */
	if (MscSendTextFile(szListFile, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		SysRemove(szListFile);
		return ErrorPop();
	}

	SysRemove(szListFile);

	return 0;
}

static int CTRLDo_frozsubmit(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Try to defroze frozen message */
	int iLevel1 = atoi(ppszTokens[1]);
	int iLevel2 = atoi(ppszTokens[2]);
	char szMessageFile[SYS_MAX_PATH] = "";

	StrSNCpy(szMessageFile, ppszTokens[3]);

	if (QueUtUnFreezeMessage(hSpoolQueue, iLevel1, iLevel2, szMessageFile) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_frozdel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			  char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Try to delete frozen message */
	int iLevel1 = atoi(ppszTokens[1]);
	int iLevel2 = atoi(ppszTokens[2]);
	char szMessageFile[SYS_MAX_PATH] = "";

	StrSNCpy(szMessageFile, ppszTokens[3]);

	if (QueUtDeleteFrozenMessage(hSpoolQueue, iLevel1, iLevel2, szMessageFile) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_frozgetlog(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Try to delete frozen message */
	int iLevel1 = atoi(ppszTokens[1]);
	int iLevel2 = atoi(ppszTokens[2]);
	char szMessageFile[SYS_MAX_PATH] = "";

	StrSNCpy(szMessageFile, ppszTokens[3]);

	/* Get log file snapshot */
	char szFileSS[SYS_MAX_PATH] = "";

	SysGetTmpFile(szFileSS);

	if (QueUtGetFrozenLogFile(hSpoolQueue, iLevel1, iLevel2, szMessageFile, szFileSS) < 0) {
		ErrorPush();
		CheckRemoveFile(szFileSS);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Send client target file */
	if (MscSendTextFile(szFileSS, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		SysRemove(szFileSS);
		return ErrorPop();
	}

	SysRemove(szFileSS);

	return 0;
}

static int CTRLDo_frozgetmsg(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
			     char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 4) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Try to delete frozen message */
	int iLevel1 = atoi(ppszTokens[1]);
	int iLevel2 = atoi(ppszTokens[2]);
	char szMessageFile[SYS_MAX_PATH] = "";

	StrSNCpy(szMessageFile, ppszTokens[3]);

	/* Get log file snapshot */
	char szFileSS[SYS_MAX_PATH] = "";

	SysGetTmpFile(szFileSS);

	if (QueUtGetFrozenMsgFile(hSpoolQueue, iLevel1, iLevel2, szMessageFile, szFileSS) < 0) {
		ErrorPush();
		CheckRemoveFile(szFileSS);
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	/* Send client target file */
	if (MscSendTextFile(szFileSS, hBSock, pCTRLCfg->iTimeout) < 0) {
		ErrorPush();
		SysRemove(szFileSS);
		return ErrorPop();
	}

	SysRemove(szFileSS);

	return 0;
}

static int CTRLDo_etrn(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
		       char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount < 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Do a matched flush of the rsnd arena */
	for (int ii = 1; ii < iTokensCount; ii++) {
		if (QueFlushRsndArena(hSpoolQueue, ppszTokens[ii]) < 0) {
			ErrorPush();
			CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
			return ErrorPop();
		}
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_aliasdomainadd(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
				 char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 3) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Filter params */
	char szDomain[MAX_HOST_NAME] = "";
	char szADomain[MAX_HOST_NAME] = "";

	StrSNCpy(szDomain, ppszTokens[1]);
	StrLower(szDomain);

	StrSNCpy(szADomain, ppszTokens[2]);
	StrLower(szADomain);

	/* Target domain MUST exit ( alias of aliases are not permitted ) */
	if (MDomLookupDomain(szDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}
	/* Alias domain MUST NOT exit */
	if ((MDomLookupDomain(szADomain) == 0) ||
	    ADomLookupDomain(szADomain, NULL, false)) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_ADOMAIN_EXIST);

		ErrSetErrorCode(ERR_ADOMAIN_EXIST);
		return ERR_ADOMAIN_EXIST;
	}
	/* Add alias domain */
	if (ADomAddADomain(szADomain, szDomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_aliasdomaindel(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
				 char const *const *ppszTokens, int iTokensCount)
{
	if (iTokensCount != 2) {
		CTRLSendCmdResult(pCTRLCfg, hBSock, ERR_BAD_CTRL_COMMAND);
		ErrSetErrorCode(ERR_BAD_CTRL_COMMAND);
		return ERR_BAD_CTRL_COMMAND;
	}
	/* Filter params */
	char szADomain[MAX_HOST_NAME] = "";

	StrSNCpy(szADomain, ppszTokens[1]);
	StrLower(szADomain);

	/* Remove alias domain */
	if (ADomRemoveADomain(szADomain) < 0) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrorFetch());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, 0);

	return 0;
}

static int CTRLDo_aliasdomainlist(CTRLConfig *pCTRLCfg, BSOCK_HANDLE hBSock,
				  char const *const *ppszTokens, int iTokensCount)
{
	ADOMAIN_HANDLE hADomainDB = ADomOpenDB();

	if (hADomainDB == INVALID_ADOMAIN_HANDLE) {
		ErrorPush();
		CTRLSendCmdResult(pCTRLCfg, hBSock, ErrGetErrorCode());
		return ErrorPop();
	}

	CTRLSendCmdResult(pCTRLCfg, hBSock, CTRL_LISTFOLLOW_RESULT);

	char const *const *ppszStrings = ADomGetFirstDomain(hADomainDB);

	for (; ppszStrings != NULL; ppszStrings = ADomGetNextDomain(hADomainDB)) {
		if ((iTokensCount < 2) ||
		    ((iTokensCount == 2) && StrStringsRIWMatch(&ppszTokens[1], ppszStrings[adomDomain])) ||
		    ((iTokensCount == 3) && StrStringsRIWMatch(&ppszTokens[1], ppszStrings[adomDomain]) &&
		     StrStringsRIWMatch(&ppszTokens[2], ppszStrings[adomADomain]))) {
			if (BSckVSendString
			    (hBSock, pCTRLCfg->iTimeout, "\"%s\"\t\"%s\"",
			     ppszStrings[adomDomain], ppszStrings[adomADomain]) < 0) {
				ErrorPush();
				ADomCloseDB(hADomainDB);
				return ErrorPop();
			}
		}
	}

	ADomCloseDB(hADomainDB);

	BSckSendString(hBSock, ".", pCTRLCfg->iTimeout);

	return 0;
}

