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

#ifndef _SLIST_H
#define _SLIST_H

#define INVALID_SLIST_PTR           0
#define ListLinkInit(p)             ((PLISTLINK) (p))->pNext = INVALID_SLIST_PTR

typedef struct s_ListLink {
    struct s_ListLink *pNext;
} LISTLINK;

typedef LISTLINK *PLISTLINK;

typedef PLISTLINK HSLIST;

void ListInit(HSLIST &hList);
void ListAddHead(HSLIST &hList, PLISTLINK pLLink);
void ListAddTail(HSLIST &hList, PLISTLINK pLLink);
PLISTLINK ListFirst(HSLIST &hList);
PLISTLINK ListNext(HSLIST &hList, PLISTLINK pLLink);
PLISTLINK ListRemovePtr(HSLIST &hList, PLISTLINK pLLink);
PLISTLINK ListRemove(HSLIST &hList);
void ListPurgeFree(HSLIST &hList);
void ListPurge(HSLIST &hList);
bool ListIsEmpty(HSLIST &hList);
int ListGetCount(HSLIST &hList);
PLISTLINK *ListGetPointers(HSLIST &hList, int &iListCount);
void ListReleasePointers(PLISTLINK *pPointers);

#endif
