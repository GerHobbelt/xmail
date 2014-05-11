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
#include "ShBlocks.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define RUNNING_PIDS_DIR      "/var/run"
#define DEVNULL               "/dev/null"
#if !defined(NOFILE)
#define NOFILE                64
#endif /* !NOFILE */

#define XMAIL_DEBUG_OPTION    "-Md"
#define XMAIL_PIDDIR_ENV      "XMAIL_PID_DIR"

#if (defined(__GLIBC__) && defined (__FreeBSD_kernel__)) || defined(__DARWIN_10_5__)
#define BSD_SETPGRP()         setpgrp()
#else
#define BSD_SETPGRP()         setpgrp(0, getpid())
#endif

static int MnEventLog(char const *pszFormat, ...)
{
    va_list Args;
    char szBuffer[2048];

    openlog(APP_NAME_STR, LOG_PID, LOG_DAEMON);
    va_start(Args, pszFormat);
    vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);

    syslog(LOG_ERR, "%s", szBuffer);

    va_end(Args);
    closelog();

    return 0;
}

static char const *MnGetPIDDir(void)
{
    char const *pszPIDDir = getenv(XMAIL_PIDDIR_ENV);

    return pszPIDDir != NULL ? pszPIDDir: RUNNING_PIDS_DIR;
}

static int MnSavePID(char const *pszPidFile)
{
    char szPidFile[SYS_MAX_PATH];

    snprintf(szPidFile, sizeof(szPidFile) - 1, "%s/%s.pid", MnGetPIDDir(),
         pszPidFile);

    FILE *pFile = fopen(szPidFile, "w");

    if (pFile == NULL) {
        perror(szPidFile);
        return -errno;
    }
    fprintf(pFile, "%u", (unsigned int) getpid());
    fclose(pFile);

    return 0;
}

static int MnRemovePID(char const *pszPidFile)
{
    char szPidFile[SYS_MAX_PATH];

    snprintf(szPidFile, sizeof(szPidFile) - 1, "%s/%s.pid", MnGetPIDDir(),
         pszPidFile);
    if (unlink(szPidFile) != 0) {
        perror(szPidFile);
        return -errno;
    }

    return 0;
}

static void MnSIGCLD(int iSignal)
{
    int iExitStatus;
    int iDeadPID;

    while ((iDeadPID = wait3(&iExitStatus, WNOHANG,
                 (struct rusage *) NULL)) > 0);
    signal(iSignal, MnSIGCLD);
}

static void MnSetupStdHandles(void)
{
    int iFD = open(DEVNULL, O_RDWR, 0);

    if (iFD == -1) {
        MnEventLog("Cannot open file %s : %s", DEVNULL, strerror(errno));
        exit(errno);
    }

    if (dup2(iFD, 0) == -1 || dup2(iFD, 1) == -1 || dup2(iFD, 2) == -1) {
        MnEventLog("File descriptor duplication error : %s", strerror(errno));
        exit(errno);
    }
    if (iFD > 2)
        close(iFD);
}

static int MnDaemonBootStrap(void)
{
    /*
     * This code is inspired from the code of the great Richard Stevens books.
     * May You RIP in programmers paradise great Richard.
     * I suggest You to buy all his collection, soon!
     */
#ifdef SIGTTOU
    signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTTIN
    signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTSTP
    signal(SIGTSTP, SIG_IGN);
#endif

    /* 1st fork */
    int iChildPID = fork();

    if (iChildPID < 0) {
        MnEventLog("Cannot fork : %s", strerror(errno));

        exit(errno);
    } else if (iChildPID > 0) {
        exit(0);
    }
    /*
     * Disassociate from controlling terminal and process group.
     * Ensure the process can't reacquire a new controlling terminal.
     */
    if (BSD_SETPGRP() == -1) {
        MnEventLog("Can't change process group : %s", strerror(errno));

        exit(errno);
    }
    /* Lose controlling tty */
    int iFdTty = open("/dev/tty", O_RDWR);

    if (iFdTty >= 0) {
        ioctl(iFdTty, TIOCNOTTY, (char *) NULL);
        close(iFdTty);
    }
    /* Close open file descriptors */
    for (int fd = 0; fd < NOFILE; fd++)
        close(fd);

    /* Set std handles */
    MnSetupStdHandles();

    /* Probably got set to EBADF from a close */
    errno = 0;

    /* Move the current directory to root, to make sure we aren't on a mounted */
    /* filesystem. */
    chdir("/");

    /* Clear any inherited file mode creation mask. */
    umask(0);

    /* Ignore childs dead. */
    signal(SIGCHLD, MnSIGCLD);

    return 0;
}

static int MnIsDebugStartup(int iArgCount, char *pszArgs[])
{
    for (int i = 0; i < iArgCount; i++)
        if (strcmp(pszArgs[i], XMAIL_DEBUG_OPTION) == 0)
            return 1;

    return 0;
}

static int MnDaemonStartup(int iArgCount, char *pszArgs[])
{
    /* Daemon bootstrap code if We're not in debug mode */
    if (!MnIsDebugStartup(iArgCount, pszArgs))
        MnDaemonBootStrap();

    /* Extract PID file name */
    char const *pszPidFile = strrchr(pszArgs[0], '/');

    pszPidFile = (pszPidFile != NULL) ? (pszPidFile + 1): pszArgs[0];

    /* Create PID file */
    MnSavePID(pszPidFile);

    int iServerResult = SvrMain(iArgCount, pszArgs);

    /* Remove PID file */
    MnRemovePID(pszPidFile);

    return iServerResult;
}

int main(int iArgCount, char *pszArgs[])
{
    return MnDaemonStartup(iArgCount, pszArgs);
}

