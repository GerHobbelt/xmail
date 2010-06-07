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

#ifndef _SYSDEPUNIX_H
#define _SYSDEPUNIX_H

#define MIN_TCP_SEND_SIZE  (1024 * 8)
#define MAX_TCP_SEND_SIZE  (1024 * 128)
#define K_IO_TIME_RATIO    8

struct ThreadExitHook {
	void (*pfHook)(void *, SYS_THREAD, int);
	void *pPrivate;
};

struct SemData {
	pthread_mutex_t Mtx;
	pthread_cond_t WaitCond;
	int iSemCounter;
	int iMaxCount;
};

struct MutexData {
	pthread_mutex_t Mtx;
	pthread_cond_t WaitCond;
	int iLocked;
};

struct EventData {
	pthread_mutex_t Mtx;
	pthread_cond_t WaitCond;
	int iSignaled;
	int iManualReset;
};

struct PEventData {
	int PipeFds[2];
	int iManualReset;
};

struct WaitData {
	pthread_mutex_t Mtx;
	pthread_cond_t WaitCond;
};

struct ThrData {
	pthread_t ThreadId;
	unsigned int (*ThreadProc) (void *);
	void *pThreadData;
	pthread_mutex_t Mtx;
	pthread_cond_t ExitWaitCond;
	int iThreadEnded;
	int iExitCode;
	int iUseCount;
};

union FilledDirent {
	struct dirent DE;
	char Pad[sizeof(struct dirent) + SYS_MAX_PATH];
};

struct FileFindData {
	char szPath[SYS_MAX_PATH];
	DIR *pDIR;
	FilledDirent FDE;
	struct stat FStat;
};

struct MMapData {
	unsigned long ulPageSize;
	int iFD;
	int iNumMaps;
	SYS_OFF_T llFileSize;
	unsigned long ulFlags;
};


int SysDepInitLibrary(void);
void SysDepCleanupLibrary(void);
int SysSendFileMMap(SYS_SOCKET SockFD, char const *pszFileName, SYS_OFF_T llBaseOffset,
		    SYS_OFF_T llEndOffset, int iTimeout);

#endif

