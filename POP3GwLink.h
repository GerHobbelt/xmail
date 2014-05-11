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

#ifndef _POP3GWLINK_H
#define _POP3GWLINK_H

#define INVALID_GWLKF_HANDLE         ((GWLKF_HANDLE) 0)

struct POP3Link {
    char *pszDomain;
    char *pszName;
    char *pszRmtDomain;
    char *pszRmtName;
    char *pszRmtPassword;
    char *pszAuthType;
};

typedef struct GWLKF_HANDLE_struct {
} *GWLKF_HANDLE;

POP3Link *GwLkAllocLink(char const *pszDomain, char const *pszName,
            char const *pszRmtDomain, char const *pszRmtName,
            char const *pszRmtPassword, char const *pszAuthType);
void GwLkFreePOP3Link(POP3Link *pPopLnk);
int GwLkAddLink(POP3Link *pPopLnk);
int GwLkRemoveLink(POP3Link *pPopLnk);
int GwLkRemoveUserLinks(const char *pszDomain, const char *pszName);
int GwLkRemoveDomainLinks(const char *pszDomain);
int GwLkGetDBFileSnapShot(const char *pszFileName);
GWLKF_HANDLE GwLkOpenDB(void);
void GwLkCloseDB(GWLKF_HANDLE hLinksDB);
POP3Link *GwLkGetFirstUser(GWLKF_HANDLE hLinksDB);
POP3Link *GwLkGetNextUser(GWLKF_HANDLE hLinksDB);
int GwLkGetMsgSyncDbFile(char const *pszRmtDomain, char const *pszRmtName,
             char *pszMsgSyncFile, int iMaxPath);
int GwLkLinkLock(POP3Link const *pPopLnk);
void GwLkLinkUnlock(POP3Link const *pPopLnk);
int GwLkClearLinkLocksDir(void);
int GwLkLocalDomain(POP3Link const *pPopLnk);
int GwLkMasqueradeDomain(POP3Link const *pPopLnk);
int GwLkCheckEnabled(POP3Link const *pPopLnk);
int GwLkEnable(POP3Link const *pPopLnk, bool bEnable);
int GwLkEnable(char const *pszDomain, char const *pszName,
           char const *pszRmtDomain, char const *pszRmtName, bool bEnable);

#endif

