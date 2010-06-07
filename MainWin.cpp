/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,..,2004  Davide Libenzi
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

/* Comment this statement if You want a normal startup ( not as a service ) */
#ifndef NO_SERVICE /* [i_a] */
#define SERVICE
#endif

#define NULFILE         "nul"

#ifndef SERVICE
/* Normal startup */

int main(int iArgCount, char *pszArgs[])
{
	return SvrMain(iArgCount, pszArgs);
}

#else				// #ifndef SERVICE

/* Service startup */

#define SZDEPENDENCIES              _T("Tcpip\0")
#define SERVER_START_WAIT           8000
#define SERVER_STOP_WAIT            4000

static int MnSetupStdHandles(void);
static VOID WINAPI ServiceCtrl(DWORD dwCtrlCode);
static VOID WINAPI ServiceMain(DWORD dwArgc, LPTSTR lpszArgv[]);
static BOOL CmdInstallService(DWORD dwStartType);
static BOOL CmdRemoveService(void);
static int CmdDebugService(int argc, TCHAR * argv[]);
static BOOL ReportStatusToSCMgr(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
static LPTSTR GetLastErrorText(LPTSTR lpszBuf, DWORD dwSize);
static VOID AddToMessageLog(LPCTSTR lpszMsg);
static int GetServiceNameFromModule(LPCTSTR pszModule, LPTSTR pszName, int iSize);

static SERVICE_STATUS ssStatus;
static SERVICE_STATUS_HANDLE sshStatusHandle = NULL;
static DWORD dwErr = 0;
static BOOL bDebug = FALSE;
static TCHAR szErr[2048] = _T("");
static TCHAR szServicePath[MAX_PATH] = _T("");
static TCHAR szServiceName[128] = _T("");
static TCHAR szServiceDispName[256] = _T("");

static int MnSetupStdHandles(void)
{
	HANDLE hInFile = CreateFile(NULFILE, GENERIC_READ | GENERIC_WRITE,
				    FILE_SHARE_READ | FILE_SHARE_WRITE,
				    NULL, OPEN_EXISTING, 0, NULL);

	if (hInFile == INVALID_HANDLE_VALUE) {
		AddToMessageLog(_T("CreateFile"));
		return -1;
	}

	HANDLE hOutFile = CreateFile(NULFILE, GENERIC_READ | GENERIC_WRITE,
				     FILE_SHARE_READ | FILE_SHARE_WRITE,
				     NULL, OPEN_EXISTING, 0, NULL);

	if (hOutFile == INVALID_HANDLE_VALUE) {
		AddToMessageLog(_T("CreateFile"));
		CloseHandle(hInFile);
		return -1;
	}

	HANDLE hErrFile = CreateFile(NULFILE, GENERIC_READ | GENERIC_WRITE,
				     FILE_SHARE_READ | FILE_SHARE_WRITE,
				     NULL, OPEN_EXISTING, 0, NULL);

	if (hErrFile == INVALID_HANDLE_VALUE) {
		AddToMessageLog(_T("CreateFile"));
		CloseHandle(hOutFile);
		CloseHandle(hInFile);
		return -1;
	}

	if (!SetStdHandle(STD_INPUT_HANDLE, hInFile) || !SetStdHandle(STD_OUTPUT_HANDLE, hOutFile)
	    || !SetStdHandle(STD_ERROR_HANDLE, hErrFile)) {
		AddToMessageLog(_T("SetStdHandle"));
		CloseHandle(hErrFile);
		CloseHandle(hOutFile);
		CloseHandle(hInFile);
		return -1;
	}

	return 0;
}

int _tmain(int argc, TCHAR * argv[])
{
	if (GetModuleFileName(NULL, szServicePath, CountOf(szServicePath)) == 0) {
		_tprintf(_T("Unable to get module name - %s\n"),
			 GetLastErrorText(szErr, CountOf(szErr)));
		return 1;
	}
	GetServiceNameFromModule(szServicePath, szServiceName, CountOf(szServiceName));
	_stprintf(szServiceDispName, _T("%s Server"), szServiceName);

	SERVICE_TABLE_ENTRY DispTable[] = {
		{szServiceName, (LPSERVICE_MAIN_FUNCTION) ServiceMain}
		,
		{NULL, NULL}
	};

	if (argc > 1) {
		if (_tcsicmp(_T("--install"), argv[1]) == 0) {
			CmdInstallService(SERVICE_DEMAND_START);
			return 0;
		}
		if (_tcsicmp(_T("--install-auto"), argv[1]) == 0) {
			CmdInstallService(SERVICE_AUTO_START);
			return 0;
		} else if (_tcsicmp(_T("--remove"), argv[1]) == 0) {
			CmdRemoveService();
			return 0;
		} else if (_tcsicmp(_T("--debug"), argv[1]) == 0) {
			bDebug = TRUE;
			CmdDebugService(argc, argv);
			return 0;
		}
	}

	_tprintf(_T("%s --install          = Install the service\n"), argv[0]);
	_tprintf(_T("%s --remove           = Remove the service\n"), argv[0]);
	_tprintf(_T("%s --debug [params]   = Run as a console app for debugging\n"), argv[0]);

	_tprintf(_T("\nStartServiceCtrlDispatcher being called.\n"));
	_tprintf(_T("This may take several seconds.  Please wait.\n"));

	/* Setup std handles */
	if (MnSetupStdHandles() < 0)
		return 1;

	/* Service loop */
	if (!StartServiceCtrlDispatcher(DispTable))
		AddToMessageLog(_T("StartServiceCtrlDispatcher"));

	return 0;
}

static void WINAPI ServiceMain(DWORD dwArgc, LPTSTR lpszArgv[])
{
	if ((sshStatusHandle = RegisterServiceCtrlHandler(szServiceName, ServiceCtrl)) != NULL) {
		ZeroData(ssStatus);

		ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		ssStatus.dwServiceSpecificExitCode = 0;

		if (ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, SERVER_START_WAIT)) {
			ReportStatusToSCMgr(SERVICE_RUNNING, NO_ERROR, 0);

			/* Run server */
			int iSvrResult = SvrMain((int) dwArgc, lpszArgv);

			if (iSvrResult < 0) {

				AddToMessageLog(ErrGetErrorString(iSvrResult));

			}
		}

		ReportStatusToSCMgr(SERVICE_STOPPED, dwErr, 0);
	} else
		AddToMessageLog(_T("RegisterServiceCtrlHandler"));

}

static VOID WINAPI ServiceCtrl(DWORD dwCtrlCode)
{
	switch (dwCtrlCode) {
	case (SERVICE_CONTROL_SHUTDOWN):
	case (SERVICE_CONTROL_STOP):
	{
		ReportStatusToSCMgr(SERVICE_STOP_PENDING, NO_ERROR, SERVER_STOP_WAIT);

		/* Signal the server to stop and wait for completion */
		SvrStopServer(false);

		while (SvrInShutdown()) {
			Sleep(SERVER_STOP_WAIT / 2);
			ReportStatusToSCMgr(SERVICE_STOP_PENDING, NO_ERROR,
					    SERVER_STOP_WAIT);
		}

		ReportStatusToSCMgr(SERVICE_STOPPED, 0, 0);
	}
	break;

	default:
		ReportStatusToSCMgr(ssStatus.dwCurrentState, NO_ERROR, 0);
	}

}

static BOOL ReportStatusToSCMgr(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;
	BOOL bResult = TRUE;

	if (!bDebug) {
		if (dwCurrentState == SERVICE_START_PENDING)
			ssStatus.dwControlsAccepted = 0;
		else
			ssStatus.dwControlsAccepted =
				SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

		ssStatus.dwCurrentState = dwCurrentState;
		ssStatus.dwWin32ExitCode = dwWin32ExitCode;
		ssStatus.dwWaitHint = dwWaitHint;

		if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))
			ssStatus.dwCheckPoint = 0;
		else
			ssStatus.dwCheckPoint = dwCheckPoint++;

		if (!(bResult = SetServiceStatus(sshStatusHandle, &ssStatus)))
			AddToMessageLog(_T("SetServiceStatus"));

	}

	return bResult;
}

static VOID AddToMessageLog(LPCTSTR lpszMsg)
{
	HANDLE hEventSource = NULL;
	LPTSTR lpszStrings[2];
	TCHAR szMsg[512] = _T("");
	TCHAR szErrMsg[2048] = _T("");

	if (!bDebug) {
		dwErr = GetLastError();

		GetLastErrorText(szErr, CountOf(szErr));

		_stprintf(szMsg, _T("%s error: %d"), szServiceName, dwErr);
		_stprintf(szErrMsg, _T("{%s}: %s"), lpszMsg, szErr);

		lpszStrings[0] = szMsg;
		lpszStrings[1] = szErrMsg;

		if ((hEventSource = RegisterEventSource(NULL, szServiceName)) != NULL) {
			ReportEvent(hEventSource,	// handle of event source
				    EVENTLOG_ERROR_TYPE,	// event type
				    0,	// event category
				    0,	// event ID
				    NULL,	// current user's SID
				    2,	// strings in lpszStrings
				    0,	// no bytes of raw data
				    (const char **) lpszStrings,	// array of error strings
				    NULL);	// no raw data

			DeregisterEventSource(hEventSource);
		}
	}

}

static BOOL CmdInstallService(DWORD dwStartType)
{
	SC_HANDLE schService = NULL;
	SC_HANDLE schSCManager = NULL;

	if ((schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) != NULL) {
		schService = CreateService(schSCManager,	// SCManager database
					   szServiceName,	// name of service
					   szServiceDispName,	// name to display
					   SERVICE_ALL_ACCESS,	// desired access
					   SERVICE_WIN32_OWN_PROCESS,	// service type
					   dwStartType,	// start type
					   SERVICE_ERROR_NORMAL,	// error control type
					   szServicePath,	// service's binary
					   NULL,	// no load ordering group
					   NULL,	// no tag identifier
					   SZDEPENDENCIES,	// dependencies
					   NULL,	// LocalSystem account
					   NULL);	// no password

		if (schService != NULL) {
			_tprintf(_T("%s installed.\n"), szServiceDispName);
			CloseServiceHandle(schService);
			CloseServiceHandle(schSCManager);

			return TRUE;
		} else
			_tprintf(_T("CreateService failed - %s\n"),
				 GetLastErrorText(szErr, CountOf(szErr)));

		CloseServiceHandle(schSCManager);
	} else
		_tprintf(_T("OpenSCManager failed - %s\n"),
			 GetLastErrorText(szErr, CountOf(szErr)));

	return FALSE;
}

static BOOL CmdRemoveService(void)
{
	SC_HANDLE schService = NULL;
	SC_HANDLE schSCManager = NULL;

	if ((schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) != NULL) {
		schService = OpenService(schSCManager, szServiceName, SERVICE_ALL_ACCESS);

		if (schService != NULL) {
			if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus)) {
				_tprintf(_T("Stopping %s."), szServiceDispName);
				Sleep(1000);

				while (QueryServiceStatus(schService, &ssStatus)) {
					if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) {
						_tprintf(_T("."));
						Sleep(1000);
					} else
						break;
				}

				if (ssStatus.dwCurrentState == SERVICE_STOPPED)
					_tprintf(_T("\n%s stopped.\n"), szServiceDispName);
				else
					_tprintf(_T("\n%s failed to stop.\n"), szServiceDispName);

			}

			if (DeleteService(schService)) {
				_tprintf(_T("%s removed.\n"), szServiceDispName);
				CloseServiceHandle(schService);
				CloseServiceHandle(schSCManager);

				return TRUE;
			} else
				_tprintf(_T("DeleteService failed - %s\n"),
					 GetLastErrorText(szErr, CountOf(szErr)));

			CloseServiceHandle(schService);
		} else
			_tprintf(_T("OpenService failed - %s\n"),
				 GetLastErrorText(szErr, CountOf(szErr)));

		CloseServiceHandle(schSCManager);
	} else
		_tprintf(_T("OpenSCManager failed - %s\n"),
			 GetLastErrorText(szErr, CountOf(szErr)));

	return FALSE;
}

static int CmdDebugService(int argc, LPTSTR argv[])
{
	_tprintf(_T("Debugging %s.\n"), szServiceDispName);

	/* Run server */
	return SvrMain(argc, argv);
}

static LPTSTR GetLastErrorText(LPTSTR lpszBuf, DWORD dwSize)
{
	DWORD dwRet;
	DWORD dwError = GetLastError();
	LPTSTR lpszTemp = NULL;

	dwRet =
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			      FORMAT_MESSAGE_ARGUMENT_ARRAY, NULL, dwError, LANG_NEUTRAL,
			      (LPTSTR) & lpszTemp, 0, NULL);

	if ((dwRet == 0) || ((long) dwSize < (long) (dwRet + 14)))
		lpszBuf[0] = TCHAR('\0');
	else {
		lpszTemp[lstrlen(lpszTemp) - 2] = TCHAR('\0');

		_stprintf(lpszBuf, _T("%s (0x%x)"), lpszTemp, dwError);
	}

	if (lpszTemp != NULL)
		LocalFree((HLOCAL) lpszTemp);

	return lpszBuf;
}

static int GetServiceNameFromModule(LPCTSTR pszModule, LPTSTR pszName, int iSize)
{
	LPCTSTR pszSlash;
	LPCTSTR pszDot;

	if ((pszSlash = _tcsrchr(pszModule, (TCHAR) '\\')) == NULL)
		pszSlash = pszModule;
	else
		pszSlash++;
	if ((pszDot = _tcschr(pszSlash, (TCHAR) '.')) == NULL)
		pszDot = pszSlash + _tcslen(pszSlash);
	iSize = Min(iSize - 1, (int) (pszDot - pszSlash));
	Cpy2Sz(pszName, pszSlash, iSize);

	return 0;
}

#endif				// #ifndef SERVICE
