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

#ifndef _QUEUEUTILS_H
#define _QUEUEUTILS_H

struct QueLogInfo {
    char *pszReason;
    char *pszServer;
};

int QueUtGetFrozenList(QUEUE_HANDLE hQueue, char const *pszListFile);
int QueUtUnFreezeMessage(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
             char const *pszMessageFile);
int QueUtDeleteFrozenMessage(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
                 char const *pszMessageFile);
int QueUtGetFrozenMsgFile(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
              char const *pszMessageFile, char const *pszOutFile);
int QueUtGetFrozenLogFile(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
              char const *pszMessageFile, char const *pszOutFile);
int QueUtErrLogMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char const *pszFormat, ...);
int QueUtGetLastLogInfo(char const *pszLogFilePath, QueLogInfo * pQLI);
void QueUtFreeLastLogInfo(QueLogInfo * pQLI);
bool QueUtRemoveSpoolErrors(void);
int QueUtNotifyPermErrDelivery(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                   SPLF_HANDLE hFSpool, char const *pszReason,
                   char const *pszServer, bool bCleanup);
int QueUtNotifyTempErrDelivery(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                   SPLF_HANDLE hFSpool, char const *pszReason,
                   char const *pszText, char const *pszServer);
int QueUtCleanupNotifyRoot(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
               SPLF_HANDLE hFSpool, char const *pszReason);
int QueUtResendMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, SPLF_HANDLE hFSpool);

#endif
