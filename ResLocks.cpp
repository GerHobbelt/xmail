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
#include "ResLocks.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "MessQueue.h"
#include "MailConfig.h"
#include "MailSvr.h"

#define STD_WAIT_GATES              37
#define STD_RES_HASH_SIZE           251
#define STD_RLCK_HASH_INIT          9677

struct ResWaitGate {
    SYS_SEMAPHORE hSemaphore;
    int iWaitingProcesses;
    int iHashSize;
    SysListHead *pResList;
};

struct ResLockEntry {
    SysListHead LLink;
    int iShLocks;
    int iExLocks;
    char szName[1];
};

struct ResLocator {
    int iWaitGate;
    int iResIdx;
};

static unsigned long RLckHashString(char const *pData,
                    unsigned long ulHashInit = STD_RLCK_HASH_INIT);

static SYS_MUTEX hRLMutex = SYS_INVALID_MUTEX;
static ResWaitGate RLGates[STD_WAIT_GATES];

int RLckInitLockers(void)
{
    /* Create resource locking mutex */
    if ((hRLMutex = SysCreateMutex()) == SYS_INVALID_MUTEX)
        return ErrGetErrorCode();

    /* Initialize wait gates */
    for (int i = 0; i < STD_WAIT_GATES; i++) {
        if ((RLGates[i].hSemaphore = SysCreateSemaphore(0,
                                SYS_DEFAULT_MAXCOUNT)) ==
            SYS_INVALID_SEMAPHORE) {
            ErrorPush();
            for (--i; i >= 0; i--) {
                SysFree(RLGates[i].pResList);
                SysCloseSemaphore(RLGates[i].hSemaphore);
            }
            SysCloseMutex(hRLMutex);
            return ErrorPop();
        }

        RLGates[i].iWaitingProcesses = 0;
        RLGates[i].iHashSize = STD_RES_HASH_SIZE;

        if ((RLGates[i].pResList = (SysListHead *)
             SysAlloc(RLGates[i].iHashSize * sizeof(SysListHead))) == NULL) {
            ErrorPush();
            SysCloseSemaphore(RLGates[i].hSemaphore);
            for (--i; i >= 0; i--) {
                SysFree(RLGates[i].pResList);
                SysCloseSemaphore(RLGates[i].hSemaphore);
            }
            SysCloseMutex(hRLMutex);
            return ErrorPop();
        }
        for (int j = 0; j < RLGates[i].iHashSize; j++)
            SYS_INIT_LIST_HEAD(&RLGates[i].pResList[j]);
    }

    return 0;
}

static int RLckFreeEntry(ResLockEntry * pRLE)
{
    SysFree(pRLE);

    return 0;
}

int RLckCleanupLockers(void)
{
    if (SysLockMutex(hRLMutex, SYS_INFINITE_TIMEOUT) < 0)
        return ErrGetErrorCode();
    for (int i = 0; i < STD_WAIT_GATES; i++) {
        for (int j = 0; j < RLGates[i].iHashSize; j++) {
            SysListHead *pHead = &RLGates[i].pResList[j];
            SysListHead *pLLink;

            while ((pLLink = SYS_LIST_FIRST(pHead)) != NULL) {
                ResLockEntry *pRLE = SYS_LIST_ENTRY(pLLink, ResLockEntry, LLink);

                SYS_LIST_DEL(&pRLE->LLink);
                RLckFreeEntry(pRLE);
            }
        }
        SysFree(RLGates[i].pResList);
        SysCloseSemaphore(RLGates[i].hSemaphore);
    }
    SysUnlockMutex(hRLMutex);
    SysCloseMutex(hRLMutex);

    return 0;
}

static unsigned long RLckHashString(char const *pData, unsigned long ulHashInit)
{
    unsigned long ulHashVal = ulHashInit;

    while (*pData != 0) {
        ulHashVal += ToLower(*(unsigned char const *) pData);
        pData++;
        ulHashVal += (ulHashVal << 10);
        ulHashVal ^= (ulHashVal >> 6);
    }
    ulHashVal += (ulHashVal << 3);
    ulHashVal ^= (ulHashVal >> 11);
    ulHashVal += (ulHashVal << 15);

    return ulHashVal;
}

static void RLckGetResLocator(char const *pszResourceName, ResLocator * pRL)
{
    unsigned long ulResHash = RLckHashString(pszResourceName);

    pRL->iWaitGate = (int) (ulResHash % STD_WAIT_GATES);
    pRL->iResIdx = (int) (ulResHash % RLGates[pRL->iWaitGate].iHashSize);
}

static ResLockEntry *RLckGetEntry(ResLocator const *pRL, char const *pszResourceName)
{
    SysListHead *pHead = &RLGates[pRL->iWaitGate].pResList[pRL->iResIdx];
    SysListHead *pLLink;

    SYS_LIST_FOR_EACH(pLLink, pHead) {
        ResLockEntry *pRLE = SYS_LIST_ENTRY(pLLink, ResLockEntry, LLink);

        if (stricmp(pszResourceName, pRLE->szName) == 0)
            return pRLE;
    }

    ErrSetErrorCode(ERR_LOCK_ENTRY_NOT_FOUND);

    return NULL;
}

static int RLckRemoveEntry(ResLocator const *pRL, ResLockEntry * pRLE)
{
    SysListHead *pHead = &RLGates[pRL->iWaitGate].pResList[pRL->iResIdx];
    SysListHead *pLLink;

    SYS_LIST_FOR_EACH(pLLink, pHead) {
        ResLockEntry *pCurrRLE = SYS_LIST_ENTRY(pLLink, ResLockEntry, LLink);

        if (pCurrRLE == pRLE) {
            SYS_LIST_DEL(&pRLE->LLink);
            RLckFreeEntry(pRLE);
            return 0;
        }
    }

    ErrSetErrorCode(ERR_LOCK_ENTRY_NOT_FOUND);
    return ERR_LOCK_ENTRY_NOT_FOUND;
}

static ResLockEntry *RLckAllocEntry(char const *pszResourceName)
{
    ResLockEntry *pRLE = (ResLockEntry *) SysAlloc(sizeof(ResLockEntry) +
                               strlen(pszResourceName));

    if (pRLE == NULL)
        return NULL;

    SYS_INIT_LIST_LINK(&pRLE->LLink);
    strcpy(pRLE->szName, pszResourceName);

    return pRLE;
}

static int RLckTryLockEX(ResLocator const *pRL, char const *pszResourceName)
{
    ResLockEntry *pRLE = RLckGetEntry(pRL, pszResourceName);

    if (pRLE == NULL) {
        SysListHead *pHead = &RLGates[pRL->iWaitGate].pResList[pRL->iResIdx];

        if ((pRLE = RLckAllocEntry(pszResourceName)) == NULL)
            return ErrGetErrorCode();
        pRLE->iExLocks = 1;
        SYS_LIST_ADDH(&pRLE->LLink, pHead);
    } else {
        if (pRLE->iExLocks > 0 || pRLE->iShLocks > 0) {
            ErrSetErrorCode(ERR_LOCKED_RESOURCE);
            return ERR_LOCKED_RESOURCE;
        }
        pRLE->iExLocks = 1;
    }

    return 0;
}

static int RLckDoUnlockEX(ResLocator const *pRL, char const *pszResourceName)
{
    ResLockEntry *pRLE = RLckGetEntry(pRL, pszResourceName);

    if (pRLE == NULL || pRLE->iExLocks == 0) {
        ErrSetErrorCode(ERR_RESOURCE_NOT_LOCKED);
        return ERR_RESOURCE_NOT_LOCKED;
    }

    pRLE->iExLocks = 0;

    /* Remove entry from list and delete entry memory */
    if (RLckRemoveEntry(pRL, pRLE) < 0)
        return ErrGetErrorCode();

    /* Release waiting processes */
    if (RLGates[pRL->iWaitGate].iWaitingProcesses > 0) {
        SysReleaseSemaphore(RLGates[pRL->iWaitGate].hSemaphore,
                    RLGates[pRL->iWaitGate].iWaitingProcesses);

        RLGates[pRL->iWaitGate].iWaitingProcesses = 0;
    }

    return 0;
}

static int RLckTryLockSH(ResLocator const *pRL, char const *pszResourceName)
{
    ResLockEntry *pRLE = RLckGetEntry(pRL, pszResourceName);

    if (pRLE == NULL) {
        SysListHead *pHead = &RLGates[pRL->iWaitGate].pResList[pRL->iResIdx];

        if ((pRLE = RLckAllocEntry(pszResourceName)) == NULL)
            return ErrGetErrorCode();
        pRLE->iShLocks = 1;
        SYS_LIST_ADDH(&pRLE->LLink, pHead);
    } else {
        if (pRLE->iExLocks > 0) {
            ErrSetErrorCode(ERR_LOCKED_RESOURCE);
            return ERR_LOCKED_RESOURCE;
        }
        ++pRLE->iShLocks;
    }

    return 0;
}

static int RLckDoUnlockSH(ResLocator const *pRL, char const *pszResourceName)
{
    ResLockEntry *pRLE = RLckGetEntry(pRL, pszResourceName);

    if (pRLE == NULL || pRLE->iShLocks == 0) {
        ErrSetErrorCode(ERR_RESOURCE_NOT_LOCKED);
        return ERR_RESOURCE_NOT_LOCKED;
    }

    if (--pRLE->iShLocks == 0) {
        /* Remove entry from list and delete entry heap memory */
        if (RLckRemoveEntry(pRL, pRLE) < 0)
            return ErrGetErrorCode();

        /* Release waiting processes */
        if (RLGates[pRL->iWaitGate].iWaitingProcesses > 0) {
            SysReleaseSemaphore(RLGates[pRL->iWaitGate].hSemaphore,
                        RLGates[pRL->iWaitGate].iWaitingProcesses);

            RLGates[pRL->iWaitGate].iWaitingProcesses = 0;
        }
    }

    return 0;
}

static RLCK_HANDLE RLckLock(char const *pszResourceName,
                int (*pLockProc) (ResLocator const *, char const *))
{
    ResLocator RL;

    RLckGetResLocator(pszResourceName, &RL);
    for (;;) {
        /* Lock resources list access */
        if (SysLockMutex(hRLMutex, SYS_INFINITE_TIMEOUT) < 0)
            return INVALID_RLCK_HANDLE;

        int iLockResult = (*pLockProc)(&RL, pszResourceName);

        if (iLockResult == ERR_LOCKED_RESOURCE) {
            SYS_SEMAPHORE SemID = RLGates[RL.iWaitGate].hSemaphore;

            ++RLGates[RL.iWaitGate].iWaitingProcesses;

            SysUnlockMutex(hRLMutex);

            if (SysWaitSemaphore(SemID, SYS_INFINITE_TIMEOUT) < 0)
                return INVALID_RLCK_HANDLE;
        } else if (iLockResult == 0) {
            SysUnlockMutex(hRLMutex);
            break;
        } else {
            SysUnlockMutex(hRLMutex);
            return INVALID_RLCK_HANDLE;
        }
    }

    return (RLCK_HANDLE) SysStrDup(pszResourceName);
}

static int RLckUnlock(RLCK_HANDLE hLock,
              int (*pUnlockProc) (ResLocator const *, char const *))
{
    char *pszResourceName = (char *) hLock;
    ResLocator RL;

    RLckGetResLocator(pszResourceName, &RL);

    /* Lock resources list access */
    if (SysLockMutex(hRLMutex, SYS_INFINITE_TIMEOUT) < 0)
        return ErrGetErrorCode();

    if ((*pUnlockProc)(&RL, pszResourceName) < 0) {
        ErrorPush();
        SysUnlockMutex(hRLMutex);
        SysFree(pszResourceName);
        return ErrorPop();
    }
    SysUnlockMutex(hRLMutex);
    SysFree(pszResourceName);

    return 0;
}

RLCK_HANDLE RLckLockEX(char const *pszResourceName)
{
    return RLckLock(pszResourceName, RLckTryLockEX);
}

int RLckUnlockEX(RLCK_HANDLE hLock)
{
    return hLock != INVALID_RLCK_HANDLE ? RLckUnlock(hLock, RLckDoUnlockEX): 0;
}

RLCK_HANDLE RLckLockSH(char const *pszResourceName)
{
    return RLckLock(pszResourceName, RLckTryLockSH);
}

int RLckUnlockSH(RLCK_HANDLE hLock)
{
    return hLock != INVALID_RLCK_HANDLE ? RLckUnlock(hLock, RLckDoUnlockSH): 0;
}

