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

struct SharedBlock {
    unsigned int uSize;
    SYS_MUTEX hMutex;
    void *pData;
};

SHB_HANDLE ShbCreateBlock(unsigned int uSize)
{
    SharedBlock *pSHB = (SharedBlock *) SysAlloc(sizeof(SharedBlock));

    if (pSHB == NULL)
        return SHB_INVALID_HANDLE;

    pSHB->uSize = uSize;
    if ((pSHB->hMutex = SysCreateMutex()) == SYS_INVALID_MUTEX) {
        SysFree(pSHB);
        return SHB_INVALID_HANDLE;
    }
    if ((pSHB->pData = SysAlloc(uSize)) == NULL) {
        SysCloseMutex(pSHB->hMutex);
        SysFree(pSHB);
        return SHB_INVALID_HANDLE;
    }

    return (SHB_HANDLE) pSHB;
}

int ShbCloseBlock(SHB_HANDLE hBlock)
{
    SharedBlock *pSHB = (SharedBlock *) hBlock;

    SysCloseMutex(pSHB->hMutex);
    SysFree(pSHB->pData);
    SysFree(pSHB);

    return 0;
}

void *ShbLock(SHB_HANDLE hBlock)
{
    SharedBlock *pSHB = (SharedBlock *) hBlock;

    if (SysLockMutex(pSHB->hMutex, SYS_INFINITE_TIMEOUT) < 0)
        return NULL;

    return pSHB->pData;
}

int ShbUnlock(SHB_HANDLE hBlock)
{
    SharedBlock *pSHB = (SharedBlock *) hBlock;

    SysUnlockMutex(pSHB->hMutex);

    return 0;
}

