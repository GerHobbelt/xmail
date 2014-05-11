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

#ifndef _POP3UTILS_H
#define _POP3UTILS_H

#define INVALID_POP3_HANDLE         ((POP3_HANDLE) 0)

#define POP3_USER_SPLITTERS         "@:%"

typedef struct POP3_HANDLE_struct {
} *POP3_HANDLE;

struct MailSyncReport {
    int iMsgSync;
    int iMsgErr;
    SYS_OFF_T llSizeSync;
    SYS_OFF_T llSizeErr;
};

struct PopLastLoginInfo {
    SYS_INET_ADDR Address;
    time_t LTime;
};

int UPopGetMailboxSize(UserInfo *pUI, SYS_OFF_T &llMBSize, unsigned long &ulNumMessages);
int UPopCheckMailboxSize(UserInfo *pUI, SYS_OFF_T *pllAvailSpace = NULL);
int UPopAuthenticateAPOP(char const *pszDomain, char const *pszUsrName,
             char const *pszTimeStamp, char const *pszDigest);
POP3_HANDLE UPopBuildSession(char const *pszDomain, char const *pszUsrName,
                 char const *pszUsrPass, SYS_INET_ADDR const *pPeerInfo);
void UPopReleaseSession(POP3_HANDLE hPOPSession, int iUpdate = 1);
char *UPopGetUserInfoVar(POP3_HANDLE hPOPSession, char const *pszName,
             char const *pszDefault = NULL);
int UPopGetSessionMsgCurrent(POP3_HANDLE hPOPSession);
int UPopGetSessionMsgTotal(POP3_HANDLE hPOPSession);
SYS_OFF_T UPopGetSessionMBSize(POP3_HANDLE hPOPSession);
int UPopGetSessionLastAccessed(POP3_HANDLE hPOPSession);
int UPopGetMessageSize(POP3_HANDLE hPOPSession, int iMsgIndex, SYS_OFF_T &llMessageSize);
int UPopGetMessageUIDL(POP3_HANDLE hPOPSession, int iMsgIndex, char *pszMessageUIDL,
               int iSize);
int UPopDeleteMessage(POP3_HANDLE hPOPSession, int iMsgIndex);
int UPopResetSession(POP3_HANDLE hPOPSession);
int UPopSendErrorResponse(BSOCK_HANDLE hBSock, int iErrorCode, int iTimeout);
int UPopSessionSendMsg(POP3_HANDLE hPOPSession, int iMsgIndex, BSOCK_HANDLE hBSock);
int UPopSessionTopMsg(POP3_HANDLE hPOPSession, int iMsgIndex, int iNumLines,
              BSOCK_HANDLE hBSock);
int UPopSaveUserIP(POP3_HANDLE hPOPSession);
int UPopSyncRemoteLink(char const *pszSyncAddr, char const *pszRmtServer,
               char const *pszRmtName, char const *pszRmtPassword,
               MailSyncReport *pSRep, char const *pszSyncCfg,
               char const *pszFetchHdrTags = "+X-Deliver-To,To,Cc",
               char const *pszErrorAccount = NULL);
int UPopUserIpCheck(UserInfo *pUI, SYS_INET_ADDR const *pPeerInfo, unsigned int uExpireTime);
int UPopGetLastLoginInfo(UserInfo *pUI, PopLastLoginInfo *pInfo);

#endif
