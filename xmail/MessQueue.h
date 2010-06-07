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

#ifndef _MESSQUEUE_H
#define _MESSQUEUE_H

#define QUEUE_MESS_DIR              "mess"
#define QUEUE_RSND_DIR              "rsnd"
#define QUEUE_INFO_DIR              "info"
#define QUEUE_TEMP_DIR              "temp"
#define QUEUE_SLOG_DIR              "slog"
#define QUEUE_CUST_DIR              "cust"
#define QUEUE_MPRC_DIR              "mprc"
#define QUEUE_FROZ_DIR              "froz"

#define STD_QUEUEFS_DIRS_X_LEVEL    23

#define INVALID_QUEUE_HANDLE        ((QUEUE_HANDLE) 0)
#define INVALID_QMSG_HANDLE         ((QMSG_HANDLE) 0)

typedef struct QUEUE_HANDLE_struct {
} *QUEUE_HANDLE;

typedef struct QMSG_HANDLE_struct {
} *QMSG_HANDLE;

QUEUE_HANDLE QueOpen(const char *pszRootPath, int iMaxRetry, int iRetryTimeout,
		     int iRetryIncrRatio, int iNumDirsLevel = STD_QUEUEFS_DIRS_X_LEVEL);
int QueClose(QUEUE_HANDLE hQueue);
int QueGetDirsLevel(QUEUE_HANDLE hQueue);
const char *QueGetRootPath(QUEUE_HANDLE hQueue);
char *QueLoadLastLogEntry(const char *pszLogFilePath);
QMSG_HANDLE QueCreateMessage(QUEUE_HANDLE hQueue);
int QueGetFilePath(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszFilePath,
		   const char *pszQueueDir = NULL);
int QueCloseMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
QMSG_HANDLE QueGetHandle(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2, const char *pszQueueDir,
			 const char *pszFileName);
const char *QueGetFileName(QMSG_HANDLE hMessage);
const char *QueGetQueueDir(QMSG_HANDLE hMessage);
int QueGetLevel1(QMSG_HANDLE hMessage);
int QueGetLevel2(QMSG_HANDLE hMessage);
int QueGetTryCount(QMSG_HANDLE hMessage);
time_t QueGetLastTryTime(QMSG_HANDLE hMessage);
time_t QueGetMessageNextOp(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
int QueInitMessageStats(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
int QueCleanupMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, bool bFreeze = false);
int QueCommitMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
int QueResendMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
QMSG_HANDLE QueExtractMessage(QUEUE_HANDLE hQueue, int iTimeout);
int QueCheckMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage);
int QueFlushRsndArena(QUEUE_HANDLE hQueue, const char *pszAddressMatch);

#endif

