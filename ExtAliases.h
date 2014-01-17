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

#ifndef _EXTALIASES_H
#define _EXTALIASES_H

#define INVALID_EXAL_HANDLE         ((EXAL_HANDLE) 0)

struct ExtAlias {
	char *pszDomain;
	char *pszName;
	char *pszRmtDomain;
	char *pszRmtName;
};

typedef struct EXAL_HANDLE_struct {
} *EXAL_HANDLE;

int ExAlCheckAliasIndexes(void);
ExtAlias *ExAlAllocAlias(void);
void ExAlFreeAlias(ExtAlias * pExtAlias);
int ExAlAddAlias(ExtAlias * pExtAlias);
ExtAlias *ExAlGetAlias(char const *pszRmtDomain, char const *pszRmtName);
int ExAlRemoveAlias(ExtAlias * pExtAlias);
int ExAlRemoveUserAliases(const char *pszDomain, const char *pszName);
int ExAlRemoveDomainAliases(const char *pszDomain);
int ExAlGetDBFileSnapShot(const char *pszFileName);
EXAL_HANDLE ExAlOpenDB(void);
void ExAlCloseDB(EXAL_HANDLE hLinksDB);
ExtAlias *ExAlGetFirstAlias(EXAL_HANDLE hLinksDB);
ExtAlias *ExAlGetNextAlias(EXAL_HANDLE hLinksDB);

#endif
