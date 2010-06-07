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
#include "SysDepUnix.h"
#include "AppDefines.h"


#if defined(__OPENBSD__) || defined(__NETBSD__)
static int SysGetPriorityMin(int iPolicy);
static int SysGetPriorityMax(int iPolicy);
#endif


int SysDepInitLibrary(void)
{
	return 0;
}

void SysDepCleanupLibrary(void)
{

}

#if defined(__OPENBSD__) || defined(__NETBSD__)

static int SysGetPriorityMin(int iPolicy)
{
	int iPriority = 0;

	switch (iPolicy) {
	case SCHED_FIFO:
		iPriority = 0;
		break;
	case SCHED_OTHER:
		iPriority = -20;
		break;
	case SCHED_RR:
		iPriority = 0;
		break;
	}

	return iPriority;
}

static int SysGetPriorityMax(int iPolicy)
{
	int iPriority = 0;

	switch (iPolicy) {
	case SCHED_FIFO:
		iPriority = 31;
		break;
	case SCHED_OTHER:
		iPriority = +20;
		break;
	case SCHED_RR:
		iPriority = 31;
		break;
	}

	return iPriority;
}

#endif

int SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority)
{
	ThrData *pTD = (ThrData *) ThreadID;
	int iPolicy;
	struct sched_param SchParam;

	if (pthread_getschedparam(pTD->ThreadId, &iPolicy, &SchParam) != 0) {
		ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
		return ERR_SET_THREAD_PRIORITY;
	}
#if defined(__FREEBSD__) || defined (__DARWIN__)
	int iMinPriority = sched_get_priority_min(iPolicy);
	int iMaxPriority = sched_get_priority_max(iPolicy);
#else
	int iMinPriority = SysGetPriorityMin(iPolicy);
	int iMaxPriority = SysGetPriorityMax(iPolicy);
#endif

	int iStdPriority = (iMinPriority + iMaxPriority) / 2;

	switch (iPriority) {
	case SYS_PRIORITY_NORMAL:
		SchParam.sched_priority = iStdPriority;
		break;

	case SYS_PRIORITY_LOWER:
		SchParam.sched_priority = iStdPriority - (iStdPriority - iMinPriority) / 3;
		break;

	case SYS_PRIORITY_HIGHER:
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
	time_t tCurr = time(NULL);
	struct tm tmCurr;

	localtime_r(&tCurr, &tmCurr);

	return -tmCurr.tm_gmtoff + tmCurr.tm_isdst * 3600;
}

int SysGetDiskSpace(char const *pszPath, SYS_INT64 *pTotal, SYS_INT64 *pFree)
{
#ifdef BSD_USE_STATVFS
#define XMAIL_STATFS statvfs
#else
#define XMAIL_STATFS statfs
#endif
	struct XMAIL_STATFS SFStat;

	if (XMAIL_STATFS(pszPath, &SFStat) != 0) {
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
#if defined(__FREEBSD__)
	int iValue;
	size_t DataLen;

	DataLen = sizeof(iValue);
	if (sysctlbyname("vm.stats.vm.v_page_size", &iValue, &DataLen, NULL, 0) != 0) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}

	SYS_INT64 PageSize = (SYS_INT64) iValue;

	DataLen = sizeof(iValue);
	if (sysctlbyname("vm.stats.vm.v_page_count", &iValue, &DataLen, NULL, 0) != 0) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}
	*pVirtTotal = *pRamTotal = (SYS_INT64) iValue *PageSize;

	DataLen = sizeof(iValue);
	if (sysctlbyname("vm.stats.vm.v_free_count", &iValue, &DataLen, NULL, 0) != 0) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}
	*pVirtFree = *pRamFree = (SYS_INT64) iValue *PageSize;

	/* Get swap infos through the kvm interface */
	char szErrBuffer[_POSIX2_LINE_MAX] = "";
	kvm_t *pKD = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, szErrBuffer);

	if (pKD == NULL) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}

	struct kvm_swap KSwap[8];
	int iSwaps = kvm_getswapinfo(pKD, KSwap, CountOf(KSwap), SWIF_DEV_PREFIX);

	for (int i; i < iSwaps; i++) {
		*pVirtFree += (SYS_INT64) (KSwap[i].ksw_total - KSwap[i].ksw_used) * PageSize;
		*pVirtTotal += (SYS_INT64) KSwap[i].ksw_total * PageSize;
	}
	kvm_close(pKD);

	return 0;
#else
	int iResult = 0, iHwPhisMem, iHwPageSize;
	size_t DataLen;
	struct vmtotal VmMeter;
	static int iHwPhisMem_mib[] = { CTL_HW, HW_PHYSMEM };
	static int iHwPageSize_mib[] = { CTL_HW, HW_PAGESIZE };
	static int VmMeter_mib[] = { CTL_VM, VM_METER };

	DataLen = sizeof(iHwPhisMem);
	if (iResult >= 0)
		iResult = sysctl(iHwPhisMem_mib, 2, &iHwPhisMem, &DataLen, NULL, 0);

	DataLen = sizeof(iHwPageSize);
	if (iResult >= 0)
		iResult = sysctl(iHwPageSize_mib, 2, &iHwPageSize, &DataLen, NULL, 0);

	DataLen = sizeof(vmtotal);
	if (iResult >= 0)
		iResult = sysctl(VmMeter_mib, 2, &VmMeter, &DataLen, NULL, 0);

	if (iResult < 0) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}

	*pRamTotal = iHwPhisMem;
	*pRamFree = (SYS_INT64) iHwPageSize *(SYS_INT64) VmMeter.t_free;
	*pVirtTotal = (SYS_INT64) iHwPageSize *(SYS_INT64) VmMeter.t_vm;
	*pVirtFree = *pVirtTotal - (SYS_INT64) iHwPageSize *(SYS_INT64) VmMeter.t_avm;

	return 0;
#endif

}

