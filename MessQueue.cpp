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
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "AppDefines.h"
#include "MiscUtils.h"
#include "SMTPUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"

#define QUEF_SHUTDOWN               (1 << 0)

#define QUMF_DELETED                (1 << 0)
#define QUMF_FREEZE                 (1 << 1)

#define QUE_MASK_TMPFLAGS(v)        ((v) & ~(QUMF_DELETED | QUMF_FREEZE))

#define QUE_ARENA_SCAN_INTERVAL     15
#define QUE_ARENA_SCAN_WAIT         2
#define QUE_SCAN_THREAD_MAXWAIT     60

struct MessageQueue {
	SysListHead ReadyQueue;
	SysListHead RsndArenaQueue;
	int iReadyCount;
	int iRsndArenaCount;
	SYS_MUTEX hMutex;
	SYS_EVENT hReadyEvent;
	char *pszRootPath;
	int iMaxRetry;
	int iRetryTimeout;
	int iRetryIncrRatio;
	int iNumDirsLevel;
	unsigned long ulFlags;
	SYS_THREAD hRsndScanThread;
};

struct QueueMessage {
	SysListHead LLink;
	int iLevel1;
	int iLevel2;
	char const *pszQueueDir;
	char *pszFileName;
	int iNumTries;
	time_t tLastTry;
	unsigned long ulFlags;
};

static int QueCreateStruct(const char *pszRootPath);
static int QueLoad(MessageQueue *pMQ);
static int QueLoadMessages(MessageQueue *pMQ, int iLevel1, int iLevel2);
static QueueMessage *QueAllocMessage(int iLevel1, int iLevel2, const char *pszQueueDir,
				     const char *pszFileName, int iNumTries, time_t tLastTry);
static int QueFreeMessage(QueueMessage *pQM);
static int QueFreeMessList(SysListHead *pHead);
static int QueLoadMessageStat(MessageQueue *pMQ, QueueMessage *pQM);
static int QueStatMessage(MessageQueue *pMQ, QueueMessage *pQM);
static int QueGetFilePath(MessageQueue *pMQ, QueueMessage *pQM, char *pszFilePath,
			  char const *pszQueueDir = NULL);
static int QueDoMessageCleanup(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
static int QueAddNew(MessageQueue *pMQ, QueueMessage *pQM);
static bool QueMessageExpired(MessageQueue *pMQ, QueueMessage *pQM);
static time_t QueNextRetryOp(int iNumTries, unsigned int uRetryTimeout,
			     unsigned int uRetryIncrRatio);
static bool QueMessageReadyToSend(MessageQueue *pMQ, QueueMessage *pQM);
static int QueAddRsnd(MessageQueue *pMQ, QueueMessage *pQM);
static unsigned int QueRsndThread(void *pThreadData);
static int QueScanRsndArena(MessageQueue *pMQ);
static bool QueMessageDestMatch(MessageQueue *pMQ, QueueMessage *pQM,
				const char *pszAddressMatch);

static QueueMessage *QueAllocMessage(int iLevel1, int iLevel2, char const *pszQueueDir,
				     char const *pszFileName, int iNumTries, time_t tLastTry)
{
	QueueMessage *pQM = (QueueMessage *) SysAlloc(sizeof(QueueMessage));

	if (pQM == NULL)
		return NULL;

	SYS_INIT_LIST_LINK(&pQM->LLink);
	pQM->iLevel1 = iLevel1;
	pQM->iLevel2 = iLevel2;
	pQM->pszQueueDir = pszQueueDir;
	pQM->pszFileName = SysStrDup(pszFileName);
	pQM->iNumTries = iNumTries;
	pQM->tLastTry = tLastTry;
	pQM->ulFlags = 0;

	return pQM;
}

static int QueFreeMessage(QueueMessage *pQM)
{
	SysFree(pQM->pszFileName);
	SysFree(pQM);

	return 0;
}

static int QueFreeMessList(SysListHead *pHead)
{
	SysListHead *pLLink;

	while ((pLLink = SYS_LIST_FIRST(pHead)) != NULL) {
		QueueMessage *pQM = SYS_LIST_ENTRY(pLLink, QueueMessage, LLink);

		SYS_LIST_DEL(pLLink);
		QueFreeMessage(pQM);
	}

	return 0;
}

static int QueCreateStruct(char const *pszRootPath)
{
	/* Create message dir (new messages queue) */
	char szDirPath[SYS_MAX_PATH];

	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_MESS_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create message resend dir (resend messages queue) */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_RSND_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create info dir */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_INFO_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create temp dir */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_TEMP_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create send log dir */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_SLOG_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create custom message processing dir */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_CUST_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create user custom message processing dir (mailproc.tab cache) */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_MPRC_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	/* Create frozen dir */
	StrSNCpy(szDirPath, pszRootPath);
	AppendSlash(szDirPath);
	StrSNCat(szDirPath, QUEUE_FROZ_DIR);

	if (!SysExistDir(szDirPath) && SysMakeDir(szDirPath) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int QueLoadMessageStat(MessageQueue *pMQ, QueueMessage *pQM)
{
	/* Build the slog file path */
	char szSlogFilePath[SYS_MAX_PATH];

	QueGetFilePath(pMQ, pQM, szSlogFilePath, QUEUE_SLOG_DIR);

	/* Try to load message statistics */
	FILE *pLogFile = fopen(szSlogFilePath, "rt");

	if (pLogFile != NULL) {
		int iNumTries = 0;
		unsigned long ulLastTime = 0;
		unsigned long ulPeekTime;
		char szLogLine[1024];

		while (MscFGets(szLogLine, sizeof(szLogLine) - 1, pLogFile) != NULL)
			if (sscanf(szLogLine, "[PeekTime] %lu", &ulPeekTime) == 1)
				++iNumTries, ulLastTime = ulPeekTime;

		fclose(pLogFile);

		pQM->iNumTries = iNumTries;
		pQM->tLastTry = (time_t) ulLastTime;
	}

	return 0;
}

static int QueLoadMessages(MessageQueue *pMQ, int iLevel1, int iLevel2)
{
	/* File scan the new messages dir */
	char szDirPath[SYS_MAX_PATH];

	SysSNPrintf(szDirPath, sizeof(szDirPath) - 1, "%s%d%s%d%s%s",
		    pMQ->pszRootPath, iLevel1, SYS_SLASH_STR, iLevel2,
		    SYS_SLASH_STR, QUEUE_MESS_DIR);

	char szMsgFileName[SYS_MAX_PATH];
	FSCAN_HANDLE hFileScan = MscFirstFile(szDirPath, 0, szMsgFileName,
					      sizeof(szMsgFileName));

	if (hFileScan != INVALID_FSCAN_HANDLE) {
		do {
			if (!IsDotFilename(szMsgFileName)) {
				QueueMessage *pQM =
					QueAllocMessage(iLevel1, iLevel2, QUEUE_MESS_DIR,
							szMsgFileName, 0, 0);

				if (pQM != NULL) {
					/* Add the file to the message queue */
					SYS_LIST_ADDT(&pQM->LLink, &pMQ->ReadyQueue);
					++pMQ->iReadyCount;
				}
			}
		} while (MscNextFile(hFileScan, szMsgFileName, sizeof(szMsgFileName)));
		MscCloseFindFile(hFileScan);

		/* Set the mess event if the queue is not empty */
		if (pMQ->iReadyCount > 0)
			SysSetEvent(pMQ->hReadyEvent);
	}
	/* File scan the resend messages dir */
	SysSNPrintf(szDirPath, sizeof(szDirPath) - 1, "%s%d%s%d%s%s",
		    pMQ->pszRootPath, iLevel1, SYS_SLASH_STR, iLevel2,
		    SYS_SLASH_STR, QUEUE_RSND_DIR);

	if ((hFileScan = MscFirstFile(szDirPath, 0, szMsgFileName,
				      sizeof(szMsgFileName))) != INVALID_FSCAN_HANDLE) {
		do {
			if (!IsDotFilename(szMsgFileName)) {
				QueueMessage *pQM = QueAllocMessage(iLevel1, iLevel2,
								    QUEUE_RSND_DIR,
								    szMsgFileName, 0, 0);

				if (pQM != NULL) {
					/* Load message statistics */
					if (QueLoadMessageStat(pMQ, pQM) < 0) {
						SysLogMessage(LOG_LEV_ERROR,
							      "Error loading queue file: '%s%d%s%d%s%s%s%s'\n",
							      pMQ->pszRootPath, iLevel1,
							      SYS_SLASH_STR, iLevel2,
							      SYS_SLASH_STR, QUEUE_RSND_DIR,
							      SYS_SLASH_STR, szMsgFileName);

						QueFreeMessage(pQM);
					} else {
						/* Add the file to the resend queue */
						SYS_LIST_ADDT(&pQM->LLink, &pMQ->RsndArenaQueue);
						++pMQ->iRsndArenaCount;
					}
				}
			}
		} while (MscNextFile(hFileScan, szMsgFileName, sizeof(szMsgFileName)));
		MscCloseFindFile(hFileScan);
	}

	return 0;
}

static int QueLoad(MessageQueue *pMQ)
{
	char szCurrPath[SYS_MAX_PATH];

	for (int i = 0; i < pMQ->iNumDirsLevel; i++) {
		SysSNPrintf(szCurrPath, sizeof(szCurrPath) - 1, "%s%d", pMQ->pszRootPath, i);
		if (!SysExistDir(szCurrPath) && (SysMakeDir(szCurrPath) < 0))
			return ErrGetErrorCode();

		for (int j = 0; j < pMQ->iNumDirsLevel; j++) {
			SysSNPrintf(szCurrPath, sizeof(szCurrPath) - 1, "%s%d%s%d",
				    pMQ->pszRootPath, i, SYS_SLASH_STR, j);
			if (!SysExistDir(szCurrPath) && (SysMakeDir(szCurrPath) < 0))
				return ErrGetErrorCode();

			if (QueCreateStruct(szCurrPath) < 0 ||
			    QueLoadMessages(pMQ, i, j) < 0)
				return ErrGetErrorCode();
		}
	}

	return 0;
}

static time_t QueNextRetryOp(int iNumTries, unsigned int uRetryTimeout,
			     unsigned int uRetryIncrRatio)
{
	unsigned int uNextOp = uRetryTimeout;

	if (uRetryIncrRatio != 0)
		for (int i = 1; i < iNumTries; i++)
			uNextOp += uNextOp / uRetryIncrRatio;

	return (time_t) uNextOp;
}

static bool QueMessageReadyToSend(MessageQueue *pMQ, QueueMessage *pQM)
{
	return (time(NULL) > (pQM->tLastTry +
			      QueNextRetryOp(pQM->iNumTries, (unsigned int) pMQ->iRetryTimeout,
					     (unsigned int) pMQ->iRetryIncrRatio)));
}

static int QueScanRsndArena(MessageQueue *pMQ)
{
	if (SysLockMutex(pMQ->hMutex, SYS_INFINITE_TIMEOUT) < 0)
		return ErrGetErrorCode();

	SysListHead *pLLink;

	SYS_LIST_FOR_EACH(pLLink, &pMQ->RsndArenaQueue) {
		QueueMessage *pQM = SYS_LIST_ENTRY(pLLink, QueueMessage, LLink);

		if (QueMessageReadyToSend(pMQ, pQM)) {
			/* Set the list pointer to the next item */
			pLLink = pLLink->pPrev;

			/* Remove item from resend arena */
			SYS_LIST_DEL(&pQM->LLink);
			--pMQ->iRsndArenaCount;

			/* Add item from resend queue */
			SYS_LIST_ADDT(&pQM->LLink, &pMQ->ReadyQueue);
			++pMQ->iReadyCount;
		}
	}
	if (pMQ->iReadyCount > 0)
		SysSetEvent(pMQ->hReadyEvent);
	SysUnlockMutex(pMQ->hMutex);

	return 0;
}

static unsigned int QueRsndThread(void *pThreadData)
{
	MessageQueue *pMQ = (MessageQueue *) pThreadData;
	int iElapsedTime = 0;

	while ((pMQ->ulFlags & QUEF_SHUTDOWN) == 0) {
		SysSleep(QUE_ARENA_SCAN_WAIT);

		iElapsedTime += QUE_ARENA_SCAN_WAIT;
		if (iElapsedTime > QUE_ARENA_SCAN_INTERVAL) {
			iElapsedTime = 0;

			/* Scan rsnd arena to prepare messages to resend */
			QueScanRsndArena(pMQ);
		}
	}
	pMQ->ulFlags &= ~QUEF_SHUTDOWN;

	return 0;
}

QUEUE_HANDLE QueOpen(char const *pszRootPath, int iMaxRetry, int iRetryTimeout,
		     int iRetryIncrRatio, int iNumDirsLevel)
{
	MessageQueue *pMQ = (MessageQueue *) SysAlloc(sizeof(MessageQueue));

	if (pMQ == NULL)
		return INVALID_QUEUE_HANDLE;

	SYS_INIT_LIST_HEAD(&pMQ->ReadyQueue);
	SYS_INIT_LIST_HEAD(&pMQ->RsndArenaQueue);
	pMQ->iReadyCount = 0;
	pMQ->iRsndArenaCount = 0;
	pMQ->iMaxRetry = iMaxRetry;
	pMQ->iRetryTimeout = iRetryTimeout;
	pMQ->iRetryIncrRatio = iRetryIncrRatio;
	pMQ->iNumDirsLevel = iNumDirsLevel;
	pMQ->ulFlags = 0;

	if ((pMQ->hMutex = SysCreateMutex()) == SYS_INVALID_MUTEX) {
		SysFree(pMQ);
		return INVALID_QUEUE_HANDLE;
	}
	if ((pMQ->hReadyEvent = SysCreateEvent(1)) == SYS_INVALID_EVENT) {
		SysCloseMutex(pMQ->hMutex);
		SysFree(pMQ);
		return INVALID_QUEUE_HANDLE;
	}
	/* Set the queue root path */
	char szRootPath[SYS_MAX_PATH];

	StrSNCpy(szRootPath, pszRootPath);
	AppendSlash(szRootPath);

	pMQ->pszRootPath = SysStrDup(szRootPath);

	/* Load queue status */
	if (QueLoad(pMQ) < 0) {
		ErrorPush();
		SysFree(pMQ->pszRootPath);
		SysCloseEvent(pMQ->hReadyEvent);
		SysCloseMutex(pMQ->hMutex);
		SysFree(pMQ);

		ErrSetErrorCode(ErrorPop());
		return INVALID_QUEUE_HANDLE;
	}
	/* Start rsnd arena scan thread */
	if ((pMQ->hRsndScanThread = SysCreateThread(QueRsndThread, pMQ)) == SYS_INVALID_THREAD) {
		ErrorPush();
		QueFreeMessList(&pMQ->ReadyQueue);
		QueFreeMessList(&pMQ->RsndArenaQueue);
		SysFree(pMQ->pszRootPath);
		SysCloseEvent(pMQ->hReadyEvent);
		SysCloseMutex(pMQ->hMutex);
		SysFree(pMQ);

		ErrSetErrorCode(ErrorPop());
		return INVALID_QUEUE_HANDLE;
	}

	return (QUEUE_HANDLE) pMQ;
}

int QueClose(QUEUE_HANDLE hQueue)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;

	/* Set the shutdown flag and wait for rsnd scan thread to terminate */
	pMQ->ulFlags |= QUEF_SHUTDOWN;
	SysWaitThread(pMQ->hRsndScanThread, QUE_SCAN_THREAD_MAXWAIT);
	SysCloseThread(pMQ->hRsndScanThread, 1);

	/* Clear queues */
	QueFreeMessList(&pMQ->ReadyQueue);
	QueFreeMessList(&pMQ->RsndArenaQueue);
	SysCloseEvent(pMQ->hReadyEvent);
	SysCloseMutex(pMQ->hMutex);
	SysFree(pMQ->pszRootPath);
	SysFree(pMQ);

	return 0;
}

int QueGetDirsLevel(QUEUE_HANDLE hQueue)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;

	return pMQ->iNumDirsLevel;
}

char const *QueGetRootPath(QUEUE_HANDLE hQueue)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;

	return pMQ->pszRootPath;
}

char *QueLoadLastLogEntry(char const *pszLogFilePath)
{
	FILE *pLogFile = fopen(pszLogFilePath, "rb");

	if (pLogFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszLogFilePath);
		return NULL;
	}

	SYS_OFF_T llCurrOffset = 0, llBaseOffset = (SYS_OFF_T) -1, llEndOffset;
	unsigned long ulPeekTime;
	char szLogLine[1024];

	for (;;) {
		llCurrOffset = Sys_ftell(pLogFile);

		if (MscFGets(szLogLine, sizeof(szLogLine) - 1, pLogFile) == NULL)
			break;
		if (sscanf(szLogLine, "[PeekTime] %lu", &ulPeekTime) == 1)
			llBaseOffset = llCurrOffset;
	}
	if (llBaseOffset == (SYS_OFF_T) -1) {
		fclose(pLogFile);
		ErrSetErrorCode(ERR_EMPTY_LOG, pszLogFilePath);
		return NULL;
	}
	/* Get end offset (end of file) */
	Sys_fseek(pLogFile, 0, SEEK_END);
	llEndOffset = Sys_ftell(pLogFile);

	/* Load last entry */
	unsigned int uEntrySize = (unsigned int) (llEndOffset - llBaseOffset);
	char *pszEntry = (char *) SysAlloc(uEntrySize + 1);

	if (pszEntry == NULL) {
		fclose(pLogFile);
		return NULL;
	}
	Sys_fseek(pLogFile, llBaseOffset, SEEK_SET);
	if (!fread(pszEntry, uEntrySize, 1, pLogFile)) {
		SysFree(pszEntry);
		fclose(pLogFile);
		ErrSetErrorCode(ERR_FILE_READ, pszLogFilePath);
		return NULL;
	}
	pszEntry[uEntrySize] = '\0';
	fclose(pLogFile);

	return pszEntry;
}

static int QueStatMessage(MessageQueue *pMQ, QueueMessage *pQM)
{
	/* Build the slog file path */
	char szSlogFilePath[SYS_MAX_PATH];

	QueGetFilePath(pMQ, pQM, szSlogFilePath, QUEUE_SLOG_DIR);

	FILE *pLogFile = fopen(szSlogFilePath, "a+t");

	if (pLogFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, szSlogFilePath);
		return ERR_FILE_OPEN;
	}
	/* Dump peek time */
	time_t tCurr = time(NULL);
	char szTime[128];

	MscGetTimeStr(szTime, sizeof(szTime) - 1, tCurr);
	fprintf(pLogFile, "[PeekTime] %lu : %s\n", (unsigned long) tCurr, szTime);
	fclose(pLogFile);

	return 0;
}

QMSG_HANDLE QueCreateMessage(QUEUE_HANDLE hQueue)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;

	/* Build message file path */
	int iLevel1 = rand() % pMQ->iNumDirsLevel;
	int iLevel2 = rand() % pMQ->iNumDirsLevel;
	char szSubPath[SYS_MAX_PATH];
	char szMsgFilePath[SYS_MAX_PATH];

	SysSNPrintf(szSubPath, sizeof(szSubPath) - 1, "%s%d%s%d%s%s",
		    pMQ->pszRootPath, iLevel1, SYS_SLASH_STR,
		    iLevel2, SYS_SLASH_STR, QUEUE_TEMP_DIR);

	if (MscUniqueFile(szSubPath, szMsgFilePath, sizeof(szMsgFilePath)) < 0)
		return INVALID_QMSG_HANDLE;

	/* Extract file name */
	char szMsgFileName[SYS_MAX_PATH];

	MscGetFileName(szMsgFilePath, szMsgFileName);

	/* Create queue message data */
	QueueMessage *pQM = QueAllocMessage(iLevel1, iLevel2, QUEUE_TEMP_DIR,
					    szMsgFileName, 0, 0);

	if (pQM == NULL)
		return INVALID_QMSG_HANDLE;

	return (QMSG_HANDLE) pQM;
}

static int QueGetFilePath(MessageQueue *pMQ, QueueMessage *pQM, char *pszFilePath,
			  char const *pszQueueDir)
{
	if (pszQueueDir == NULL)
		pszQueueDir = pQM->pszQueueDir;

	SysSNPrintf(pszFilePath, SYS_MAX_PATH - 1, "%s%d%s%d%s%s%s%s",
		    pMQ->pszRootPath, pQM->iLevel1, SYS_SLASH_STR,
		    pQM->iLevel2, SYS_SLASH_STR, pszQueueDir, SYS_SLASH_STR, pQM->pszFileName);

	return 0;
}

int QueGetFilePath(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszFilePath,
		   char const *pszQueueDir)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return QueGetFilePath(pMQ, pQM, pszFilePath, pszQueueDir);
}

static int QueDoMessageCleanup(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;
	char szQueueFilePath[SYS_MAX_PATH];

	if (pQM->ulFlags & QUMF_FREEZE) {
		/* Move message file */
		char szTargetFile[SYS_MAX_PATH];

		QueGetFilePath(pMQ, pQM, szQueueFilePath);
		QueGetFilePath(pMQ, pQM, szTargetFile, QUEUE_FROZ_DIR);
		if (SysMoveFile(szQueueFilePath, szTargetFile) < 0)
			return ErrGetErrorCode();

		/* Change message location */
		pQM->pszQueueDir = QUEUE_FROZ_DIR;
	} else {
		/* Clean message file */
		QueGetFilePath(pMQ, pQM, szQueueFilePath);
		SysRemove(szQueueFilePath);

		/* Clean 'info' file */
		QueGetFilePath(pMQ, pQM, szQueueFilePath, QUEUE_INFO_DIR);
		SysRemove(szQueueFilePath);

		/* Clean 'slog' file */
		QueGetFilePath(pMQ, pQM, szQueueFilePath, QUEUE_SLOG_DIR);
		SysRemove(szQueueFilePath);
	}

	/* Clean 'temp' file */
	QueGetFilePath(pMQ, pQM, szQueueFilePath, QUEUE_TEMP_DIR);
	SysRemove(szQueueFilePath);

	/* Clean 'cust' file */
	QueGetFilePath(pMQ, pQM, szQueueFilePath, QUEUE_CUST_DIR);
	SysRemove(szQueueFilePath);

	/* Clean 'mprc' file */
	QueGetFilePath(pMQ, pQM, szQueueFilePath, QUEUE_MPRC_DIR);
	SysRemove(szQueueFilePath);

	return 0;
}

int QueCloseMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;

	if (pQM->ulFlags & QUMF_DELETED)
		QueDoMessageCleanup(hQueue, hMessage);
	QueFreeMessage(pQM);

	return 0;
}

QMSG_HANDLE QueGetHandle(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2, char const *pszQueueDir,
			 char const *pszFileName)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = QueAllocMessage(iLevel1, iLevel2, pszQueueDir, pszFileName, 0, 0);

	if (pQM == NULL)
		return INVALID_QMSG_HANDLE;

	/* Load message statistics */
	QueLoadMessageStat(pMQ, pQM);

	return (QMSG_HANDLE) pQM;
}

char const *QueGetFileName(QMSG_HANDLE hMessage)
{
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->pszFileName;
}

char const *QueGetQueueDir(QMSG_HANDLE hMessage)
{
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->pszQueueDir;
}

int QueGetLevel1(QMSG_HANDLE hMessage)
{
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->iLevel1;
}

int QueGetLevel2(QMSG_HANDLE hMessage)
{
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->iLevel2;
}

int QueGetTryCount(QMSG_HANDLE hMessage)
{
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->iNumTries;
}

time_t QueGetLastTryTime(QMSG_HANDLE hMessage)
{
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->tLastTry;
}

time_t QueGetMessageNextOp(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;

	return pQM->tLastTry + QueNextRetryOp(pQM->iNumTries, (unsigned int) pMQ->iRetryTimeout,
					      (unsigned int) pMQ->iRetryIncrRatio);
}

int QueInitMessageStats(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;
	char szQueueFilePath[SYS_MAX_PATH];

	QueGetFilePath(pMQ, pQM, szQueueFilePath, QUEUE_SLOG_DIR);
	SysRemove(szQueueFilePath);

	/* Init message statistics */
	pQM->iNumTries = 0;
	pQM->tLastTry = 0;

	return 0;
}

int QueCleanupMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, bool bFreeze)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;

	pQM->ulFlags |= QUMF_DELETED;
	if (bFreeze)
		pQM->ulFlags |= QUMF_FREEZE;

	return 0;
}

static int QueAddNew(MessageQueue *pMQ, QueueMessage *pQM)
{
	/* Add the queue entry */
	if (SysLockMutex(pMQ->hMutex, SYS_INFINITE_TIMEOUT) < 0)
		return ErrGetErrorCode();

	SYS_LIST_ADDT(&pQM->LLink, &pMQ->ReadyQueue);
	++pMQ->iReadyCount;
	SysSetEvent(pMQ->hReadyEvent);
	SysUnlockMutex(pMQ->hMutex);

	return 0;
}

int QueCommitMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;

	/* Move message file (if not in mess) */
	if (strcmp(pQM->pszQueueDir, QUEUE_MESS_DIR) != 0) {
		char szSourceFile[SYS_MAX_PATH];
		char szTargetFile[SYS_MAX_PATH];

		QueGetFilePath(pMQ, pQM, szSourceFile);
		QueGetFilePath(pMQ, pQM, szTargetFile, QUEUE_MESS_DIR);

		if (SysMoveFile(szSourceFile, szTargetFile) < 0)
			return ErrGetErrorCode();

		/* Change message location */
		pQM->pszQueueDir = QUEUE_MESS_DIR;
	}
	/* Unmask temporary flags */
	pQM->ulFlags = QUE_MASK_TMPFLAGS(pQM->ulFlags);

	/* Add to queue */
	if (QueAddNew(pMQ, pQM) < 0)
		return ErrGetErrorCode();

	return 0;
}

static bool QueMessageExpired(MessageQueue *pMQ, QueueMessage *pQM)
{
	return pQM->iNumTries >= pMQ->iMaxRetry;
}

static int QueAddRsnd(MessageQueue *pMQ, QueueMessage *pQM)
{
	/* Add the queue entry */
	if (SysLockMutex(pMQ->hMutex, SYS_INFINITE_TIMEOUT) < 0)
		return ErrGetErrorCode();

	SYS_LIST_ADDT(&pQM->LLink, &pMQ->RsndArenaQueue);
	++pMQ->iRsndArenaCount;
	SysUnlockMutex(pMQ->hMutex);

	return 0;
}

int QueResendMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;

	/* Check for message expired */
	if (QueMessageExpired(pMQ, pQM)) {
		ErrSetErrorCode(ERR_SPOOL_FILE_EXPIRED);
		return ERR_SPOOL_FILE_EXPIRED;
	}
	/* Move message file (if not in rsnd dir) */
	if (strcmp(pQM->pszQueueDir, QUEUE_RSND_DIR) != 0) {
		char szSourceFile[SYS_MAX_PATH];
		char szTargetFile[SYS_MAX_PATH];

		QueGetFilePath(pMQ, pQM, szSourceFile);
		QueGetFilePath(pMQ, pQM, szTargetFile, QUEUE_RSND_DIR);

		if (SysMoveFile(szSourceFile, szTargetFile) < 0)
			return ErrGetErrorCode();

		/* Change message location */
		pQM->pszQueueDir = QUEUE_RSND_DIR;
	}
	/* Unmask temporary flags */
	pQM->ulFlags = QUE_MASK_TMPFLAGS(pQM->ulFlags);

	/* Add to queue */
	if (QueAddRsnd(pMQ, pQM) < 0)
		return ErrGetErrorCode();

	return 0;
}

QMSG_HANDLE QueExtractMessage(QUEUE_HANDLE hQueue, int iTimeout)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;

	/* Wait for message available */
	if (SysWaitEvent(pMQ->hReadyEvent, iTimeout) < 0)
		return INVALID_QMSG_HANDLE;

	if (SysLockMutex(pMQ->hMutex, SYS_INFINITE_TIMEOUT) < 0)
		return INVALID_QMSG_HANDLE;

	/* Get the first message of the queue */
	SysListHead *pLLink = SYS_LIST_FIRST(&pMQ->ReadyQueue);

	if (pLLink == NULL) {
		SysUnlockMutex(pMQ->hMutex);
		return INVALID_QMSG_HANDLE;
	}
	/* Remove the message from the list */
	SYS_LIST_DEL(pLLink);

	/* Decrement message count by resetting the event if no more messages are in */
	if (--pMQ->iReadyCount == 0)
		SysResetEvent(pMQ->hReadyEvent);

	SysUnlockMutex(pMQ->hMutex);

	/* Get queue message pointer */
	QueueMessage *pQM = SYS_LIST_ENTRY(pLLink, QueueMessage, LLink);

	/* Update message statistics */
	++pQM->iNumTries;
	pQM->tLastTry = time(NULL);

	/* Update log file */
	QueStatMessage(pMQ, pQM);

	return (QMSG_HANDLE) pQM;
}

int QueCheckMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;
	QueueMessage *pQM = (QueueMessage *) hMessage;
	char szQueueFilePath[SYS_MAX_PATH];

	if (pQM->ulFlags & QUMF_DELETED) {
		ErrSetErrorCode(ERR_MESSAGE_DELETED);
		return ERR_MESSAGE_DELETED;
	}

	QueGetFilePath(hQueue, hMessage, szQueueFilePath, QUEUE_MESS_DIR);

	if (!SysExistFile(szQueueFilePath)) {
		QueGetFilePath(hQueue, hMessage, szQueueFilePath, QUEUE_RSND_DIR);

		if (!SysExistFile(szQueueFilePath)) {
			ErrSetErrorCode(ERR_NO_MESSAGE_FILE);
			return ERR_NO_MESSAGE_FILE;
		}
	}

	return 0;
}

static bool QueMessageDestMatch(MessageQueue *pMQ, QueueMessage *pQM,
				char const *pszAddressMatch)
{
	SpoolFileHeader SFH;
	char szQueueFilePath[SYS_MAX_PATH];

	QueGetFilePath(pMQ, pQM, szQueueFilePath);
	if (USmlLoadSpoolFileHeader(szQueueFilePath, SFH) < 0)
		return false;

	bool bAddressMatch = false;

	if (strchr(pszAddressMatch, '@') == NULL) {
		/* RFC style ETRN (domain based) */
		char szDestUser[MAX_ADDR_NAME];
		char szDestDomain[MAX_ADDR_NAME];

		if (StrStringsCount(SFH.ppszRcpt) < 1 ||
		    USmtpSplitEmailAddr(SFH.ppszRcpt[0], szDestUser, szDestDomain) < 0) {
			USmlCleanupSpoolFileHeader(SFH);
			return false;
		}

		bAddressMatch = (StrIWildMatch(szDestDomain, pszAddressMatch) != 0);
	} else {
		/* XMail style ETRN (email based) */
		bAddressMatch = (StrIWildMatch(SFH.ppszRcpt[0], pszAddressMatch) != 0);
	}
	USmlCleanupSpoolFileHeader(SFH);

	return bAddressMatch;
}

int QueFlushRsndArena(QUEUE_HANDLE hQueue, char const *pszAddressMatch)
{
	MessageQueue *pMQ = (MessageQueue *) hQueue;

	if (SysLockMutex(pMQ->hMutex, SYS_INFINITE_TIMEOUT) < 0)
		return ErrGetErrorCode();

	SysListHead *pLLink;

	SYS_LIST_FOR_EACH(pLLink, &pMQ->RsndArenaQueue) {
		QueueMessage *pQM = SYS_LIST_ENTRY(pLLink, QueueMessage, LLink);

		if (pszAddressMatch == NULL ||
		    QueMessageDestMatch(pMQ, pQM, pszAddressMatch)) {
			/* Set the list pointer to the next item */
			pLLink = pLLink->pPrev;

			/* Remove item from resend arena */
			SYS_LIST_DEL(&pQM->LLink);
			--pMQ->iRsndArenaCount;

			/* Add item from resend queue */
			SYS_LIST_ADDT(&pQM->LLink, &pMQ->ReadyQueue);
			++pMQ->iReadyCount;
		}
	}

	/* If the count of rsnd queue is not zero, set the event */
	if (pMQ->iReadyCount > 0)
		SysSetEvent(pMQ->hReadyEvent);
	SysUnlockMutex(pMQ->hMutex);

	return 0;
}

