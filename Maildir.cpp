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
#include "StrUtils.h"
#include "SList.h"
#include "MD5.h"
#include "Base64Enc.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "Maildir.h"

static int MdirMessageID(char *pszMessageID, int iMaxMessageID)
{
    /*
     * Get thread ID and host name. We do not use atomic inc on ulUniqSeq, since
     * collision is prevented by the thread ID
     */
    static unsigned long ulUniqSeq = 0;
    unsigned long ulThreadID = SysGetCurrentThreadId();
    SYS_INT64 iMsTime = SysMsTime();
    char szHostName[MAX_HOST_NAME] = "";

    gethostname(szHostName, sizeof(szHostName) - 1);
    SysSNPrintf(pszMessageID, iMaxMessageID,
            SYS_LLU_FMT ".%lu.%lx.%s",
            iMsTime, ulThreadID, ulUniqSeq++, szHostName);

    return 0;
}

int MdirCreateStructure(const char *pszBasePath)
{
    /* Create Maildir directory */
    char szMaildirPath[SYS_MAX_PATH] = "";

    StrSNCpy(szMaildirPath, pszBasePath);

    AppendSlash(szMaildirPath);
    StrSNCat(szMaildirPath, MAILDIR_DIRECTORY);

    if (SysMakeDir(szMaildirPath) < 0)
        return ErrGetErrorCode();

    /* Create Maildir/tmp directory */
    char szSubPath[SYS_MAX_PATH] = "";

    sprintf(szSubPath, "%s" SYS_SLASH_STR "tmp", szMaildirPath);

    if (SysMakeDir(szSubPath) < 0)
        return ErrGetErrorCode();

    /* Create Maildir/new directory */
    sprintf(szSubPath, "%s" SYS_SLASH_STR "new", szMaildirPath);

    if (SysMakeDir(szSubPath) < 0)
        return ErrGetErrorCode();

    /* Create Maildir/cur directory */
    sprintf(szSubPath, "%s" SYS_SLASH_STR "cur", szMaildirPath);

    if (SysMakeDir(szSubPath) < 0)
        return ErrGetErrorCode();

    return 0;
}

int MdirGetTmpMaildirEntry(const char *pszMaildirPath, char *pszFilePath,
               int iMaxPath)
{
    char szMessageID[SYS_MAX_PATH];

    if (MdirMessageID(szMessageID, sizeof(szMessageID)) < 0)
        return ErrGetErrorCode();
    SysSNPrintf(pszFilePath, iMaxPath,
            "%s" SYS_SLASH_STR "tmp" SYS_SLASH_STR "%s",
            pszMaildirPath, szMessageID);

    return 0;
}

int MdirMoveTmpEntryInNew(const char *pszTmpEntryPath)
{
    /* Lookup Maildir/tmp/ subpath */
    const char *pszTmpDir = MAILDIR_DIRECTORY SYS_SLASH_STR "tmp" SYS_SLASH_STR;
    const char *pszLookup = strstr(pszTmpEntryPath, pszTmpDir);

    if (pszLookup == NULL) {
        ErrSetErrorCode(ERR_INVALID_MAILDIR_SUBPATH);
        return ERR_INVALID_MAILDIR_SUBPATH;
    }
    /* Build Maildir/new file path */
    int iBaseLength = (int) (pszLookup - pszTmpEntryPath);
    const char *pszNewDir = MAILDIR_DIRECTORY SYS_SLASH_STR "new" SYS_SLASH_STR;
    const char *pszSlash = strrchr(pszTmpEntryPath, SYS_SLASH_CHAR);
    char szNewEntryPath[SYS_MAX_PATH] = "";

    StrSNCpy(szNewEntryPath, pszTmpEntryPath);
    StrNCpy(szNewEntryPath + iBaseLength, pszNewDir, sizeof(szNewEntryPath) - iBaseLength);
    StrNCat(szNewEntryPath, pszSlash + 1, sizeof(szNewEntryPath));

    /* Move to Maildir/new */
    if (SysMoveFile(pszTmpEntryPath, szNewEntryPath) < 0)
        return ErrGetErrorCode();

    return 0;
}

/*
 * This function must be called with file names located onto the same
 * mount/drive: of the destination Maildir. This is accomplished by
 * the function UsrGetTmpFile().
 */
int MdirMoveMessage(const char *pszMaildirPath, const char *pszFileName,
            const char *pszMessageID)
{
    char szMessageID[SYS_MAX_PATH];
    char szNewEntryPath[SYS_MAX_PATH];

    if (pszMessageID == NULL) {
        if (MdirMessageID(szMessageID, sizeof(szMessageID)) < 0)
            return ErrGetErrorCode();
        pszMessageID = szMessageID;
    }
    SysSNPrintf(szNewEntryPath, sizeof(szNewEntryPath),
            "%s" SYS_SLASH_STR "new" SYS_SLASH_STR "%s",
            pszMaildirPath, pszMessageID);
    if (SysMoveFile(pszFileName, szNewEntryPath) < 0)
        return ErrGetErrorCode();

    return 0;
}

