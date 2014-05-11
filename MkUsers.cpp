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

#if defined(WIN32)

#if 0
#include <stdio.h>
#include <io.h>
#include <direct.h>
#include <stdlib.h>
#include <string.h>
#endif

/*
#define SYS_SLASH_CHAR              '\\'
#define SYS_SLASH_STR               "\\"
#define SYS_MAX_PATH                256
*/

int SysPathExist(char const *pszPathName)
{
    return (_access(pszPathName, 0) == 0) ? 1 : 0;
}

int SysMakeDir(char const *pszPathName)
{
    return (_mkdir(pszPathName) == 0) ? 1 : 0;
}

int SysErrNo(void)
{
    return errno;
}

char const *SysErrStr(void)
{
    return strerror(errno);
}

#else               // #if defined(WIN32)
#if defined(__LINUX__) || defined(__SOLARIS__) || defined(__BSD__)

#if 0
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#define SYS_SLASH_CHAR              '/'
#define SYS_SLASH_STR               "/"
#define SYS_MAX_PATH                256
#endif

int SysPathExist(char const *pszPathName)
{
    return (access(pszPathName, 0) == 0) ? 1 : 0;
}

int SysMakeDir(char const *pszPathName)
{
    return (mkdir(pszPathName, 0700) == 0) ? 1 : 0;
}

int SysErrNo(void)
{
    return errno;
}

char const *SysErrStr(void)
{
    return strerror(errno);
}

#else               // #if defined(__LINUX__) || defined(__SOLARIS__)

#error system type not defined !

#endif              // #if defined(__LINUX__) || defined(__SOLARIS__)
#endif              // #if defined(WIN32)

#define MAX_MB_SIZE             10000
#define StrNCpy(t, s, n)        do { strncpy(t, s, n); (t)[(n) - 1] = '\0'; } while (0)
#define StrSNCpy(t, s)          StrNCpy(t, s, sizeof(t))

char *StrCrypt(char const *pszInput, char *pszCrypt)
{
    strcpy(pszCrypt, "");

    for (int ii = 0; pszInput[ii] != '\0'; ii++) {
        unsigned int uChar = (unsigned int) pszInput[ii];
        char szByte[32] = "";

        sprintf(szByte, "%02x", (uChar ^ 101) & 0xff);

        strcat(pszCrypt, szByte);
    }

    return pszCrypt;
}

int CreateUser(char const *pszRootDir, char const *pszDomain,
           char const *pszUsername, char const *pszPassword,
           unsigned int uUserId, char const *pszRealName,
           char const *pszHomePage, unsigned int uMBSize, bool bMaildir, FILE * pUsrFile)
{
    FILE *pTabFile;
    char szPathName[SYS_MAX_PATH] = "";
    char szCryptPwd[256] = "";

    /* Check-create domain directory */
    sprintf(szPathName, "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s", pszRootDir, pszDomain);  /* [i_a] */

    if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
        perror(szPathName);
        return SysErrNo();
    }
    /* Check-create domain/user directory */
    sprintf(szPathName, "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s", pszRootDir,  /* [i_a] */
        pszDomain, pszUsername);

    if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
        perror(szPathName);
        return SysErrNo();
    }

    if (bMaildir) {
        /* Check-create domain/user/Maildir/(tmp,new,cur) directories */
        sprintf(szPathName,
            "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s" SYS_SLASH_STR MAILDIR_DIRECTORY,  /* [i_a] */
            pszRootDir, pszDomain, pszUsername);

        if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
            perror(szPathName);
            return SysErrNo();
        }

        sprintf(szPathName,
            "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s" SYS_SLASH_STR MAILDIR_DIRECTORY  /* [i_a] */
            SYS_SLASH_STR "tmp", pszRootDir, pszDomain, pszUsername);

        if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
            perror(szPathName);
            return SysErrNo();
        }

        sprintf(szPathName,
            "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s" SYS_SLASH_STR MAILDIR_DIRECTORY  /* [i_a] */
            SYS_SLASH_STR "new", pszRootDir, pszDomain, pszUsername);

        if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
            perror(szPathName);
            return SysErrNo();
        }

        sprintf(szPathName,
            "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s" SYS_SLASH_STR MAILDIR_DIRECTORY  /* [i_a] */
            SYS_SLASH_STR "cur", pszRootDir, pszDomain, pszUsername);

        if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
            perror(szPathName);
            return SysErrNo();
        }
    } else {
        /* Check-create domain/user/mailbox directory */
        sprintf(szPathName,
            "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s" SYS_SLASH_STR MAILBOX_DIRECTORY,  /* [i_a] */
            pszRootDir, pszDomain, pszUsername);

        if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
            perror(szPathName);
            return SysErrNo();
        }
    }

    /* Check-create domain/user/mailbox/user.tab file */
    sprintf(szPathName,
        "%s" MAIL_DOMAINS_DIR SYS_SLASH_STR "%s" SYS_SLASH_STR "%s" SYS_SLASH_STR USER_PROFILE_FILE,  /* [i_a] */
        pszRootDir, pszDomain, pszUsername);

    if (!SysPathExist(szPathName)) {
        if ((pTabFile = fopen(szPathName, "wt")) == NULL) {
            perror(szPathName);
            return SysErrNo();
        }

        fprintf(pTabFile,
            "\"RealName\"\t\"%s\"\n"
            "\"HomePage\"\t\"%s\"\n"
            "\"MaxMBSize\"\t\"%u\"\n", pszRealName, pszHomePage, uMBSize);

        fclose(pTabFile);
    }
    /* Add user to users file */
    fprintf(pUsrFile,
        "\"%s\"\t"
        "\"%s\"\t"
        "\"%s\"\t"
        "\"%u\"\t"
        "\"%s\"\t"
        "\"U\"\n", pszDomain, pszUsername, StrCrypt(pszPassword, szCryptPwd), uUserId,
        pszUsername);

    return 0;
}

void ShowUsage(char const *pszProgName)
{
    fprintf(stderr,
        "use : %s [-adfursih]\n"
        "          -a numusers     = number of users to create in auto-mode\n"
        "          -d domain       = domain name in auto-mode\n"
        "          -f inputFile    = input file name {stdin}\n"
        "          -u username     = radix user name in auto-mode\n"
        "          -r rootdir      = mail root path {.%s}\n"
        "          -s mboxsize     = mailbox maximum size {%d}\n"
        "          -i useridbase   = base user id {1}\n"
        "          -m              = create Maildir boxes\n"
        "          -h              = show this message\n",
        pszProgName, SYS_SLASH_STR, MAX_MB_SIZE);

}

int main(int argc, char *argv[])
{
    int ii;
    int iNumUsers = 0;
    bool bMaildir = false;
    unsigned int uMBSize = MAX_MB_SIZE;
    unsigned int uUserId = 1;
    FILE *pUsrFile;
    FILE *pInFile = stdin;
    char szRootDir[SYS_MAX_PATH] = "." SYS_SLASH_STR;
    char szInputFile[SYS_MAX_PATH] = "";
    char szPathName[SYS_MAX_PATH] = "";
    char szAutoDomain[256] = "mkuser.net";
    char szAutoUsr[128] = "mkuser";
    char szUsrLine[1024] = "";

    for (ii = 1; ii < argc; ii++) {
        if (argv[ii][0] != '-')
            continue;

        switch (argv[ii][1]) {
        case ('a'):
            if (++ii < argc)
                iNumUsers = atoi(argv[ii]);
            break;

        case ('d'):
            if (++ii < argc)
                StrSNCpy(szAutoDomain, argv[ii]);
            break;

        case ('f'):
            if (++ii < argc)
                StrSNCpy(szInputFile, argv[ii]);
            break;

        case ('u'):
            if (++ii < argc)
                StrSNCpy(szAutoUsr, argv[ii]);
            break;

        case ('r'):
            if (++ii < argc)
                StrSNCpy(szRootDir, argv[ii]);
            break;

        case ('s'):
            if (++ii < argc)
                uMBSize = (unsigned int) atol(argv[ii]);
            break;

        case ('i'):
            if (++ii < argc)
                uUserId = (unsigned int) atol(argv[ii]);
            break;

        case ('m'):
            bMaildir = true;
            break;

        case ('h'):
            ShowUsage(argv[0]);
            return 0;

        default:
            ShowUsage(argv[0]);
            return 1;
        }
    }

    /* Root directory slash termination */
    if (szRootDir[strlen(szRootDir) - 1] != SYS_SLASH_CHAR)
        strcat(szRootDir, SYS_SLASH_STR);

    /* Check-create domains directory */
    sprintf(szPathName, "%s%s", szRootDir, MAIL_DOMAINS_DIR);  /* [i_a] */

    if (!SysPathExist(szPathName) && !SysMakeDir(szPathName)) {
        perror(szPathName);
        return SysErrNo();
    }
    /* Create mailusers.tab file */
    sprintf(szPathName, "%s%s", szRootDir, SVR_TABLE_FILE);  /* [i_a] */

    if (SysPathExist(szPathName)) {
        fprintf(stderr, "%s already exist\n", szPathName);
        return 1;
    }

    if ((pUsrFile = fopen(szPathName, "wt")) == NULL) {
        perror(szPathName);
        return SysErrNo();
    }

    if (iNumUsers == 0) {
        if ((strlen(szInputFile) != 0) && ((pInFile = fopen(szInputFile, "rt")) == NULL)) {
            perror(szPathName);
            fclose(pUsrFile);
            return SysErrNo();
        }
        /* Get input from stdin */
        while (fgets(szUsrLine, sizeof(szUsrLine) - 1, pInFile) != NULL) {
            char *pszDomain;
            char *pszUsername;
            char *pszPassword;
            char *pszRealName;
            char *pszHomePage;

            szUsrLine[strlen(szUsrLine) - 1] = '\0';

            if ((szUsrLine[0] == '#') ||
                ((pszDomain = strtok(szUsrLine, ";")) == NULL) ||
                ((pszUsername = strtok(NULL, ";")) == NULL) ||
                ((pszPassword = strtok(NULL, ";")) == NULL) ||
                ((pszRealName = strtok(NULL, ";")) == NULL) ||
                ((pszHomePage = strtok(NULL, ";")) == NULL))
                continue;

            if (CreateUser(szRootDir, pszDomain, pszUsername, pszPassword, uUserId++,
                       pszRealName, pszHomePage, uMBSize, bMaildir,
                       pUsrFile) != 0) {

                fprintf(stderr, "error creating <%s@%s> : %s\n",
                    pszUsername, pszDomain, SysErrStr());

            }

        }

        if (pInFile != stdin)
            fclose(pInFile);
    } else {
        /* Automatically generate users */
        for (ii = 1; ii <= iNumUsers; ii++) {
            char szUsername[256] = "";
            char szHomePage[256] = "";

            sprintf(szUsername, "%s%d", szAutoUsr, ii);

            sprintf(szHomePage, "http://www.%s/~%s", szAutoDomain, szUsername);

            if (CreateUser(szRootDir, szAutoDomain, szUsername, szUsername, uUserId++,
                       szUsername, szHomePage, uMBSize, bMaildir, pUsrFile) != 0)
            {

                fprintf(stderr, "error creating <%s@%s> : %s\n",
                    szUsername, szAutoDomain, SysErrStr());

            }

        }
    }

    fclose(pUsrFile);

    return 0;
}
