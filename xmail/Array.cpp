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
#include "Array.h"


#define ARRAY_EXTRA_SPACE 16


struct Array {
	long lAlloc;
	long lCount;
	void **ppData;
};


ARRAY_HANDLE ArrayCreate(long lSize)
{
	Array *pAr;

	if ((pAr = (Array *) SysAlloc(sizeof(Array))) == NULL)
		return INVALID_ARRAY_HANDLE;
	pAr->lAlloc = lSize + 1;
	if ((pAr->ppData = (void **) SysAlloc(pAr->lAlloc * sizeof(void *))) == NULL) {
		SysFree(pAr);
		return INVALID_ARRAY_HANDLE;
	}

	return (ARRAY_HANDLE) pAr;
}

void ArrayFree(ARRAY_HANDLE hArray, void (*pfFree)(void *, void *),
	       void *pPrivate)
{
	Array *pAr;

	/*
	 * Having *Free() functions to accept NULL handles is useful in
	 * order to streamline the cleanup path of functions using them.
	 */
	if (hArray == INVALID_ARRAY_HANDLE)
		return;
	pAr = (Array *) hArray;
	if (pfFree != NULL)
		for (long i = pAr->lCount - 1; i >= 0; i--)
			if (pAr->ppData[i] != NULL)
				(*pfFree)(pPrivate, pAr->ppData[i]);
	SysFree(pAr->ppData);
	SysFree(pAr);
}

int ArraySet(ARRAY_HANDLE hArray, long lIdx, void *pData)
{
	Array *pAr = (Array *) hArray;

	if (lIdx < 0) {
		ErrSetErrorCode(ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}
	if (lIdx >= pAr->lAlloc) {
		long i, lAlloc = 2 * pAr->lAlloc + ARRAY_EXTRA_SPACE;
		void **ppData = (void **) SysRealloc(pAr->ppData, lAlloc * sizeof(void *));

		if (ppData == NULL)
			return ErrGetErrorCode();
		for (i = lAlloc - 1; i >= pAr->lAlloc; i--)
			ppData[i] = NULL;
		pAr->lAlloc = lAlloc;
		pAr->ppData = ppData;
	}
	pAr->ppData[lIdx] = pData;
	if (lIdx >= pAr->lCount)
		pAr->lCount = lIdx + 1;

	return 0;
}

long ArrayLength(ARRAY_HANDLE hArray)
{
	Array *pAr = (Array *) hArray;

	return pAr->lCount;
}

void *ArrayGet(ARRAY_HANDLE hArray, long lIdx)
{
	Array *pAr = (Array *) hArray;

	if (lIdx < 0 || lIdx >= pAr->lCount) {
		ErrSetErrorCode(ERR_NOT_FOUND);
		return NULL;
	}

	return pAr->ppData[lIdx];
}

int ArrayAppend(ARRAY_HANDLE hArray, void *pData)
{
	return ArraySet(hArray, ArrayLength(hArray), pData);
}

