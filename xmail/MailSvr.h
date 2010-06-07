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

#ifndef _MAILSVR_H
#define _MAILSVR_H

#define XMAIL_MAILDIR    1
#define XMAIL_MAILBOX    2

/* Defined in MailSvr.cpp */
extern SHB_HANDLE hShbFING, hShbCTRL, hShbPOP3, hShbSMTP, hShbSMAIL, hShbPSYNC, hShbLMAIL;
extern char szMailPath[SYS_MAX_PATH];
extern bool bServerDebug;
extern int iFilterTimeout;
extern bool bFilterLogEnabled;
extern QUEUE_HANDLE hSpoolQueue;
extern SYS_SEMAPHORE hSyncSem;
extern int iLogRotateHours;
extern int iQueueSplitLevel;
extern int iAddrFamily;
extern int iPOP3ClientTimeout;
extern int iMailboxType;

int SvrMain(int iArgCount, char *pszArgs[]);
int SvrStopServer(bool bWait = true);
bool SvrInShutdown(bool bForceCheck = false);

#endif
