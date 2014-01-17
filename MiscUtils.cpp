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
/* [i_a] */
#include "SysAssert.h"

#include "SvrDefines.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "Hash.h"
#include "MD5.h"
#include "Base64Enc.h"
#include "BuffSock.h"
#include "SSLBind.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "SSLMisc.h"
#include "MailSvr.h"
#include "MiscUtils.h"

#define IPPROP_LINE_MAX             1024
#define SERVICE_ACCEPT_TIMEOUT      4000
#define SERVICE_WAIT_SLEEP          2
#define MAX_CLIENTS_WAIT            300
#define MAX_RND_TIME                600

enum IPMapFileds {
	ipmFromIP = 0,
	ipmFromMask,
	ipmAllow,
	ipmPrecedence,

	ipmMax
};

struct FileScan {
	SysListHead FList;
	SysListHead *pPos;
};


int MscDatumAlloc(Datum *pDm, void const *pData, long lSize)
{
	if ((pDm->pData = (char *) SysAllocNZ(lSize + 1)) == NULL)
		return ErrGetErrorCode();
	memcpy(pDm->pData, pData, lSize);
	pDm->pData[lSize] = 0;
	pDm->lSize = lSize;

	return 0;
}

LstDatum *MscLstDatumAlloc(void const *pData, long lSize)
{
	LstDatum *pLDm = (LstDatum *) SysAlloc(sizeof(LstDatum));

	if (pLDm != NULL) {
		SYS_INIT_LIST_HEAD(&pLDm->LLnk);
		if (MscDatumAlloc(&pLDm->Data, pData, lSize) < 0) {
			SysFree(pLDm);
			return NULL;
		}
	}

	return pLDm;
}

int MscLstDatumAddT(SysListHead *pHead, void const *pData, long lSize)
{
	LstDatum *pLDm = MscLstDatumAlloc(pData, lSize);

	if (pLDm == NULL)
		return ErrGetErrorCode();
	SYS_LIST_ADDT(&pLDm->LLnk, pHead);

	return 0;
}

void MscFreeDatumList(SysListHead *pHead)
{
	SysListHead *pLLink;

	while ((pLLink = SYS_LIST_FIRST(pHead)) != NULL) {
		LstDatum *pLDm = SYS_LIST_ENTRY(pLLink, LstDatum, LLnk);

		SYS_LIST_DEL(pLLink);
		SysFree(pLDm->Data.pData);
		SysFree(pLDm);
	}
}

int MscUniqueFile(char const *pszDir, char *pszFilePath, int iMaxPath)
{
	/*
	 * Get thread ID and host name. We do not use atomic inc on ulUniqSeq, since
	 * collision is prevented by the thread and process IDs.
	 */
	static unsigned long ulUniqSeq = 0;
	unsigned long ulThreadID = SysGetCurrentThreadId();
	unsigned long ulProcessID = SysGetCurrentProcessId();
	SYS_INT64 iMsTime = SysMsTime();
	char szHostName[MAX_HOST_NAME] = "";

	gethostname(szHostName, sizeof(szHostName) - 1);
	SysSNPrintf(pszFilePath, iMaxPath,
		    "%s" SYS_SLASH_STR SYS_LLU_FMT ".%lx.%lx.%lx.%s",
		    pszDir, iMsTime, ulThreadID, ulProcessID, ulUniqSeq++, szHostName);

	return 0;
}

void MscSafeGetTmpFile(char *pszPath, int iMaxPath)
{
	time_t tmNow;
	unsigned long ulID;
	SYS_INT64 MsTime;
	md5_ctx_t MCtx;
	char szTempDir[SYS_MAX_PATH], szMD5[128];
	static time_t tmRnd;
	static unsigned char RndBytes[64];

	if ((tmNow = time(NULL)) > tmRnd + MAX_RND_TIME) {
		SSLGetRandBytes(RndBytes, sizeof(RndBytes));
		tmRnd = tmNow;
	}

	md5_init(&MCtx);
	MsTime = SysMsTime();
	md5_update(&MCtx, (unsigned char *) &MsTime, sizeof(MsTime));
	ulID = SysGetCurrentProcessId();
	md5_update(&MCtx, (unsigned char *) &ulID, sizeof(ulID));
	ulID = SysGetCurrentThreadId();
	md5_update(&MCtx, (unsigned char *) &ulID, sizeof(ulID));
	ulID = rand();
	md5_update(&MCtx, (unsigned char *) &ulID, sizeof(ulID));
	md5_update(&MCtx, RndBytes, sizeof(RndBytes));
	md5_final(&MCtx);
	md5_hex(MCtx.digest, szMD5);

	SysGetTempDir(szTempDir, sizeof(szTempDir));
	SysSNPrintf(pszPath, iMaxPath, "%s%s.xtmp", szTempDir, szMD5);
}

int MscRecvTextFile(char const *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
		    int (*pStopProc) (void *), void *pParam)
{
	FILE *pFile = fopen(pszFileName, "wt");
	char szBuffer[2048];

	if (pFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
		return ERR_FILE_CREATE;
	}
	while (BSckGetString(hBSock, szBuffer, sizeof(szBuffer) - 1, iTimeout) != NULL) {
		if (strcmp(szBuffer, ".") == 0)
			break;

		if (szBuffer[0] == '.')
			fprintf(pFile, "%s\n", szBuffer + 1);
		else
			fprintf(pFile, "%s\n", szBuffer);
		if (pStopProc != NULL && (*pStopProc)(pParam)) {
			fclose(pFile);
			return ErrGetErrorCode();
		}
	}
	fclose(pFile);

	return 0;
}

int MscSendTextFile(char const *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
		    int (*pStopProc) (void *), void *pParam)
{
	FILE *pFile = fopen(pszFileName, "rt");
	char szBuffer[2048];

	if (pFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName);
		return ERR_FILE_OPEN;
	}
	while (MscFGets(szBuffer, sizeof(szBuffer) - 1, pFile) != NULL) {
		if (szBuffer[0] == '.')
			for (int i = (int)strlen(szBuffer); i >= 0; i--)
				szBuffer[i + 1] = szBuffer[i];

		if (BSckSendString(hBSock, szBuffer, iTimeout) <= 0) {
			fclose(pFile);
			return ErrGetErrorCode();
		}
		if (pStopProc != NULL && (*pStopProc)(pParam)) {
			fclose(pFile);
			return ErrGetErrorCode();
		}
	}
	if (ferror(pFile)) {
		fclose(pFile);
		ErrSetErrorCode(ERR_FILE_READ, pszFileName);
		return ERR_FILE_READ;
	}
	fclose(pFile);

	return BSckSendString(hBSock, ".", iTimeout);
}

int MscSendFileCRLF(char const *pszFilePath, BSOCK_HANDLE hBSock, int iTimeout)
{
	int iLength;
	FILE *pFile;
	char szBuffer[2048];

	if ((pFile = fopen(pszFilePath, "rb")) == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFilePath);
		return ERR_FILE_OPEN;
	}
	while (fgets(szBuffer, sizeof(szBuffer) - 1, pFile) != NULL) {
		iLength = (int)strlen(szBuffer);
		if (szBuffer[iLength - 1] == '\r')
			szBuffer[iLength++] = '\n';
		else if (szBuffer[iLength - 1] == '\n' &&
			 (iLength < 2 || szBuffer[iLength - 2] != '\r')) {
			szBuffer[iLength - 1] = '\r';
			szBuffer[iLength++] = '\n';
		}
		if (BSckSendData(hBSock, szBuffer, iLength, iTimeout) < 0) {
			fclose(pFile);
			return ErrGetErrorCode();
		}
	}
	if (ferror(pFile)) {
		fclose(pFile);
		ErrSetErrorCode(ERR_FILE_READ, pszFilePath);
		return ERR_FILE_READ;
	}
	fclose(pFile);

	return 0;
}

char *MscTranslatePath(char *pszPath)
{
	for (int i = 0; pszPath[i] != '\0'; i++) {
		switch (pszPath[i]) {
		case '/':
		case '\\':
			pszPath[i] = SYS_SLASH_CHAR;
			break;
		}
	}

	return pszPath;
}

void *MscLoadFile(char const *pszFilePath, size_t *pulFileSize)
{
	size_t FileSize, RdBytes;
	SYS_OFF_T llFileSize;
	FILE *pFile;
	void *pFileData;

	if ((pFile = fopen(pszFilePath, "rb")) == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFilePath);
		return NULL;
	}
	Sys_fseek(pFile, 0, SEEK_END);
	llFileSize = Sys_ftell(pFile);
	if (llFileSize >= (1LL << (CHAR_BIT * sizeof(size_t) - 1))) {
		fclose(pFile);
		ErrSetErrorCode(ERR_TOO_BIG, pszFilePath);
		return NULL;
	}
	FileSize = (size_t) llFileSize;

	/*
	 * Alloc one extra byte to enable placing a '\0' to terminate an eventual
	 * string representation and to avoid SysAlloc() to fail if ulFileSize == 0
	 */
	if ((pFileData = SysAlloc(FileSize + 1)) == NULL) {
		fclose(pFile);
		return NULL;
	}
	rewind(pFile);
	RdBytes = fread(pFileData, FileSize, 1, pFile);
	fclose(pFile);
	if (RdBytes != FileSize) {
		SysFree(pFileData);
		ErrSetErrorCode(ERR_FILE_READ, pszFilePath);
		return NULL;
	}
	((char *) pFileData)[FileSize] = 0;
	if (pulFileSize != NULL)
		*pulFileSize = FileSize;

	return pFileData;
}

int MscLockFile(char const *pszFileName, int iMaxWait, int iWaitStep)
{
	while (iMaxWait > 0 && SysLockFile(pszFileName) < 0) {
		if (SysSleep(iWaitStep) < 0)
			return ErrGetErrorCode();
		iMaxWait -= iWaitStep;
	}

	return (iMaxWait > 0) ? 0: SysLockFile(pszFileName);
}

int MscGetTimeNbrString(char *pszTimeStr, int iStringSize, time_t tTime)
{
	if (tTime == 0)
		time(&tTime);

	struct tm tmSession;

	SysLocalTime(&tTime, &tmSession);
	SysSNPrintf(pszTimeStr, iStringSize, "%04d-%02d-%02d %02d:%02d:%02d",
		    tmSession.tm_year + 1900,
		    tmSession.tm_mon + 1,
		    tmSession.tm_mday, tmSession.tm_hour, tmSession.tm_min, tmSession.tm_sec);

	return 0;
}

int MscGetTime(struct tm &tmLocal, int &iDiffHours, int &iDiffMins, time_t tCurr)
{
	if (tCurr == 0)
		time(&tCurr);

	SysLocalTime(&tCurr, &tmLocal);

	struct tm tmTimeLOC = tmLocal;
	struct tm tmTimeGM;

	SysGMTime(&tCurr, &tmTimeGM);

	tmTimeLOC.tm_isdst = 0;
	tmTimeGM.tm_isdst = 0;

	time_t tLocal = mktime(&tmTimeLOC);
	time_t tGM = mktime(&tmTimeGM);

	int iSecsDiff = (int) difftime(tLocal, tGM);
	int iSignDiff = Sign(iSecsDiff);
	int iMinutes = Abs(iSecsDiff) / 60;

	iDiffMins = iMinutes % 60;
	iDiffHours = iSignDiff * (iMinutes / 60);

	return 0;
}

char *MscStrftime(struct tm const *ptmTime, char *pszDateStr, int iSize)
{
	static char const * const pszWDays[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static char const * const pszMonths[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	SysSNPrintf(pszDateStr, iSize, "%s, %d %s %d %02d:%02d:%02d",
		    pszWDays[ptmTime->tm_wday], ptmTime->tm_mday,
		    pszMonths[ptmTime->tm_mon], ptmTime->tm_year + 1900,
		    ptmTime->tm_hour, ptmTime->tm_min, ptmTime->tm_sec);

	return pszDateStr;
}

int MscGetTimeStr(char *pszTimeStr, int iStringSize, time_t tCurr)
{
	int iDiffHours = 0;
	int iDiffMins = 0;
	struct tm tmTime;
	char szDiffTime[128];

	MscGetTime(tmTime, iDiffHours, iDiffMins, tCurr);
	if (iDiffHours > 0)
		sprintf(szDiffTime, " +%02d%02d", iDiffHours, iDiffMins);
	else
		sprintf(szDiffTime, " -%02d%02d", -iDiffHours, iDiffMins);

	MscStrftime(&tmTime, pszTimeStr, iStringSize - (int)strlen(szDiffTime) - 1);
	strcat(pszTimeStr, szDiffTime);

	return 0;
}

int MscGetDirectorySize(char const *pszPath, bool bRecurse, SYS_OFF_T &llDirSize,
			unsigned long &ulNumFiles, int (*pFNValidate) (char const *))
{
	char szFileName[SYS_MAX_PATH];
	SYS_HANDLE hFind = SysFirstFile(pszPath, szFileName, sizeof(szFileName));

	if (hFind == SYS_INVALID_HANDLE)
		return ErrGetErrorCode();
	llDirSize = 0;
	do {
		if (SysIsDirectory(hFind)) {
			if (bRecurse && SYS_IS_VALID_FILENAME(szFileName)) {
				SYS_OFF_T llSubDirSize = 0;
				unsigned long ulSubNumFiles = 0;
				char szSubPath[SYS_MAX_PATH];

				StrSNCpy(szSubPath, pszPath);
				AppendSlash(szSubPath);
				StrSNCat(szSubPath, szFileName);

				if (MscGetDirectorySize(szSubPath, bRecurse, llSubDirSize,
							ulSubNumFiles, pFNValidate) < 0) {
					ErrorPush();
					SysFindClose(hFind);
					return ErrorPop();
				}
				ulNumFiles += ulSubNumFiles;
				llDirSize += llSubDirSize;
			}
		} else if (pFNValidate == NULL || (*pFNValidate)(szFileName)) {
			++ulNumFiles;
			llDirSize += SysGetSize(hFind);
		}
	} while (SysNextFile(hFind, szFileName, sizeof(szFileName)));
	SysFindClose(hFind);

	return 0;
}

FSCAN_HANDLE MscFirstFile(char const *pszPath, int iListDirs, char *pszFileName, int iSize)
{
	FileScan *pFS = (FileScan *) SysAlloc(sizeof(FileScan));

	if (pFS == NULL)
		return INVALID_FSCAN_HANDLE;

	SYS_INIT_LIST_HEAD(&pFS->FList);
	if (MscGetFileList(pszPath, iListDirs, &pFS->FList) < 0) {
		SysFree(pFS);
		return INVALID_FSCAN_HANDLE;
	}
	if ((pFS->pPos = SYS_LIST_FIRST(&pFS->FList)) == NULL) {
		MscFreeDatumList(&pFS->FList);
		SysFree(pFS);
		return INVALID_FSCAN_HANDLE;
	}

	LstDatum *pLDm = SYS_LIST_ENTRY(pFS->pPos, LstDatum, LLnk);

	pFS->pPos = SYS_LIST_NEXT(pFS->pPos, &pFS->FList);
	StrNCpy(pszFileName, pLDm->Data.pData, iSize);

	return (FSCAN_HANDLE) pFS;
}

int MscNextFile(FSCAN_HANDLE hFileScan, char *pszFileName, int iSize)
{
	FileScan *pFS = (FileScan *) hFileScan;

	if (pFS->pPos == NULL)
		return 0;

	LstDatum *pLDm = SYS_LIST_ENTRY(pFS->pPos, LstDatum, LLnk);

	pFS->pPos = SYS_LIST_NEXT(pFS->pPos, &pFS->FList);
	StrNCpy(pszFileName, pLDm->Data.pData, iSize);

	return 1;
}

void MscCloseFindFile(FSCAN_HANDLE hFileScan)
{
	FileScan *pFS = (FileScan *) hFileScan;

	MscFreeDatumList(&pFS->FList);
	SysFree(pFS);
}

int MscGetFileList(char const *pszPath, int iListDirs, SysListHead *pHead)
{
	char szFileName[SYS_MAX_PATH];
	SYS_HANDLE hFind = SysFirstFile(pszPath, szFileName, sizeof(szFileName));

	SYS_INIT_LIST_HEAD(pHead);
	if (hFind != SYS_INVALID_HANDLE) {
		do {
			if ((iListDirs || !SysIsDirectory(hFind)) &&
			    MscLstDatumAddT(pHead, szFileName, (int)strlen(szFileName)) < 0) {
				MscFreeDatumList(pHead);
				return ErrGetErrorCode();
			}
		} while (SysNextFile(hFind, szFileName, sizeof(szFileName)));
		SysFindClose(hFind);
	}

	return 0;
}

int MscCreateEmptyFile(char const *pszFileName)
{
	FILE *pFile = fopen(pszFileName, "wb");

	if (pFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
		return ERR_FILE_CREATE;
	}
	fclose(pFile);

	return 0;
}

int MscClearDirectory(char const *pszPath, int iRecurseSubs)
{
	SysListHead FList;
	char szFileName[SYS_MAX_PATH];
	SYS_HANDLE hFind = SysFirstFile(pszPath, szFileName, sizeof(szFileName));

	if (hFind == SYS_INVALID_HANDLE)
		return 0;
	SYS_INIT_LIST_HEAD(&FList);
	do {
		if (SysIsDirectory(hFind)) {
			if (iRecurseSubs && SYS_IS_VALID_FILENAME(szFileName)) {
				char szSubPath[SYS_MAX_PATH];

				StrSNCpy(szSubPath, pszPath);
				AppendSlash(szSubPath);
				StrSNCat(szSubPath, szFileName);
				if (MscClearDirectory(szSubPath, iRecurseSubs) < 0 ||
				    SysRemoveDir(szSubPath) < 0) {
					MscFreeDatumList(&FList);
					return ErrGetErrorCode();
				}
			}
		} else {
			if (MscLstDatumAddT(&FList, szFileName, (int)strlen(szFileName)) < 0) {
				MscFreeDatumList(&FList);
				return ErrGetErrorCode();
			}
		}

	} while (SysNextFile(hFind, szFileName, sizeof(szFileName)));
	SysFindClose(hFind);

	SysListHead *pPos;

	for (pPos = SYS_LIST_FIRST(&FList); pPos != NULL;
	     pPos = SYS_LIST_NEXT(pPos, &FList)) {
		LstDatum *pLDm = SYS_LIST_ENTRY(pPos, LstDatum, LLnk);

		StrSNCpy(szFileName, pszPath);
		AppendSlash(szFileName);
		StrSNCat(szFileName, pLDm->Data.pData);
		if (SysRemove(szFileName) < 0 && SysExistFile(szFileName)) {
			MscFreeDatumList(&FList);
			return ErrGetErrorCode();
		}
	}
	MscFreeDatumList(&FList);

	return 0;
}

static int MscCopyFileLL(char const *pszCopyTo, char const *pszCopyFrom,
			 char const *pszMode)
{
	size_t RdBytes;
	FILE *pFileIn, *pFileOut;
	char szBuffer[2048];

	if ((pFileIn = fopen(pszCopyFrom, "rb")) == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszCopyFrom);
		return ERR_FILE_OPEN;
	}
	if ((pFileOut = fopen(pszCopyTo, pszMode)) == NULL) {
		fclose(pFileIn);
		ErrSetErrorCode(ERR_FILE_CREATE, pszCopyTo);
		return ERR_FILE_CREATE;
	}
	do {
		if ((RdBytes = fread(szBuffer, 1, sizeof(szBuffer), pFileIn)) > 0) {
			if (fwrite(szBuffer, 1, RdBytes, pFileOut) != RdBytes) {
				fclose(pFileOut);
				fclose(pFileIn);
				SysRemove(pszCopyTo);

				ErrSetErrorCode(ERR_FILE_WRITE, pszCopyTo);
				return ERR_FILE_WRITE;
			}
		}
	} while (RdBytes == sizeof(szBuffer));
	fclose(pFileOut);
	fclose(pFileIn);

	return 0;
}

int MscCopyFile(char const *pszCopyTo, char const *pszCopyFrom)
{
	return MscCopyFileLL(pszCopyTo, pszCopyFrom, "wb");
}

int MscAppendFile(char const *pszCopyTo, char const *pszCopyFrom)
{
	return MscCopyFileLL(pszCopyTo, pszCopyFrom, "a+b");
}

int MscCopyFile(FILE *pFileOut, FILE *pFileIn, SYS_OFF_T llBaseOffset,
		SYS_OFF_T llCopySize)
{
	size_t RdBytes, ToRead;
	SYS_OFF_T llFileSize;
	char szBuffer[2048];

	if (llBaseOffset == (SYS_OFF_T) -1)
		llBaseOffset = Sys_ftell(pFileIn);
	Sys_fseek(pFileIn, 0, SEEK_END);
	llFileSize = Sys_ftell(pFileIn);
	if (llCopySize == (SYS_OFF_T) -1)
		llCopySize = llFileSize - llBaseOffset;
	else
		llCopySize = Min(llCopySize, llFileSize - llBaseOffset);
	Sys_fseek(pFileIn, llBaseOffset, SEEK_SET);
	while (llCopySize > 0) {
		ToRead = (size_t) Min(llCopySize, sizeof(szBuffer));
		if ((RdBytes = fread(szBuffer, 1, ToRead, pFileIn)) > 0) {
			if (fwrite(szBuffer, 1, RdBytes, pFileOut) != RdBytes) {
				ErrSetErrorCode(ERR_FILE_WRITE);
				return ERR_FILE_WRITE;
			}
			llCopySize -= RdBytes;
		}
		if (RdBytes != ToRead) {
			ErrSetErrorCode(ERR_FILE_READ);
			return ERR_FILE_READ;
		}
	}

	return 0;
}

int MscDos2UnixFile(FILE *pFileOut, FILE *pFileIn)
{
	int iLength;
	char szBuffer[2048];

	while (fgets(szBuffer, sizeof(szBuffer), pFileIn) != NULL) {
		iLength = (int)strlen(szBuffer);
		if (szBuffer[iLength - 1] == '\r')
			szBuffer[iLength - 1] = '\n';
		if (fwrite(szBuffer, 1, iLength, pFileOut) != iLength) {
			ErrSetErrorCode(ERR_FILE_WRITE);
			return ERR_FILE_WRITE;
		}
	}
	if (ferror(pFileIn)) {
		ErrSetErrorCode(ERR_FILE_READ);
		return ERR_FILE_READ;
	}

	return 0;
}

int MscMoveFile(char const *pszOldName, char const *pszNewName)
{
	if (MscCopyFile(pszNewName, pszOldName) < 0)
		return ErrGetErrorCode();

	return SysRemove(pszOldName);
}

char *MscGetString(FILE *pFile, char *pszBuffer, int iMaxChars, int *piGotNL)
{
	size_t iLength;

	ASSERT(pFile);
	ASSERT(pszBuffer);
	ASSERT(iMaxChars > 1);
	pszBuffer[0] = 0; /* [i_a] */
	if (fgets(pszBuffer, iMaxChars, pFile) == NULL)
		return NULL;
	iLength = strlen(pszBuffer);
	// TODO: remove the loop: N^2 search through text; needless.
	if (piGotNL != NULL)
		*piGotNL = (iLength > 0 &&
			    strchr("\r\n", pszBuffer[iLength - 1]) != NULL);
	for (; iLength > 0 && strchr("\r\n", pszBuffer[iLength - 1]) != NULL;
	     iLength--);
	pszBuffer[iLength] = '\0';

	return pszBuffer;
}

char *MscFGets(char *pszLine, int iLineSize, FILE *pFile)
{
	return MscGetString(pFile, pszLine, iLineSize, NULL);
}

char *MscGetConfigLine(char *pszLine, int iLineSize, FILE *pFile, bool bSkipComments)
{
	while (MscFGets(pszLine, iLineSize, pFile) != NULL) {
		if (strlen(pszLine) > 0 &&
		    (!bSkipComments || pszLine[0] != TAB_COMMENT_CHAR))
			return pszLine;
	}

	ErrSetErrorCode(ERR_FILE_EOF);
	return NULL;
}

int MscGetPeerHost(SYS_SOCKET SockFD, char *pszFQDN, int iSize)
{
	SYS_INET_ADDR PeerInfo;

	ZeroData(PeerInfo); /* [i_a] */

	if (SysGetPeerInfo(SockFD, PeerInfo) < 0)
		return ErrGetErrorCode();

	return SysGetHostByAddr(PeerInfo, pszFQDN, iSize);
}

int MscGetSockHost(SYS_SOCKET SockFD, char *pszFQDN, int iSize)
{
	SYS_INET_ADDR SockInfo;

	ZeroData(SockInfo); /* [i_a] */

	if (SysGetSockInfo(SockFD, SockInfo) < 0)
		return ErrGetErrorCode();

	return SysGetHostByAddr(SockInfo, pszFQDN, iSize);
}

int MscGetServerAddress(char const *pszServer, SYS_INET_ADDR &SvrAddr, int iPortNo)
{
	char szServer[MAX_HOST_NAME];

	if (MscSplitAddressPort(pszServer, szServer, iPortNo, iPortNo) < 0 ||
	    SysGetHostByName(szServer, iAddrFamily, SvrAddr) < 0 ||
	    SysSetAddrPort(SvrAddr, iPortNo) < 0)
		return ErrGetErrorCode();

	return 0;
}

int MscSplitFQDN(char const *pszFQDN, char *pszHost, int iHSize,
		 char *pszDomain, int iDSize)
{
	char const *pszDot = strchr(pszFQDN, '.');

	if (pszDot == NULL) {
		if (pszHost != NULL)
			StrNCpy(pszHost, pszFQDN, iHSize);
		if (pszDomain != NULL)
			SetEmptyString(pszDomain);
	} else {
		if (pszHost != NULL) {
			int iHostLength = (int) (pszDot - pszFQDN);

			if (iHostLength >= iHSize)
				iHostLength = iHSize - 1;
			Cpy2Sz(pszHost, pszFQDN, iHostLength);
		}
		if (pszDomain != NULL) {
			StrNCpy(pszDomain, pszDot + 1, iDSize);
			DelFinalChar(pszDomain, '.');
		}
	}

	return 0;
}

char *MscLogFilePath(char const *pszLogFile, char *pszLogFilePath)
{
	time_t tCurrent;

	time(&tCurrent);

	long lRotStep = (unsigned long) (3600L * iLogRotateHours);
	long lTimeZone = SysGetTimeZone();
	long lDayLight = SysGetDayLight();
	time_t tLogFileTime = (time_t) (NbrFloor((SYS_INT64) tCurrent - lTimeZone + lDayLight,
						 lRotStep) + lTimeZone - lDayLight);
	struct tm tmLocTime;
	char szLogsDir[SYS_MAX_PATH];

	SysLocalTime(&tLogFileTime, &tmLocTime);

	SvrGetLogsDir(szLogsDir, sizeof(szLogsDir));
	AppendSlash(szLogsDir);

	sprintf(pszLogFilePath, "%s%s-%04d%02d%02d%02d%02d",
		szLogsDir, pszLogFile,
		tmLocTime.tm_year + 1900,
		tmLocTime.tm_mon + 1, tmLocTime.tm_mday, tmLocTime.tm_hour, tmLocTime.tm_min);

	return pszLogFilePath;
}

int MscFileLog(char const *pszLogFile, char const *pszFormat, ...)
{
	char szLogFilePath[SYS_MAX_PATH];

	MscLogFilePath(pszLogFile, szLogFilePath);

	FILE *pLogFile = fopen(szLogFilePath, "a+t");

	if (pLogFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, szLogFilePath); /* [i_a] */
		return ERR_FILE_OPEN;
	}

	va_list Args;

	va_start(Args, pszFormat);
	vfprintf(pLogFile, pszFormat, Args);
	va_end(Args);
	fclose(pLogFile);

	return 0;
}

int MscSplitPath(char const *pszFilePath, char *pszDir, int iDSize,
		 char *pszFName, int iFSize, char *pszExt, int iESize)
{
	char const *pszSlash = strrchr(pszFilePath, SYS_SLASH_CHAR);
	char const *pszFile = NULL;

	if (pszSlash != NULL) {
		pszFile = pszSlash + 1;
		if (pszDir != NULL) {
			int iDirLength = (int) (pszFile - pszFilePath);

			if (iDirLength >= iDSize)
				iDirLength = iDSize - 1;
			Cpy2Sz(pszDir, pszFilePath, iDirLength);
		}
	} else {
		pszFile = pszFilePath;
		if (pszDir != NULL)
			SetEmptyString(pszDir);
	}

	char const *pszDot = strrchr(pszFile, '.');

	if (pszDot != NULL) {
		if (pszFName != NULL) {
			int iNameLength = (int) (pszDot - pszFile);

			if (iNameLength >= iFSize)
				iNameLength = iFSize - 1;
			Cpy2Sz(pszFName, pszFile, iNameLength);
		}
		if (pszExt != NULL)
			StrNCpy(pszExt, pszDot, iESize);
	} else {
		if (pszFName != NULL)
			StrNCpy(pszFName, pszFile, iFSize);
		if (pszExt != NULL)
			SetEmptyString(pszExt);
	}

	return 0;
}

int MscGetFileName(char const *pszFilePath, char *pszFileName)
{
	char const *pszSlash = strrchr(pszFilePath, SYS_SLASH_CHAR);

	strcpy(pszFileName, (pszSlash != NULL) ? (pszSlash + 1): pszFilePath);

	return 0;
}

int MscCreateClientSocket(char const *pszServer, int iPortNo, int iSockType,
			  SYS_SOCKET *pSockFD, SYS_INET_ADDR *pSvrAddr,
			  SYS_INET_ADDR *pSockAddr, int iTimeout)
{
	SYS_INET_ADDR SvrAddr;

	ZeroData(SvrAddr); /* [i_a] */

	if (MscGetServerAddress(pszServer, SvrAddr, iPortNo) < 0)
		return ErrGetErrorCode();

	SYS_SOCKET SockFD = SysCreateSocket(SysGetAddrFamily(SvrAddr), iSockType, 0);

	if (SockFD == SYS_INVALID_SOCKET)
		return ErrGetErrorCode();

	if (SysConnect(SockFD, &SvrAddr, iTimeout) < 0) {
		ErrorPush();
		SysCloseSocket(SockFD);
		return ErrorPop();
	}

	SYS_INET_ADDR SockAddr;

	ZeroData(SockAddr); /* [i_a] */

	if (SysGetSockInfo(SockFD, SockAddr) < 0) {
		ErrorPush();
		SysCloseSocket(SockFD);
		return ErrorPop();
	}
	*pSockFD = SockFD;
	if (pSvrAddr != NULL)
		*pSvrAddr = SvrAddr;
	if (pSockAddr != NULL)
		*pSockAddr = SockAddr;

	return 0;
}

int MscCreateServerSockets(int iNumAddr, SYS_INET_ADDR const *pSvrAddr, int iFamily,
			   int iPortNo, int iListenSize, SYS_SOCKET *pSockFDs,
			   int &iNumSockFDs)
{
	if (iNumAddr == 0) {
		SYS_SOCKET SvrSockFD = SysCreateSocket(iFamily, SOCK_STREAM, 0);

		if (SvrSockFD == SYS_INVALID_SOCKET)
			return ErrGetErrorCode();

		SYS_INET_ADDR InSvrAddr;

		ZeroData(InSvrAddr); /* [i_a] */

		if (SysInetAnySetup(InSvrAddr, iFamily, iPortNo) < 0 ||
		    SysBindSocket(SvrSockFD, &InSvrAddr) < 0) {
			ErrorPush();
			SysCloseSocket(SvrSockFD);
			return ErrorPop();
		}
		SysListenSocket(SvrSockFD, iListenSize);

		*pSockFDs = SvrSockFD;
		iNumSockFDs = 1;
	} else {
		iNumSockFDs = 0;

		for (int i = 0; i < iNumAddr; i++) {
			SYS_SOCKET SvrSockFD = SysCreateSocket(SysGetAddrFamily(pSvrAddr[i]),
							       SOCK_STREAM, 0);

			if (SvrSockFD == SYS_INVALID_SOCKET) {
				ErrorPush();
				for (--iNumSockFDs; iNumSockFDs >= 0; iNumSockFDs--)
					SysCloseSocket(pSockFDs[iNumSockFDs]);
				return ErrorPop();
			}

			SYS_INET_ADDR InSvrAddr = pSvrAddr[i];

			if (SysGetAddrPort(InSvrAddr) == 0)
				SysSetAddrPort(InSvrAddr, iPortNo);
			if (SysBindSocket(SvrSockFD, &InSvrAddr) < 0) {
				ErrorPush();
				SysCloseSocket(SvrSockFD);
				for (--iNumSockFDs; iNumSockFDs >= 0; iNumSockFDs--)
					SysCloseSocket(pSockFDs[iNumSockFDs]);
				return ErrorPop();
			}
			SysListenSocket(SvrSockFD, iListenSize);

			pSockFDs[iNumSockFDs++] = SvrSockFD;
		}
	}

	return 0;
}

int MscGetMaxSockFD(SYS_SOCKET const *pSockFDs, int iNumSockFDs)
{
	int iMaxFD = 0;

	for (int i = 0; i < iNumSockFDs; i++)
		if (iMaxFD < (int) pSockFDs[i])
			iMaxFD = (int) pSockFDs[i];

	return iMaxFD;
}

int MscAcceptServerConnection(SYS_SOCKET const *pSockFDs, int iNumSockFDs,
			      SYS_SOCKET *pConnSockFD, int &iNumConnSockFD, int iTimeout)
{
	int i;
	SYS_fd_set fdReadSet;

	ZeroData(fdReadSet);
	SYS_FD_ZERO(&fdReadSet);

	for (i = 0; i < iNumSockFDs; i++)
		SYS_FD_SET(pSockFDs[i], &fdReadSet);

	int iSelectResult = SysSelect(MscGetMaxSockFD(pSockFDs, iNumSockFDs),
				      &fdReadSet, NULL, NULL, iTimeout);

	if (iSelectResult < 0)
		return ErrGetErrorCode();

	iNumConnSockFD = 0;
	for (i = 0; i < iNumSockFDs; i++) {
		if (SYS_FD_ISSET(pSockFDs[i], &fdReadSet)) {
			SYS_INET_ADDR ConnAddr;

			ZeroData(ConnAddr);

			SYS_SOCKET ConnSockFD = SysAccept(pSockFDs[i], &ConnAddr,
							  iTimeout);

			if (ConnSockFD != SYS_INVALID_SOCKET)
				pConnSockFD[iNumConnSockFD++] = ConnSockFD;
		}
	}

	return 0;
}

int MscLoadAddressFilter(char const *const *ppszFilter, int iNumTokens, AddressFilter &AF)
{
	int iASize;
	void const *pAData;
	char const *pszMask;
	SYS_INET_ADDR Mask;

	ZeroData(Mask); /* [i_a] */
	ZeroData(AF);

	if ((pszMask = strchr(ppszFilter[0], '/')) != NULL) {
		int i;
		int iAddrLength = (int) (pszMask - ppszFilter[0]);
		int iMaskBits = atoi(pszMask + 1);
		char szFilter[128];

		iAddrLength = Min(iAddrLength, sizeof(szFilter) - 1);
		Cpy2Sz(szFilter, ppszFilter[0], iAddrLength);
		if (SysGetHostByName(szFilter, -1, AF.Addr) < 0)
			return ErrGetErrorCode();

		ZeroData(AF.Mask);
		iMaskBits = Min(iMaskBits, CHAR_BIT * (int) sizeof(AF.Mask));
		for (i = 0; (i + CHAR_BIT) <= iMaskBits; i += CHAR_BIT)
			AF.Mask[i / CHAR_BIT] = 0xff;
		if (i < iMaskBits)
			AF.Mask[i / CHAR_BIT] = (SYS_UINT8)
				(((1 << (iMaskBits - i)) - 1) << (CHAR_BIT - iMaskBits + i));
	} else if (iNumTokens > 1) {
		/*
		 * This is for the old IPV4 representation. They both must
		 * be two IPV4 addresses (first network, second netmask).
		 */
		if (SysGetHostByName(ppszFilter[0], AF_INET, AF.Addr) < 0 ||
		    SysGetHostByName(ppszFilter[1], AF_INET, Mask) < 0 ||
		    (pAData = SysInetAddrData(Mask, &iASize)) == NULL)
			return ErrGetErrorCode();
		ZeroData(AF.Mask);
		memcpy(AF.Mask, pAData, Min(iASize, sizeof(AF.Mask)));
	} else {
		ErrSetErrorCode(ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}

	return 0;
}

int MscAddressMatch(AddressFilter const &AF, SYS_INET_ADDR const &TestAddr)
{
	return SysInetAddrMatch(AF.Addr, AF.Mask, sizeof(AF.Mask),
				TestAddr);
}

int MscCheckAllowedIP(char const *pszMapFile, const SYS_INET_ADDR &PeerInfo, bool bDefault)
{
	FILE *pMapFile = fopen(pszMapFile, "rt");

	if (pMapFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszMapFile);
		return ERR_FILE_OPEN;
	}

	bool bAllow = bDefault;
	int iPrecedence = -1, iFieldsCount;
	AddressFilter AF;
	char szMapLine[512];

	while (MscGetConfigLine(szMapLine, sizeof(szMapLine) - 1, pMapFile) != NULL) {
		char **ppszStrings = StrGetTabLineStrings(szMapLine);

		if (ppszStrings == NULL)
			continue;

		iFieldsCount = StrStringsCount(ppszStrings);
		if (iFieldsCount >= ipmMax &&
		    MscLoadAddressFilter(ppszStrings, iFieldsCount, AF) == 0 &&
		    MscAddressMatch(AF, PeerInfo)) {
			int iCurrPrecedence = atoi(ppszStrings[ipmPrecedence]);

			if (iCurrPrecedence >= iPrecedence) {
				iPrecedence = iCurrPrecedence;

				bAllow = (stricmp(ppszStrings[ipmAllow], "ALLOW") == 0) ? true: false;
			}
		}
		StrFreeStrings(ppszStrings);
	}
	fclose(pMapFile);

	if (!bAllow) {
		ErrSetErrorCode(ERR_IP_NOT_ALLOWED);
		return ERR_IP_NOT_ALLOWED;
	}

	return 0;
}

char **MscGetIPProperties(char const *pszFileName, const SYS_INET_ADDR *pPeerInfo)
{
	FILE *pFile = fopen(pszFileName, "rt");

	if (pFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName); /* [i_a] */
		return NULL;
	}

	int iFieldsCount;
	AddressFilter AFPeer;
	char szLine[IPPROP_LINE_MAX];

	while (MscGetConfigLine(szLine, sizeof(szLine) - 1, pFile) != NULL) {
		char **ppszTokens = StrGetTabLineStrings(szLine);

		if (ppszTokens == NULL)
			continue;
		iFieldsCount = StrStringsCount(ppszTokens);
		if (iFieldsCount >= 1 &&
		    MscLoadAddressFilter(&ppszTokens[0], 1, AFPeer) == 0 &&
		    MscAddressMatch(AFPeer, *pPeerInfo)) {
			fclose(pFile);
			return ppszTokens;
		}
		StrFreeStrings(ppszTokens);
	}
	fclose(pFile);

	ErrSetErrorCode(ERR_NOT_FOUND);

	return NULL;
}

int MscHostSubMatch(char const *pszHostName, char const *pszHostMatch)
{
	int iMatchResult = 0, iHLen, iMLen;

	if ((iHLen = (int)strlen(pszHostMatch)) > 0 && pszHostMatch[iHLen - 1] == '.')
		iHLen--;
	if (*pszHostMatch == '.') {
		pszHostMatch++;
		if ((iMLen = (int)strlen(pszHostMatch)) <= iHLen)
			iMatchResult = strnicmp(pszHostName + iMLen - iHLen,
						pszHostMatch, iHLen) == 0;
	} else
		iMatchResult = strnicmp(pszHostName, pszHostMatch, iHLen) == 0;

	return iMatchResult;
}

char **MscGetHNProperties(char const *pszFileName, char const *pszHostName)
{
	int iFieldsCount;
	FILE *pFile = fopen(pszFileName, "rt");
	char szLine[IPPROP_LINE_MAX];

	if (pFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszFileName); /* [i_a] */
		return NULL;
	}

	while (MscGetConfigLine(szLine, sizeof(szLine) - 1, pFile) != NULL) {
		char **ppszTokens = StrGetTabLineStrings(szLine);

		if (ppszTokens == NULL)
			continue;
		iFieldsCount = StrStringsCount(ppszTokens);
		if (iFieldsCount >= 1 &&
		    MscHostSubMatch(pszHostName, ppszTokens[0])) {
			fclose(pFile);
			return ppszTokens;
		}
		StrFreeStrings(ppszTokens);
	}
	fclose(pFile);

	ErrSetErrorCode(ERR_NOT_FOUND);

	return NULL;
}

int MscMD5Authenticate(char const *pszPassword, char const *pszTimeStamp, char const *pszDigest)
{
	char *pszHash = StrSprint("%s%s", pszTimeStamp, pszPassword);
	char szMD5[128];

	if (pszHash == NULL)
		return ErrGetErrorCode();
	do_md5_string(pszHash, (int)strlen(pszHash), szMD5);
	SysFree(pszHash);
	if (stricmp(pszDigest, szMD5) != 0) {
		ErrSetErrorCode(ERR_MD5_AUTH_FAILED);
		return ERR_MD5_AUTH_FAILED;
	}

	return 0;
}

char *MscExtractServerTimeStamp(char const *pszResponse, char *pszTimeStamp, int iMaxTimeStamp)
{
	char const *pszStartTS, *pszEndTS;

	if ((pszStartTS = strchr(pszResponse, '<')) == NULL ||
	    (pszEndTS = strchr(pszStartTS + 1, '>')) == NULL)
		return NULL;

	int iLengthTS = (int) (pszEndTS - pszStartTS) + 1;

	iLengthTS = Min(iLengthTS, iMaxTimeStamp - 1);
	Cpy2Sz(pszTimeStamp, pszStartTS, iLengthTS);

	return pszTimeStamp;
}

int MscRootedName(char const *pszHostName)
{
	char const *pszDot = strrchr(pszHostName, '.');

	return (pszDot == NULL) ? 0: ((strlen(pszDot) == 0) ? 1: 0);
}

int MscCramMD5(char const *pszSecret, char const *pszChallenge, char *pszDigest)
{
	int iLenght = (int) strlen(pszSecret);
	md5_ctx_t ctx;
	unsigned char isecret[64];
	unsigned char osecret[64];
	unsigned char md5secret[MD5_DIGEST_LEN];

	if (iLenght > 64) {
		md5_init(&ctx);
		md5_update(&ctx, (unsigned char const *) pszSecret, iLenght);
		md5_final(&ctx);

		memcpy(md5secret, ctx.digest, MD5_DIGEST_LEN);
		pszSecret = (char const *) md5secret;
		iLenght = 16;
	}

	ZeroData(isecret);
	memcpy(isecret, pszSecret, iLenght);

	ZeroData(osecret);
	memcpy(osecret, pszSecret, iLenght);

	for (int i = 0; i < 64; i++) {
		isecret[i] ^= 0x36;
		osecret[i] ^= 0x5c;
	}

	md5_init(&ctx);
	md5_update(&ctx, isecret, 64);
	md5_update(&ctx, (unsigned char *) pszChallenge, (int) strlen(pszChallenge));
	md5_final(&ctx);
	memcpy(md5secret, ctx.digest, MD5_DIGEST_LEN);

	md5_init(&ctx);
	md5_update(&ctx, osecret, 64);
	md5_update(&ctx, md5secret, MD5_DIGEST_LEN);
	md5_final(&ctx);

	md5_hex(ctx.digest, pszDigest);

	return 0;
}

/*
 * NOTE: This is used by TabIndex.cpp to compute the hash of its indexes.
 *       If this function gets changed in any way, the TAB_INDEX_CURR_VERSION
 *       value in TabIndex.cpp must be bumped to reflect a different file format.
 */
unsigned long MscHashString(char const *pszBuffer, size_t iLength, unsigned long ulHashInit)
{
	unsigned long ulHashVal = ulHashInit;

	while (iLength > 0) {
		--iLength;
		ulHashVal += *(unsigned char const *) pszBuffer++;
		ulHashVal += (ulHashVal << 10);
		ulHashVal ^= (ulHashVal >> 6);
	}
	ulHashVal += (ulHashVal << 3);
	ulHashVal ^= (ulHashVal >> 11);
	ulHashVal += (ulHashVal << 15);

	return ulHashVal;
}

int MscSplitAddressPort(char const *pszConnSpec, char *pszAddress, int &iPortNo, int iDefPortNo)
{
	char const *pszEnd = NULL, *pszPort;

	iPortNo = iDefPortNo;

	if (*pszConnSpec == '[') {
		pszConnSpec++;
		if ((pszEnd = strchr(pszConnSpec, ']')) == NULL) {
			ErrSetErrorCode(ERR_BAD_SERVER_ADDR);
			return ERR_BAD_SERVER_ADDR;
		}
		if ((pszPort = strrchr(pszEnd + 1, '|')) == NULL &&
		    (pszPort = strrchr(pszEnd + 1, ':')) == NULL)
			pszPort = strrchr(pszEnd + 1, ';');
		if (pszPort != NULL)
			iPortNo = atoi(pszPort + 1);
	} else {
		if ((pszPort = strrchr(pszConnSpec, '|')) == NULL &&
		    (pszPort = strrchr(pszConnSpec, ':')) == NULL)
			pszPort = strrchr(pszConnSpec, ';');
		if ((pszEnd = pszPort) != NULL)
			iPortNo = atoi(pszPort + 1);
	}
	if (pszEnd != NULL) {
		int iAddrLen = Min((int) (pszEnd - pszConnSpec), MAX_HOST_NAME - 1);

		Cpy2Sz(pszAddress, pszConnSpec, iAddrLen);
	} else
		strncpy(pszAddress, pszConnSpec, MAX_HOST_NAME - 1);

	return 0;
}

SYS_UINT16 MscReadUint16(void const *pData)
{
	SYS_UINT16 uValue;

	/*
	 * Reading possibly unaligned data ...
	 */
	memcpy(&uValue, pData, sizeof(uValue));

	return uValue;
}

SYS_UINT32 MscReadUint32(void const *pData)
{
	SYS_UINT32 uValue;

	/*
	 * Reading possibly unaligned data ...
	 */
	memcpy(&uValue, pData, sizeof(uValue));

	return uValue;
}

SYS_UINT64 MscReadUint64(void const *pData)
{
	SYS_UINT64 uValue;

	/*
	 * Reading possibly unaligned data ...
	 */
	memcpy(&uValue, pData, sizeof(uValue));

	return uValue;
}

void *MscWriteUint16(void *pData, SYS_UINT16 uValue)
{
	return memcpy(pData, &uValue, sizeof(uValue));
}

void *MscWriteUint32(void *pData, SYS_UINT32 uValue)
{
	return memcpy(pData, &uValue, sizeof(uValue));
}

void *MscWriteUint64(void *pData, SYS_UINT64 uValue)
{
	return memcpy(pData, &uValue, sizeof(uValue));
}

int MscCmdStringCheck(char const *pszString)
{
	for (; *pszString != '\0'; pszString++)
		if (*((unsigned char const *) pszString) > 127) {
			ErrSetErrorCode(ERR_BAD_CMDSTR_CHARS);
			return ERR_BAD_CMDSTR_CHARS;
		}

	return 0;
}

int MscGetSectionSize(FileSection const *pFS, SYS_OFF_T *pllSize)
{
	if (pFS->llEndOffset == (SYS_OFF_T) -1) {
		SYS_FILE_INFO FI;

		if (SysGetFileInfo(pFS->szFilePath, FI) < 0)
			return ErrGetErrorCode();

		*pllSize = FI.llSize - pFS->llStartOffset;
	} else
		*pllSize = pFS->llEndOffset - pFS->llStartOffset;

	return 0;
}

int MscIsIPDomain(char const *pszDomain, char *pszIP, int iIPSize)
{
	int i, j;
	char const *pszBase = pszDomain;

	if (*pszDomain == '[')
		pszDomain++;
	for (j = 4; j >= 1; j--) {
		for (i = 0; i < 3; i++)
			if (!isdigit(pszDomain[i]))
				break;
		if (!i || atoi(pszDomain) > 255)
			return 0;
		pszDomain += i;
		if (j > 1 && *pszDomain++ != '.')
			return 0;
	}
	if ((*pszBase == '[' && *pszDomain != ']') ||
	    (*pszBase != '[' && *pszDomain != '\0'))
		return 0;
	if (pszIP != NULL) {
		if (*pszBase == '[')
			pszBase++;
		i = (int) (pszDomain - pszBase);
		i = Min(i, iIPSize - 1);
		Cpy2Sz(pszIP, pszBase, i);
	}

	return 1;
}

static char *MscMacroReplace(char const *pszIn, int *piSize,
			     char *(*pLkupProc)(void *, char const *, int), void *pPriv)
{
	char *pszLkup;

	if (strncmp(pszIn, "@@", 2) != 0)
		return StrMacSubst(pszIn, piSize, pLkupProc, pPriv);
	if ((pszLkup = (*pLkupProc)(pPriv, pszIn + 2, (int)strlen(pszIn + 2))) == NULL)
		return NULL;
	if (piSize != NULL)
		*piSize = (int)strlen(pszLkup);

	return pszLkup;
}

int MscReplaceTokens(char **ppszTokens, char *(*pLkupProc)(void *, char const *, int),
		     void *pPriv)
{
	int i;

	for (i = 0; ppszTokens[i] != NULL; i++) {
		char *pszRepl = MscMacroReplace(ppszTokens[i], NULL, pLkupProc, pPriv);

		if (pszRepl == NULL)
			return ErrGetErrorCode();
		SysFree(ppszTokens[i]);
		ppszTokens[i] = pszRepl;
	}

	return 0;
}

int MscGetAddrString(SYS_INET_ADDR const &AddrInfo, char *pszAStr, int iSize)
{
	char szIP[128] = "";

	SysInetNToA(AddrInfo, szIP, sizeof(szIP));
	SysSNPrintf(pszAStr, iSize, "[%s]:%d", szIP, SysGetAddrPort(AddrInfo));

	return 0;
}

unsigned int MscServiceThread(void *pThreadData)
{
	ThreadConfig const *pThCfg = (ThreadConfig const *) pThreadData;

	SysLogMessage(LOG_LEV_MESSAGE, "%s started\n", pThCfg->pszName);

	for (;;) {
		int iNumConnSockFD = 0;
		SYS_SOCKET ConnSockFD[MAX_ACCEPT_ADDRESSES];

		if (MscAcceptServerConnection(pThCfg->SockFDs, pThCfg->iNumSockFDs, ConnSockFD,
					      iNumConnSockFD, SERVICE_ACCEPT_TIMEOUT) < 0) {
			if (pThCfg->ulFlags & THCF_SHUTDOWN)
				break;
			continue;
		}
		for (int i = 0; i < iNumConnSockFD; i++) {
			SYS_THREAD hClientThread = SYS_INVALID_THREAD;
			ThreadCreateCtx *pThCtx = (ThreadCreateCtx *) SysAlloc(sizeof(ThreadCreateCtx));

			if (pThCtx != NULL) {
				pThCtx->SockFD = ConnSockFD[i];
				pThCtx->pThCfg = pThCfg;
				hClientThread = SysCreateThread(pThCfg->pfThreadProc, pThCtx);
			}
			if (hClientThread != SYS_INVALID_THREAD)
				SysCloseThread(hClientThread, 0);
			else {
				SysFree(pThCtx);
				SysCloseSocket(ConnSockFD[i]);
			}
		}
	}

	/* Wait for client completion */
	for (int iTotalWait = 0; iTotalWait < MAX_CLIENTS_WAIT;
	     iTotalWait += SERVICE_WAIT_SLEEP) {
		if ((*pThCfg->pfThreadCnt)(pThCfg) == 0)
			break;
		SysSleep(SERVICE_WAIT_SLEEP);
	}
	SysLogMessage(LOG_LEV_MESSAGE, "%s stopped\n", pThCfg->pszName);

	return 0;
}

int MscSslEnvCB(void *pPrivate, int iID, void const *pData)
{
	SslBindEnv *pSslE = (SslBindEnv *) pPrivate;

	return 0;
}

int MscParseOptions(char const *pszOpts, int (*pfAssign)(void *, char const *, char const *),
		    void *pPrivate)
{
	char **ppszToks = StrTokenize(pszOpts, ",\r\n");

	if (ppszToks == NULL)
		return ErrGetErrorCode();
	for (int i = 0; ppszToks[i] != NULL; i++) {
		char *pszName = ppszToks[i];
		char *pszValue = strchr(pszName, '=');

		if (pszValue != NULL)
			*pszValue++ = '\0';
		if ((*pfAssign)(pPrivate, pszName, pszValue) < 0) {
			StrFreeStrings(ppszToks);
			return ErrGetErrorCode();
		}
	}
	StrFreeStrings(ppszToks);

	return 0;
}

void MscSysFreeCB(void *pPrivate, void *pData)
{
	SysFree(pData);
}

void MscRandomizeStringsOrder(char **ppszStrings)
{
	int i, iCount = StrStringsCount(ppszStrings);

	for (i = 0; i < iCount - 1; i++) {
		int iChoice = rand() % (iCount - i);
		char *pszTmp = ppszStrings[i];

		ppszStrings[i] = ppszStrings[i + iChoice];
		ppszStrings[i + iChoice] = pszTmp;
	}
}

unsigned long MscStringHashCB(void *pPrivate, HashDatum const *pDatum)
{
	char const *pszStr = (char const *) pDatum->pData;

	return MscHashString(pszStr, strlen(pszStr));
}

int MscStringCompareCB(void *pPrivate, HashDatum const *pDatum1,
		       HashDatum const *pDatum2)
{
	return strcmp((char const *) pDatum1->pData, (char const *) pDatum2->pData);
}

