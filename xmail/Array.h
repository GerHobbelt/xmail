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

#ifndef _ARRAY_H
#define _ARRAY_H


#define INVALID_ARRAY_HANDLE ((ARRAY_HANDLE) 0)


typedef struct ARRAY_HANDLE_struct {
} *ARRAY_HANDLE;


ARRAY_HANDLE ArrayCreate(long lSize);
void ArrayFree(ARRAY_HANDLE hArray, void (*pfFree)(void *, void *),
	       void *pPrivate);
int ArraySet(ARRAY_HANDLE hArray, long lIdx, void *pData);
long ArrayLength(ARRAY_HANDLE hArray);
void *ArrayGet(ARRAY_HANDLE hArray, long lIdx);
int ArrayAppend(ARRAY_HANDLE hArray, void *pData);

#endif

