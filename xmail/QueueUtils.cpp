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
#include "MD5.h"
#include "Base64Enc.h"
#include "BuffSock.h"
#include "MessQueue.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SMTPUtils.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"

#define QUE_SMTP_MAILER_ERROR_HDR   "X-MailerError"
#define QUE_MAILER_HDR              "X-MailerServer"

static int QueUtDumpFrozen(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, FILE *pListFile)
{
	/* Get message file path */
	char szQueueFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szQueueFilePath);

	/* Load spool file info and make a type check */
	SYS_FILE_INFO FI;

	if (SysGetFileInfo(szQueueFilePath, FI) < 0)
		return ErrGetErrorCode();

	/* Load the spool file header */
	SpoolFileHeader SFH;

	if (USmlLoadSpoolFileHeader(szQueueFilePath, SFH) < 0)
		return ErrGetErrorCode();

	char *pszFrom = USmlAddrConcat(SFH.ppszFrom);

	if (pszFrom == NULL) {
		ErrorPush();
		USmlCleanupSpoolFileHeader(SFH);
		return ErrorPop();
	}

	char *pszRcpt = USmlAddrConcat(SFH.ppszRcpt);

	if (pszRcpt == NULL) {
		ErrorPush();
		SysFree(pszFrom);
		USmlCleanupSpoolFileHeader(SFH);
		return ErrorPop();
	}

	char szTime[128] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1, QueGetLastTryTime(hMessage));

	fprintf(pListFile,
		"\"%s\"\t"
		"\"%d\"\t"
		"\"%d\"\t"
		"\"<%s>\"\t"
		"\"<%s>\"\t"
		"\"%s\"\t"
		"\"" SYS_OFFT_FMT "u\"\t"
		"\"%d\"\t"
		"\"%s\"\n",
		QueGetFileName(hMessage), QueGetLevel1(hMessage), QueGetLevel2(hMessage),
		pszFrom, pszRcpt, szTime, FI.llSize, QueGetTryCount(hMessage),
		QueGetQueueDir(hMessage));

	SysFree(pszRcpt);
	SysFree(pszFrom);
	USmlCleanupSpoolFileHeader(SFH);

	return 0;
}

int QueUtGetFrozenList(QUEUE_HANDLE hQueue, char const *pszListFile)
{
	int iNumDirsLevel = QueGetDirsLevel(hQueue);
	char const *pszRootPath = QueGetRootPath(hQueue);

	/* Creates the list file and start scanning the frozen queue */
	FILE *pListFile = fopen(pszListFile, "wt");

	if (pListFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszListFile);
		return ERR_FILE_CREATE;
	}
	for (int i = 0; i < iNumDirsLevel; i++) {
		for (int j = 0; j < iNumDirsLevel; j++) {
			char szCurrPath[SYS_MAX_PATH] = "";

			SysSNPrintf(szCurrPath, sizeof(szCurrPath) - 1, "%s%d%s%d%s%s",
				    pszRootPath, i, SYS_SLASH_STR, j, SYS_SLASH_STR,
				    QUEUE_FROZ_DIR);

			char szFrozFileName[SYS_MAX_PATH] = "";
			FSCAN_HANDLE hFileScan = MscFirstFile(szCurrPath, 0, szFrozFileName,
							      sizeof(szFrozFileName));

			if (hFileScan != INVALID_FSCAN_HANDLE) {
				do {
					if (!SYS_IS_VALID_FILENAME(szFrozFileName))
						continue;

					/* Create queue file handle */
					QMSG_HANDLE hMessage = QueGetHandle(hQueue, i, j,
									    QUEUE_FROZ_DIR,
									    szFrozFileName);

					if (hMessage != INVALID_QMSG_HANDLE) {
						QueUtDumpFrozen(hQueue, hMessage, pListFile);
						QueCloseMessage(hQueue, hMessage);
					}
				} while (MscNextFile(hFileScan, szFrozFileName,
						     sizeof(szFrozFileName)));
				MscCloseFindFile(hFileScan);
			}
		}
	}
	fclose(pListFile);

	return 0;
}

int QueUtUnFreezeMessage(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
			 char const *pszMessageFile)
{
	/* Create queue file handle */
	QMSG_HANDLE hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
					    pszMessageFile);

	if (hMessage == INVALID_QMSG_HANDLE)
		return ErrGetErrorCode();

	/* Init message statistics */
	QueInitMessageStats(hQueue, hMessage);

	/* Try to re-commit the frozen message */
	if (QueCommitMessage(hQueue, hMessage) < 0) {
		ErrorPush();
		QueCloseMessage(hQueue, hMessage);
		return ErrorPop();
	}

	return 0;
}

int QueUtDeleteFrozenMessage(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
			     char const *pszMessageFile)
{
	/* Create queue file handle */
	QMSG_HANDLE hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
					    pszMessageFile);

	if (hMessage == INVALID_QMSG_HANDLE)
		return ErrGetErrorCode();

	QueCleanupMessage(hQueue, hMessage);
	QueCloseMessage(hQueue, hMessage);

	return 0;
}

int QueUtGetFrozenMsgFile(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
			  char const *pszMessageFile, char const *pszOutFile)
{
	/* Create queue file handle */
	QMSG_HANDLE hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
					    pszMessageFile);

	if (hMessage == INVALID_QMSG_HANDLE)
		return ErrGetErrorCode();

	/* Get slog file path */
	char szQueueFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szQueueFilePath);

	/* Copy the requested file */
	if (MscCopyFile(pszOutFile, szQueueFilePath) < 0) {
		ErrorPush();
		QueCloseMessage(hQueue, hMessage);
		return ErrorPop();
	}
	QueCloseMessage(hQueue, hMessage);

	return 0;
}

int QueUtGetFrozenLogFile(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
			  char const *pszMessageFile, char const *pszOutFile)
{
	/* Create queue file handle */
	QMSG_HANDLE hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
					    pszMessageFile);

	if (hMessage == INVALID_QMSG_HANDLE)
		return ErrGetErrorCode();

	/* Get slog file path */
	char szQueueFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szQueueFilePath, QUEUE_SLOG_DIR);

	/* Copy the requested file */
	if (MscCopyFile(pszOutFile, szQueueFilePath) < 0) {
		ErrorPush();
		QueCloseMessage(hQueue, hMessage);
		return ErrorPop();
	}

	QueCloseMessage(hQueue, hMessage);

	return 0;
}

int QueUtErrLogMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char const *pszFormat, ...)
{
	/* Get slog file path */
	char szSlogFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szSlogFilePath, QUEUE_SLOG_DIR);

	char *pszMessage = NULL;

	StrVSprint(pszMessage, pszFormat, pszFormat);

	if (pszMessage == NULL)
		return ErrGetErrorCode();

	if (ErrFileLogString(szSlogFilePath, pszMessage) < 0) {
		SysFree(pszMessage);
		return ErrGetErrorCode();
	}
	SysFree(pszMessage);

	return 0;
}

static char *QueUtGetLogEntryVar(char const *pszLog, char const *pszVarName)
{
	int iVarLength, iLenght = strlen(pszVarName);
	char const *pszEPos, *pszTmp, *pszEnd;

	for (pszEPos = pszLog;;) {
		if ((pszTmp = strstr(pszEPos, pszVarName)) == NULL)
			return NULL;
		pszEPos = pszTmp + 1;
		if (pszTmp > pszLog && pszTmp[-1] != '\n')
			continue;
		pszTmp += iLenght;
		StrSkipSpaces(pszTmp);
		if (*pszTmp == '=') {
			pszEPos = pszTmp + 1;
			break;
		}
	}
	StrSkipSpaces(pszEPos);

	if ((pszEnd = strchr(pszEPos, '\r')) == NULL &&
	    (pszEnd = strchr(pszEPos, '\n')) == NULL)
		pszEnd = pszEPos + strlen(pszEPos);

	if (*pszEPos == '"') {
		++pszEPos;
		if (pszEnd[-1] == '"')
			pszEnd--;
	}
	if ((iVarLength = (int) (pszEnd - pszEPos)) <= 0)
		return NULL;

	char *pszVarValue = (char *) SysAlloc(iVarLength + 1);

	if (pszVarValue != NULL) {
		memcpy(pszVarValue, pszEPos, iVarLength);
		pszVarValue[iVarLength] = '\0';
	}

	return pszVarValue;
}

int QueUtGetLastLogInfo(char const *pszLogFilePath, QueLogInfo *pQLI)
{
	char *pszEntry = QueLoadLastLogEntry(pszLogFilePath);

	pQLI->pszReason = pQLI->pszServer = NULL;

	if (pszEntry == NULL)
		return ErrGetErrorCode();

	pQLI->pszReason = QueUtGetLogEntryVar(pszEntry, SMTP_ERROR_VARNAME);
	pQLI->pszServer = QueUtGetLogEntryVar(pszEntry, SMTP_SERVER_VARNAME);

	SysFree(pszEntry);

	return 0;
}

void QueUtFreeLastLogInfo(QueLogInfo *pQLI)
{
	SysFree(pQLI->pszServer);
	SysFree(pQLI->pszReason);
}

bool QueUtRemoveSpoolErrors(void)
{
	return SvrTestConfigFlag("RemoveSpoolErrors", false);
}

static char *QueUtGetReplyAddress(SPLF_HANDLE hFSpool)
{
	/* Extract the sender */
	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);
	int iFromDomains = StrStringsCount(ppszFrom);

	if (iFromDomains == 0) {
		ErrSetErrorCode(ERR_NULL_SENDER);
		return NULL;
	}

	char const *pszSender = ppszFrom[iFromDomains - 1];
	char szSenderDomain[MAX_ADDR_NAME] = "";
	char szSenderName[MAX_ADDR_NAME] = "";

	if (USmtpSplitEmailAddr(pszSender, szSenderName, szSenderDomain) < 0)
		return SysStrDup(pszSender);

	/* Lookup special reply-to header tags */

	return SysStrDup(pszSender);
}

static int QueUtBuildErrorResponse(char const *pszSMTPDomain, SPLF_HANDLE hFSpool,
				   char const *pszFrom, char const *pszSmtpFrom, char const *pszTo,
				   char const *pszResponseFile, char const *pszReason,
				   char const *pszText, char const *pszServer, int iLinesExtra,
				   char const *pszLogFile)
{
	char const *pszSpoolFileName = USmlGetSpoolFile(hFSpool);
	char const *pszSmtpMsgID = USmlGetSmtpMessageID(hFSpool);

	/* Retrieve a new message ID */
	SYS_UINT64 ullMessageID = 0;

	if (SvrGetMessageID(&ullMessageID) < 0)
		return ErrGetErrorCode();

	FILE *pRespFile = fopen(pszResponseFile, "wb");

	if (pRespFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszResponseFile);
		return ERR_FILE_CREATE;
	}

	/* Try to remap target user address */
	char szDomain[MAX_ADDR_NAME] = "";
	char szName[MAX_ADDR_NAME] = "";
	char szTo[MAX_ADDR_NAME] = "";

	if (USmlMapAddress(pszTo, szDomain, szName) < 0)
		StrSNCpy(szTo, pszTo);
	else
		SysSNPrintf(szTo, sizeof(szTo) - 1, "%s@%s", szName, szDomain);

	/* Get the current time string */
	char szTime[128] = "";

	MscGetTimeStr(szTime, sizeof(szTime) - 1);

	/* Write info line */
	USmtpWriteInfoLine(pRespFile, LOCAL_ADDRESS_SQB ":0",
			   LOCAL_ADDRESS_SQB ":0", szTime);

	/* Write domain */
	fprintf(pRespFile, "%s\r\n", pszSMTPDomain);

	/* Write message ID */
	fprintf(pRespFile, "X" SYS_LLX_FMT "\r\n", ullMessageID);

	/* Write MAIL FROM */
	fprintf(pRespFile, "MAIL FROM:<%s>\r\n", pszSmtpFrom);

	/* Write RCPT TO */
	fprintf(pRespFile, "RCPT TO:<%s>\r\n", szTo);

	/* Write SPOOL_FILE_DATA_START */
	fprintf(pRespFile, "%s\r\n", SPOOL_FILE_DATA_START);

	/* Write Date ( mail data ) */
	fprintf(pRespFile, "Date:   %s\r\n", szTime);

	/* Write X-MessageId ( mail data ) */
	fprintf(pRespFile, "X-MessageId: <%s>\r\n", pszSpoolFileName);

	/* Write X-SmtpMessageId ( mail data ) */
	fprintf(pRespFile, "X-SmtpMessageId: <%s>\r\n", pszSmtpMsgID);

	/* Write From ( mail data ) */
	fprintf(pRespFile, "From: %s PostMaster <%s>\r\n", pszSMTPDomain, pszFrom);

	/* Write To ( mail data ) */
	fprintf(pRespFile, "To: %s\r\n", pszTo);

	/* Write Subject ( mail data ) */
	fprintf(pRespFile, "Subject: Error sending message [%s] from [%s]\r\n",
		pszSpoolFileName, pszSMTPDomain);

	/* Write X-MailerServer ( mail data ) */
	fprintf(pRespFile, "%s: %s\r\n", QUE_MAILER_HDR, APP_NAME_VERSION_STR);

	/* Write X-SMTP-Mailer-Error ( mail data ) */
	fprintf(pRespFile, "%s: Message = [%s] Server = [%s]\r\n",
		QUE_SMTP_MAILER_ERROR_HDR, pszSpoolFileName, pszSMTPDomain);

	/* Write blank line ( mail data ) */
	fprintf(pRespFile, "\r\n");

	/* Write XMail bounce identifier */
	char const *pszMailFrom = USmlMailFrom(hFSpool);
	char const *pszRcptTo = USmlRcptTo(hFSpool);

	fprintf(pRespFile, "[<00>] XMail bounce: Rcpt=[%s];Error=[%s]\r\n\r\n\r\n",
		pszRcptTo, (pszReason != NULL) ? pszReason: "");

	/* Write error message ( mail data ) */
	fprintf(pRespFile, "[<01>] Error sending message [%s] from [%s].\r\n\r\n"
		"ID:        <%s>\r\n"
		"Mail From: <%s>\r\n"
		"Rcpt To:   <%s>\r\n",
		pszSpoolFileName, pszSMTPDomain, pszSmtpMsgID, pszMailFrom, pszRcptTo);

	if (pszServer != NULL) {
		SYS_INET_ADDR SvrAddr;
		char szIP[128] = "???.???.???.???";
		char szServer[MAX_HOST_NAME] = "";

		if (MscGetServerAddress(pszServer, SvrAddr) == 0 &&
		    SysGetHostByAddr(SvrAddr, szServer, sizeof(szServer)) == 0) {
			pszServer = szServer;
			SysInetNToA(SvrAddr, szIP, sizeof(szIP));
		} else
			StrSNCpy(szIP, pszServer);

		fprintf(pRespFile, "Server:    <%s> [%s]\r\n", pszServer, szIP);
	}

	fprintf(pRespFile, "\r\n\r\n");

	/* Emit the delivery failure reason */
	if (pszReason != NULL)
		fprintf(pRespFile, "[<02>] The reason of the delivery failure was:\r\n\r\n"
			"%s\r\n\r\n\r\n", pszReason);

	/* Emit extra text */
	if (pszText != NULL)
		fprintf(pRespFile, "[<03>] Note:\r\n\r\n" "%s\r\n\r\n\r\n", pszText);

	/* Dump the log file associated with the message */
	char szBuffer[1536] = "";

	if (pszLogFile != NULL) {
		FILE *pLogFile = fopen(pszLogFile, "rb");

		if (pLogFile != NULL) {
			fprintf(pRespFile, "[<04>] Here is listed the message log file:\r\n\r\n");

			while (MscGetString(pLogFile, szBuffer, sizeof(szBuffer) - 1) != NULL)
				fprintf(pRespFile, "%s\r\n", szBuffer);

			fprintf(pRespFile, "\r\n\r\n");
			fclose(pLogFile);
		}
	}
	/* This function retrieve the spool file message section and sync the content. */
	/* This is necessary before reading the file */
	FileSection FSect;

	if (USmlGetMsgFileSection(hFSpool, FSect) < 0) {
		ErrorPush();
		fclose(pRespFile);
		SysRemove(pszResponseFile);
		return ErrorPop();
	}
	/* Open spool file */
	FILE *pMsgFile = fopen(FSect.szFilePath, "rb");

	if (pMsgFile == NULL) {
		fclose(pRespFile);
		SysRemove(pszResponseFile);

		ErrSetErrorCode(ERR_FILE_OPEN, FSect.szFilePath);
		return ERR_FILE_OPEN;
	}
	/* Seek at the beginning of the message ( headers section ) */
	Sys_fseek(pMsgFile, FSect.llStartOffset, SEEK_SET);

	fprintf(pRespFile, "[<05>] Here is listed the initial part of the message:\r\n\r\n");

	bool bInHeaders = true;

	while (MscGetString(pMsgFile, szBuffer, sizeof(szBuffer) - 1) != NULL) {
		char *pszXDomain, *pszTmp;

		/* Mail error loop deteced */
		if (StrNComp(szBuffer, QUE_SMTP_MAILER_ERROR_HDR) == 0 &&
		    (pszXDomain = strrchr(szBuffer, '[')) != NULL &&
		    (pszTmp = strrchr(++pszXDomain, ']')) != NULL &&
		    strnicmp(pszXDomain, pszSMTPDomain, pszTmp - pszXDomain) == 0){
			fclose(pMsgFile);
			fclose(pRespFile);
			SysRemove(pszResponseFile);

			ErrSetErrorCode(ERR_MAIL_ERROR_LOOP);
			return ERR_MAIL_ERROR_LOOP;
		}

		if (bInHeaders && IsEmptyString(szBuffer))
			bInHeaders = false;

		if (!bInHeaders && (iLinesExtra-- < 0))
			break;

		fprintf(pRespFile, "%s\r\n", szBuffer);
	}
	fclose(pMsgFile);
	fclose(pRespFile);

	return 0;
}

static int QueUtTXErrorNotifySender(SPLF_HANDLE hFSpool, char const *pszAdminAddrVar,
				    char const *pszReason, char const *pszText,
				    char const *pszServer, char const *pszLogFile)
{
	/* Extract the sender */
	char *pszReplyTo = QueUtGetReplyAddress(hFSpool);

	if (pszReplyTo == NULL)
		return ErrGetErrorCode();

	/* Load configuration handle */
	SVRCFG_HANDLE hSvrConfig = SvrGetConfigHandle();

	if (hSvrConfig == INVALID_SVRCFG_HANDLE) {
		ErrorPush();
		SysFree(pszReplyTo);
		return ErrorPop();
	}

	char szPMAddress[MAX_ADDR_NAME] = "";
	char szMailDomain[MAX_HOST_NAME] = "";

	if (SvrConfigVar("PostMaster", szPMAddress, sizeof(szPMAddress) - 1, hSvrConfig) < 0 ||
	    SvrConfigVar("RootDomain", szMailDomain, sizeof(szMailDomain) - 1, hSvrConfig) < 0)
	{
		SvrReleaseConfigHandle(hSvrConfig);
		SysFree(pszReplyTo);

		ErrSetErrorCode(ERR_INCOMPLETE_CONFIG);
		return ERR_INCOMPLETE_CONFIG;
	}
	/* Get message handle */
	int iMsgLinesExtra = SvrGetConfigInt("NotifyMsgLinesExtra", 0, hSvrConfig);
	bool bSenderLog = SvrTestConfigFlag("NotifySendLogToSender", false, hSvrConfig);
	bool bNoSenderBounce = SvrTestConfigFlag("NoSenderBounce", false, hSvrConfig);
	QMSG_HANDLE hMessage = QueCreateMessage(hSpoolQueue);

	if (hMessage == INVALID_QMSG_HANDLE) {
		ErrorPush();
		SvrReleaseConfigHandle(hSvrConfig);
		SysFree(pszReplyTo);
		return ErrorPop();
	}

	char szQueueFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

	/* Build error response mail file */
	if (QueUtBuildErrorResponse(szMailDomain, hFSpool, szPMAddress,
				    bNoSenderBounce ? "": szPMAddress, pszReplyTo,
				    szQueueFilePath, pszReason, pszText, pszServer,
				    iMsgLinesExtra, bSenderLog ? pszLogFile : NULL) < 0) {
		ErrorPush();
		QueCleanupMessage(hSpoolQueue, hMessage);
		QueCloseMessage(hSpoolQueue, hMessage);
		SvrReleaseConfigHandle(hSvrConfig);
		SysFree(pszReplyTo);
		return ErrorPop();
	}

	SysFree(pszReplyTo);

	/* Send error response mail file */
	if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
		ErrorPush();
		QueCleanupMessage(hSpoolQueue, hMessage);
		QueCloseMessage(hSpoolQueue, hMessage);
		SvrReleaseConfigHandle(hSvrConfig);
		return ErrorPop();
	}

	/* Notify "error-handler" admin */
	char szEHAdmin[MAX_ADDR_NAME] = "";

	if (SvrConfigVar(pszAdminAddrVar, szEHAdmin, sizeof(szEHAdmin) - 1, hSvrConfig) == 0 &&
	    !IsEmptyString(szEHAdmin)) {
		/* Get message handle */
		if ((hMessage = QueCreateMessage(hSpoolQueue)) == INVALID_QMSG_HANDLE) {
			ErrorPush();
			SvrReleaseConfigHandle(hSvrConfig);
			return ErrorPop();
		}

		QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

		/* Build error response mail file */
		if (QueUtBuildErrorResponse(szMailDomain, hFSpool, szPMAddress,
					    bNoSenderBounce ? "": szPMAddress, szEHAdmin,
					    szQueueFilePath, pszReason, pszText,
					    pszServer, iMsgLinesExtra, pszLogFile) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			SvrReleaseConfigHandle(hSvrConfig);
			return ErrorPop();
		}
		/* Send error response mail file */
		if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			SvrReleaseConfigHandle(hSvrConfig);
			return ErrorPop();
		}
	}
	SvrReleaseConfigHandle(hSvrConfig);

	return 0;
}

static int QueUtTXErrorNotifyRoot(SPLF_HANDLE hFSpool, char const *pszReason,
				  char const *pszLogFile)
{
	/* Load configuration handle */
	SVRCFG_HANDLE hSvrConfig = SvrGetConfigHandle();

	if (hSvrConfig == INVALID_SVRCFG_HANDLE)
		return ErrGetErrorCode();

	char szPMAddress[MAX_ADDR_NAME] = "";
	char szMailDomain[MAX_HOST_NAME] = "";

	if (SvrConfigVar("PostMaster", szPMAddress, sizeof(szPMAddress) - 1, hSvrConfig) < 0 ||
	    SvrConfigVar("RootDomain", szMailDomain, sizeof(szMailDomain) - 1, hSvrConfig) < 0)
	{
		SvrReleaseConfigHandle(hSvrConfig);

		ErrSetErrorCode(ERR_INCOMPLETE_CONFIG);
		return ERR_INCOMPLETE_CONFIG;
	}
	/* Get message handle */
	int iMsgLinesExtra = SvrGetConfigInt("NotifyMsgLinesExtra", 0, hSvrConfig);
	QMSG_HANDLE hMessage = QueCreateMessage(hSpoolQueue);

	if (hMessage == INVALID_QMSG_HANDLE) {
		ErrorPush();
		SvrReleaseConfigHandle(hSvrConfig);
		return ErrorPop();
	}

	char szQueueFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

	/* Build error response mail file */
	if (QueUtBuildErrorResponse(szMailDomain, hFSpool, szPMAddress, szPMAddress,
				    szPMAddress, szQueueFilePath, pszReason, NULL, NULL,
				    iMsgLinesExtra, pszLogFile) < 0) {
		ErrorPush();
		QueCleanupMessage(hSpoolQueue, hMessage);
		QueCloseMessage(hSpoolQueue, hMessage);
		SvrReleaseConfigHandle(hSvrConfig);
		return ErrorPop();
	}
	SvrReleaseConfigHandle(hSvrConfig);

	/* Send error response mail file */
	if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
		ErrorPush();
		QueCleanupMessage(hSpoolQueue, hMessage);
		QueCloseMessage(hSpoolQueue, hMessage);
		return ErrorPop();
	}

	return 0;
}

static int QueUtTXErrorExNotifyRoot(SPLF_HANDLE hFSpool, char const *pszMessFilePath,
				    char const *pszReason, char const *pszLogFile)
{
	bool bCloseHSpool = false;

	if (hFSpool == INVALID_SPLF_HANDLE) {
		if ((hFSpool = USmlCreateHandle(pszMessFilePath)) == INVALID_SPLF_HANDLE)
			return ErrGetErrorCode();
		bCloseHSpool = true;
	}

	int iResult = QueUtTXErrorNotifyRoot(hFSpool, pszReason, pszLogFile);

	if (bCloseHSpool)
		USmlCloseHandle(hFSpool);

	return iResult;
}

static int QueUtTXErrorExNotifySender(SPLF_HANDLE hFSpool, char const *pszMessFilePath,
				      char const *pszAdminAddrVar, char const *pszReason,
				      char const *pszText, char const *pszServer,
				      char const *pszLogFile)
{
	bool bCloseHSpool = false;

	if (hFSpool == INVALID_SPLF_HANDLE) {
		if ((hFSpool = USmlCreateHandle(pszMessFilePath)) == INVALID_SPLF_HANDLE)
			return ErrGetErrorCode();
		bCloseHSpool = true;
	}

	int iResult = QueUtTXErrorNotifySender(hFSpool, pszAdminAddrVar, pszReason,
					       pszText, pszServer, pszLogFile);

	if (bCloseHSpool)
		USmlCloseHandle(hFSpool);

	return iResult;
}

int QueUtNotifyPermErrDelivery(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
			       SPLF_HANDLE hFSpool, char const *pszReason,
			       char const *pszServer, bool bCleanup)
{
	/* Get message file path */
	char szQueueFilePath[SYS_MAX_PATH] = "";
	char szQueueLogFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szQueueFilePath);
	QueGetFilePath(hQueue, hMessage, szQueueLogFilePath, QUEUE_SLOG_DIR);

	/* Load log information from the slog file */
	bool bFreeLogInfo = false;
	QueLogInfo QLI;

	if (pszReason == NULL || pszServer == NULL) {
		QueUtGetLastLogInfo(szQueueLogFilePath, &QLI);

		if (pszReason == NULL)
			pszReason = QLI.pszReason;
		if (pszServer == NULL)
			pszServer = QLI.pszServer;
		bFreeLogInfo = true;
	}

	int iResult = QueUtTXErrorExNotifySender(hFSpool, szQueueFilePath,
						 "ErrorsAdmin", pszReason, NULL,
						 pszServer, szQueueLogFilePath);

	if (bCleanup)
		QueCleanupMessage(hQueue, hMessage, !QueUtRemoveSpoolErrors());
	if (bFreeLogInfo)
		QueUtFreeLastLogInfo(&QLI);

	return iResult;
}

int QueUtNotifyTempErrDelivery(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
			       SPLF_HANDLE hFSpool, char const *pszReason,
			       char const *pszText, char const *pszServer)
{
	/* Get message file path */
	char szQueueLogFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szQueueLogFilePath, QUEUE_SLOG_DIR);

	/* Load log information from the slog file */
	bool bFreeLogInfo = false;
	QueLogInfo QLI;

	if (pszReason == NULL || pszServer == NULL) {
		QueUtGetLastLogInfo(szQueueLogFilePath, &QLI);
		if (pszReason == NULL)
			pszReason = QLI.pszReason;
		if (pszServer == NULL)
			pszServer = QLI.pszServer;
		bFreeLogInfo = true;
	}

	int iResult = QueUtTXErrorNotifySender(hFSpool, "TempErrorsAdmin",
					       pszReason, pszText, pszServer,
					       szQueueLogFilePath);

	if (bFreeLogInfo)
		QueUtFreeLastLogInfo(&QLI);

	return iResult;
}

int QueUtCleanupNotifyRoot(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
			   SPLF_HANDLE hFSpool, char const *pszReason)
{
	/* Get message file path */
	char szQueueFilePath[SYS_MAX_PATH] = "";
	char szQueueLogFilePath[SYS_MAX_PATH] = "";

	QueGetFilePath(hQueue, hMessage, szQueueFilePath);
	QueGetFilePath(hQueue, hMessage, szQueueLogFilePath, QUEUE_SLOG_DIR);

	/* Load log information from the slog file */
	bool bFreeLogInfo = false;
	QueLogInfo QLI;

	if (pszReason == NULL) {
		QueUtGetLastLogInfo(szQueueLogFilePath, &QLI);

		pszReason = QLI.pszReason;
		bFreeLogInfo = true;
	}

	int iResult = QueUtTXErrorExNotifyRoot(hFSpool, szQueueFilePath, pszReason,
					       szQueueLogFilePath);

	if (bFreeLogInfo)
		QueUtFreeLastLogInfo(&QLI);
	QueCleanupMessage(hQueue, hMessage, !QueUtRemoveSpoolErrors());

	return iResult;
}

int QueUtResendMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, SPLF_HANDLE hFSpool)
{
	/* Try to resend the message */
	int iResult = QueResendMessage(hQueue, hMessage);

	/* If the message is expired ... */
	if (iResult == ERR_SPOOL_FILE_EXPIRED) {
		/* Handle notifications and cleanup the message */
		iResult = QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool,
						     "The maximum number of delivery attempts has been reached",
						     NULL, true);

		QueCloseMessage(hQueue, hMessage);
	}

	return iResult;
}

