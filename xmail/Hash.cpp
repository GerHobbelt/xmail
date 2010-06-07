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
#include "Hash.h"

#define HMASK_TOP_BIT (1UL << (sizeof(long) * 8 - 1))

struct Hash {
	HashOps Ops;
	unsigned long ulCount;
	unsigned long ulHMask;
	SysListHead *pBkts;
};


HASH_HANDLE HashCreate(HashOps const *pOps, unsigned long ulSize)
{
	unsigned long i, ulHMask;
	Hash *pHash;

	if ((pHash = (Hash *) SysAlloc(sizeof(Hash))) == NULL)
		return INVALID_HASH_HANDLE;
	pHash->Ops = *pOps;
	for (ulHMask = 2; !(ulHMask & HMASK_TOP_BIT) && ulHMask <= ulSize; ulHMask <<= 1);
	ulHMask--;
	pHash->ulHMask = ulHMask;
	if ((pHash->pBkts = (SysListHead *)
	     SysAlloc((ulHMask + 1) * sizeof(SysListHead))) == NULL) {
		SysFree(pHash);
		return INVALID_HASH_HANDLE;
	}
	for (i = 0; i <= ulHMask; i++)
		SYS_INIT_LIST_HEAD(&pHash->pBkts[i]);

	return (HASH_HANDLE) pHash;
}

void HashFree(HASH_HANDLE hHash, void (*pFree)(void *, HashNode *),
	      void *pPrivate)
{
	Hash *pHash = (Hash *) hHash;
	unsigned long i;
	SysListHead *pHead, *pPos;
	HashNode *pHNode;

	if (pHash != NULL) {
		if (pFree != NULL) {
			for (i = 0; i <= pHash->ulHMask; i++) {
				pHead = &pHash->pBkts[i];
				while ((pPos = SYS_LIST_FIRST(pHead)) != NULL) {
					pHNode = SYS_LIST_ENTRY(pPos, HashNode, Lnk);
					SYS_LIST_DEL(&pHNode->Lnk);
					(*pFree)(pPrivate, pHNode);
				}
			}
		}
		SysFree(pHash->pBkts);
		SysFree(pHash);
	}
}

unsigned long HashGetCount(HASH_HANDLE hHash)
{
	Hash *pHash = (Hash *) hHash;

	return pHash->ulCount;
}

void HashInitNode(HashNode *pHNode)
{
	ZeroData(*pHNode);
	SYS_INIT_LIST_HEAD(&pHNode->Lnk);
}

static int HashGrow(Hash *pHash)
{
	unsigned long i, ulHIdx, ulHMask;
	SysListHead *pBkts, *pHead, *pPos;
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
	for (i = 0; i <= pHash->ulHMask; i++) {
		pHead = &pHash->pBkts[i];
		while ((pPos = SYS_LIST_FIRST(pHead)) != NULL) {
			pHNode = SYS_LIST_ENTRY(pPos, HashNode, Lnk);
			SYS_LIST_DEL(&pHNode->Lnk);
			ulHIdx = (*pHash->Ops.pGetHashVal)(pHash->Ops.pPrivate,
							   &pHNode->Key) & ulHMask;
			SYS_LIST_ADDT(&pHNode->Lnk, &pBkts[ulHIdx]);
		}
	}
	SysFree(pHash->pBkts);
	pHash->pBkts = pBkts;
	pHash->ulHMask = ulHMask;

	return 0;
}

int HashAdd(HASH_HANDLE hHash, HashNode *pHNode)
{
	Hash *pHash = (Hash *) hHash;
	unsigned long ulHIdx;
	SysListHead *pHead;

	if (pHash->ulCount >= pHash->ulHMask && HashGrow(pHash) < 0)
		return ErrGetErrorCode();
	ulHIdx = (*pHash->Ops.pGetHashVal)(pHash->Ops.pPrivate,
					   &pHNode->Key) & pHash->ulHMask;
	pHead = &pHash->pBkts[ulHIdx];
	SYS_LIST_ADDT(&pHNode->Lnk, pHead);
	pHash->ulCount++;

	return 0;
}

void HashDel(HASH_HANDLE hHash, HashNode *pHNode)
{
	Hash *pHash = (Hash *) hHash;

	SYS_LIST_DEL(&pHNode->Lnk);
	pHash->ulCount--;
}

int HashGetFirst(HASH_HANDLE hHash, HashDatum const *pKey,
		 HashEnum *pHEnum, HashNode **ppHNode)
{
	Hash *pHash = (Hash *) hHash;
	unsigned long ulHIdx;
	SysListHead *pHead, *pPos;
	HashNode *pHNode;

	ulHIdx = (*pHash->Ops.pGetHashVal)(pHash->Ops.pPrivate,
					   pKey) & pHash->ulHMask;
	pHead = &pHash->pBkts[ulHIdx];
	for (pPos = SYS_LIST_FIRST(pHead); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, pHead)) {
		pHNode = SYS_LIST_ENTRY(pPos, HashNode, Lnk);
		if ((*pHash->Ops.pCompare)(pHash->Ops.pPrivate, pKey,
					   &pHNode->Key) == 0) {
			*ppHNode = pHNode;
			pHEnum->ulHIdx = ulHIdx;
			pHEnum->pNext = SYS_LIST_NEXT(pPos, pHead);
			return 0;
		}
	}

	ErrSetErrorCode(ERR_NOT_FOUND);
	return ERR_NOT_FOUND;
}

int HashGetNext(HASH_HANDLE hHash, HashDatum const *pKey,
		HashEnum *pHEnum, HashNode **ppHNode)
{
	Hash *pHash = (Hash *) hHash;
	SysListHead *pPos, *pHead;
	HashNode *pHNode;

	pHead = &pHash->pBkts[pHEnum->ulHIdx];
	for (pPos = pHEnum->pNext; pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, pHead)) {
		pHNode = SYS_LIST_ENTRY(pPos, HashNode, Lnk);
		if ((*pHash->Ops.pCompare)(pHash->Ops.pPrivate, pKey,
					   &pHNode->Key) == 0) {
			*ppHNode = pHNode;
			pHEnum->pNext = SYS_LIST_NEXT(pPos, pHead);
			return 0;
		}
	}

	ErrSetErrorCode(ERR_NOT_FOUND);
	return ERR_NOT_FOUND;
}

static int HashFetchNext(Hash *pHash, unsigned long ulFrom, HashEnum *pHEnum,
			 HashNode **ppHNode)
{
	unsigned long i;
	SysListHead *pHead, *pPos;

	for (i = ulFrom; i <= pHash->ulHMask; i++) {
		pHead = &pHash->pBkts[i];
		if ((pPos = SYS_LIST_FIRST(pHead)) != NULL) {
			pHEnum->ulHIdx = i;
			pHEnum->pNext = SYS_LIST_NEXT(pPos, pHead);
			*ppHNode = SYS_LIST_ENTRY(pPos, HashNode, Lnk);

			return 0;
		}
	}

	ErrSetErrorCode(ERR_NOT_FOUND);
	return ERR_NOT_FOUND;
}

int HashFirst(HASH_HANDLE hHash, HashEnum *pHEnum, HashNode **ppHNode)
{
	Hash *pHash = (Hash *) hHash;

	return HashFetchNext(pHash, 0, pHEnum, ppHNode);
}

int HashNext(HASH_HANDLE hHash, HashEnum *pHEnum, HashNode **ppHNode)
{
	Hash *pHash = (Hash *) hHash;
	SysListHead *pPos;

	if ((pPos = pHEnum->pNext) != NULL) {
		pHEnum->pNext = SYS_LIST_NEXT(pPos, &pHash->pBkts[pHEnum->ulHIdx]);
		*ppHNode = SYS_LIST_ENTRY(pPos, HashNode, Lnk);

		return 0;
	}

	return HashFetchNext(pHash, pHEnum->ulHIdx + 1, pHEnum, ppHNode);
}

