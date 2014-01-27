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

#ifndef _ALIASDOMAIN_H
#define _ALIASDOMAIN_H

#define INVALID_ADOMAIN_HANDLE          ((ADOMAIN_HANDLE) 0)

enum ADomainFileds {
	adomADomain = 0,
	adomDomain,

	adomMax
};

typedef struct ADOMAIN_HANDLE_struct {
} *ADOMAIN_HANDLE;

int ADomCheckDomainsIndexes(void);
int ADomLookupDomain(const char *pszADomain, char *pszDomain, bool bWildMatch);
int ADomAddADomain(char const *pszADomain, char const *pszDomain);
int ADomRemoveADomain(char const *pszADomain);
int ADomRemoveLinkedDomains(char const *pszDomain);
int ADomGetADomainFileSnapShot(const char *pszFileName);
ADOMAIN_HANDLE ADomOpenDB(void);
void ADomCloseDB(ADOMAIN_HANDLE hDomainsDB);
char const *const *ADomGetFirstDomain(ADOMAIN_HANDLE hDomainsDB);
char const *const *ADomGetNextDomain(ADOMAIN_HANDLE hDomainsDB);

#endif
