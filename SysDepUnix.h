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

#define SYS_INVALID_EVENTFD     ((SYS_EVENTFD) 0)

#define SYS_CLOSE_PIPE(p)       do { close((p)[0]); close((p)[1]); } while (0)

typedef void *SYS_EVENTFD;

struct ThreadExitHook {
	void (*pfHook)(void *, SYS_THREAD, int);
	void *pPrivate;
};

struct WaitCond {
	pthread_mutex_t Mtx;
	pthread_cond_t Cond;
};

struct SemData {
	WaitCond Wait;
	int iSemCounter;
	int iMaxCount;
};

struct MutexData {
	WaitCond Wait;
	int iLocked;
};

struct EventData {
	WaitCond Wait;
	int iSignaled;
	int iManualReset;
};

struct PEventData {
	SYS_EVENTFD hEventfd;
	int iManualReset;
};

struct ThrData {
	WaitCond Wait;
	pthread_t ThreadId;
	unsigned int (*ThreadProc) (void *);
	void *pThreadData;
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
int SysBlockFD(int iFD, int iBlocking);
int SysFdWait(int iFD, unsigned int uEvents, int iTimeout);

SYS_EVENTFD SysEventfdCreate(void);
int SysEventfdClose(SYS_EVENTFD hEventfd);
int SysEventfdWaitFD(SYS_EVENTFD hEventfd);
int SysEventfdSet(SYS_EVENTFD hEventfd);
int SysEventfdReset(SYS_EVENTFD hEventfd);

#endif

