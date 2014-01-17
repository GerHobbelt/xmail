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

#ifndef _MAILDOMAINS_H
#define _MAILDOMAINS_H

#define INVALID_DOMLS_HANDLE            ((DOMLS_HANDLE) 0)

typedef struct DOMLS_HANDLE_struct {
} *DOMLS_HANDLE;

int MDomCheckDomainsIndexes(void);
char *MDomGetDomainPath(char const *pszDomain, char *pszDomainPath, int iMaxPath,
			int iFinalSlash);
int MDomLookupDomain(char const *pszDomain);
int MDomAddDomain(char const *pszDomain);
int MDomRemoveDomain(char const *pszDomain);
int MDomGetDomainsFileSnapShot(const char *pszFileName);
DOMLS_HANDLE MDomOpenDB(void);
void MDomCloseDB(DOMLS_HANDLE hDomainsDB);
char const *MDomGetFirstDomain(DOMLS_HANDLE hDomainsDB);
char const *MDomGetNextDomain(DOMLS_HANDLE hDomainsDB);
int MDomGetClientDomain(char const *pszFQDN, char *pszClientDomain, int iMaxDomain);
int MDomIsHandledDomain(char const *pszDomain);

#endif
