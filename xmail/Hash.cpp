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

#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "Hash.h"

#define HMASK_TOP_BIT (1UL << (sizeof(long) * 8 - 1))

struct Hash {
	unsigned long ulCount;
	unsigned long ulHMask;
	SysListHead *pBkts;
	SysListHead NodeList;
};


HASH_HANDLE HashCreate(unsigned long ulSize) {
	unsigned long i, ulHMask;
	Hash *pHash;

	if ((pHash = (Hash *) SysAlloc(sizeof(Hash))) == NULL)
		return INVALID_HASH_HANDLE;
	for (ulHMask = 2; !(ulHMask & HMASK_TOP_BIT) && ulHMask <= ulSize; ulHMask <<= 1);
	ulHMask--;
	pHash->ulCount = 0;
	pHash->ulHMask = ulHMask;
	if ((pHash->pBkts = (SysListHead *)
	     SysAlloc((ulHMask + 1) * sizeof(SysListHead))) == NULL) {
		SysFree(pHash);
		return INVALID_HASH_HANDLE;
	}
	for (i = 0; i <= ulHMask; i++)
		SYS_INIT_LIST_HEAD(&pHash->pBkts[i]);
	SYS_INIT_LIST_HEAD(&pHash->NodeList);

	return (HASH_HANDLE) pHash;
}

void HashFree(HASH_HANDLE hHash, void (*pfFree)(void *, HashNode *),
	      void *pPrivate) {
	Hash *pHash;
	SysListHead *pPos;
	HashNode *pHNode;

	/*
	 * Having *Free() functions to accept NULL handles is useful in
	 * order to streamline the cleanup path of functions using them.
	 */
	if (hHash == INVALID_HASH_HANDLE)
		return;
	pHash = (Hash *) hHash;
	if (pfFree != NULL) {
		while ((pPos = SYS_LIST_FIRST(&pHash->NodeList)) != NULL) {
			pHNode = SYS_LIST_ENTRY(pPos, HashNode, LLnk);
			SYS_LIST_DEL(&pHNode->LLnk);
			SYS_LIST_DEL(&pHNode->HLnk);
			(*pfFree)(pPrivate, pHNode);
		}
	}
	SysFree(pHash->pBkts);
	SysFree(pHash);
}

void HashInitNode(HashNode *pHNode) {
	ZeroData(*pHNode);
	SYS_INIT_LIST_HEAD(&pHNode->LLnk);
	SYS_INIT_LIST_HEAD(&pHNode->HLnk);
}

static int HashGrow(Hash *pHash) {
	unsigned long i, ulHIdx, ulHMask;
	SysListHead *pBkts, *pPos;
	HashNode *pHNode;

	ulHMask = pHash->ulHMask + 1;
	if (ulHMask & HMASK_TOP_BIT)
		return 0;
	ulHMask = (ulHMask << 1) - 1;
	if ((pBkts = (SysListHead *)
	     SysAlloc((ulHMask + 1) * sizeof(SysListHead))) == NULL)
		return ErrGetErrorCode();
	for (i = 0; i <= ulHMask; i++)
		SYS_INIT_LIST_HEAD(&pBkts[i]);
	for (pPos = SYS_LIST_FIRST(&pHash->NodeList); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, &pHash->NodeList)) {
		pHNode = SYS_LIST_ENTRY(pPos, HashNode, LLnk);
		SYS_LIST_DEL(&pHNode->HLnk);
		ulHIdx = MscHashString(pHNode->Key.pData, pHNode->Key.lSize) & ulHMask;
		SYS_LIST_ADDT(&pHNode->HLnk, &pBkts[ulHIdx]);
	}
	SysFree(pHash->pBkts);
	pHash->pBkts = pBkts;
	pHash->ulHMask = ulHMask;

	return 0;
}

int HashAdd(HASH_HANDLE hHash, HashNode *pHNode) {
	Hash *pHash = (Hash *) hHash;
	unsigned long ulHIdx;
	SysListHead *pHead;

	if (pHash->ulCount >= pHash->ulHMask && HashGrow(pHash) < 0)
		return ErrGetErrorCode();
	ulHIdx = MscHashString(pHNode->Key.pData, pHNode->Key.lSize) & pHash->ulHMask;
	pHead = &pHash->pBkts[ulHIdx];
	SYS_LIST_ADDT(&pHNode->HLnk, pHead);
	SYS_LIST_ADDT(&pHNode->LLnk, &pHash->NodeList);
	pHash->ulCount++;

	return 0;
}

void HashDel(HASH_HANDLE hHash, HashNode *pHNode) {
	Hash *pHash = (Hash *) hHash;

	SYS_LIST_DEL(&pHNode->HLnk);
	SYS_LIST_DEL(&pHNode->LLnk);
	pHash->ulCount--;
}

int HashGetFirst(HASH_HANDLE hHash, Datum const *Key,
		 HashEnum *pHEnum, HashNode **ppHNode) {
	Hash *pHash = (Hash *) hHash;
	unsigned long ulHIdx;
	SysListHead *pHead, *pPos;
	HashNode *pHNode;

	ulHIdx = MscHashString(Key->pData, Key->lSize) & pHash->ulHMask;
	pHead = &pHash->pBkts[ulHIdx];
	for (pPos = SYS_LIST_FIRST(pHead); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, pHead)) {
		pHNode = SYS_LIST_ENTRY(pPos, HashNode, HLnk);
		if (EquivDatum(Key, &pHNode->Key)) {
			*ppHNode = pHNode;
			pHEnum->ulHIdx = ulHIdx;
			pHEnum->pNext = SYS_LIST_NEXT(pPos, pHead);
			return 0;
		}
	}

	ErrSetErrorCode(ERR_NOT_FOUND);
	return ERR_NOT_FOUND;
}

int HashGetNext(HASH_HANDLE hHash, Datum const *Key,
		HashEnum *pHEnum, HashNode **ppHNode) {
	Hash *pHash = (Hash *) hHash;
	SysListHead *pPos, *pHead;
	HashNode *pHNode;

	pHead = &pHash->pBkts[pHEnum->ulHIdx];
	for (pPos = pHEnum->pNext; pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, pHead)) {
		pHNode = SYS_LIST_ENTRY(pPos, HashNode, HLnk);
		if (EquivDatum(Key, &pHNode->Key)) {
			*ppHNode = pHNode;
			pHEnum->pNext = SYS_LIST_NEXT(pPos, pHead);
			return 0;
		}
	}

	ErrSetErrorCode(ERR_NOT_FOUND);
	return ERR_NOT_FOUND;
}

int HashFirst(HASH_HANDLE hHash, SysListHead **ppPos, HashNode **ppHNode) {
	Hash *pHash = (Hash *) hHash;
	SysListHead *pPos;

	if ((pPos = SYS_LIST_FIRST(&pHash->NodeList)) == NULL) {
		ErrSetErrorCode(ERR_NOT_FOUND);
		return ERR_NOT_FOUND;
	}
	*ppHNode = SYS_LIST_ENTRY(pPos, HashNode, LLnk);
	*ppPos = SYS_LIST_NEXT(pPos, &pHash->NodeList);

	return 0;
}

int HashNext(HASH_HANDLE hHash, SysListHead **ppPos, HashNode **ppHNode) {
	Hash *pHash = (Hash *) hHash;
	SysListHead *pPos = *ppPos;

	if (pPos == NULL) {
		ErrSetErrorCode(ERR_NOT_FOUND);
		return ERR_NOT_FOUND;
	}
	*ppHNode = SYS_LIST_ENTRY(pPos, HashNode, LLnk);
	*ppPos = SYS_LIST_NEXT(pPos, &pHash->NodeList);

	return 0;
}

