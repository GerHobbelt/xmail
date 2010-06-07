/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,..,2004  Davide Libenzi
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
#include "SysDepUnix.h"
#include "AppDefines.h"


#define MAX_THREAD_CONCURRENCY      512
#define MAX_SWAP_NAME_SIZE          256


static int SysGetSwapInfo(SYS_INT64 *pSwapTotal, SYS_INT64 *pSwapFree);


int SysDepInitLibrary(void)
{
	thr_setconcurrency(MAX_THREAD_CONCURRENCY);

	return 0;
}

void SysDepCleanupLibrary(void)
{

}

int SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority)
{
	ThrData *pTD = (ThrData *) ThreadID;
	int iPolicy;
	struct sched_param SchParam;

	if (pthread_getschedparam(pTD->ThreadId, &iPolicy, &SchParam) != 0) {
		ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
		return ERR_SET_THREAD_PRIORITY;
	}

	int iMinPriority = sched_get_priority_min(iPolicy),
		iMaxPriority = sched_get_priority_max(iPolicy),
		iStdPriority = (iMinPriority + iMaxPriority) / 2;

	switch (iPriority) {
	case (SYS_PRIORITY_NORMAL):
		SchParam.sched_priority = iStdPriority;
		break;

	case (SYS_PRIORITY_LOWER):
		SchParam.sched_priority = iStdPriority - (iStdPriority - iMinPriority) / 3;
		break;

	case (SYS_PRIORITY_HIGHER):
		SchParam.sched_priority = iStdPriority + (iStdPriority - iMinPriority) / 3;
		break;
	}

	if (pthread_setschedparam(pTD->ThreadId, iPolicy, &SchParam) != 0) {
		ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
		return ERR_SET_THREAD_PRIORITY;
	}

	return 0;
}

long SysGetTimeZone(void)
{
	return (long) timezone;
}

int SysGetDiskSpace(char const *pszPath, SYS_INT64 *pTotal, SYS_INT64 *pFree)
{
	struct statvfs SFStat;

	if (statvfs(pszPath, &SFStat) != 0) {
		ErrSetErrorCode(ERR_GET_DISK_SPACE_INFO);
		return ERR_GET_DISK_SPACE_INFO;
	}

	*pTotal = (SYS_INT64) SFStat.f_bsize * (SYS_INT64) SFStat.f_blocks;
	*pFree = (SYS_INT64) SFStat.f_bsize * (SYS_INT64) SFStat.f_bavail;

	return 0;
}

int SysMemoryInfo(SYS_INT64 *pRamTotal, SYS_INT64 *pRamFree,
		  SYS_INT64 *pVirtTotal, SYS_INT64 *pVirtFree)
{
	SYS_INT64 SwapTotal;
	SYS_INT64 SwapFree;

	if (SysGetSwapInfo(&SwapTotal, &SwapFree) < 0) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}

	SYS_INT64 PageSize = (SYS_INT64) sysconf(_SC_PAGESIZE);

	*pRamTotal = (SYS_INT64) sysconf(_SC_PHYS_PAGES) * PageSize;
	*pRamFree = (SYS_INT64) sysconf(_SC_AVPHYS_PAGES) * PageSize;
	*pVirtTotal = SwapTotal + *pRamTotal;
	*pVirtFree = SwapFree + *pRamFree;

	return 0;
}

static int SysGetSwapInfo(SYS_INT64 *pSwapTotal, SYS_INT64 *pSwapFree)
{
	*pSwapTotal = *pSwapFree = 0;

	int iNumSwaps = swapctl(SC_GETNSWP, 0);

	if (iNumSwaps == -1)
		return -1;

	if (iNumSwaps == 0)
		return 0;

	swaptbl_t *pSwTab = (swaptbl_t *) malloc(iNumSwaps * sizeof(swapent_t) +
						 sizeof(struct swaptable));

	if (pSwTab == (void *) 0)
		return -1;

	memset(pSwTab, 0, iNumSwaps * sizeof(swapent_t) + sizeof(struct swaptable));

	char *pszNameTab = (char *) malloc(iNumSwaps * MAX_SWAP_NAME_SIZE);

	if (pszNameTab == (void *) 0) {
		free(pSwTab);
		return -1;
	}

	memset(pszNameTab, 0, iNumSwaps * MAX_SWAP_NAME_SIZE);

	int ii;

	for (ii = 0; ii < iNumSwaps; ii++)
		pSwTab->swt_ent[ii].ste_path = pszNameTab + (ii * MAX_SWAP_NAME_SIZE);

	pSwTab->swt_n = iNumSwaps;
	if ((iNumSwaps = swapctl(SC_LIST, pSwTab)) < 0) {
		free(pszNameTab);
		free(pSwTab);
		return -1;
	}

	SYS_INT64 PageSize = (SYS_INT64) sysconf(_SC_PAGESIZE);

	for (ii = 0; ii < iNumSwaps; ii++) {
		*pSwapTotal += (SYS_INT64) pSwTab->swt_ent[ii].ste_pages * PageSize;

		*pSwapFree += (SYS_INT64) pSwTab->swt_ent[ii].ste_free * PageSize;
	}
	free(pszNameTab);
	free(pSwTab);

	return 0;
}

