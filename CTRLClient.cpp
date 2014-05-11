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
#include "BuffSock.h"
#include "SSLBind.h"
#include "SSLConfig.h"
#include "MiscUtils.h"
#include "StrUtils.h"
#include "MD5.h"
#include "Errors.h"

#define CCLN_TLS_INIT_STR           "#!TLS"
#define STD_CTRL_PORT               6017
#define STD_CTRL_TIMEOUT            90000
#define CTRL_LISTFOLLOW_RESULT      100
#define CTRL_WAITDATA_RESULT        101
#define CCLN_ERROR_BASE             (-10000)
#define CCLN_ERR_BAD_USAGE          (-10000)
#define CCLN_ERR_SSL_KEYCERT        (-10001)

#define CCLN_CHF_USEMD5 (1 << 0)
#define CCLN_CHF_SSLSWITCH (1 << 1)
#define CCLN_CHF_USESSL (1 << 2)

struct CClnChannelCfg {
    SslServerBind SSLB;
    unsigned long ulFlags;
};

/* Needed by library functions ( START ) */
bool bServerDebug = false;
int iLogRotateHours = 24;
int iAddrFamily = AF_INET;
static char const * const pszCClnErrors[] = {
    "Wrong command line usage",
    "Either none or both private key and certificate file must be supplied"
};

char *SvrGetLogsDir(char *pszLogsDir, int iMaxPath)
{
    SysSNPrintf(pszLogsDir, iMaxPath - 1, ".");

    return pszLogsDir;
}

/* Needed by library functions ( END ) */

int CClnGetResponse(BSOCK_HANDLE hBSock, char *pszError, int iMaxError,
            int *piErrorCode, int iTimeout)
{
    char szRespBuffer[2048] = "";

    if (BSckGetString(hBSock, szRespBuffer, sizeof(szRespBuffer) - 1, iTimeout) == NULL)
        return ErrGetErrorCode();

    if ((szRespBuffer[0] != '+') && (szRespBuffer[0] != '-')) {
        ErrSetErrorCode(ERR_CCLN_INVALID_RESPONSE, szRespBuffer);
        return ERR_CCLN_INVALID_RESPONSE;
    }

    char *pszToken = szRespBuffer + 1;

    if (piErrorCode != NULL)
        *piErrorCode = atoi(pszToken) * ((szRespBuffer[0] == '-') ? -1: +1);

    for (; isdigit(*pszToken); pszToken++);
    if (*pszToken != ' ') {
        ErrSetErrorCode(ERR_CCLN_INVALID_RESPONSE, szRespBuffer);
        return ERR_CCLN_INVALID_RESPONSE;
    }
    for (; *pszToken == ' '; pszToken++);
    if (pszError != NULL) {
        strncpy(pszError, pszToken, iMaxError);
        pszError[iMaxError - 1] = '\0';
    }

    return 0;
}

int CClnRecvTextFile(const char *pszFileName, BSOCK_HANDLE hBSock, int iTimeout)
{
    bool bCloseFile = false;
    FILE *pFile = stdout;
    char szBuffer[2048] = "";

    if (pszFileName != NULL) {
        if ((pFile = fopen(pszFileName, "wt")) == NULL) {
            ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
            return ERR_FILE_CREATE;
        }
        bCloseFile = true;
    }
    while (BSckGetString(hBSock, szBuffer, sizeof(szBuffer) - 1, iTimeout) != NULL) {
        if (strcmp(szBuffer, ".") == 0)
            break;

        if (szBuffer[0] == '.')
            fprintf(pFile, "%s\n", szBuffer + 1);
        else
            fprintf(pFile, "%s\n", szBuffer);
    }
    if (bCloseFile)
        fclose(pFile);

    return 0;
}

int CClnSendTextFile(const char *pszFileName, BSOCK_HANDLE hBSock, int iTimeout)
{
    bool bCloseFile = false;
    FILE *pFile = stdin;
    char szBuffer[2048] = "";

    if (pszFileName != NULL) {
        if ((pFile = fopen(pszFileName, "rt")) == NULL) {
            ErrSetErrorCode(ERR_FILE_OPEN, pszFileName);
            return ERR_FILE_OPEN;
        }
        bCloseFile = true;
    }
    while (MscFGets(szBuffer, sizeof(szBuffer) - 1, pFile) != NULL) {
        if (szBuffer[0] == '.')
            for (int i = (int)strlen(szBuffer); i >= 0; i--)
                szBuffer[i + 1] = szBuffer[i];

        if (BSckSendString(hBSock, szBuffer, iTimeout) <= 0) {
            fclose(pFile);
            return ErrGetErrorCode();
        }
    }
    if (bCloseFile)
        fclose(pFile);

    return BSckSendString(hBSock, ".", iTimeout);
}

int CClnSubmitCommand(BSOCK_HANDLE hBSock, char const *pszCommand,
              char *pszError, int iMaxError, char const *pszIOFile, int iTimeout)
{
    if (BSckSendString(hBSock, pszCommand, iTimeout) < 0)
        return ErrGetErrorCode();

    int iErrorCode = 0;

    if (CClnGetResponse(hBSock, pszError, iMaxError, &iErrorCode, iTimeout) < 0)
        return ErrGetErrorCode();
    if (iErrorCode < 0) {
        ErrSetErrorCode(ERR_CCLN_ERROR_RESPONSE, pszError);
        return ERR_CCLN_ERROR_RESPONSE;
    }
    if (iErrorCode == CTRL_LISTFOLLOW_RESULT) {
        if (CClnRecvTextFile(pszIOFile, hBSock, iTimeout) < 0)
            return ErrGetErrorCode();
    } else if (iErrorCode == CTRL_WAITDATA_RESULT) {
        if (CClnSendTextFile(pszIOFile, hBSock, iTimeout) < 0)
            return ErrGetErrorCode();
        if (CClnGetResponse(hBSock, pszError, iMaxError, &iErrorCode, iTimeout) < 0)
            return ErrGetErrorCode();
    }

    return 0;
}

static int CClnSslEnvCB(void *pPrivate, int iID, void const *pData)
{
    SslBindEnv *pSslE = (SslBindEnv *) pPrivate;

    return 0;
}

static int CClnSslBind(CClnChannelCfg const *pChCfg, BSOCK_HANDLE hBSock)
{
    SslBindEnv SslE;

    ZeroData(SslE);
    if (BSslBindClient(hBSock, &pChCfg->SSLB, CClnSslEnvCB, &SslE) < 0)
        return ErrGetErrorCode();
    /*
     * We may want to add verify code here ...
     */

    SysFree(SslE.pszIssuer);
    SysFree(SslE.pszSubject);

    return 0;
}

BSOCK_HANDLE CClnConnectServer(char const *pszServer, int iPortNo,
                   char const *pszUsername, char const *pszPassword,
                   CClnChannelCfg const *pChCfg, int iTimeout)
{
    /* Get server address */
    SYS_INET_ADDR SvrAddr;

    ZeroData(SvrAddr); /* [i_a] */

    if (MscGetServerAddress(pszServer, SvrAddr, iPortNo) < 0)
        return INVALID_BSOCK_HANDLE;

    /* Try connect to server */
    SYS_SOCKET SockFD = SysCreateSocket(SysGetAddrFamily(SvrAddr), SOCK_STREAM, 0);

    if (SockFD == SYS_INVALID_SOCKET)
        return INVALID_BSOCK_HANDLE;

    if (SysConnect(SockFD, &SvrAddr, iTimeout) < 0) {
        SysCloseSocket(SockFD);
        return INVALID_BSOCK_HANDLE;
    }

    BSOCK_HANDLE hBSock = BSckAttach(SockFD);

    if (hBSock == INVALID_BSOCK_HANDLE) {
        SysCloseSocket(SockFD);
        return INVALID_BSOCK_HANDLE;
    }

    int iErrorCode = 0;
    char szRTXBuffer[2048] = "";

    if ((pChCfg->ulFlags & CCLN_CHF_USESSL) &&
        CClnSslBind(pChCfg, hBSock) < 0) {
        BSckDetach(hBSock, 1);
        return INVALID_BSOCK_HANDLE;
    }

    /* Read welcome message */
    if (CClnGetResponse(hBSock, szRTXBuffer, sizeof(szRTXBuffer), &iErrorCode, iTimeout) < 0) {
        BSckDetach(hBSock, 1);
        return INVALID_BSOCK_HANDLE;
    }
    if (iErrorCode < 0) {
        BSckDetach(hBSock, 1);
        ErrSetErrorCode(ERR_CCLN_ERROR_RESPONSE, szRTXBuffer);
        return INVALID_BSOCK_HANDLE;
    }

    /*
     * Do we need to switch to SSL?
     */
    if (pChCfg->ulFlags & CCLN_CHF_SSLSWITCH) {
        if (BSckSendString(hBSock, CCLN_TLS_INIT_STR, iTimeout) < 0 ||
            CClnGetResponse(hBSock, szRTXBuffer, sizeof(szRTXBuffer), &iErrorCode,
                    iTimeout) < 0) {
            BSckDetach(hBSock, 1);
            return INVALID_BSOCK_HANDLE;
        }
        if (iErrorCode < 0) {
            BSckDetach(hBSock, 1);
            ErrSetErrorCode(ERR_CCLN_ERROR_RESPONSE, szRTXBuffer);
            return INVALID_BSOCK_HANDLE;
        }
        if (CClnSslBind(pChCfg, hBSock) < 0) {
            BSckDetach(hBSock, 1);
            return INVALID_BSOCK_HANDLE;
        }
    }

    /* Prepare login */
    char szTimeStamp[256] = "";

    if ((pChCfg->ulFlags & CCLN_CHF_USEMD5) == 0 ||
        MscExtractServerTimeStamp(szRTXBuffer, szTimeStamp, sizeof(szTimeStamp)) == NULL)
        SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer), "\"%s\"\t\"%s\"",
                pszUsername, pszPassword);
    else {
        /* Perform MD5 authentication */
        char *pszHash = StrSprint("%s%s", szTimeStamp, pszPassword);
        char szMD5[128] = "";

        if (pszHash == NULL) {
            BSckDetach(hBSock, 1);
            return INVALID_BSOCK_HANDLE;
        }
        do_md5_string(pszHash, (int)strlen(pszHash), szMD5);
        SysFree(pszHash);

        /* Add a  #  char in head of password field */
        SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer), "\"%s\"\t\"#%s\"",
                pszUsername, szMD5);
    }

    /* Send login */
    if (BSckSendString(hBSock, szRTXBuffer, iTimeout) < 0) {
        BSckDetach(hBSock, 1);
        return INVALID_BSOCK_HANDLE;
    }
    if (CClnGetResponse(hBSock, szRTXBuffer, sizeof(szRTXBuffer), &iErrorCode,
                iTimeout) < 0) {
        BSckDetach(hBSock, 1);
        return INVALID_BSOCK_HANDLE;
    }
    if (iErrorCode < 0) {
        BSckDetach(hBSock, 1);
        ErrSetErrorCode(ERR_CCLN_ERROR_RESPONSE, szRTXBuffer);
        return INVALID_BSOCK_HANDLE;
    }

    return hBSock;
}

int CClnQuitConnection(BSOCK_HANDLE hBSock, int iTimeout)
{
    CClnSubmitCommand(hBSock, "\"quit\"", NULL, 0, NULL, iTimeout);
    BSckDetach(hBSock, 1);

    return 0;
}

int CClnLogError(int iError)
{
    char *pszError;

    if (iError <= CCLN_ERROR_BASE) {
        if (CCLN_ERROR_BASE - iError >= CountOf(pszCClnErrors))
            return iError;
        pszError = SysStrDup(pszCClnErrors[CCLN_ERROR_BASE - iError]);
    } else
        pszError = ErrGetErrorStringInfo(iError);
    if (pszError == NULL)
        return iError;
    fprintf(stderr, "%s\n", pszError);
    SysFree(pszError);

    return 0;
}

void CClnShowUsage(char const *pszProgName)
{
    fprintf(stderr,
        "use :  %s  [-snuptfSLcKCXHD]  ...\n"
        "options :\n"
        "       -s server        = set server address\n"
        "       -n port          = set server port [%d]\n"
        "       -u user          = set username\n"
        "       -p pass          = set password\n"
        "       -t timeout       = set timeout [%d]\n"
        "       -f filename      = set I/O filename [stdin/stdout]\n"
        "       -S               = enable SSL link negotiation\n"
        "       -L               = use native SSL link\n"
        "       -K filename      = set the SSL private key file\n"
        "       -C filename      = set the SSL certificate file\n"
        "       -X filename      = set the SSL certificate-list file\n"
        "       -H dir           = set the SSL certificate-store directory\n"
        "       -c               = disable MD5 authentication\n"
        "       -D               = enable debug\n",
        pszProgName, STD_CTRL_PORT, STD_CTRL_TIMEOUT);
}

int CClnExec(int iArgCount, char *pszArgs[])
{
    int i;
    int iPortNo = STD_CTRL_PORT;
    int iTimeout = STD_CTRL_TIMEOUT;
    CClnChannelCfg ChCfg;
    char szServer[MAX_HOST_NAME] = "";
    char szUsername[256] = "";
    char szPassword[256] = "";
    char szIOFile[SYS_MAX_PATH] = "";

    ZeroData(ChCfg);
    ChCfg.ulFlags = CCLN_CHF_USEMD5;
    ChCfg.SSLB.pszKeyFile = SysGetEnv("CTRL_KEY_FILE");
    ChCfg.SSLB.pszCertFile = SysGetEnv("CTRL_CERT_FILE");
    ChCfg.SSLB.pszCAFile = SysGetEnv("CTRL_CA_FILE");
    ChCfg.SSLB.pszCAPath = SysGetEnv("CTRL_CA_PATH");

    for (i = 1; i < iArgCount; i++) {
        if (pszArgs[i][0] != '-')
            break;

        switch (pszArgs[i][1]) {
        case ('s'):
            if (++i < iArgCount)
                StrSNCpy(szServer, pszArgs[i]);
            break;

        case ('n'):
            if (++i < iArgCount)
                iPortNo = atoi(pszArgs[i]);
            break;

        case ('u'):
            if (++i < iArgCount)
                StrSNCpy(szUsername, pszArgs[i]);
            break;

        case ('p'):
            if (++i < iArgCount)
                StrSNCpy(szPassword, pszArgs[i]);
            break;

        case ('t'):
            if (++i < iArgCount)
                iTimeout = atoi(pszArgs[i]) * 1000;
            break;

        case ('f'):
            if (++i < iArgCount)
                StrSNCpy(szIOFile, pszArgs[i]);
            break;

        case ('c'):
            ChCfg.ulFlags &= ~CCLN_CHF_USEMD5;
            break;

        case ('S'):
            ChCfg.ulFlags &= ~CCLN_CHF_USESSL;
            ChCfg.ulFlags |= CCLN_CHF_SSLSWITCH;
            break;

        case ('L'):
            ChCfg.ulFlags &= ~CCLN_CHF_SSLSWITCH;
            ChCfg.ulFlags |= CCLN_CHF_USESSL;
            break;

        case ('K'):
            if (++i < iArgCount) {
                SysFree(ChCfg.SSLB.pszKeyFile);
                ChCfg.SSLB.pszKeyFile = SysStrDup(pszArgs[i]);
            }
            break;

        case ('C'):
            if (++i < iArgCount) {
                SysFree(ChCfg.SSLB.pszCertFile);
                ChCfg.SSLB.pszCertFile = SysStrDup(pszArgs[i]);
            }
            break;

        case ('X'):
            if (++i < iArgCount) {
                SysFree(ChCfg.SSLB.pszCAFile);
                ChCfg.SSLB.pszCAFile = SysStrDup(pszArgs[i]);
            }
            break;

        case ('H'):
            if (++i < iArgCount) {
                SysFree(ChCfg.SSLB.pszCAPath);
                ChCfg.SSLB.pszCAPath = SysStrDup(pszArgs[i]);
            }
            break;

        case ('D'):
            bServerDebug = true;
            break;

        default:
            return CCLN_ERR_BAD_USAGE;
        }
    }
    if (strlen(szServer) == 0 || strlen(szUsername) == 0 ||
        strlen(szPassword) == 0 || i == iArgCount) {
        SysFree(ChCfg.SSLB.pszKeyFile);
        SysFree(ChCfg.SSLB.pszCertFile);
        return CCLN_ERR_BAD_USAGE;
    }
    if ((ChCfg.SSLB.pszKeyFile != NULL) != (ChCfg.SSLB.pszCertFile != NULL)) {
        SysFree(ChCfg.SSLB.pszKeyFile);
        SysFree(ChCfg.SSLB.pszCertFile);
        return CCLN_ERR_SSL_KEYCERT;
    }

    int iFirstParam = i;
    int iCmdLength = 0;

    for (; i < iArgCount; i++)
        iCmdLength += (int)strlen(pszArgs[i]) + 4;

    char *pszCommand = (char *) SysAlloc(iCmdLength + 1);

    if (pszCommand == NULL)
        return ErrGetErrorCode();

    for (i = iFirstParam; i < iArgCount; i++) {
        if (i == iFirstParam)
            sprintf(pszCommand, "\"%s\"", pszArgs[i]);
        else
            sprintf(pszCommand + strlen(pszCommand), "\t\"%s\"", pszArgs[i]);
    }

    BSOCK_HANDLE hBSock = CClnConnectServer(szServer, iPortNo, szUsername, szPassword,
                        &ChCfg, iTimeout);

    SysFree(ChCfg.SSLB.pszKeyFile);
    SysFree(ChCfg.SSLB.pszCertFile);
    SysFree(ChCfg.SSLB.pszCAFile);
    SysFree(ChCfg.SSLB.pszCAPath);
    if (hBSock == INVALID_BSOCK_HANDLE) {
        ErrorPush();
        SysFree(pszCommand);
        return ErrorPop();
    }

    char szRTXBuffer[2048] = "";

    if (CClnSubmitCommand(hBSock, pszCommand, szRTXBuffer, sizeof(szRTXBuffer),
                  (strlen(szIOFile) != 0) ? szIOFile: NULL, iTimeout) < 0) {
        ErrorPush();
        CClnQuitConnection(hBSock, iTimeout);
        SysFree(pszCommand);
        return ErrorPop();
    }
    SysFree(pszCommand);

    CClnQuitConnection(hBSock, iTimeout);

    return 0;
}

#ifndef __CTRLCLNT_LIBRARY__

int main(int iArgCount, char *pszArgs[])
{
    if (SysInitLibrary() < 0) {
        CClnLogError(ErrGetErrorCode());
        return 1;
    }
    if (BSslInit() < 0) {
        CClnLogError(ErrGetErrorCode());
        SysCleanupLibrary();
        return 2;
    }

    int iExecResult = CClnExec(iArgCount, pszArgs);

    if (iExecResult == CCLN_ERR_BAD_USAGE) {
        CClnShowUsage(pszArgs[0]);
        BSslCleanup();
        SysCleanupLibrary();
        return 3;
    } else if (iExecResult < 0) {
        CClnLogError(iExecResult);
        BSslCleanup();
        SysCleanupLibrary();
        return 4;
    }
    BSslCleanup();
    SysCleanupLibrary();

    return 0;
}

#endif              // #ifndef __CTRLCLNT_LIBRARY__

