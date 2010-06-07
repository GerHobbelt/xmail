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

#ifndef _USRAUTH_H
#define _USRAUTH_H

#define AUTH_SERVICE_POP3               "pop3"
#define AUTH_SERVICE_SMTP               "smtp"

char *UAthGetRootPath(char const *pszService, char *pszAuthPath, int iMaxPath);
int UAthAuthenticateUser(char const *pszService, char const *pszDomain,
			 char const *pszUsername, char const *pszPassword);
int UAthAddUser(char const *pszService, UserInfo * pUI);
int UAthModifyUser(char const *pszService, UserInfo * pUI);
int UAthDelUser(char const *pszService, UserInfo * pUI);
int UAthDropDomain(char const *pszService, char const *pszDomain);

#endif
