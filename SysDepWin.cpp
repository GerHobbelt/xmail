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
#include "AppDefines.h"

#define MAX_TLS_KEYS                    64
#define UNUSED_TLS_KEY_PROC             ((void (*)(void *)) -1L)

#define SOCK_VERSION_REQUESTED          MAKEWORD(2, 0)

/*
 * Under certain circumstances (MS Proxy installed ?!) a waiting operation
 * may be unlocked even if the IO terminal is not ready to perform following
 * IO request (an error code WSAEWOULDBLOCK is returned). When this happens,
 * the waiting operation will always return WSAEWOULDBLOCK by making the code
 * to spin burning CPU cycles. The only solution I see is to sleep a bit to
 * prevent processor time wasting :(
 */
#define BLOCKED_SNDRCV_MSSLEEP          250
#define HANDLE_SOCKS_SUCKS()            do { Sleep(BLOCKED_SNDRCV_MSSLEEP); } while (0)

#define SAIN_Addr(s)                    (s).sin_addr.S_un.S_addr

#define MIN_TCP_SEND_SIZE               (1024 * 8)
#define MAX_TCP_SEND_SIZE               (1024 * 128)
#define K_IO_TIME_RATIO                 8

#define SYS_MAX_TH_EXIT_HOOKS           64

struct ThreadExitHook {
    void (*pfHook)(void *, SYS_THREAD, int);
    void *pPrivate;
};

struct TlsKeyEntry {
    void (*pFreeProc) (void *);
};

struct TlsKeyData {
    void *pData;
};

struct FileFindData {
    char szFindPath[SYS_MAX_PATH];
    HANDLE hFind;
    WIN32_FIND_DATA WFD;
};

struct ThreadRunner {
    unsigned int (*pThreadProc) (void *);
    void *pThreadData;
};

struct MMapData {
    unsigned long ulPageSize;
    HANDLE hFile;
    HANDLE hFileMap;
    int iNumMaps;
    SYS_OFF_T llFileSize;
    unsigned long ulFlags;
};

static int iNumThExitHooks;
static ThreadExitHook ThExitHooks[SYS_MAX_TH_EXIT_HOOKS];
static char szServerName[SYS_MAX_PATH];
static CRITICAL_SECTION csTLS;
static bool bSetupEntries = true;
static TlsKeyEntry TlsKeyEntries[MAX_TLS_KEYS];
static __declspec(thread) TlsKeyData TlsKeys[MAX_TLS_KEYS];
static time_t tSysStart;
static SYS_INT64 PCFreq;
static SYS_INT64 PCSysStart;
static int iSndBufSize = -1, iRcvBufSize = -1;
static CRITICAL_SECTION csLog;
static void (*pSysBreakHandler) (void) = NULL;
static HANDLE hShutdownEvent = NULL;

static void SysInitTlsKeyEntries(void)
{
    if (bSetupEntries) {
        for (int i = 0; i < MAX_TLS_KEYS; i++)
            TlsKeyEntries[i].pFreeProc = UNUSED_TLS_KEY_PROC;
        bSetupEntries = false;
    }
}

static void SysInitTlsKeys(void)
{
    for (int i = 0; i < MAX_TLS_KEYS; i++)
        TlsKeys[i].pData = NULL;
}

static void SysCleanupTlsKeys(void)
{
    EnterCriticalSection(&csTLS);

    for (int i = 0; i < MAX_TLS_KEYS; i++) {
        if (TlsKeyEntries[i].pFreeProc != UNUSED_TLS_KEY_PROC &&
            TlsKeyEntries[i].pFreeProc != NULL)
            TlsKeyEntries[i].pFreeProc(TlsKeys[i].pData);

        TlsKeys[i].pData = NULL;
    }

    LeaveCriticalSection(&csTLS);
}

static int SysSetServerName(void)
{
    int iSize;
    char *pszSlash, *pszDot;
    char szPath[SYS_MAX_PATH] = APP_NAME_STR;

    GetModuleFileName(NULL, szPath, CountOf(szPath));
    if ((pszSlash = strrchr(szPath, '\\')) == NULL)
        pszSlash = szPath;
    else
        pszSlash++;
    if ((pszDot = strchr(pszSlash, '.')) == NULL)
        pszDot = pszSlash + strlen(pszSlash);

    iSize = Min(sizeof(szServerName) - 1, (int) (pszDot - pszSlash));
    Cpy2Sz(szServerName, pszSlash, iSize);

    return 0;
}

/*
 * We need to setup our own report hook in order to run debug builds
 * w/out being interrupted by CRT assertions. We log the assert message
 * and we continue to run.
 */
static int SysCrtReportHook(int iType, char *pszMsg, int *piRetVal)
{
    int iLogLevel, iRetCode;
    char const *pszType;

    switch (iType) {
    case _CRT_ERROR:
        iLogLevel = LOG_LEV_ERROR;
        pszType = "CRT Error";
        iRetCode = FALSE;
        break;
    case _CRT_ASSERT:
        iLogLevel = LOG_LEV_WARNING;
        pszType = "CRT Assert";
        *piRetVal = 0;
        iRetCode = TRUE;
        break;
    case _CRT_WARN:
        iLogLevel = LOG_LEV_WARNING;
        pszType = "CRT Warning";
        *piRetVal = 0;
        iRetCode = TRUE;
        break;
    default:
        return FALSE;
    }

    SysLogMessage(iLogLevel, "%s: %s\n", pszType, pszMsg);

    return iRetCode;
}

static void SysRunThreadExitHooks(SYS_THREAD ThreadID, int iMode)
{
    int i;

    for (i = iNumThExitHooks - 1; i >= 0; i--)
        (*ThExitHooks[i].pfHook)(ThExitHooks[i].pPrivate, ThreadID, iMode);
}

static int SysThreadSetup(SYS_THREAD ThreadID)
{
    SysInitTlsKeys();
    SysRunThreadExitHooks(ThreadID, SYS_THREAD_ATTACH);

    return 0;
}

static int SysThreadCleanup(SYS_THREAD ThreadID)
{
    SysRunThreadExitHooks(ThreadID, SYS_THREAD_DETACH);
    SysCleanupTlsKeys();

    return 0;
}

int SysInitLibrary(void)
{
    /* Setup timers */
    LARGE_INTEGER PerfCntFreq;
    LARGE_INTEGER PerfCntCurr;

    _tzset();

    QueryPerformanceFrequency(&PerfCntFreq);
    QueryPerformanceCounter(&PerfCntCurr);

    PCFreq = (SYS_INT64) PerfCntFreq.QuadPart;
    PCFreq /= 1000;
    PCSysStart = (SYS_INT64) PerfCntCurr.QuadPart;

    time(&tSysStart);

    iNumThExitHooks = 0;

    /* Set the server name */
    SysSetServerName();

    /* Setup sockets */
    WSADATA WSD;

    ZeroData(WSD);
    if (WSAStartup(SOCK_VERSION_REQUESTED, &WSD)) {
        ErrSetErrorCode(ERR_NETWORK);
        return ERR_NETWORK;
    }

    InitializeCriticalSection(&csTLS);
    SysInitTlsKeyEntries();

    InitializeCriticalSection(&csLog);
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, SysCrtReportHook);

    if ((hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, SysCrtReportHook);
        DeleteCriticalSection(&csLog);
        DeleteCriticalSection(&csTLS);
        ErrSetErrorCode(ERR_CREATEEVENT);
        return ERR_CREATEEVENT;
    }
    if (SysThreadSetup(SYS_INVALID_THREAD) < 0) {
        ErrorPush();
        CloseHandle(hShutdownEvent);
        DeleteCriticalSection(&csLog);
        DeleteCriticalSection(&csTLS);
        return ErrorPop();
    }
    SRand();

    return 0;
}

void SysCleanupLibrary(void)
{
    _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, SysCrtReportHook);
    SysThreadCleanup(SYS_INVALID_THREAD);
    DeleteCriticalSection(&csLog);
    DeleteCriticalSection(&csTLS);
    CloseHandle(hShutdownEvent);
    WSACleanup();
}

int SysAddThreadExitHook(void (*pfHook)(void *, SYS_THREAD, int), void *pPrivate)
{
    if (iNumThExitHooks >= SYS_MAX_TH_EXIT_HOOKS)
        return -1;
    ThExitHooks[iNumThExitHooks].pfHook = pfHook;
    ThExitHooks[iNumThExitHooks].pPrivate = pPrivate;
    iNumThExitHooks++;

    return 0;
}

int SysShutdownLibrary(int iMode)
{
    SetEvent(hShutdownEvent);

    return 0;
}

int SysSetupSocketBuffers(int *piSndBufSize, int *piRcvBufSize)
{
    if (piSndBufSize != NULL)
        iSndBufSize = *piSndBufSize;
    if (piRcvBufSize != NULL)
        iRcvBufSize = *piRcvBufSize;

    return 0;
}

static int SysSetSocketsOptions(SYS_SOCKET SockFD)
{
    /* Set socket buffer sizes */
    if (iSndBufSize > 0) {
        int iSize = iSndBufSize;

        setsockopt((int) SockFD, SOL_SOCKET, SO_SNDBUF, (char const *) &iSize,
               sizeof(iSize));
    }
    if (iRcvBufSize > 0) {
        int iSize = iRcvBufSize;

        setsockopt((int) SockFD, SOL_SOCKET, SO_RCVBUF, (char const *) &iSize,
               sizeof(iSize));
    }

    int iActivate = 1;

    if (setsockopt(SockFD, SOL_SOCKET, SO_REUSEADDR, (char const *) &iActivate,
               sizeof(iActivate)) != 0) {
        ErrSetErrorCode(ERR_SETSOCKOPT);
        return ERR_SETSOCKOPT;
    }
    /* Disable linger */
    struct linger Ling;

    ZeroData(Ling);
    Ling.l_onoff = 0;
    Ling.l_linger = 0;

    setsockopt(SockFD, SOL_SOCKET, SO_LINGER, (char const *) &Ling, sizeof(Ling));

    /* Set KEEPALIVE if supported */
    setsockopt(SockFD, SOL_SOCKET, SO_KEEPALIVE, (char const *) &iActivate,
           sizeof(iActivate));

    return 0;
}

SYS_SOCKET SysCreateSocket(int iAddressFamily, int iType, int iProtocol)
{
    SOCKET SockFD = WSASocket(iAddressFamily, iType, iProtocol, NULL, 0,
                  WSA_FLAG_OVERLAPPED);

    if (SockFD == INVALID_SOCKET) {
        ErrSetErrorCode(ERR_SOCKET_CREATE);
        return SYS_INVALID_SOCKET;
    }
    if (SysSetSocketsOptions((SYS_SOCKET) SockFD) < 0) {
        SysCloseSocket((SYS_SOCKET) SockFD);
        return SYS_INVALID_SOCKET;
    }

    return (SYS_SOCKET) SockFD;
}

int SysBlockSocket(SYS_SOCKET SockFD, int iBlocking)
{
    u_long IoctlLong = iBlocking <= 0;

    /*
     * Pending non-blocking operations should not be active, but we call
     * WSAEventSelect() just to be sure ...
     */
    WSAEventSelect(SockFD, NULL, 0);

    if (ioctlsocket(SockFD, FIONBIO, &IoctlLong) == SOCKET_ERROR) {
        ErrSetErrorCode(ERR_NETWORK);
        return ERR_NETWORK;
    }

    return 0;
}

void SysCloseSocket(SYS_SOCKET SockFD)
{
    if (SockFD != SYS_INVALID_SOCKET)
        closesocket(SockFD);
}

int SysShutdownSocket(SYS_SOCKET SockFD, int iHow)
{
    if (shutdown(SockFD, iHow)) {
        ErrSetErrorCode(ERR_SOCKET_SHUTDOWN);
        return ERR_SOCKET_SHUTDOWN;
    }

    return 0;
}

int SysBindSocket(SYS_SOCKET SockFD, const SYS_INET_ADDR *SockName)
{
    if (bind(SockFD, (const struct sockaddr *) SockName->Addr,
         SockName->iSize) == SOCKET_ERROR) {
        ErrSetErrorCode(ERR_SOCKET_BIND);
        return ERR_SOCKET_BIND;
    }

    return 0;
}

void SysListenSocket(SYS_SOCKET SockFD, int iConnections)
{
    listen(SockFD, iConnections);
}

static int SysRecvLL(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize)
{
    DWORD dwRtxBytes = 0;
    DWORD dwRtxFlags = 0;
    WSABUF WSABuff;

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = pszBuffer;

    return WSARecv(SockFD, &WSABuff, 1, &dwRtxBytes, &dwRtxFlags,
               NULL, NULL) == 0 ? (int) dwRtxBytes: -WSAGetLastError();
}

static int SysSendLL(SYS_SOCKET SockFD, char const *pszBuffer, int iBufferSize)
{
    DWORD dwRtxBytes = 0;
    WSABUF WSABuff;

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = (char *) pszBuffer;

    return WSASend(SockFD, &WSABuff, 1, &dwRtxBytes, 0,
               NULL, NULL) == 0 ? (int) dwRtxBytes: -WSAGetLastError();
}

int SysRecvData(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{
    HANDLE hReadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hReadEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return ERR_CREATEEVENT;
    }

    int iRecvBytes = 0;
    HANDLE hWaitEvents[2] = { hReadEvent, hShutdownEvent };

    for (;;) {
        WSAEventSelect(SockFD, (WSAEVENT) hReadEvent, FD_READ | FD_CLOSE);

        DWORD dwWaitResult = WSAWaitForMultipleEvents(2, hWaitEvents, FALSE,
                                  iTimeout, TRUE);

        WSAEventSelect(SockFD, NULL, 0);

        if (dwWaitResult == (WSA_WAIT_EVENT_0 + 1)) {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            return ERR_SERVER_SHUTDOWN;
        } else if (dwWaitResult != WSA_WAIT_EVENT_0) {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }

        if ((iRecvBytes = SysRecvLL(SockFD, pszBuffer, iBufferSize)) >= 0)
            break;

        int iErrorCode = -iRecvBytes;

        if (iErrorCode != WSAEWOULDBLOCK) {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return ERR_NETWORK;
        }
        /* You should never get here if Win32 API worked fine */
        HANDLE_SOCKS_SUCKS();
    }
    CloseHandle(hReadEvent);

    return iRecvBytes;
}

int SysRecv(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{
    int iRtxBytes = 0;

    while (iRtxBytes < iBufferSize) {
        int iRtxCurrent = SysRecvData(SockFD, pszBuffer + iRtxBytes,
                          iBufferSize - iRtxBytes, iTimeout);

        if (iRtxCurrent <= 0)
            return iRtxBytes;
        iRtxBytes += iRtxCurrent;
    }

    return iRtxBytes;
}

int SysRecvDataFrom(SYS_SOCKET SockFD, SYS_INET_ADDR *pFrom, char *pszBuffer,
            int iBufferSize, int iTimeout)
{
    HANDLE hReadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hReadEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return ERR_CREATEEVENT;
    }

    DWORD dwRtxBytes = 0;
    WSABUF WSABuff;
    HANDLE hWaitEvents[2] = { hReadEvent, hShutdownEvent };

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = pszBuffer;

    for (;;) {
        WSAEventSelect(SockFD, (WSAEVENT) hReadEvent, FD_READ | FD_CLOSE);

        DWORD dwWaitResult = WSAWaitForMultipleEvents(2, hWaitEvents, FALSE,
                                  iTimeout, TRUE);

        WSAEventSelect(SockFD, NULL, 0);

        if (dwWaitResult == (WSA_WAIT_EVENT_0 + 1)) {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            return ERR_SERVER_SHUTDOWN;
        } else if (dwWaitResult != WSA_WAIT_EVENT_0) {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }

        INT FromLen = (INT) sizeof(pFrom->Addr);
        DWORD dwRtxFlags = 0;

        if (WSARecvFrom(SockFD, &WSABuff, 1, &dwRtxBytes, &dwRtxFlags,
                (struct sockaddr *) pFrom->Addr, &FromLen,
                NULL, NULL) == 0) {
            pFrom->iSize = (int) FromLen;
            break;
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return ERR_NETWORK;
        }
        /* You should never get here if Win32 API worked fine */
        HANDLE_SOCKS_SUCKS();
    }
    CloseHandle(hReadEvent);

    return (int) dwRtxBytes;
}

int SysSendData(SYS_SOCKET SockFD, char const *pszBuffer, int iBufferSize, int iTimeout)
{
    HANDLE hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hWriteEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return ERR_CREATEEVENT;
    }

    int iSendBytes = 0;
    HANDLE hWaitEvents[2] = { hWriteEvent, hShutdownEvent };

    for (;;) {
        WSAEventSelect(SockFD, (WSAEVENT) hWriteEvent, FD_WRITE | FD_CLOSE);

        DWORD dwWaitResult = WSAWaitForMultipleEvents(2, hWaitEvents, FALSE,
                                  iTimeout, TRUE);

        WSAEventSelect(SockFD, NULL, 0);

        if (dwWaitResult == (WSA_WAIT_EVENT_0 + 1)) {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            return ERR_SERVER_SHUTDOWN;
        } else if (dwWaitResult != WSA_WAIT_EVENT_0) {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }

        if ((iSendBytes = SysSendLL(SockFD, pszBuffer, iBufferSize)) >= 0)
            break;

        int iErrorCode = -iSendBytes;

        if (iErrorCode != WSAEWOULDBLOCK) {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return ERR_NETWORK;
        }
        /* You should never get here if Win32 API worked fine */
        HANDLE_SOCKS_SUCKS();
    }
    CloseHandle(hWriteEvent);

    return iSendBytes;
}

int SysSend(SYS_SOCKET SockFD, char const *pszBuffer, int iBufferSize, int iTimeout)
{
    int iRtxBytes = 0;

    while (iRtxBytes < iBufferSize) {
        int iRtxCurrent = SysSendData(SockFD, pszBuffer + iRtxBytes,
                          iBufferSize - iRtxBytes, iTimeout);

        if (iRtxCurrent <= 0)
            return iRtxBytes;
        iRtxBytes += iRtxCurrent;
    }

    return iRtxBytes;
}

int SysSendDataTo(SYS_SOCKET SockFD, const SYS_INET_ADDR *pTo,
          char const *pszBuffer, int iBufferSize, int iTimeout)
{
    HANDLE hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hWriteEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return ERR_CREATEEVENT;
    }

    DWORD dwRtxBytes = 0;
    WSABUF WSABuff;
    HANDLE hWaitEvents[2] = { hWriteEvent, hShutdownEvent };

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = (char *) pszBuffer;

    for (;;) {
        WSAEventSelect(SockFD, (WSAEVENT) hWriteEvent, FD_WRITE | FD_CLOSE);

        DWORD dwWaitResult = WSAWaitForMultipleEvents(2, hWaitEvents, FALSE,
                                  iTimeout, TRUE);

        WSAEventSelect(SockFD, NULL, 0);

        if (dwWaitResult == (WSA_WAIT_EVENT_0 + 1)) {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            return ERR_SERVER_SHUTDOWN;
        } else if (dwWaitResult != WSA_WAIT_EVENT_0) {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }

        DWORD dwRtxFlags = 0;

        if (WSASendTo(SockFD, &WSABuff, 1, &dwRtxBytes, dwRtxFlags,
                  (const struct sockaddr *) pTo->Addr, pTo->iSize,
                  NULL, NULL) == 0)
            break;

        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return ERR_NETWORK;
        }
        /* You should never get here if Win32 API worked fine */
        HANDLE_SOCKS_SUCKS();
    }
    CloseHandle(hWriteEvent);

    return (int) dwRtxBytes;
}

int SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR *pSockName, int iTimeout)
{
    HANDLE hConnectEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hConnectEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return ERR_CREATEEVENT;
    }

    WSAEventSelect(SockFD, (WSAEVENT) hConnectEvent, FD_CONNECT);

    int iConnectResult = WSAConnect(SockFD, (const struct sockaddr *) pSockName->Addr,
                    pSockName->iSize, NULL, NULL, NULL, NULL);
    int iConnectError = WSAGetLastError();

    if (iConnectResult != 0 && iConnectError == WSAEWOULDBLOCK) {
        HANDLE hWaitEvents[2] = { hConnectEvent, hShutdownEvent };
        DWORD dwWaitResult = WSAWaitForMultipleEvents(2, hWaitEvents, FALSE,
                                  iTimeout, TRUE);

        if (dwWaitResult == WSA_WAIT_EVENT_0) {
            WSANETWORKEVENTS NetEvents;

            if (WSAEnumNetworkEvents(SockFD, hConnectEvent, &NetEvents) != 0 ||
                NetEvents.iErrorCode[FD_CONNECT_BIT] != 0) {
                ErrSetErrorCode(ERR_CONNECT);

                iConnectResult = ERR_CONNECT;
            } else
                iConnectResult = 0;
        } else if (dwWaitResult == (WSA_WAIT_EVENT_0 + 1)) {
            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            iConnectResult = ERR_SERVER_SHUTDOWN;
        } else {
            ErrSetErrorCode(ERR_TIMEOUT);
            iConnectResult = ERR_TIMEOUT;
        }
    }
    WSAEventSelect(SockFD, NULL, 0);
    CloseHandle(hConnectEvent);

    return iConnectResult;
}

SYS_SOCKET SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR *pSockName, int iTimeout)
{
    HANDLE hAcceptEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hAcceptEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return SYS_INVALID_SOCKET;
    }

    WSAEventSelect(SockFD, (WSAEVENT) hAcceptEvent, FD_ACCEPT);

    INT iNameLen = (INT) sizeof(pSockName->Addr);
    SOCKET SockFDAccept = WSAAccept(SockFD, (struct sockaddr *) pSockName->Addr,
                    &iNameLen, NULL, 0);

    int iConnectError = WSAGetLastError();

    if (SockFDAccept == INVALID_SOCKET && iConnectError == WSAEWOULDBLOCK) {
        HANDLE hWaitEvents[2] = { hAcceptEvent, hShutdownEvent };
        DWORD dwWaitResult = WSAWaitForMultipleEvents(2, hWaitEvents, FALSE,
                                  iTimeout, TRUE);

        if (dwWaitResult == WSA_WAIT_EVENT_0) {
            iNameLen = (INT) sizeof(pSockName->Addr);
            SockFDAccept = WSAAccept(SockFD, (struct sockaddr *) pSockName->Addr,
                         &iNameLen, NULL, 0);
        } else if (dwWaitResult == (WSA_WAIT_EVENT_0 + 1))
            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
        else
            ErrSetErrorCode(ERR_TIMEOUT);
    }
    WSAEventSelect(SockFD, NULL, 0);
    if (SockFDAccept != INVALID_SOCKET)
        WSAEventSelect(SockFDAccept, NULL, 0);
    CloseHandle(hAcceptEvent);

    if (SockFDAccept != INVALID_SOCKET) {
        pSockName->iSize = (int) iNameLen;
        if (SysBlockSocket(SockFDAccept, 0) < 0 ||
            SysSetSocketsOptions(SockFDAccept) < 0) {
            SysCloseSocket(SockFDAccept);
            return SYS_INVALID_SOCKET;
        }
    }

    return SockFDAccept;
}

int SysSelect(int iMaxFD, SYS_fd_set *pReadFDs, SYS_fd_set *pWriteFDs, SYS_fd_set *pExcptFDs,
          int iTimeout)
{
    struct timeval TV;

    ZeroData(TV);
    if (iTimeout != SYS_INFINITE_TIMEOUT) {
        TV.tv_sec = iTimeout / 1000;
        TV.tv_usec = (iTimeout % 1000) * 1000;
    }

    int iSelectResult = select(iMaxFD + 1, pReadFDs, pWriteFDs, pExcptFDs,
                   iTimeout != SYS_INFINITE_TIMEOUT ? &TV: NULL);

    if (iSelectResult < 0) {
        ErrSetErrorCode(ERR_SELECT);
        return ERR_SELECT;
    }
    if (iSelectResult == 0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return iSelectResult;
}

int SysSendFile(SYS_SOCKET SockFD, char const *pszFileName, SYS_OFF_T llBaseOffset,
        SYS_OFF_T llEndOffset, int iTimeout)
{
    /* Open the source file */
    HANDLE hFile = CreateFile(pszFileName, GENERIC_READ, FILE_SHARE_READ,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        ErrSetErrorCode(ERR_FILE_OPEN, pszFileName); /* [i_a] */
        return ERR_FILE_OPEN;
    }
    /* Create file mapping and the file */
    DWORD dwFileSizeHi = 0;
    DWORD dwFileSizeLo = GetFileSize(hFile, &dwFileSizeHi);
    HANDLE hFileMap = CreateFileMapping(hFile, NULL, PAGE_READONLY,
                        dwFileSizeHi, dwFileSizeLo, NULL);

    if (hFileMap == NULL) {
        CloseHandle(hFile);

        ErrSetErrorCode(ERR_CREATEFILEMAPPING);
        return ERR_CREATEFILEMAPPING;
    }

    void *pAddress = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 0);

    if (pAddress == NULL) {
        CloseHandle(hFileMap);
        CloseHandle(hFile);

        ErrSetErrorCode(ERR_MAPVIEWOFFILE);
        return ERR_MAPVIEWOFFILE;
    }

    int iSndBuffSize = (iTimeout != SYS_INFINITE_TIMEOUT) ?
        MIN_TCP_SEND_SIZE: MAX_TCP_SEND_SIZE;
    SYS_UINT64 ullFileSize = (((SYS_UINT64) dwFileSizeHi) << 32) | (SYS_UINT64) dwFileSizeLo;
    SYS_UINT64 ullEndOffset = (llEndOffset != -1) ? (SYS_UINT64) llEndOffset: ullFileSize;
    SYS_UINT64 ullCurrOffset = (SYS_UINT64) llBaseOffset;
    char *pszBuffer = (char *) pAddress + llBaseOffset;
    SYS_INT64 tStart;

    while (ullCurrOffset < ullEndOffset) {
        int iCurrSend = (int) Min(iSndBuffSize, ullEndOffset - ullCurrOffset);

        tStart = SysMsTime();
        if ((iCurrSend = SysSendData(SockFD, pszBuffer, iCurrSend, iTimeout)) < 0) {
            ErrorPush();
            UnmapViewOfFile(pAddress);
            CloseHandle(hFileMap);
            CloseHandle(hFile);
            return ErrorPop();
        }
        if (iSndBuffSize < MAX_TCP_SEND_SIZE &&
            ((SysMsTime() - tStart) * K_IO_TIME_RATIO) < (SYS_INT64) iTimeout)
            iSndBuffSize = Min(iSndBuffSize * 2, MAX_TCP_SEND_SIZE);
        pszBuffer += iCurrSend;
        ullCurrOffset += (SYS_UINT64) iCurrSend;
    }
    UnmapViewOfFile(pAddress);
    CloseHandle(hFileMap);
    CloseHandle(hFile);

    return 0;
}

SYS_SEMAPHORE SysCreateSemaphore(int iInitCount, int iMaxCount)
{
    HANDLE hSemaphore = CreateSemaphore(NULL, iInitCount, iMaxCount, NULL);

    if (hSemaphore == NULL) {
        ErrSetErrorCode(ERR_CREATESEMAPHORE);
        return SYS_INVALID_SEMAPHORE;
    }

    return (SYS_SEMAPHORE) hSemaphore;
}

int SysCloseSemaphore(SYS_SEMAPHORE hSemaphore)
{
    if (hSemaphore != SYS_INVALID_SEMAPHORE) {
        if (!CloseHandle((HANDLE) hSemaphore)) {
            ErrSetErrorCode(ERR_CLOSEHANDLE);
            return ERR_CLOSEHANDLE;
        }
    }

    return 0;
}

int SysWaitSemaphore(SYS_SEMAPHORE hSemaphore, int iTimeout)
{
    if (WaitForSingleObject((HANDLE) hSemaphore,
                iTimeout) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

int SysReleaseSemaphore(SYS_SEMAPHORE hSemaphore, int iCount)
{
    if (!ReleaseSemaphore((HANDLE) hSemaphore, (LONG) iCount, NULL)) {
        ErrSetErrorCode(ERR_RELEASESEMAPHORE);
        return ERR_RELEASESEMAPHORE;
    }

    return 0;
}

int SysTryWaitSemaphore(SYS_SEMAPHORE hSemaphore)
{
    if (WaitForSingleObject((HANDLE) hSemaphore, 0) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

SYS_MUTEX SysCreateMutex(void)
{
    HANDLE hMutex = CreateMutex(NULL, FALSE, NULL);

    if (hMutex == NULL) {
        ErrSetErrorCode(ERR_CREATEMUTEX);
        return SYS_INVALID_MUTEX;
    }

    return (SYS_MUTEX) hMutex;
}

int SysCloseMutex(SYS_MUTEX hMutex)
{
    if (hMutex != SYS_INVALID_MUTEX) {
        if (!CloseHandle((HANDLE) hMutex)) {
            ErrSetErrorCode(ERR_CLOSEHANDLE);
            return ERR_CLOSEHANDLE;
        }
    }

    return 0;
}

int SysLockMutex(SYS_MUTEX hMutex, int iTimeout)
{
    if (WaitForSingleObject((HANDLE) hMutex,
                iTimeout) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

int SysUnlockMutex(SYS_MUTEX hMutex)
{
    ReleaseMutex((HANDLE) hMutex);

    return 0;
}

int SysTryLockMutex(SYS_MUTEX hMutex)
{
    if (WaitForSingleObject((HANDLE) hMutex, 0) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

SYS_EVENT SysCreateEvent(int iManualReset)
{
    HANDLE hEvent = CreateEvent(NULL, iManualReset, FALSE, NULL);

    if (hEvent == NULL) {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return SYS_INVALID_EVENT;
    }

    return (SYS_EVENT) hEvent;
}

int SysCloseEvent(SYS_EVENT hEvent)
{
    if (hEvent != SYS_INVALID_EVENT) {
        if (!CloseHandle((HANDLE) hEvent)) {
            ErrSetErrorCode(ERR_CLOSEHANDLE);
            return ERR_CLOSEHANDLE;
        }
    }

    return 0;
}

int SysWaitEvent(SYS_EVENT hEvent, int iTimeout)
{
    if (WaitForSingleObject((HANDLE) hEvent,
                iTimeout) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

int SysSetEvent(SYS_EVENT hEvent)
{
    SetEvent((HANDLE) hEvent);

    return 0;
}

int SysResetEvent(SYS_EVENT hEvent)
{
    ResetEvent((HANDLE) hEvent);

    return 0;
}

int SysTryWaitEvent(SYS_EVENT hEvent)
{
    if (WaitForSingleObject((HANDLE) hEvent, 0) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

/*
 * On Windows, Events and PEvents maps to the same implementation, since
 * we use WaitForMultipleObjects() for general waiting.
 */
SYS_PEVENT SysCreatePEvent(int iManualReset)
{
    return (SYS_PEVENT) SysCreateEvent(iManualReset);
}

int SysClosePEvent(SYS_PEVENT hPEvent)
{
    return SysCloseEvent((SYS_EVENT) hPEvent);
}

int SysWaitPEvent(SYS_PEVENT hPEvent, int iTimeout)
{
    return SysWaitEvent((SYS_EVENT) hPEvent, iTimeout);
}

int SysSetPEvent(SYS_PEVENT hPEvent)
{
    return SysSetEvent((SYS_EVENT) hPEvent);
}

int SysResetPEvent(SYS_PEVENT hPEvent)
{
    return SysResetEvent((SYS_EVENT) hPEvent);
}

int SysTryWaitPEvent(SYS_PEVENT hPEvent)
{
    return SysTryWaitEvent((SYS_EVENT) hPEvent);
}

static unsigned int SysThreadRunner(void *pRunData)
{
    ThreadRunner *pTR = (ThreadRunner *) pRunData;

    if (SysThreadSetup((SYS_THREAD) GetCurrentThread()) < 0) {
        SysFree(pTR);
        return ErrGetErrorCode();
    }

    unsigned int uResultCode = (*pTR->pThreadProc)(pTR->pThreadData);

    SysThreadCleanup((SYS_THREAD) GetCurrentThread());
    SysFree(pTR);

    return uResultCode;
}

SYS_THREAD SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData)
{
    /* Alloc thread runner data */
    ThreadRunner *pTR = (ThreadRunner *) SysAlloc(sizeof(ThreadRunner));

    if (pTR == NULL)
        return SYS_INVALID_THREAD;

    pTR->pThreadProc = pThreadProc;
    pTR->pThreadData = pThreadData;

    /* Create the thread */
    unsigned int uThreadId = 0;
    unsigned long ulThread = _beginthreadex(NULL, 0,
                        (unsigned (__stdcall *) (void *)) SysThreadRunner,
                        pTR, 0, &uThreadId);

    if (ulThread == 0) {
        SysFree(pTR);
        ErrSetErrorCode(ERR_BEGINTHREADEX);
        return SYS_INVALID_THREAD;
    }

    return (SYS_THREAD) ulThread;
}

void SysCloseThread(SYS_THREAD ThreadID, int iForce)
{
    if (ThreadID != SYS_INVALID_THREAD) {
        if (iForce)
            TerminateThread((HANDLE) ThreadID, (DWORD) -1);
        CloseHandle((HANDLE) ThreadID);
    }
}

int SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority)
{
    BOOL bSetResult = FALSE;

    switch (iPriority) {
    case SYS_PRIORITY_NORMAL:
        bSetResult = SetThreadPriority((HANDLE) ThreadID, THREAD_PRIORITY_NORMAL);
        break;

    case SYS_PRIORITY_LOWER:
        bSetResult = SetThreadPriority((HANDLE) ThreadID, THREAD_PRIORITY_BELOW_NORMAL);
        break;

    case SYS_PRIORITY_HIGHER:
        bSetResult = SetThreadPriority((HANDLE) ThreadID, THREAD_PRIORITY_ABOVE_NORMAL);
        break;
    }
    if (!bSetResult) {
        ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
        return ERR_SET_THREAD_PRIORITY;
    }

    return 0;
}

int SysWaitThread(SYS_THREAD ThreadID, int iTimeout)
{
    if (WaitForSingleObject((HANDLE) ThreadID,
                iTimeout) != WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    return 0;
}

unsigned long SysGetCurrentThreadId(void)
{
    return (unsigned long) GetCurrentThreadId();
}

int SysExec(char const *pszCommand, char const *const *pszArgs, int iWaitTimeout,
        int iPriority, int *piExitStatus)
{
    int i, iCommandLength = strlen(pszCommand) + 4;

    for (i = 1; pszArgs[i] != NULL; i++)
        iCommandLength += strlen(pszArgs[i]) + 4;

    char *pszCmdLine = (char *) SysAlloc(iCommandLength + 1);

    if (pszCmdLine == NULL)
        return ErrGetErrorCode();

    strcpy(pszCmdLine, pszCommand);
    for (i = 1; pszArgs[i] != NULL; i++)
        sprintf(StrEnd(pszCmdLine), " \"%s\"", pszArgs[i]);

    PROCESS_INFORMATION PI;
    STARTUPINFO SI;

    ZeroData(PI);
    ZeroData(SI);
    SI.cb = sizeof(STARTUPINFO);

    BOOL bProcessCreated = CreateProcess(NULL, pszCmdLine, NULL, NULL, FALSE,
                         CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS,
                         NULL, NULL, &SI, &PI);

    SysFree(pszCmdLine);
    if (!bProcessCreated) {
        ErrSetErrorCode(ERR_PROCESS_EXECUTE);
        return ERR_PROCESS_EXECUTE;
    }

    SysSetThreadPriority((SYS_THREAD) PI.hThread, iPriority);

    if (iWaitTimeout > 0 || iWaitTimeout == SYS_INFINITE_TIMEOUT) {
        if (WaitForSingleObject(PI.hProcess, iWaitTimeout) != WAIT_OBJECT_0) {
            CloseHandle(PI.hThread);
            CloseHandle(PI.hProcess);

            ErrSetErrorCode(ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }

        if (piExitStatus != NULL) {
            DWORD dwExitCode = 0;

            if (!GetExitCodeProcess(PI.hProcess, &dwExitCode))
                *piExitStatus = -1;
            else
                *piExitStatus = (int) dwExitCode;
        }
    } else if (piExitStatus != NULL) {
        *piExitStatus = -1;
    }
    CloseHandle(PI.hThread);
    CloseHandle(PI.hProcess);

    return 0;
}

static BOOL WINAPI SysBreakHandlerRoutine(DWORD dwCtrlType)
{
    BOOL bReturnValue = FALSE;

    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (pSysBreakHandler != NULL)
            (*pSysBreakHandler)(), bReturnValue = TRUE;
        break;
    }

    return bReturnValue;
}

void SysSetBreakHandler(void (*pBreakHandler) (void))
{
    if (pSysBreakHandler == NULL)
        SetConsoleCtrlHandler(SysBreakHandlerRoutine,
                      (pBreakHandler != NULL) ? TRUE: FALSE);
    pSysBreakHandler = pBreakHandler;
}

unsigned long SysGetCurrentProcessId(void)
{
    return GetCurrentProcessId();
}

int SysCreateTlsKey(SYS_TLSKEY &TlsKey, void (*pFreeProc) (void *))
{
    EnterCriticalSection(&csTLS);

    for (int i = 0; i < MAX_TLS_KEYS; i++) {
        if (TlsKeyEntries[i].pFreeProc == UNUSED_TLS_KEY_PROC) {
            TlsKeyEntries[i].pFreeProc = pFreeProc;
            LeaveCriticalSection(&csTLS);
            TlsKey = (SYS_TLSKEY) i;

            return 0;
        }
    }

    LeaveCriticalSection(&csTLS);

    ErrSetErrorCode(ERR_NOMORE_TLSKEYS);
    return ERR_NOMORE_TLSKEYS;
}

int SysDeleteTlsKey(SYS_TLSKEY &TlsKey)
{
    int iKey = (int) TlsKey;

    EnterCriticalSection(&csTLS);

    if (iKey < 0 || iKey >= MAX_TLS_KEYS ||
        TlsKeyEntries[iKey].pFreeProc == UNUSED_TLS_KEY_PROC) {
        LeaveCriticalSection(&csTLS);

        ErrSetErrorCode(ERR_INVALID_TLSKEY);
        return ERR_INVALID_TLSKEY;
    }
    TlsKeyEntries[iKey].pFreeProc = UNUSED_TLS_KEY_PROC;

    LeaveCriticalSection(&csTLS);

    TlsKey = (SYS_TLSKEY) - 1;

    return 0;
}

int SysSetTlsKeyData(SYS_TLSKEY &TlsKey, void *pData)
{
    int iKey = (int) TlsKey;

    if (iKey < 0 || iKey >= MAX_TLS_KEYS ||
        TlsKeyEntries[iKey].pFreeProc == UNUSED_TLS_KEY_PROC) {
        ErrSetErrorCode(ERR_INVALID_TLSKEY);
        return ERR_INVALID_TLSKEY;
    }
    TlsKeys[iKey].pData = pData;

    return 0;
}

void *SysGetTlsKeyData(SYS_TLSKEY &TlsKey)
{
    int iKey = (int) TlsKey;

    if (iKey < 0 || iKey >= MAX_TLS_KEYS ||
        TlsKeyEntries[iKey].pFreeProc == UNUSED_TLS_KEY_PROC) {
        ErrSetErrorCode(ERR_INVALID_TLSKEY);
        return NULL;
    }

    return TlsKeys[iKey].pData;
}

void SysThreadOnce(SYS_THREAD_ONCE *pThrOnce, void (*pOnceProc) (void))
{
    if (InterlockedExchange(&pThrOnce->lOnce, 1) == 0) {
        (*pOnceProc)();
        pThrOnce->lDone++;
    }
    while (!pThrOnce->lDone)
        Sleep(0);
}

void *SysAllocNZ(size_t uSize)
{
    void *pData = malloc(uSize);

    if (pData == NULL)
        ErrSetErrorCode(ERR_MEMORY);

    return pData;
}

void *SysAlloc(size_t uSize)
{
    void *pData = SysAllocNZ(uSize);

    if (pData != NULL)
        memset(pData, 0, uSize);

    return pData;
}

void SysFree(void *pData)
{
    free(pData);
}

void *SysRealloc(void *pData, size_t uSize)
{
    void *pNewData = realloc(pData, uSize);

    if (pNewData == NULL)
        ErrSetErrorCode(ERR_MEMORY);

    return pNewData;
}

int SysLockFile(char const *pszFileName, char const *pszLockExt)
{
    char szLockFile[SYS_MAX_PATH];

    SysSNPrintf(szLockFile, sizeof(szLockFile) - 1, "%s%s", pszFileName, pszLockExt);

    /* Try to create lock file */
    HANDLE hFile = CreateFile(szLockFile, GENERIC_READ | GENERIC_WRITE,
                  0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            ErrSetErrorCode(ERR_LOCKED);
            return ERR_LOCKED;
        }

        ErrSetErrorCode(ERR_FILE_CREATE, szLockFile); /* [i_a] */
        return ERR_FILE_CREATE;
    }

    DWORD dwWritten = 0;
    char szLock[128];

    sprintf(szLock, "%lu", (unsigned long) GetCurrentThreadId());
    if (!WriteFile(hFile, szLock, (int)strlen(szLock) + 1, &dwWritten, NULL)) {
        CloseHandle(hFile);
        ErrSetErrorCode(ERR_FILE_WRITE, szLockFile); /* [i_a] */
        return ERR_FILE_WRITE;
    }
    CloseHandle(hFile);

    return 0;
}

int SysUnlockFile(char const *pszFileName, char const *pszLockExt)
{
    char szLockFile[SYS_MAX_PATH] = "";

    SysSNPrintf(szLockFile, sizeof(szLockFile) - 1, "%s%s", pszFileName, pszLockExt);
    if (_unlink(szLockFile) != 0) {
        ErrSetErrorCode(ERR_NOT_LOCKED);
        return ERR_NOT_LOCKED;
    }

    return 0;
}

SYS_HANDLE SysOpenModule(char const *pszFilePath)
{
    HMODULE hModule = LoadLibrary(pszFilePath);

    if (hModule == NULL) {
        ErrSetErrorCode(ERR_LOADMODULE, pszFilePath);
        return SYS_INVALID_HANDLE;
    }

    return (SYS_HANDLE) hModule;
}

int SysCloseModule(SYS_HANDLE hModule)
{
    if (hModule != SYS_INVALID_HANDLE)
        FreeLibrary((HMODULE) hModule);

    return 0;
}

void *SysGetSymbol(SYS_HANDLE hModule, char const *pszSymbol)
{
    void *pSymbol = (void *) GetProcAddress((HMODULE) hModule, pszSymbol);

    if (pSymbol == NULL) {
        ErrSetErrorCode(ERR_LOADMODULESYMBOL, pszSymbol);
        return NULL;
    }

    return pSymbol;
}

int SysEventLogV(int iLogLevel, char const *pszFormat, va_list Args)
{
    HANDLE hEventSource = RegisterEventSource(NULL, szServerName);

    if (hEventSource == NULL) {
        ErrSetErrorCode(ERR_REGISTER_EVENT_SOURCE);
        return ERR_REGISTER_EVENT_SOURCE;
    }

    char *pszStrings[2];
    char szBuffer[2048] = "";

    _vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);
    pszStrings[0] = szBuffer;

    ReportEvent(hEventSource,
            iLogLevel == LOG_LEV_ERROR ? EVENTLOG_ERROR_TYPE:
            iLogLevel == LOG_LEV_WARNING ? EVENTLOG_WARNING_TYPE:
            EVENTLOG_INFORMATION_TYPE, 0, 0, NULL, 1, 0, (char const **) pszStrings, NULL);

    DeregisterEventSource(hEventSource);

    return 0;
}

int SysEventLog(int iLogLevel, char const *pszFormat, ...)
{
    int iError;
    va_list Args;

    va_start(Args, pszFormat);
    iError = SysEventLogV(iLogLevel, pszFormat, Args);
    va_end(Args);

    return iError;
}

int SysLogMessage(int iLogLevel, char const *pszFormat, ...)
{
    va_list Args;
    extern bool bServerDebug;

    va_start(Args, pszFormat);
    EnterCriticalSection(&csLog);
    if (bServerDebug) {
        /* Debug implementation */

        vprintf(pszFormat, Args);
    } else {
        switch (iLogLevel) {
        case LOG_LEV_WARNING:
        case LOG_LEV_ERROR:
            SysEventLogV(iLogLevel, pszFormat, Args);
            break;
        }
    }
    LeaveCriticalSection(&csLog);
    va_end(Args);

    return 0;
}

int SysSleep(int iTimeout)
{
    return SysMsSleep(iTimeout * 1000);
}

int SysMsSleep(int iMsTimeout)
{
    if (WaitForSingleObject(hShutdownEvent,
                iMsTimeout) == WAIT_OBJECT_0) {
        ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
        return ERR_SERVER_SHUTDOWN;
    }

    return 0;
}

static void SysTimetToFileTime(time_t tTime, LPFILETIME pFT)
{
    LONGLONG llTime = Int32x32To64(tTime, 10000000) + 116444736000000000;

    pFT->dwLowDateTime = (DWORD) llTime;
    pFT->dwHighDateTime = (DWORD) (llTime >> 32);
}

static time_t SysFileTimeToTimet(LPFILETIME pFT)
{
    LONGLONG llTime = ((LONGLONG) pFT->dwLowDateTime) |
        (((LONGLONG) pFT->dwHighDateTime) << 32);

    return (time_t) ((llTime - 116444736000000000) / 10000000);
}

SYS_INT64 SysMsTime(void)
{
    SYS_INT64 MsTicks;
    LARGE_INTEGER PerfCntCurr;

    QueryPerformanceCounter(&PerfCntCurr);
    MsTicks = (SYS_INT64) PerfCntCurr.QuadPart;
    MsTicks -= PCSysStart;
    MsTicks /= PCFreq;
    MsTicks += (SYS_INT64) tSysStart * 1000;

    return MsTicks;
}

int SysExistFile(char const *pszFilePath)
{
    DWORD dwAttr = GetFileAttributes(pszFilePath);

    if (dwAttr == (DWORD) - 1)
        return 0;

    return (dwAttr & FILE_ATTRIBUTE_DIRECTORY) ? 0: 1;
}

int SysExistDir(char const *pszDirPath)
{
    DWORD dwAttr = GetFileAttributes(pszDirPath);

    if (dwAttr == (DWORD) - 1)
        return 0;

    return (dwAttr & FILE_ATTRIBUTE_DIRECTORY) ? 1: 0;
}

SYS_HANDLE SysFirstFile(char const *pszPath, char *pszFileName, int iSize)
{
    char szMatch[SYS_MAX_PATH];

    StrNCpy(szMatch, pszPath, sizeof(szMatch) - 2);
    AppendSlash(szMatch);
    strcat(szMatch, "*");

    WIN32_FIND_DATA WFD;
    HANDLE hFind = FindFirstFile(szMatch, &WFD);

    if (hFind == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
        ErrSetErrorCode(ERR_OPENDIR, szMatch); /* [i_a] - bugfix for spurious error, e.g. when dir is non-existent, then no error would be reported */
        return SYS_INVALID_HANDLE;
    }

    FileFindData *pFFD = (FileFindData *) SysAlloc(sizeof(FileFindData));

    if (pFFD == NULL) {
        FindClose(hFind);
        return SYS_INVALID_HANDLE;
    }

    StrNCpy(pFFD->szFindPath, pszPath, sizeof(pFFD->szFindPath) - 1);
    AppendSlash(pFFD->szFindPath);

    pFFD->hFind = hFind;
    pFFD->WFD = WFD;

    StrNCpy(pszFileName, pFFD->WFD.cFileName, iSize);

    return (SYS_HANDLE) pFFD;
}

int SysIsDirectory(SYS_HANDLE hFind)
{
    FileFindData *pFFD = (FileFindData *) hFind;

    return (pFFD->WFD.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1: 0;
}

SYS_OFF_T SysGetSize(SYS_HANDLE hFind)
{
    FileFindData *pFFD = (FileFindData *) hFind;

    return (SYS_OFF_T) pFFD->WFD.nFileSizeLow | (((SYS_OFF_T) pFFD->WFD.nFileSizeHigh) << 32);
}

int SysNextFile(SYS_HANDLE hFind, char *pszFileName, int iSize)
{
    FileFindData *pFFD = (FileFindData *) hFind;

    if (!FindNextFile(pFFD->hFind, &pFFD->WFD))
        return 0;
    StrNCpy(pszFileName, pFFD->WFD.cFileName, iSize);

    return 1;
}

void SysFindClose(SYS_HANDLE hFind)
{
    FileFindData *pFFD = (FileFindData *) hFind;

    if (pFFD != NULL) {
        FindClose(pFFD->hFind);
        SysFree(pFFD);
    }
}

int SysGetFileInfo(char const *pszFileName, SYS_FILE_INFO &FI)
{
    WIN32_FIND_DATA WFD;
    HANDLE hFind = FindFirstFile(pszFileName, &WFD);

    if (hFind == INVALID_HANDLE_VALUE) {
        ErrSetErrorCode(ERR_STAT, pszFileName); /* [i_a] */
        return ERR_STAT;
    }

    ZeroData(FI);
    FI.iFileType = (WFD.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? ftDirectory: ftNormal;
    FI.llSize = (SYS_OFF_T) WFD.nFileSizeLow | (((SYS_OFF_T) WFD.nFileSizeHigh) << 32);
    FI.tMod = SysFileTimeToTimet(&WFD.ftLastWriteTime);

    FindClose(hFind);

    return 0;
}

int SysSetFileModTime(char const *pszFileName, time_t tMod)
{
    HANDLE hFile = CreateFile(pszFileName, GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        ErrSetErrorCode(ERR_SET_FILE_TIME);
        return ERR_SET_FILE_TIME;
    }

    FILETIME MFT;

    SysTimetToFileTime(tMod, &MFT);
    if (!SetFileTime(hFile, NULL, &MFT, &MFT)) {
        CloseHandle(hFile);
        ErrSetErrorCode(ERR_SET_FILE_TIME);
        return ERR_SET_FILE_TIME;
    }
    CloseHandle(hFile);

    return 0;
}

char *SysStrDup(char const *pszString)
{
    int iStrLength = (int)strlen(pszString);
    char *pszBuffer = (char *) SysAllocNZ(iStrLength + 1);

    if (pszBuffer != NULL)
        memcpy(pszBuffer, pszString, iStrLength + 1);

    return pszBuffer;
}

char *SysGetEnv(char const *pszVarName)
{
    HKEY hKey;
    char szRKeyPath[256];

    SysSNPrintf(szRKeyPath, sizeof(szRKeyPath) - 1, "SOFTWARE\\%s\\%s",
            APP_PRODUCER, szServerName);
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, szRKeyPath, 0, KEY_QUERY_VALUE,
             &hKey) == ERROR_SUCCESS) {
        char szKeyValue[2048] = "";
        DWORD dwSize = sizeof(szKeyValue);
        DWORD dwKeyType;

        if (RegQueryValueEx(hKey, pszVarName, NULL, &dwKeyType,
                    (u_char *) szKeyValue,
                    &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);

            return SysStrDup(szKeyValue);
        }
        RegCloseKey(hKey);
    }

    char const *pszValue = getenv(pszVarName);

    return (pszValue != NULL) ? SysStrDup(pszValue): NULL;
}

char *SysGetTempDir(char *pszPath, int iMaxPath)
{
    GetTempPath(iMaxPath - 2, pszPath);
    AppendSlash(pszPath);

    return pszPath;
}

int SysRemove(char const *pszFileName)
{
    if (!DeleteFile(pszFileName)) {
        ErrSetErrorCode(ERR_FILE_DELETE, pszFileName); /* [i_a] */
        return ERR_FILE_DELETE;
    }

    return 0;
}

int SysMakeDir(char const *pszPath)
{
    if (!CreateDirectory(pszPath, NULL)) {
        ErrSetErrorCode(ERR_DIR_CREATE, pszPath); /* [i_a] */
        return ERR_DIR_CREATE;
    }

    return 0;
}

int SysRemoveDir(char const *pszPath)
{
    if (!RemoveDirectory(pszPath)) {
        ErrSetErrorCode(ERR_DIR_DELETE, pszPath); /* [i_a] */
        return ERR_DIR_DELETE;
    }

    return 0;
}

int SysMoveFile(char const *pszOldName, char const *pszNewName)
{
    if (!MoveFileEx(pszOldName, pszNewName,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        ErrSetErrorCode(ERR_FILE_MOVE);
        return ERR_FILE_MOVE;
    }

    return 0;
}

int SysVSNPrintf(char *pszBuffer, int iSize, char const *pszFormat, va_list Args)
{
    return _vsnprintf(pszBuffer, iSize, pszFormat, Args);
}

int SysFileSync(FILE *pFile)
{
    if (fflush(pFile) || _commit(_fileno(pFile))) {
        ErrSetErrorCode(ERR_FILE_WRITE);
        return ERR_FILE_WRITE;
    }

    return 0;
}

char *SysStrTok(char *pszData, char const *pszDelim, char **ppszSavePtr)
{
    return *ppszSavePtr = strtok(pszData, pszDelim);
}

char *SysCTime(time_t *pTimer, char *pszBuffer)
{
    return strcpy(pszBuffer, ctime(pTimer));
}

struct tm *SysLocalTime(time_t *pTimer, struct tm *pTStruct)
{
    *pTStruct = *localtime(pTimer);

    return pTStruct;
}

struct tm *SysGMTime(time_t *pTimer, struct tm *pTStruct)
{
    *pTStruct = *gmtime(pTimer);

    return pTStruct;
}

char *SysAscTime(struct tm *pTStruct, char *pszBuffer, int iBufferSize)
{
    strncpy(pszBuffer, asctime(pTStruct), iBufferSize);
    pszBuffer[iBufferSize - 1] = '\0';

    return pszBuffer;
}

long SysGetTimeZone(void)
{
    return (long) _timezone;
}

long SysGetDayLight(void)
{
    time_t tCurr = time(NULL);
    struct tm tmCurr = *localtime(&tCurr);

    return (tmCurr.tm_isdst <= 0) ? 0: 3600;
}

int SysGetDiskSpace(char const *pszPath, SYS_INT64 *pTotal, SYS_INT64 *pFree)
{
    ULARGE_INTEGER BytesAvail, BytesOnDisk, BytesFree;
    char szXPath[SYS_MAX_PATH] = "";

    StrSNCpy(szXPath, pszPath);
    AppendSlash(szXPath);
    if (!GetDiskFreeSpaceEx(szXPath, &BytesAvail, &BytesOnDisk, &BytesFree)) {
        ErrSetErrorCode(ERR_GET_DISK_SPACE_INFO);
        return ERR_GET_DISK_SPACE_INFO;
    }

    *pTotal = *(SYS_INT64 *) &BytesOnDisk;
    *pFree = *(SYS_INT64 *) &BytesAvail;

    return 0;
}

int SysMemoryInfo(SYS_INT64 *pRamTotal, SYS_INT64 *pRamFree,
          SYS_INT64 *pVirtTotal, SYS_INT64 *pVirtFree)
{
#if _WIN32_WINNT >= 0x0500
    MEMORYSTATUSEX MSEX;

    ZeroData(MSEX);
    if (!GlobalMemoryStatusEx(&MSEX)) {
        ErrSetErrorCode(ERR_GET_MEMORY_INFO);
        return ERR_GET_MEMORY_INFO;
    }
    *pRamTotal = (SYS_INT64) MSEX.ullTotalPhys;
    *pRamFree = (SYS_INT64) MSEX.ullAvailPhys;
    *pVirtTotal = (SYS_INT64) MSEX.ullTotalVirtual;
    *pVirtFree = (SYS_INT64) MSEX.ullAvailVirtual;
#else
    MEMORYSTATUS MS;

    ZeroData(MS);
    GlobalMemoryStatus(&MS);
    *pRamTotal = (SYS_INT64) MS.dwTotalPhys;
    *pRamFree = (SYS_INT64) MS.dwAvailPhys;
    *pVirtTotal = (SYS_INT64) MS.dwTotalVirtual;
    *pVirtFree = (SYS_INT64) MS.dwAvailVirtual;
#endif

    return 0;
}

SYS_MMAP SysCreateMMap(char const *pszFileName, unsigned long ulFlags)
{
    HANDLE hFile = CreateFile(pszFileName, (ulFlags & SYS_MMAP_WRITE) ?
                  GENERIC_WRITE | GENERIC_READ: GENERIC_READ,
                  (ulFlags & SYS_MMAP_WRITE) ? 0: FILE_SHARE_READ,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        ErrSetErrorCode(ERR_FILE_OPEN, pszFileName); /* [i_a] */
        return SYS_INVALID_MMAP;
    }

    DWORD dwFileSizeHi = 0;
    DWORD dwFileSizeLo = GetFileSize(hFile, &dwFileSizeHi);
    HANDLE hFileMap = CreateFileMapping(hFile, NULL, (ulFlags & SYS_MMAP_WRITE) ?
                        PAGE_READWRITE: PAGE_READONLY,
                        dwFileSizeHi, dwFileSizeLo, NULL);

    if (hFileMap == NULL) {
        CloseHandle(hFile);
        ErrSetErrorCode(ERR_CREATEFILEMAPPING);
        return SYS_INVALID_MMAP;
    }

    MMapData *pMMD = (MMapData *) SysAlloc(sizeof(MMapData));
    SYSTEM_INFO SI;

    if (pMMD == NULL) {
        CloseHandle(hFileMap);
        CloseHandle(hFile);
        return SYS_INVALID_MMAP;
    }
    GetSystemInfo(&SI);

    pMMD->ulPageSize = (unsigned long) SI.dwPageSize;
    pMMD->hFile = hFile;
    pMMD->hFileMap = hFileMap;
    pMMD->iNumMaps = 0;
    pMMD->ulFlags = ulFlags;
    pMMD->llFileSize = (SYS_OFF_T) dwFileSizeLo | ((SYS_OFF_T) dwFileSizeHi << 32);

    return (SYS_MMAP) pMMD;
}

void SysCloseMMap(SYS_MMAP hMap)
{
    MMapData *pMMD = (MMapData *) hMap;

    if (pMMD != NULL) {
        if (pMMD->iNumMaps > 0) {

        }
        CloseHandle(pMMD->hFileMap);
        CloseHandle(pMMD->hFile);
        SysFree(pMMD);
    }
}

SYS_OFF_T SysMMapSize(SYS_MMAP hMap)
{
    MMapData *pMMD = (MMapData *) hMap;

    return pMMD->llFileSize;
}

SYS_OFF_T SysMMapOffsetAlign(SYS_MMAP hMap, SYS_OFF_T llOffset)
{
    MMapData *pMMD = (MMapData *) hMap;

    return llOffset & ~((SYS_OFF_T) pMMD->ulPageSize - 1);
}

void *SysMapMMap(SYS_MMAP hMap, SYS_OFF_T llOffset, SYS_SIZE_T lSize)
{
    MMapData *pMMD = (MMapData *) hMap;
    DWORD dwMapFlags = 0;
    void *pMapAddress;

    if (llOffset % pMMD->ulPageSize) {
        ErrSetErrorCode(ERR_INVALID_MMAP_OFFSET);
        return NULL;
    }
    if (pMMD->ulFlags & SYS_MMAP_READ)
        dwMapFlags |= FILE_MAP_READ;
    if (pMMD->ulFlags & SYS_MMAP_WRITE)
        dwMapFlags |= FILE_MAP_WRITE;

    if ((pMapAddress = MapViewOfFile(pMMD->hFileMap, dwMapFlags,
                     (DWORD) (llOffset >> 32), (DWORD) llOffset,
                     lSize)) == NULL) {
        ErrSetErrorCode(ERR_MAPVIEWOFFILE);
        return NULL;
    }
    pMMD->iNumMaps++;

    return pMapAddress;
}

int SysUnmapMMap(SYS_MMAP hMap, void *pAddr, SYS_SIZE_T lSize)
{
    MMapData *pMMD = (MMapData *) hMap;

    if (!UnmapViewOfFile(pAddr)) {
        ErrSetErrorCode(ERR_UNMAPFILEVIEW);
        return ERR_UNMAPFILEVIEW;
    }
    pMMD->iNumMaps--;

    return 0;
}

