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
	unsigned long ulAlloc;
	unsigned long ulCount;
	void **ppData;
};


ARRAY_HANDLE ArrayCreate(unsigned long ulSize)
{
	Array *pAr;

	if ((pAr = (Array *) SysAlloc(sizeof(Array))) == NULL)
		return INVALID_ARRAY_HANDLE;
	pAr->ulAlloc = ulSize + 1;
	if ((pAr->ppData = (void **) SysAlloc(pAr->ulAlloc * sizeof(void *))) == NULL) {
		SysFree(pAr);
		return INVALID_ARRAY_HANDLE;
	}

	return (ARRAY_HANDLE) pAr;
}

void ArrayFree(ARRAY_HANDLE hArray, void (*pFree)(void *, void *),
	       void *pPrivate)
{
	Array *pAr = (Array *) hArray;

	if (pAr != NULL) {
		if (pFree != NULL)
			for (unsigned long i = 0; i < pAr->ulCount; i++)
				if (pAr->ppData[i] != NULL)
					(*pFree)(pPrivate, pAr->ppData[i]);
		SysFree(pAr->ppData);
		SysFree(pAr);
	}
}

int ArraySet(ARRAY_HANDLE hArray, unsigned long ulIdx, void *pData)
{
	Array *pAr = (Array *) hArray;

	if (ulIdx >= pAr->ulAlloc) {
		unsigned long i, ulAlloc = (3 * ulIdx) / 2 + ARRAY_EXTRA_SPACE;
		void **ppData = (void **) SysRealloc(pAr->ppData,
						     ulAlloc * sizeof(void *));

		if (ppData == NULL)
			return ErrGetErrorCode();
		for (i = pAr->ulAlloc; i < ulAlloc; i++)
			ppData[i] = NULL;
		pAr->ulAlloc = ulAlloc;
		pAr->ppData = ppData;
	}
	pAr->ppData[ulIdx] = pData;
	if (ulIdx >= pAr->ulCount)
		pAr->ulCount = ulIdx + 1;

	return 0;
}

unsigned long ArrayCount(ARRAY_HANDLE hArray)
{
	Array *pAr = (Array *) hArray;

	return pAr->ulCount;
}

void *ArrayGet(ARRAY_HANDLE hArray, unsigned long ulIdx)
{
	Array *pAr = (Array *) hArray;

	if (ulIdx >= pAr->ulCount) {
		ErrSetErrorCode(ERR_NOT_FOUND);
		return NULL;
	}

	return pAr->ppData[ulIdx];
}

int ArrayAppend(ARRAY_HANDLE hArray, void *pData)
{
	return ArraySet(hArray, ArrayCount(hArray), pData);
}

