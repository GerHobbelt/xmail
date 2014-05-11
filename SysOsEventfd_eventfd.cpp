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
    int iEventfd;
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
    if ((pEFD->iEventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) == -1) {
        pthread_mutex_destroy(&pEFD->Mtx);
        SysFree(pEFD);
        ErrSetErrorCode(ERR_EVENTFD);
        return SYS_INVALID_EVENTFD;
    }

    return (SYS_EVENTFD) pEFD;
}

int SysEventfdClose(SYS_EVENTFD hEventfd)
{
    EventfdData *pEFD = (EventfdData *) hEventfd;

    if (pEFD != NULL) {
        close(pEFD->iEventfd);
        pthread_mutex_destroy(&pEFD->Mtx);
        SysFree(pEFD);
    }

    return 0;
}

int SysEventfdWaitFD(SYS_EVENTFD hEventfd)
{
    EventfdData *pEFD = (EventfdData *) hEventfd;

    return pEFD->iEventfd;
}

int SysEventfdSet(SYS_EVENTFD hEventfd)
{
    EventfdData *pEFD = (EventfdData *) hEventfd;
    SYS_UINT64 Value = 1;

    pthread_mutex_lock(&pEFD->Mtx);
    if (pEFD->iSignaled == 0) {
        write(pEFD->iEventfd, &Value, sizeof(Value));
        pEFD->iSignaled++;
    }
    pthread_mutex_unlock(&pEFD->Mtx);

    return 0;
}

int SysEventfdReset(SYS_EVENTFD hEventfd)
{
    EventfdData *pEFD = (EventfdData *) hEventfd;
    int iCount = 0;
    SYS_UINT64 Value;

    pthread_mutex_lock(&pEFD->Mtx);
    if (pEFD->iSignaled > 0) {
        /*
         * We write no more than one byte on the pipe (see above the
         * SysEventfdSet() function), so we need one read of one byte
         * only to flush it.
         */
        if (read(pEFD->iEventfd, &Value, sizeof(Value)) == sizeof(Value))
            iCount++;
        pEFD->iSignaled = 0;
    }
    pthread_mutex_unlock(&pEFD->Mtx);

    return iCount;
}

