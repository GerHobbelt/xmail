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

#define SINF_SIZE(s, f) (((SYS_INT64) (s).f) * (s).mem_unit)

int SysDepInitLibrary(void)
{
	return 0;
}

void SysDepCleanupLibrary(void)
{

}

#ifndef SYS_USE_MMSENDFILE

int SysSendFile(SYS_SOCKET SockFD, char const *pszFileName, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llEndOffset, int iTimeout)
{
	int iFileID = open(pszFileName, O_RDONLY);

	if (iFileID == -1) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName);
		return ERR_FILE_OPEN;
	}

	SYS_OFF_T llFileSize = (SYS_OFF_T) lseek(iFileID, 0, SEEK_END);

	lseek(iFileID, 0, SEEK_SET);
	if (llEndOffset == -1)
		llEndOffset = llFileSize;

	/* Set send timeout */
	socklen_t OptLenght = sizeof(struct timeval);
	struct timeval oldTV;
	struct timeval newTV;

	if (getsockopt((int) SockFD, SOL_SOCKET, SO_SNDTIMEO, &oldTV, &OptLenght)) {
		close(iFileID);
		ErrSetErrorCode(ERR_GETSOCKOPT);
		return ERR_GETSOCKOPT;
	}

	newTV.tv_sec = iTimeout;
	newTV.tv_usec = 0;
	setsockopt((int) SockFD, SOL_SOCKET, SO_SNDTIMEO, &newTV, sizeof(newTV));

	/* Send the file */
	size_t iSndBuffSize = MIN_TCP_SEND_SIZE;
	time_t tStart;

	while (llBaseOffset < llEndOffset) {
		size_t iCurrSend = (size_t) Min((SYS_OFF_T) iSndBuffSize, llEndOffset - llBaseOffset);
		off_t ulStartOffset = (off_t) llBaseOffset;

		tStart = time(NULL);
		size_t iSendSize = sendfile((int) SockFD, iFileID, &ulStartOffset, iCurrSend);

		if (iSendSize != iCurrSend) {
			setsockopt((int) SockFD, SOL_SOCKET, SO_SNDTIMEO, &oldTV, sizeof(oldTV));
			close(iFileID);
			ErrSetErrorCode(ERR_SENDFILE);
			return ERR_SENDFILE;
		}

		if ((((time(NULL) - tStart) * K_IO_TIME_RATIO) < iTimeout) &&
		    (iSndBuffSize < MAX_TCP_SEND_SIZE))
			iSndBuffSize = Min(iSndBuffSize * 2, MAX_TCP_SEND_SIZE);

		llBaseOffset += iCurrSend;
	}
	setsockopt((int) SockFD, SOL_SOCKET, SO_SNDTIMEO, &oldTV, sizeof(oldTV));
	close(iFileID);

	return 0;
}

#else

int SysSendFile(SYS_SOCKET SockFD, char const *pszFileName, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llEndOffset, int iTimeout)
{

	return SysSendFileMMap(SockFD, pszFileName, llBaseOffset, llEndOffset, iTimeout);
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

	int iMinPriority = sched_get_priority_min(iPolicy),
		iMaxPriority = sched_get_priority_max(iPolicy),
		iStdPriority = (iMinPriority + iMaxPriority) / 2;

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
	return (long) timezone;
}

int SysGetDiskSpace(char const *pszPath, SYS_INT64 *pTotal, SYS_INT64 *pFree)
{
	struct statfs SFStat;

	if (statfs(pszPath, &SFStat) != 0) {
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
	struct sysinfo SI;

	if (sysinfo(&SI) < 0) {
		ErrSetErrorCode(ERR_GET_MEMORY_INFO);
		return ERR_GET_MEMORY_INFO;
	}
	*pRamTotal = SINF_SIZE(SI, totalram);
	*pRamFree = SINF_SIZE(SI, freeram);
	*pVirtTotal = SINF_SIZE(SI, totalswap) + SINF_SIZE(SI, totalram);
	*pVirtFree = SINF_SIZE(SI, freeswap) + SINF_SIZE(SI, freeram);

	return 0;
}

