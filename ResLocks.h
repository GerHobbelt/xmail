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

#ifndef _RESLOCKS_H
#define _RESLOCKS_H

#define INVALID_RLCK_HANDLE         ((RLCK_HANDLE) 0)

typedef struct RLCK_HANDLE_struct {
} *RLCK_HANDLE;

int RLckInitLockers(void);
int RLckCleanupLockers(void);
RLCK_HANDLE RLckLockEX(char const *pszResourceName);
int RLckUnlockEX(RLCK_HANDLE hLock);
RLCK_HANDLE RLckLockSH(char const *pszResourceName);
int RLckUnlockSH(RLCK_HANDLE hLock);

#endif
