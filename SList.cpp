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
#include "SList.h"

void ListInit(HSLIST &hList)
{
	hList = INVALID_SLIST_PTR;
}

void ListAddHead(HSLIST &hList, PLISTLINK pLLink)
{
	pLLink->pNext = hList;
	hList = pLLink;
}

void ListAddTail(HSLIST &hList, PLISTLINK pLLink)
{
	PLISTLINK lpPrev = INVALID_SLIST_PTR, lpCurr = hList;

	while (lpCurr != INVALID_SLIST_PTR) {
		lpPrev = lpCurr;
		lpCurr = lpCurr->pNext;
	}

	pLLink->pNext = INVALID_SLIST_PTR;

	if (lpPrev == INVALID_SLIST_PTR)
		hList = pLLink;
	else
		lpPrev->pNext = pLLink;
}

PLISTLINK ListFirst(HSLIST &hList)
{
	return hList;
}

PLISTLINK ListNext(HSLIST &hList, PLISTLINK pLLink)
{
	return pLLink->pNext;
}

PLISTLINK ListRemovePtr(HSLIST &hList, PLISTLINK pLLink)
{
	PLISTLINK lpPrev = INVALID_SLIST_PTR, lpCurr = hList;

	while (lpCurr != INVALID_SLIST_PTR) {
		if (lpCurr == pLLink)
			break;
		lpPrev = lpCurr;
		lpCurr = lpCurr->pNext;
	}

	if (lpCurr == INVALID_SLIST_PTR)
		return INVALID_SLIST_PTR;

	if (lpPrev == INVALID_SLIST_PTR)
		hList = lpCurr->pNext;
	else
		lpPrev->pNext = lpCurr->pNext;

	lpCurr->pNext = INVALID_SLIST_PTR;

	return lpCurr;
}

PLISTLINK ListRemove(HSLIST &hList)
{
	PLISTLINK lpCurr = hList;

	if (lpCurr != INVALID_SLIST_PTR)
		hList = lpCurr->pNext, lpCurr->pNext = INVALID_SLIST_PTR;

	return lpCurr;
}

void ListPurgeFree(HSLIST &hList)
{
	PLISTLINK lpCurr;

	while ((lpCurr = ListRemove(hList)) != INVALID_SLIST_PTR)
		SysFree(lpCurr);
}

void ListPurge(HSLIST &hList)
{
	PLISTLINK lpCurr;

	while ((lpCurr = ListRemove(hList)) != INVALID_SLIST_PTR);
}

bool ListIsEmpty(HSLIST &hList)
{
	return hList == INVALID_SLIST_PTR;
}

int ListGetCount(HSLIST &hList)
{
	int i;
	PLISTLINK lpCurr = ListFirst(hList);

	for (i = 0; lpCurr != INVALID_SLIST_PTR;
	     lpCurr = ListNext(hList, lpCurr), i++);

	return i;
}

PLISTLINK *ListGetPointers(HSLIST &hList, int &iListCount)
{
	iListCount = ListGetCount(hList);

	PLISTLINK *pPointers = (PLISTLINK *) SysAlloc((iListCount + 1) * sizeof(PLISTLINK));

	if (pPointers != NULL) {
		int i;
		PLISTLINK lpCurr = ListFirst(hList);

		for (i = 0; lpCurr != INVALID_SLIST_PTR; lpCurr = ListNext(hList, lpCurr), i++)
			pPointers[i] = lpCurr;
		pPointers[i] = INVALID_SLIST_PTR;
	}

	return pPointers;
}

void ListReleasePointers(PLISTLINK * pPointers)
{
	SysFree(pPointers);
}

