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

#ifndef _HASH_H
#define _HASH_H


#define INVALID_HASH_HANDLE ((HASH_HANDLE) 0)


typedef struct HASH_HANDLE_struct {
} *HASH_HANDLE;

struct HashNode {
	SysListHead LLnk;
	SysListHead HLnk;
	Datum Key;
};

struct HashEnum {
	unsigned long ulHIdx;
	SysListHead *pNext;
};


HASH_HANDLE HashCreate(unsigned long ulSize);
void HashFree(HASH_HANDLE hHash, void (*pfFree)(void *, HashNode *),
	      void *pPrivate);
void HashInitNode(HashNode *pHNode);
int HashAdd(HASH_HANDLE hHash, HashNode *pHNode);
void HashDel(HASH_HANDLE hHash, HashNode *pHNode);
int HashGetFirst(HASH_HANDLE hHash, Datum const *Key,
		 HashEnum *pHEnum, HashNode **ppHNode);
int HashGetNext(HASH_HANDLE hHash, Datum const *Key,
		HashEnum *pHEnum, HashNode **ppHNode);
int HashFirst(HASH_HANDLE hHash, SysListHead **ppPos, HashNode **ppHNode);
int HashNext(HASH_HANDLE hHash, SysListHead **ppPos, HashNode **ppHNode);


#endif

