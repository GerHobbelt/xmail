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
#include "StrUtils.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MailDomains.h"
#include "SMTPUtils.h"
#include "ExtAliases.h"
#include "UsrMailList.h"
#include "Filter.h"
#include "MailConfig.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define FILTER_LOG_FILE             "filters"
#define FILTER_STORAGE_DIR          "filters"
#define FILTER_SELECT_MAX           128
#define FILTER_DB_LINE_MAX          512
#define FILTER_LINE_MAX             1024

#define FILTV_SET(p, d) ((p) == NULL ? (d): atoi(p))

struct FilterMsgInfo {
	char szSender[MAX_ADDR_NAME];
	char szRecipient[MAX_ADDR_NAME];
	SYS_INET_ADDR LocalAddr;
	SYS_INET_ADDR RemoteAddr;
	char szSpoolFile[SYS_MAX_PATH];
	char szAuthName[MAX_ADDR_NAME];
};

struct FilterMacroSubstCtx {
	SPLF_HANDLE hFSpool;
	FilterMsgInfo const *pFMI;
	FileSection FSect;
};

static char *FilGetLogExecStr(FilterLogInfo const *pFLI, int *piSize);
static int FilLogExec(FilterMsgInfo const &FMI, char const * const *ppszExec,
		      int iExecResult, int iExitCode, char const *pszType,
		      char const *pszInfo);
static int FilLoadMsgInfo(SPLF_HANDLE hFSpool, FilterMsgInfo & FMI);
static void FilFreeMsgInfo(FilterMsgInfo & FMI);
static int FilGetFilePath(char const *pszMode, char *pszFilePath, int iMaxPath);
static int FilAddFilter(char **ppszFilters, int &iNumFilters, char const *pszFilterName);
static int FilSelectFilters(char const *pszFilterFilePath, char const *pszMode,
			    FilterMsgInfo const &FMI, char **ppszFilters, int iMaxFilters);
static void FilFreeFilters(char **ppszFilters, int iNumFilters);
static int FilGetFilterPath(char const *pszFileName, char *pszFilePath, int iMaxPath);
static int FilPreExec(FilterMsgInfo const &FMI, FilterExecCtx *pFCtx,
		      FilterTokens *pToks, char **ppszPEError);
static int FilApplyFilter(char const *pszFilterPath, SPLF_HANDLE hFSpool,
			  QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, FilterMsgInfo const &FMI,
			  char const *pszType);
static char *FilMacroLkupProc(void *pPrivate, char const *pszName, int iSize);
static int FilFilterMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool,
				     FilterMsgInfo const &FMI);

static char *FilGetLogExecStr(FilterLogInfo const *pFLI, int *piSize)
{
	int i;
	DynString DynS;

	if (StrDynInit(&DynS, NULL) < 0)
		return NULL;
	for (i = 0; pFLI->ppszExec[i] != NULL; i++) {
		if (StrDynAdd(&DynS, pFLI->ppszExec[i], -1) < 0 ||
		    StrDynAdd(&DynS, ";", 1) < 0) {
			StrDynFree(&DynS);
			return NULL;
		}
	}

	return StrDynDrop(&DynS, piSize);
}

int FilLogFilter(FilterLogInfo const *pFLI)
{
	char szTime[256] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1);

	char *pszExStr = FilGetLogExecStr(pFLI, NULL);

	if (pszExStr == NULL)
		return ErrGetErrorCode();

	RLCK_HANDLE hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR FILTER_LOG_FILE);

	if (hResLock == INVALID_RLCK_HANDLE) {
		SysFree(pszExStr);
		return ErrGetErrorCode();
	}

	char szLocIP[128] = "???.???.???.???";
	char szRmtIP[128] = "???.???.???.???";

	MscFileLog(FILTER_LOG_FILE, "\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%d\""
		   "\t\"%d\""
		   "\t\"%s\""
		   "\n", pFLI->pszSender, pFLI->pszRecipient,
		   SysInetNToA(pFLI->LocalAddr, szLocIP, sizeof(szLocIP)),
		   SysInetNToA(pFLI->RemoteAddr, szRmtIP, sizeof(szRmtIP)),
		   szTime, pFLI->pszType, pFLI->pszInfo, pFLI->iExecResult,
		   pFLI->iExitCode, pszExStr);

	RLckUnlockEX(hResLock);
	SysFree(pszExStr);

	return 0;
}

static int FilLogExec(FilterMsgInfo const &FMI, char const * const *ppszExec,
		      int iExecResult, int iExitCode, char const *pszType,
		      char const *pszInfo)
{
	FilterLogInfo FLI;

	ZeroData(FLI);
	FLI.pszSender = FMI.szSender;
	FLI.pszRecipient = FMI.szRecipient;
	FLI.LocalAddr = FMI.LocalAddr;
	FLI.RemoteAddr = FMI.RemoteAddr;
	FLI.ppszExec = ppszExec;
	FLI.iExecResult = iExecResult;
	FLI.iExitCode = iExitCode;
	FLI.pszType = pszType;
	FLI.pszInfo = pszInfo != NULL ? pszInfo: "";

	return FilLogFilter(&FLI);
}

static int FilLoadMsgInfo(SPLF_HANDLE hFSpool, FilterMsgInfo & FMI)
{
	UserInfo *pUI;
	char const *const *ppszInfo = USmlGetInfo(hFSpool);
	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);
	char const *const *ppszRcpt = USmlGetRcptTo(hFSpool);
	char const *pszSpoolFile = USmlGetSpoolFilePath(hFSpool);
	int iFromDomains = StrStringsCount(ppszFrom);
	int iRcptDomains = StrStringsCount(ppszRcpt);
	char szUser[MAX_ADDR_NAME] = "";
	char szDomain[MAX_ADDR_NAME] = "";

	ZeroData(FMI);
	if (iFromDomains > 0 &&
	    USmtpSplitEmailAddr(ppszFrom[iFromDomains - 1], szUser, szDomain) == 0) {
		if ((pUI = UsrGetUserByNameOrAlias(szDomain, szUser)) != NULL) {
			UsrGetAddress(pUI, FMI.szSender);

			UsrFreeUserInfo(pUI);
		} else
			StrSNCpy(FMI.szSender, ppszFrom[iFromDomains - 1]);
	} else
		SetEmptyString(FMI.szSender);

	if (iRcptDomains > 0 &&
	    USmtpSplitEmailAddr(ppszRcpt[iRcptDomains - 1], szUser, szDomain) == 0) {
		if ((pUI = UsrGetUserByNameOrAlias(szDomain, szUser)) != NULL) {
			UsrGetAddress(pUI, FMI.szRecipient);

			UsrFreeUserInfo(pUI);
		} else
			StrSNCpy(FMI.szRecipient, ppszRcpt[iRcptDomains - 1]);
	} else
		SetEmptyString(FMI.szRecipient);

	if (MscGetServerAddress(ppszInfo[smiServerAddr], FMI.LocalAddr) < 0 ||
	    MscGetServerAddress(ppszInfo[smiClientAddr], FMI.RemoteAddr) < 0)
		return ErrGetErrorCode();

	StrSNCpy(FMI.szSpoolFile, pszSpoolFile);

	if (USmlMessageAuth(hFSpool, FMI.szAuthName, sizeof(FMI.szAuthName) - 1) < 0)
		SetEmptyString(FMI.szAuthName);

	return 0;
}

static void FilFreeMsgInfo(FilterMsgInfo & FMI)
{
}

char *FilGetFilterRejMessage(char const *pszSpoolFile)
{
	FILE *pFile;
	char szRejFilePath[SYS_MAX_PATH] = "";
	char szRejMsg[512] = "";

	SysSNPrintf(szRejFilePath, sizeof(szRejFilePath) - 1, "%s.rej", pszSpoolFile);
	if ((pFile = fopen(szRejFilePath, "rb")) == NULL)
		return NULL;

	MscFGets(szRejMsg, sizeof(szRejMsg) - 1, pFile);

	fclose(pFile);
	SysRemove(szRejFilePath);

	return SysStrDup(szRejMsg);
}

static int FilGetFilePath(char const *pszMode, char *pszFilePath, int iMaxPath)
{
	char szMailRootPath[SYS_MAX_PATH] = "";

	CfgGetRootPath(szMailRootPath, sizeof(szMailRootPath));

	SysSNPrintf(pszFilePath, iMaxPath - 1, "%sfilters.%s.tab", szMailRootPath, pszMode);

	return 0;
}

static int FilAddFilter(char **ppszFilters, int &iNumFilters, char const *pszFilterName)
{
	for (int ii = 0; ii < iNumFilters; ii++)
		if (strcmp(ppszFilters[ii], pszFilterName) == 0)
			return 0;

	if ((ppszFilters[iNumFilters] = SysStrDup(pszFilterName)) == NULL)
		return ErrGetErrorCode();
	iNumFilters++;

	return 0;
}

static int FilSelectFilters(char const *pszFilterFilePath, char const *pszMode,
			    FilterMsgInfo const &FMI, char **ppszFilters, int iMaxFilters)
{
	/* Share lock the filter table file */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(pszFilterFilePath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* Open the filter database. Fail smootly if the file does not exist */
	FILE *pFile = fopen(pszFilterFilePath, "rt");

	if (pFile == NULL) {
		RLckUnlockSH(hResLock);
		return 0;
	}

	int iNumFilters = 0;
	char szLine[FILTER_DB_LINE_MAX] = "";

	while ((iNumFilters < iMaxFilters) &&
	       (MscGetConfigLine(szLine, sizeof(szLine) - 1, pFile) != NULL)) {
		char **ppszTokens = StrGetTabLineStrings(szLine);

		if (ppszTokens == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszTokens);

		if ((iFieldsCount >= filMax) &&
		    StrIWildMatch(FMI.szSender, ppszTokens[filSender]) &&
		    StrIWildMatch(FMI.szRecipient, ppszTokens[filRecipient])) {
			AddressFilter AFRemote;
			AddressFilter AFLocal;

			if ((MscLoadAddressFilter(&ppszTokens[filRemoteAddr], 1, AFRemote) == 0)
			    && MscAddressMatch(AFRemote, FMI.RemoteAddr) &&
			    (MscLoadAddressFilter(&ppszTokens[filLocalAddr], 1, AFLocal) == 0) &&
			    MscAddressMatch(AFLocal, FMI.LocalAddr)) {

				FilAddFilter(ppszFilters, iNumFilters, ppszTokens[filFileName]);

			}
		}
		StrFreeStrings(ppszTokens);
	}
	fclose(pFile);
	RLckUnlockSH(hResLock);

	return iNumFilters;
}

static void FilFreeFilters(char **ppszFilters, int iNumFilters)
{
	for (iNumFilters--; iNumFilters >= 0; iNumFilters--)
		SysFree(ppszFilters[iNumFilters]);

}

static int FilGetFilterPath(char const *pszFileName, char *pszFilePath, int iMaxPath)
{
	char szMailRootPath[SYS_MAX_PATH] = "";

	CfgGetRootPath(szMailRootPath, sizeof(szMailRootPath));
	SysSNPrintf(pszFilePath, iMaxPath - 1, "%s%s%s%s",
		    szMailRootPath, FILTER_STORAGE_DIR, SYS_SLASH_STR, pszFileName);

	return 0;
}

int FilExecPreParse(FilterExecCtx *pCtx, char **ppszPEError)
{
	int i;
	char **ppszEToks;
	char const *pszEx = pCtx->pToks->ppszCmdTokens[0];

	if (pCtx->pToks->iTokenCount < 1 || *pszEx != '!')
		return 0;

	pCtx->pToks->iTokenCount--;
	pCtx->pToks->ppszCmdTokens++;

	if ((ppszEToks = StrTokenize(pszEx + 1, ",")) == NULL)
		return ErrGetErrorCode();
	for (i = 0; ppszEToks[i] != NULL; i++) {
		char *pszVar = ppszEToks[i], *pszVal;

		if ((pszVal = strchr(pszVar, '=')) != NULL)
			*pszVal++ = '\0';
		if (strcmp(pszVar, "aex") == 0) {
			if (FILTV_SET(pszVal, 1) &&
			    !IsEmptyString(pCtx->pszAuthName)) {
				StrFreeStrings(ppszEToks);

				*ppszPEError = SysStrDup("EXCL");

				return -1;
			}
		} else if (strcmp(pszVar, "wlex") == 0) {
			if (FILTV_SET(pszVal, 1) &&
			    (pCtx->ulFlags & FILTER_XFL_WHITELISTED)) {
				StrFreeStrings(ppszEToks);

				*ppszPEError = SysStrDup("WLISTED");

				return -1;
			}
		} else if (strcmp(pszVar, "timeo") == 0) {
			if (pszVal != NULL)
				pCtx->iTimeout = atoi(pszVal);
		}
	}
	StrFreeStrings(ppszEToks);

	return 0;
}

static int FilPreExec(FilterMsgInfo const &FMI, FilterExecCtx *pFCtx,
		      FilterTokens *pToks, char **ppszPEError)
{
	pFCtx->pToks = pToks;
	pFCtx->pszAuthName = FMI.szAuthName;
	pFCtx->ulFlags = 0;
	pFCtx->iTimeout = iFilterTimeout;

	return FilExecPreParse(pFCtx, ppszPEError);
}

static int FilApplyFilter(char const *pszFilterPath, SPLF_HANDLE hFSpool,
			  QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, FilterMsgInfo const &FMI,
			  char const *pszType)
{
	/* Share lock the filter file */
	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(pszFilterPath, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* This should not happen but if it happens we let the message pass through */
	FILE *pFiltFile = fopen(pszFilterPath, "rt");

	if (pFiltFile == NULL) {
		RLckUnlockSH(hResLock);
		return 0;
	}
	/* Filter this message */
	char szFiltLine[FILTER_LINE_MAX] = "";

	while (MscGetConfigLine(szFiltLine, sizeof(szFiltLine) - 1, pFiltFile) != NULL) {
		char **ppszCmdTokens = StrGetTabLineStrings(szFiltLine);

		if (ppszCmdTokens == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszCmdTokens);

		if (iFieldsCount < 1) {
			StrFreeStrings(ppszCmdTokens);
			continue;
		}

		/* Perform pre-exec filtering (like exec exclude if authenticated, ...) */
		char *pszPEError = NULL;
		FilterExecCtx FCtx;
		FilterTokens Toks;

		ZeroData(FCtx);
		Toks.ppszCmdTokens = ppszCmdTokens;
		Toks.iTokenCount = iFieldsCount;

		if (FilPreExec(FMI, &FCtx, &Toks, &pszPEError) < 0) {
			if (bFilterLogEnabled)
				FilLogExec(FMI, Toks.ppszCmdTokens, -1,
					   -1, pszType, pszPEError);
			SysFree(pszPEError);
			StrFreeStrings(ppszCmdTokens);
			continue;
		}

		/* Do filter line macro substitution */
		FilFilterMacroSubstitutes(Toks.ppszCmdTokens, hFSpool, FMI);

		/* Time to fire the external executable ... */
		int iExitCode = -1;
		int iExitFlags = 0;
		int iExecResult = SysExec(Toks.ppszCmdTokens[0], &Toks.ppszCmdTokens[0],
					  FCtx.iTimeout, FILTER_PRIORITY, &iExitCode);

		/* Log the operation, if requested. */
		if (bFilterLogEnabled)
			FilLogExec(FMI, Toks.ppszCmdTokens, iExecResult,
				   iExitCode, pszType, NULL);

		if (iExecResult == 0) {
			SysLogMessage(LOG_LEV_MESSAGE,
				      "Filter run: Sender = \"%s\" Recipient = \"%s\" Filter = \"%s\" Retcode = %d\n",
				      FMI.szSender, FMI.szRecipient, Toks.ppszCmdTokens[0],
				      iExitCode);

			/* Separate code from flags */
			iExitFlags = iExitCode & FILTER_FLAGS_MASK;
			iExitCode &= ~FILTER_FLAGS_MASK;

			if (iExitCode == FILTER_OUT_EXITCODE ||
			    iExitCode == FILTER_OUT_NN_EXITCODE ||
			    iExitCode == FILTER_OUT_NNF_EXITCODE) {
				fclose(pFiltFile);
				RLckUnlockSH(hResLock);

				/* Filter out message */
				char *pszRejMsg = FilGetFilterRejMessage(FMI.szSpoolFile);

				if (iExitCode == FILTER_OUT_EXITCODE)
					QueUtNotifyPermErrDelivery(hQueue, hMessage, NULL,
								   (pszRejMsg != NULL) ? pszRejMsg:
								   ErrGetErrorString(ERR_FILTERED_MESSAGE),
								   NULL, true);
				else if (iExitCode == FILTER_OUT_NN_EXITCODE)
					QueCleanupMessage(hQueue, hMessage,
							  !QueUtRemoveSpoolErrors());
				else
					QueCleanupMessage(hQueue, hMessage, false);

				StrFreeStrings(ppszCmdTokens);
				SysFree(pszRejMsg);

				ErrSetErrorCode(ERR_FILTERED_MESSAGE);
				return ERR_FILTERED_MESSAGE;
			} else if (iExitCode == FILTER_MODIFY_EXITCODE) {
				/* Filter modified the message, we need to reload the spool handle */
				if (USmlReloadHandle(hFSpool) < 0) {
					ErrorPush();
					fclose(pFiltFile);
					RLckUnlockSH(hResLock);

					SysLogMessage(LOG_LEV_MESSAGE,
						      "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
						      FMI.szSender, FMI.szRecipient,
						      Toks.ppszCmdTokens[0]);

					QueUtErrLogMessage(hQueue, hMessage,
							   "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
							   FMI.szSender, FMI.szRecipient,
							   Toks.ppszCmdTokens[0]);

					QueCleanupMessage(hQueue, hMessage, true);
					StrFreeStrings(ppszCmdTokens);

					return ErrorPop();
				}
			}
		} else {
			SysLogMessage(LOG_LEV_ERROR,
				      "Filter error (%d): Sender = \"%s\" Recipient = \"%s\" Filter = \"%s\"\n",
				      iExecResult, FMI.szSender, FMI.szRecipient,
				      Toks.ppszCmdTokens[0]);

			QueUtErrLogMessage(hQueue, hMessage,
					   "Filter error (%d): Sender = \"%s\" Recipient = \"%s\" Filter = \"%s\"\n",
					   iExecResult, FMI.szSender, FMI.szRecipient,
					   Toks.ppszCmdTokens[0]);
		}
		StrFreeStrings(ppszCmdTokens);

		/* Filter list processing break required ? */
		if (iExitFlags & FILTER_FLAGS_BREAK) {
			fclose(pFiltFile);
			RLckUnlockSH(hResLock);
			return 1;
		}
	}
	fclose(pFiltFile);
	RLckUnlockSH(hResLock);

	return 0;
}

int FilFilterMessage(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
		     QMSG_HANDLE hMessage, char const *pszMode)
{
	/* Get filter file path and returns immediately if no file is defined */
	char szFilterFilePath[SYS_MAX_PATH] = "";

	FilGetFilePath(pszMode, szFilterFilePath, sizeof(szFilterFilePath));
	if (!SysExistFile(szFilterFilePath))
		return 0;

	/* Load the message info */
	FilterMsgInfo FMI;

	if (FilLoadMsgInfo(hFSpool, FMI) < 0)
		return ErrGetErrorCode();

	/* Select applicable filters */
	int iNumFilters;
	char *pszFilters[FILTER_SELECT_MAX];

	if ((iNumFilters = FilSelectFilters(szFilterFilePath, pszMode, FMI, pszFilters,
					    CountOf(pszFilters))) < 0) {
		ErrorPush();
		FilFreeMsgInfo(FMI);
		return ErrorPop();
	}
	/* Sequentially apply each selected filter */
	for (int ii = 0; ii < iNumFilters; ii++) {
		int iFilterResult;
		char szFilterPath[SYS_MAX_PATH] = "";

		FilGetFilterPath(pszFilters[ii], szFilterPath, sizeof(szFilterPath));

		if ((iFilterResult = FilApplyFilter(szFilterPath, hFSpool, hQueue,
						    hMessage, FMI, pszMode)) < 0) {
			ErrorPush();
			FilFreeFilters(pszFilters, iNumFilters);
			FilFreeMsgInfo(FMI);
			return ErrorPop();
		}
		/* A return code greater than zero means exit filter processing loop soon */
		if (iFilterResult > 0)
			break;
	}
	FilFreeFilters(pszFilters, iNumFilters);
	FilFreeMsgInfo(FMI);

	return 0;
}

static char *FilMacroLkupProc(void *pPrivate, char const *pszName, int iSize)
{
	FilterMacroSubstCtx *pFMS = (FilterMacroSubstCtx *) pPrivate;

	if (MemMatch(pszName, iSize, "FROM", 4)) {
		char const *const *ppszFrom = USmlGetMailFrom(pFMS->hFSpool);
		int iFromDomains = StrStringsCount(ppszFrom);

		return SysStrDup((iFromDomains > 0) ? ppszFrom[iFromDomains - 1] : "");
	} else if (MemMatch(pszName, iSize, "RCPT", 4)) {
		char const *const *ppszRcpt = USmlGetRcptTo(pFMS->hFSpool);
		int iRcptDomains = StrStringsCount(ppszRcpt);

		return SysStrDup((iRcptDomains > 0) ? ppszRcpt[iRcptDomains - 1] : "");
	} else if (MemMatch(pszName, iSize, "RFROM", 5)) {

		return SysStrDup(pFMS->pFMI->szSender);
	} else if (MemMatch(pszName, iSize, "RRCPT", 5)) {

		return SysStrDup(pFMS->pFMI->szRecipient);
	} else if (MemMatch(pszName, iSize, "FILE", 4)) {

		return SysStrDup(pFMS->FSect.szFilePath);
	} else if (MemMatch(pszName, iSize, "MSGID", 5)) {

		return SysStrDup(USmlGetSpoolFile(pFMS->hFSpool));
	} else if (MemMatch(pszName, iSize, "MSGREF", 6)) {

		return SysStrDup(USmlGetSmtpMessageID(pFMS->hFSpool));
	} else if (MemMatch(pszName, iSize, "LOCALADDR", 9)) {
		char const *const *ppszInfo = USmlGetInfo(pFMS->hFSpool);

		return SysStrDup(ppszInfo[smiServerAddr]);
	} else if (MemMatch(pszName, iSize, "REMOTEADDR", 10)) {
		char const *const *ppszInfo = USmlGetInfo(pFMS->hFSpool);

		return SysStrDup(ppszInfo[smiClientAddr]);
	} else if (MemMatch(pszName, iSize, "USERAUTH", 8)) {

		return SysStrDup(IsEmptyString(pFMS->pFMI->szAuthName) ? "-":
				 pFMS->pFMI->szAuthName);
	}

	return SysStrDup("");
}

static int FilFilterMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool,
				     FilterMsgInfo const &FMI)
{
	FilterMacroSubstCtx FMS;

	FMS.hFSpool = hFSpool;
	FMS.pFMI = &FMI;
	/*
	 * This function retrieve the spool file message section and sync the content.
	 * This is necessary before passing the file name to external programs.
	 */
	if (USmlGetMsgFileSection(hFSpool, FMS.FSect) < 0)
		return ErrGetErrorCode();

	return MscReplaceTokens(ppszCmdTokens, FilMacroLkupProc, &FMS);
}

