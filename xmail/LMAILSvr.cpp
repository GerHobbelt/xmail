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
#include "MessQueue.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "SMTPUtils.h"
#include "SMAILUtils.h"
#include "ExtAliases.h"
#include "UsrMailList.h"
#include "MailConfig.h"
#include "LMAILSvr.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define LMAIL_SERVER_NAME           "[" APP_NAME_VERSION_STR " LMAIL Server]"
#define LOCAL_SPOOL_DIR             "local"
#define LMAIL_LOG_FILE              "lmail"

static int LMAILThreadCountAdd(long lCount, SHB_HANDLE hShbLMAIL, LMAILConfig *pLMAILCfg = NULL);
static int LMAILLogEnabled(SHB_HANDLE hShbLMAIL, LMAILConfig *pLMAILCfg = NULL);

char *LMAILGetSpoolDir(char *pszSpoolPath, int iMaxPath)
{
	SvrGetSpoolDir(pszSpoolPath, iMaxPath);

	AppendSlash(pszSpoolPath);
	StrNCat(pszSpoolPath, LOCAL_SPOOL_DIR, iMaxPath);

	return pszSpoolPath;
}

static LMAILConfig *LMAILGetConfigCopy(SHB_HANDLE hShbLMAIL)
{
	LMAILConfig *pLMAILCfg = (LMAILConfig *) ShbLock(hShbLMAIL);

	if (pLMAILCfg == NULL)
		return NULL;

	LMAILConfig *pNewLMAILCfg = (LMAILConfig *) SysAlloc(sizeof(LMAILConfig));

	if (pNewLMAILCfg != NULL)
		memcpy(pNewLMAILCfg, pLMAILCfg, sizeof(LMAILConfig));

	ShbUnlock(hShbLMAIL);

	return pNewLMAILCfg;
}

static int LMAILThreadCountAdd(long lCount, SHB_HANDLE hShbLMAIL, LMAILConfig *pLMAILCfg)
{
	int iDoUnlock = 0;

	if (pLMAILCfg == NULL) {
		if ((pLMAILCfg = (LMAILConfig *) ShbLock(hShbLMAIL)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}

	pLMAILCfg->lThreadCount += lCount;

	if (iDoUnlock)
		ShbUnlock(hShbLMAIL);

	return 0;
}

static int LMAILLogEnabled(SHB_HANDLE hShbLMAIL, LMAILConfig *pLMAILCfg)
{
	int iDoUnlock = 0;

	if (pLMAILCfg == NULL) {
		if ((pLMAILCfg = (LMAILConfig *) ShbLock(hShbLMAIL)) == NULL)
			return ErrGetErrorCode();
		++iDoUnlock;
	}

	unsigned long ulFlags = pLMAILCfg->ulFlags;

	if (iDoUnlock)
		ShbUnlock(hShbLMAIL);

	return (ulFlags & LMAILF_LOG_ENABLED) ? 1 : 0;
}

static int LMAILGetFilesSnapShot(LMAILConfig *pLMAILCfg, long lThreadId, char *pszSSFileName,
				 int iMaxSSFileName)
{
	char szSpoolDir[SYS_MAX_PATH] = "";

	LMAILGetSpoolDir(szSpoolDir, sizeof(szSpoolDir));

	/* Share lock local spool directory */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szSpoolDir, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	UsrGetTmpFile(NULL, pszSSFileName, iMaxSSFileName);

	FILE *pSSFile = fopen(pszSSFileName, "wb");

	if (pSSFile == NULL) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}

	int iFileCount = 0;
	char szSpoolFileName[SYS_MAX_PATH] = "";
	FSCAN_HANDLE hFileScan = MscFirstFile(szSpoolDir, 0, szSpoolFileName,
					      sizeof(szSpoolFileName));

	if (hFileScan != INVALID_FSCAN_HANDLE) {
		do {
			unsigned long ulHashValue =
				MscHashString(szSpoolFileName, strlen(szSpoolFileName));

			if ((ulHashValue % (unsigned long) pLMAILCfg->lNumThreads) ==
			    (unsigned long) lThreadId) {
				fprintf(pSSFile, "%s\r\n", szSpoolFileName);
				++iFileCount;
			}
		} while (MscNextFile(hFileScan, szSpoolFileName, sizeof(szSpoolFileName)));
		MscCloseFindFile(hFileScan);
	}
	fclose(pSSFile);
	RLckUnlockSH(hResLock);

	if (iFileCount == 0) {
		SysRemove(pszSSFileName);
		SetEmptyString(pszSSFileName);

		ErrSetErrorCode(ERR_NO_LOCAL_SPOOL_FILES);
		return ERR_NO_LOCAL_SPOOL_FILES;
	}

	return 0;
}

static int LMAILRemoveProcessed(LMAILConfig *pLMAILCfg, char const *pszListFileName)
{
	char szSpoolDir[SYS_MAX_PATH] = "";

	LMAILGetSpoolDir(szSpoolDir, sizeof(szSpoolDir));

	/* Share lock local spool directory */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szSpoolDir, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	FILE *pSSFile = fopen(pszListFileName, "rb");

	if (pSSFile == NULL) {
		ErrorPush();
		RLckUnlockEX(hResLock);
		return ErrorPop();
	}

	char szSpoolFileName[SYS_MAX_PATH] = "";

	while (MscGetString(pSSFile, szSpoolFileName, sizeof(szSpoolFileName) - 1) != NULL) {
		char szSpoolFilePath[SYS_MAX_PATH] = "";

		sprintf(szSpoolFilePath, "%s%s%s", szSpoolDir, SYS_SLASH_STR, szSpoolFileName);
		CheckRemoveFile(szSpoolFilePath);
	}
	fclose(pSSFile);
	RLckUnlockEX(hResLock);

	return 0;
}

static int LMAILLogMessage(char const *pszMailFile, char const *pszSMTPDomain,
			   char const *pszMessageID)
{
	char szTime[256] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1);

	char szLocalFile[SYS_MAX_PATH] = "";

	MscGetFileName(pszMailFile, szLocalFile);

	RLCK_HANDLE hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR LMAIL_LOG_FILE);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	MscFileLog(LMAIL_LOG_FILE,
		   "\"%s\""
		   "\t\"%s\""
		   "\t\"%s\"" "\t\"%s\"" "\n", pszSMTPDomain, szLocalFile, pszMessageID, szTime);

	RLckUnlockEX(hResLock);

	return 0;
}

static int LMAILAddReceived(FILE *pSpoolFile, char const *pszSMTPDomain,
			    char const *pszMailFrom, char const *pszRcptTo, char const *pszTime)
{
	int iError;
	char szFrom[MAX_SMTP_ADDRESS] = "";
	char szRcpt[MAX_SMTP_ADDRESS] = "";

	if ((iError = USmlParseAddress(pszMailFrom, NULL, 0, szFrom,
				       sizeof(szFrom) - 1)) < 0 &&
	    iError != ERR_EMPTY_ADDRESS)
		return iError;
	if (USmlParseAddress(pszRcptTo, NULL, 0, szRcpt, sizeof(szRcpt) - 1) < 0)
		return ErrGetErrorCode();

	/* Add "Received:" tag */
	fprintf(pSpoolFile,
		"Received: from /spool/local\r\n"
		"\tby %s with %s\r\n"
		"\tfor <%s> from <%s>;\r\n"
		"\t%s\r\n", pszSMTPDomain, LMAIL_SERVER_NAME, szRcpt, szFrom, pszTime);

	return 0;
}

static int LMAILSubmitLocalFile(LMAILConfig *pLMAILCfg, const char *pszMailFile,
				long lThreadId, char const *pszSMTPDomain)
{
	FILE *pMailFile = fopen(pszMailFile, "rb");

	if (pMailFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN);
		return ERR_FILE_OPEN;
	}
	/* Get a message ID */
	SYS_UINT64 uMessageID;
	char szMessageID[128] = "";

	if (SvrGetMessageID(&uMessageID) < 0) {
		ErrorPush();
		fclose(pMailFile);
		return ErrorPop();
	}

	sprintf(szMessageID, "L" SYS_LLX_FMT, uMessageID);

	/* Get current time */
	char szTime[256] = "";

	MscGetTimeStr(szTime, sizeof(szTime) - 1);

	/* Log current opeartion */
	if (LMAILLogEnabled(SHB_INVALID_HANDLE, pLMAILCfg))
		LMAILLogMessage(pszMailFile, pszSMTPDomain, szMessageID);

	/* Search mail data start */
	char szSpoolLine[MAX_SPOOL_LINE] = "";

	while (MscGetString(pMailFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL)
		if (IsEmptyString(szSpoolLine))
			break;

	if (feof(pMailFile)) {
		fclose(pMailFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszMailFile);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Get the offset at which the message data begin and rewind the file */
	unsigned long ulMsgOffset = (unsigned long) ftell(pMailFile);

	rewind(pMailFile);

	/* Read "MAIL FROM:" ( 1th row of the smtp-mail file ) */
	char szMailFrom[MAX_SPOOL_LINE] = "";

	if (MscGetString(pMailFile, szMailFrom, sizeof(szMailFrom) - 1) == NULL ||
	    StrINComp(szMailFrom, MAIL_FROM_STR) != 0) {
		fclose(pMailFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszMailFile);
		return ERR_INVALID_SPOOL_FILE;
	}

	/* Read "RCPT TO:" ( 2nd[,...] row(s) of the local-mail file ) */
	while ((MscGetString(pMailFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL) &&
	       !IsEmptyString(szSpoolLine)) {
		/* Get message handle */
		QMSG_HANDLE hMessage = QueCreateMessage(hSpoolQueue);

		if (hMessage == INVALID_QMSG_HANDLE) {
			ErrorPush();
			fclose(pMailFile);
			return ErrorPop();
		}

		char szQueueFilePath[SYS_MAX_PATH] = "";

		QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

		FILE *pSpoolFile = fopen(szQueueFilePath, "wb");

		if (pSpoolFile == NULL) {
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			fclose(pMailFile);
			ErrSetErrorCode(ERR_FILE_CREATE);
			return ERR_FILE_CREATE;
		}
		/* Write info line */
		USmtpWriteInfoLine(pSpoolFile, LOCAL_ADDRESS_SQB ":0",
				   LOCAL_ADDRESS_SQB ":0", szTime);

		/* Write SMTP domain */
		fprintf(pSpoolFile, "%s\r\n", pszSMTPDomain);

		/* Write message ID */
		fprintf(pSpoolFile, "%s\r\n", szMessageID);

		/* Write "MAIL FROM:" */
		fprintf(pSpoolFile, "%s\r\n", szMailFrom);

		/* Write "RCPT TO:" */
		fprintf(pSpoolFile, "%s\r\n", szSpoolLine);

		/* Write SPOOL_FILE_DATA_START */
		fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

		/* Write "Received:" tag */
		LMAILAddReceived(pSpoolFile, pszSMTPDomain, szMailFrom, szSpoolLine, szTime);

		/* Write mail data, saving and restoring the current file pointer */
		unsigned long ulCurrOffset = (unsigned long) ftell(pMailFile);

		if (MscCopyFile(pSpoolFile, pMailFile, ulMsgOffset, (unsigned long) -1) < 0) {
			ErrorPush();
			fclose(pSpoolFile);
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			fclose(pMailFile);
			return ErrorPop();
		}

		if (SysFileSync(pSpoolFile) < 0) {
			ErrorPush();
			fclose(pSpoolFile);
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			fclose(pMailFile);
			return ErrorPop();
		}
		if (fclose(pSpoolFile)) {
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			fclose(pMailFile);
			ErrSetErrorCode(ERR_FILE_WRITE, szQueueFilePath);
			return ERR_FILE_WRITE;
		}
		fseek(pMailFile, ulCurrOffset, SEEK_SET);

		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			fclose(pMailFile);
			return ErrorPop();
		}
	}
	fclose(pMailFile);

	return 0;
}

static int LMAILProcessList(LMAILConfig *pLMAILCfg, long lThreadId, char const *pszSSFileName)
{
	char szSpoolDir[SYS_MAX_PATH] = "";

	LMAILGetSpoolDir(szSpoolDir, sizeof(szSpoolDir));

	/* Retrieve SMTP domain */
	SVRCFG_HANDLE hSvrConfig = SvrGetConfigHandle();

	if (hSvrConfig == INVALID_SVRCFG_HANDLE)
		return ErrGetErrorCode();

	char szSMTPDomain[MAX_HOST_NAME] = "localdomain";
	char *pszDefDomain = SvrGetConfigVar(hSvrConfig, "RootDomain");

	if (pszDefDomain != NULL) {
		StrSNCpy(szSMTPDomain, pszDefDomain);

		SysFree(pszDefDomain);
	}

	SvrReleaseConfigHandle(hSvrConfig);

	FILE *pSSFile = fopen(pszSSFileName, "rb");

	if (pSSFile == NULL)
		return ErrGetErrorCode();

	char szSpoolFileName[SYS_MAX_PATH] = "";

	while (MscGetString(pSSFile, szSpoolFileName, sizeof(szSpoolFileName) - 1) != NULL) {
		char szSpoolFilePath[SYS_MAX_PATH] = "";

		sprintf(szSpoolFilePath, "%s%s%s", szSpoolDir, SYS_SLASH_STR, szSpoolFileName);

		if (LMAILSubmitLocalFile(pLMAILCfg, szSpoolFilePath, lThreadId, szSMTPDomain) < 0) {
			SysLogMessage(LOG_LEV_ERROR, "LMAIL [%02ld] error ( \"%s\" ): %s\n",
				      lThreadId, ErrGetErrorString(), szSpoolFilePath);

		} else {
			SysLogMessage(LOG_LEV_MESSAGE, "LMAIL [%02ld] file processed: %s\n",
				      lThreadId, szSpoolFilePath);
		}
	}
	fclose(pSSFile);

	return 0;
}

static int LMAILProcessLocalSpool(SHB_HANDLE hShbLMAIL, long lThreadId)
{
	LMAILConfig *pLMAILCfg = LMAILGetConfigCopy(hShbLMAIL);

	if (pLMAILCfg == NULL)
		return ErrGetErrorCode();

	char szSSFileName[SYS_MAX_PATH] = "";

	if (LMAILGetFilesSnapShot(pLMAILCfg, lThreadId, szSSFileName,
				  sizeof(szSSFileName)) < 0) {
		ErrorPush();
		SysFree(pLMAILCfg);
		return ErrorPop();
	}

	if (LMAILProcessList(pLMAILCfg, lThreadId, szSSFileName) < 0) {
		ErrorPush();
		SysRemove(szSSFileName);
		SysFree(pLMAILCfg);
		return ErrorPop();
	}

	LMAILRemoveProcessed(pLMAILCfg, szSSFileName);

	SysRemove(szSSFileName);
	SysFree(pLMAILCfg);

	return 0;
}

unsigned int LMAILThreadProc(void *pThreadData)
{
	LMAILConfig *pLMAILCfg = (LMAILConfig *) ShbLock(hShbLMAIL);

	if (pLMAILCfg == NULL) {
		ErrorPush();
		SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
		return ErrorPop();
	}
	/* Get thread id and sleep timeout */
	int iSleepTimeout = pLMAILCfg->iSleepTimeout;
	long lThreadId = pLMAILCfg->lThreadCount;

	/* Increase thread count */
	LMAILThreadCountAdd(+1, hShbLMAIL, pLMAILCfg);

	ShbUnlock(hShbLMAIL);

	SysLogMessage(LOG_LEV_MESSAGE, "LMAIL thread [%02ld] started\n", lThreadId);

	for (;;) {
		/* Check shutdown condition */
		pLMAILCfg = (LMAILConfig *) ShbLock(hShbLMAIL);

		if (pLMAILCfg == NULL || pLMAILCfg->ulFlags & LMAILF_STOP_SERVER) {
			SysLogMessage(LOG_LEV_MESSAGE, "LMAIL thread [%02ld] exiting\n",
				      lThreadId);

			if (pLMAILCfg != NULL)
				ShbUnlock(hShbLMAIL);
			break;
		}

		ShbUnlock(hShbLMAIL);

		/* Process local spool files */
		int iProcessResult = LMAILProcessLocalSpool(hShbLMAIL, lThreadId);

		if (iProcessResult == ERR_NO_LOCAL_SPOOL_FILES)
			SysSleep(iSleepTimeout);

	}

	/* Decrease thread count */
	LMAILThreadCountAdd(-1, hShbLMAIL);

	SysLogMessage(LOG_LEV_MESSAGE, "LMAIL thread [%02ld] stopped\n", lThreadId);

	return 0;
}

