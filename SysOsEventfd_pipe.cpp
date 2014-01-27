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

struct EventfdData {
	pthread_mutex_t Mtx;
	int iSignaled;
	int PipeFds[2];
};

SYS_EVENTFD SysEventfdCreate(void)
{
	EventfdData *pEFD;

	if ((pEFD = (EventfdData *) SysAlloc(sizeof(EventfdData))) == NULL)
		return SYS_INVALID_EVENTFD;
	if (pthread_mutex_init(&pEFD->Mtx, NULL) != 0) {
		SysFree(pEFD);
		ErrSetErrorCode(ERR_MUTEXINIT);
		return SYS_INVALID_EVENTFD;
	}
	if (pipe(pEFD->PipeFds) == -1) {
		pthread_mutex_destroy(&pEFD->Mtx);
		SysFree(pEFD);
		ErrSetErrorCode(ERR_PIPE);
		return SYS_INVALID_EVENTFD;
	}
	if (SysBlockFD(pEFD->PipeFds[0], 0) < 0 ||
	    SysBlockFD(pEFD->PipeFds[1], 0) < 0) {
		SYS_CLOSE_PIPE(pEFD->PipeFds);
		pthread_mutex_destroy(&pEFD->Mtx);
		SysFree(pEFD);
		return SYS_INVALID_EVENTFD;
	}

	return (SYS_EVENTFD) pEFD;
}

int SysEventfdClose(SYS_EVENTFD hEventfd)
{
	EventfdData *pEFD = (EventfdData *) hEventfd;

	if (pEFD != NULL) {
		SYS_CLOSE_PIPE(pEFD->PipeFds);
		pthread_mutex_destroy(&pEFD->Mtx);
		SysFree(pEFD);
	}

	return 0;
}

int SysEventfdWaitFD(SYS_EVENTFD hEventfd)
{
	EventfdData *pEFD = (EventfdData *) hEventfd;

	return pEFD->PipeFds[0];
}

int SysEventfdSet(SYS_EVENTFD hEventfd)
{
	EventfdData *pEFD = (EventfdData *) hEventfd;

	pthread_mutex_lock(&pEFD->Mtx);
	if (pEFD->iSignaled == 0) {
		write(pEFD->PipeFds[1], ".", 1);
		pEFD->iSignaled++;
	}
	pthread_mutex_unlock(&pEFD->Mtx);

	return 0;
}

int SysEventfdReset(SYS_EVENTFD hEventfd)
{
	EventfdData *pEFD = (EventfdData *) hEventfd;
	int iCount = 0;
	unsigned char uByte;

	pthread_mutex_lock(&pEFD->Mtx);
	if (pEFD->iSignaled > 0) {
		/*
		 * We write no more than one byte on the pipe (see above the
		 * SysEventfdSet() function), so we need one read of one byte
		 * only to flush it.
		 */
		if (read(pEFD->PipeFds[0], &uByte, 1) == 1)
			iCount++;
		pEFD->iSignaled = 0;
	}
	pthread_mutex_unlock(&pEFD->Mtx);

	return iCount;
}

