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
#include "AppDefines.h"

#if defined(WIN32)

#if 0
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <direct.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#endif


/* [i_a]
#define SYS_SLASH_CHAR              '\\'
#define SYS_SLASH_STR               "\\"
#define SYS_MAX_PATH                256
*/

#define SysSNPrintf                 _snprintf

#define Sign(v)                     (((v) < 0) ? -1: +1)
#define Min(a, b)                   (((a) < (b)) ? (a): (b))
#define Max(a, b)                   (((a) > (b)) ? (a): (b))
#define Abs(v)                      (((v) > 0) ? (v): -(v))

int SysFileSync(FILE * pFile)
{
	if (fflush(pFile) || _commit(_fileno(pFile)))
		return -1;

	return 0;
}

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

unsigned long SysGetProcessId(void)
{
	return (unsigned long) GetCurrentThreadId();
}

int SysMoveFile(char const *pszOldName, char const *pszNewName)
{
	if (!MoveFileEx
	    (pszOldName, pszNewName, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
		return -1;

	return 0;
}

int SysGetHostName(char *pszHostName, int iNameSize)
{
	DWORD dwSize = (DWORD) iNameSize;

	GetComputerName(pszHostName, &dwSize);

	return 0;
}

int SysMsSleep(int iMsTimeout)
{
	Sleep(iMsTimeout);

	return 0;
}

char *SysGetEnv(char const *pszVarName)
{
	HKEY hKey;
	char const *pszValue;
	char szRKeyPath[256];

	sprintf(szRKeyPath, "SOFTWARE\\%s\\%s", APP_PRODUCER, APP_NAME_STR);

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, szRKeyPath, 0, KEY_QUERY_VALUE,
			 &hKey) == ERROR_SUCCESS) {
		char szKeyValue[2048] = "";
		DWORD dwSize = sizeof(szKeyValue), dwKeyType;

		if (RegQueryValueEx(hKey, pszVarName, NULL, &dwKeyType, (u_char *) szKeyValue,
				    &dwSize) == ERROR_SUCCESS) {
			RegCloseKey(hKey);

			return strdup(szKeyValue);
		}

		RegCloseKey(hKey);
	}

	pszValue = getenv(pszVarName);

	return (pszValue != NULL) ? strdup(pszValue) : NULL;
}

char *SysGetUserAddress(char *pszUser, int iSize, char const *pszEnvDomain)
{
	DWORD dwSize;
	char const *pszDomain;
	char szUser[256] = "";
	char szDomain[256] = "";

	dwSize = sizeof(szUser);
	GetUserName(szUser, &dwSize);

	if ((pszDomain = SysGetEnv(pszEnvDomain)) == NULL) {
		dwSize = sizeof(szDomain);
		GetComputerName(szDomain, &dwSize);
		pszDomain = szDomain;
	}

	SysSNPrintf(pszUser, iSize, "%s@%s", szUser, pszDomain);

	return pszUser;
}

#else				// #if defined(WIN32)
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

#define SysSNPrintf                 snprintf
#define stricmp                     strcasecmp
#define strnicmp                    strncasecmp

#define Sign(v)                     (((v) < 0) ? -1: +1)
#define Min(a, b)                   (((a) < (b)) ? (a): (b))
#define Max(a, b)                   (((a) > (b)) ? (a): (b))
#define Abs(v)                      (((v) > 0) ? (v): -(v))

int SysFileSync(FILE * pFile)
{
	if (fflush(pFile) || fsync(fileno(pFile)))
		return -1;

	return 0;
}

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

unsigned long SysGetProcessId(void)
{
	return (unsigned long) getpid();
}

int SysMoveFile(char const *pszOldName, char const *pszNewName)
{
	if (rename(pszOldName, pszNewName) != 0)
		return -1;

	return 0;
}

int SysGetHostName(char *pszHostName, int iNameSize)
{
	gethostname(pszHostName, iNameSize);

	return 0;
}

int SysMsSleep(int iMsTimeout)
{
	usleep(iMsTimeout * 1000);

	return 0;
}

char *SysGetEnv(char const *pszVarName)
{
	char const *pszValue = getenv(pszVarName);

	return (pszValue != NULL) ? strdup(pszValue) : NULL;
}

char *SysGetUserAddress(char *pszUser, int iSize, char const *pszEnvDomain)
{
	char const *pszEnv;
	char szUser[256] = "mail";
	char szDomain[256] = "localhost";

	if ((pszEnv = SysGetEnv("USER")) != NULL) {
		strncpy(szUser, pszEnv, sizeof(szUser) - 1);
		szUser[sizeof(szUser) - 1] = '\0';
	}
	if (((pszEnv = SysGetEnv(pszEnvDomain)) != NULL) ||
	    ((pszEnv = SysGetEnv("HOSTNAME")) != NULL)) {
		strncpy(szDomain, pszEnv, sizeof(szDomain) - 1);
		szDomain[sizeof(szDomain) - 1] = '\0';
	}

	SysSNPrintf(pszUser, iSize, "%s@%s", szUser, szDomain);

	return pszUser;
}


#else				// #if defined(__LINUX__) || defined(__SOLARIS__)

#error system type not defined !

#endif				// #if defined(__LINUX__) || defined(__SOLARIS__)
#endif				// #if defined(WIN32)

#if 0 /* [i_a] */
#define ENV_MAIL_ROOT           "MAIL_ROOT"
#define LOCAL_TEMP_SUBPATH      "spool" SYS_SLASH_STR "temp" SYS_SLASH_STR
#define LOCAL_SUBPATH           "spool" SYS_SLASH_STR "local" SYS_SLASH_STR
#endif
#define MAIL_DATA_TAG           "<<MAIL-DATA>>"
#define MAX_ADDR_NAME           256
#define SAPE_OPEN_TENTATIVES    5
#define SAPE_OPEN_DELAY         500
#define ENV_DEFAULT_DOMAIN      "DEFAULT_DOMAIN"
#define ENV_USER                "USER"

#define SetEmptyString(s)       (s)[0] = '\0'
#define IsEmptyString(s)        (*(s) == '\0')
#define StrNCpy(t, s, n)        do { strncpy(t, s, n); (t)[(n) - 1] = '\0'; } while (0)
#define StrSNCpy(t, s)          StrNCpy(t, s, sizeof(t))

static FILE *SafeOpenFile(char const *pszFilePath, char const *pszMode)
{
	FILE *pFile;

	for (int i = 0; i < SAPE_OPEN_TENTATIVES; i++) {
		if ((pFile = fopen(pszFilePath, pszMode)) != NULL)
			return pFile;

		SysMsSleep(SAPE_OPEN_DELAY);
	}

	return NULL;
}

/* [i_a] -- routine replicated from USmlAddressFromAtPtr() */
static char const *AddressFromAtPtr(char const *pszAt, char const *pszBase, char *pszAddress,
				    int iSize)
{
	int iAddrLength;
	char const *pszStart = pszAt, *pszEnd;

	for (; pszStart >= pszBase && strchr("<> \t,\":;'\r\n", *pszStart) == NULL;
	     pszStart--);

	++pszStart;

	for (pszEnd = pszAt + 1; *pszEnd != '\0' &&
		     strchr("<> \t,\":;'\r\n", *pszEnd) == NULL; pszEnd++);

	iAddrLength = Min((int) (pszEnd - pszStart), iSize - 1);

	strncpy(pszAddress, pszStart, iAddrLength);
	pszAddress[iAddrLength] = '\0';

	return pszEnd;
}

static int EmitRecipients(FILE * pMailFile, char const *pszAddrList)
{
	int iRcptCount = 0;
	char const *pszCurr = pszAddrList;

	for (; pszCurr != NULL && *pszCurr != '\0';) {
		char szAddress[256] = "";

		/*
		   [i_a] '@' is also accepted in the section before the '<email-address>', e.g.
		   "loony@toones <ano@box.xom>"

		   Besides, this code must be able to handle lines like
		   'from bla <mail@box.com>; via blub (mail@box.net); etc.'
		 */
		char const *lt_p = strchr(pszCurr, '<');
		char const *gt_p = (!lt_p ? lt_p : strchr(lt_p + 1, '>'));
		char const *pszAt = strchr(lt_p ? lt_p + 1 : pszCurr, '@');
		while (lt_p && gt_p && pszAt) {
			if (pszAt > gt_p) {
				lt_p = strchr(lt_p + 1, '<');
				gt_p = (!lt_p ? lt_p : strchr(lt_p + 1, '>'));
				pszAt = (!lt_p ? /* copout for bad line */ strchr(pszCurr, '@') : strchr(lt_p + 1, '@'));
			}
			else {
				break;
			}
		}

		if (pszAt == NULL)
			break;
		if ((pszCurr = AddressFromAtPtr(pszAt, pszAddrList, szAddress,
						sizeof(szAddress))) != NULL) {
			fprintf(pMailFile, "rcpt to:<%s>\r\n", szAddress);
			++iRcptCount;
		}
	}

	return iRcptCount;
}

static int GetTime(struct tm &tmLocal, int &iDiffHours, int &iDiffMins, time_t tCurr)
{
	if (tCurr == 0)
		time(&tCurr);

	tmLocal = *localtime(&tCurr);

	struct tm tmTimeLOC = tmLocal;
	struct tm tmTimeGM;

	tmTimeGM = *gmtime(&tCurr);

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

static int GetTimeStr(char *pszTimeStr, int iStringSize, time_t tCurr)
{
	int iDiffHours = 0;
	int iDiffMins = 0;
	struct tm tmTime;
	char szDiffTime[128];

	GetTime(tmTime, iDiffHours, iDiffMins, tCurr);
	if (iDiffHours > 0)
		sprintf(szDiffTime, " +%02d%02d", iDiffHours, iDiffMins);
	else
		sprintf(szDiffTime, " -%02d%02d", -iDiffHours, iDiffMins);

	MscStrftime(&tmTime, pszTimeStr, iStringSize - (int)strlen(szDiffTime) - 1);

	strcat(pszTimeStr, szDiffTime);

	return 0;
}

static char *CopyAddress(char *pszDest, char const *pszAddr, int iSize)
{
	char *pszDomain;

	if (strchr(pszAddr, '@') != NULL ||
	    (pszDomain = SysGetEnv(ENV_DEFAULT_DOMAIN)) == NULL)
		StrNCpy(pszDest, pszAddr, iSize);
	else
		SysSNPrintf(pszDest, iSize, "%s@%s", pszAddr, pszDomain);

	return pszDest;
}

static bool IsRFC822HeaderLine(char const *pszLn)
{
	char const *pszColon;

	if (!isalpha(*pszLn) || (pszColon = strchr(pszLn, ':')) == NULL)
		return false;
	for (pszLn++; pszLn < pszColon; pszLn++)
		if (!(isalpha(*pszLn) || isdigit(*pszLn) ||
		      strchr("_-", *pszLn) != NULL))
			return false;

	return true;
}

int main(int iArgCount, char *pszArgs[])
{
	int iVarLength = 0;
	FILE *pInFile = stdin;
	char *pszMailRoot;
	char szMailRoot[SYS_MAX_PATH] = "";

	/* Initialize time */
	tzset();

	if ((pszMailRoot = SysGetEnv(ENV_MAIL_ROOT)) == NULL ||
	    (iVarLength = (int)strlen(pszMailRoot)) == 0) {
		free(pszMailRoot);
		fprintf(stderr, "cannot find environment variable: %s\n",
			ENV_MAIL_ROOT);
		return 1;
	}
	StrSNCpy(szMailRoot, pszMailRoot);
	if (szMailRoot[iVarLength - 1] != SYS_SLASH_CHAR)
		strcat(szMailRoot, SYS_SLASH_STR);
	free(pszMailRoot);

	/* Parse command line */
	int i;
	bool bExtractRcpts = false;
	bool bXMailFormat = false;
	bool bDotMode = true;
	char szMailFrom[256] = "";
	char szExtMailFrom[256] = "";
	char szInputFile[SYS_MAX_PATH] = "";
	char szRcptFile[SYS_MAX_PATH] = "";

	SysGetUserAddress(szMailFrom, sizeof(szMailFrom) - 1, ENV_DEFAULT_DOMAIN);

	for (i = 1; i < iArgCount; i++) {
		if (strcmp(pszArgs[i], "--") == 0) {
			++i;
			break;
		}

		if (strcmp(pszArgs[i], "--rcpt-file") == 0) {
			if (++i < iArgCount)
				StrSNCpy(szRcptFile, pszArgs[i]);
		} else if (strcmp(pszArgs[i], "--xinput-file") == 0) {
			if (++i < iArgCount) {
				StrSNCpy(szInputFile, pszArgs[i]);

				bXMailFormat = true;
			}
		} else if (strcmp(pszArgs[i], "--input-file") == 0) {
			if (++i < iArgCount)
				StrSNCpy(szInputFile, pszArgs[i]);
		} else if (strcmp(pszArgs[i], "-i") == 0 ||
			   strcmp(pszArgs[i], "-oi") == 0) {
			bDotMode = false;
		} else if (strcmp(pszArgs[i], "-t") == 0) {
			bExtractRcpts = true;
		} else if (strncmp(pszArgs[i], "-f", 2) == 0) {
			if (pszArgs[i][2] != '\0')
				CopyAddress(szMailFrom, pszArgs[i] + 2,
					    sizeof(szMailFrom) - 1);
			else if ((i + 1) < iArgCount) {
				CopyAddress(szMailFrom, pszArgs[i + 1],
					    sizeof(szMailFrom) - 1);
				i++;
			}
		} else if (strncmp(pszArgs[i], "-F", 2) == 0) {
			if (pszArgs[i][2] != '\0')
				StrSNCpy(szExtMailFrom, pszArgs[i] + 2);
			else if (i + 1 < iArgCount) {
				StrSNCpy(szExtMailFrom, pszArgs[i + 1]);
				i++;
			}

			char const *pszOpen = strchr(szExtMailFrom, '<');

			if (pszOpen == NULL)
				CopyAddress(szMailFrom, szExtMailFrom,
					    sizeof(szMailFrom) - 1);
			else {
				char szTmpMailFrom[256];

				StrSNCpy(szTmpMailFrom, pszOpen + 1);

				char *pszClose =
					(char *) strchr(szTmpMailFrom, '>');

				if (pszClose != NULL)
					*pszClose = '\0';

				CopyAddress(szMailFrom, szTmpMailFrom,
					    sizeof(szMailFrom) - 1);
			}
		} else if (strncmp(pszArgs[i], "-o", 2) == 0) {

		} else if (strcmp(pszArgs[i], "-L") == 0 ||
			   strcmp(pszArgs[i], "-O") == 0 ||
			   strcmp(pszArgs[i], "-R") == 0 ||
			   strcmp(pszArgs[i], "-X") == 0 ||
			   strcmp(pszArgs[i], "-V") == 0 ||
			   strcmp(pszArgs[i], "-N") == 0 ||
			   strncmp(pszArgs[i], "-qG", 3) == 0 ||
			   strncmp(pszArgs[i], "-q!", 3) == 0) {
			if (i + 1 < iArgCount)
				i++;
		} else
			break;
	}

	/* Save recipients index and counter */
	int iRcptIndex, iRcptCount;

	iRcptIndex = Min(i, iArgCount);
	iRcptCount = iArgCount - iRcptIndex;

	/* Check if recipients are supplied */
	if (!bExtractRcpts && iRcptCount == 0 && IsEmptyString(szRcptFile)) {
		fprintf(stderr, "empty recipient list\n");
		return 2;
	}

	if (!IsEmptyString(szInputFile)) {
		if ((pInFile = fopen(szInputFile, "rb")) == NULL) {
			perror(szInputFile);
			return 3;
		}
	}

        /* Check if the sender is specified */
        if (IsEmptyString(szMailFrom)) {
                char const *pszUser = SysGetEnv(ENV_USER);

                if (pszUser == NULL)
                        pszUser = "postmaster";
                CopyAddress(szMailFrom, pszUser, sizeof(szMailFrom) - 1);
        }

	/* Create file name */
	char szHostName[256] = "";
	char szDataFile[SYS_MAX_PATH] = "";
	char szMailFile[SYS_MAX_PATH] = "";

	SysGetHostName(szHostName, sizeof(szHostName) - 1);

	SysSNPrintf(szDataFile, sizeof(szDataFile), "%s%s%lu000.%lu.%s",
		    szMailRoot, LOCAL_TEMP_SUBPATH, (unsigned long) time(NULL),
		    SysGetProcessId(), szHostName);
	SysSNPrintf(szMailFile, sizeof(szMailFile), "%s.mail", szDataFile);

	/* Open raw data file */
	FILE *pDataFile = fopen(szDataFile, "w+b");

	if (pDataFile == NULL) {
		perror(szDataFile);
		if (pInFile != stdin)
			fclose(pInFile);
		return 4;
	}
	/* Open maildrop file */
	FILE *pMailFile = fopen(szMailFile, "wb");

	if (pMailFile == NULL) {
		perror(szMailFile);
		fclose(pDataFile);
		remove(szDataFile);
		if (pInFile != stdin)
			fclose(pInFile);
		return 5;
	}
	fprintf(pMailFile, "mail from:<%s>\r\n", szMailFrom);

	for (i = iRcptIndex; i < iArgCount; i++) {
		char szAddr[256];

		CopyAddress(szAddr, pszArgs[i], sizeof(szAddr) - 1);
		fprintf(pMailFile, "rcpt to:<%s>\r\n", szAddr);
	}

	int iLine;
	bool bInHeaders = true;
	bool bHasFrom = false;
	bool bHasDate = false;
	bool bRcptSource = false;
	bool bNoEmit = false;
	char szBuffer[1536];

	for (iLine = 0; fgets(szBuffer, sizeof(szBuffer) - 1, pInFile) != NULL; iLine++) {
		int iLineLength = (int)strlen(szBuffer);

		for (; iLineLength > 0 && (szBuffer[iLineLength - 1] == '\r' ||
					   szBuffer[iLineLength - 1] == '\n'); iLineLength--);

		szBuffer[iLineLength] = '\0';

		/* Is it time to stop reading ? */
		if (bDotMode && strcmp(szBuffer, ".") == 0)
			break;

		/* Decode XMail spool file format */
		if (bXMailFormat) {
			if (strcmp(szBuffer, MAIL_DATA_TAG) == 0)
				bXMailFormat = false;

			continue;
		}
		/* Extract mail from */
		if (bInHeaders) {
			bool bStraightFile = false;

			if ((iLine == 0) && !IsRFC822HeaderLine(szBuffer))
				bStraightFile = true;

			if (bStraightFile || iLineLength == 0) {
				bInHeaders = false;
				bNoEmit = false;

				/* Add mail from (if not present) */
				if (!bHasFrom) {
					if (strlen(szExtMailFrom) != 0)
						fprintf(pDataFile, "From: %s\r\n", szExtMailFrom);
					else
						fprintf(pDataFile, "From: <%s>\r\n", szMailFrom);
				}
				/* Add date (if not present) */
				if (!bHasDate) {
					char szDate[128] = "";

					GetTimeStr(szDate, sizeof(szDate) - 1, time(NULL));
					fprintf(pDataFile, "Date: %s\r\n", szDate);
				}

				if (bStraightFile)
					fprintf(pDataFile, "\r\n");
			}

			if (szBuffer[0] == ' ' || szBuffer[0] == '\t') {
				if (bExtractRcpts && bRcptSource) {
					int iRcptCurr = EmitRecipients(pMailFile, szBuffer);

					if (iRcptCurr > 0)
						iRcptCount += iRcptCurr;
				}
			} else if (iLineLength > 0) {
				bNoEmit = (strnicmp(szBuffer, "Bcc:", 4) == 0);
				bRcptSource = strnicmp(szBuffer, "To:", 3) == 0 ||
					strnicmp(szBuffer, "Cc:", 3) == 0 ||
					strnicmp(szBuffer, "Bcc:", 4) == 0;

				if (bExtractRcpts && bRcptSource) {
					int iRcptCurr = EmitRecipients(pMailFile, szBuffer);

					if (iRcptCurr > 0)
						iRcptCount += iRcptCurr;
				}

				if (!bHasFrom && strnicmp(szBuffer, "From:", 5) == 0)
					bHasFrom = true;

				if (!bHasDate && strnicmp(szBuffer, "Date:", 5) == 0)
					bHasDate = true;
			}
		}
		if (!bNoEmit)
			fprintf(pDataFile, "%s\r\n", szBuffer);
	}

	/* Close input file if different from stdin */
	if (pInFile != stdin)
		fclose(pInFile);

	/* Dump recipient file */
	if (!IsEmptyString(szRcptFile)) {
		FILE *pRcptFile = SafeOpenFile(szRcptFile, "rb");

		if (pRcptFile == NULL) {
			perror(szRcptFile);
			fclose(pDataFile);
			remove(szDataFile);
			fclose(pMailFile);
			remove(szMailFile);
			return 6;
		}

		while (fgets(szBuffer, sizeof(szBuffer) - 1, pRcptFile) != NULL) {
			int iLineLength = (int)strlen(szBuffer);

			for (; iLineLength > 0 &&
				     (szBuffer[iLineLength - 1] == '\r' ||
				      szBuffer[iLineLength - 1] == '\n'); iLineLength--);

			szBuffer[iLineLength] = '\0';

			if (iLineLength >= MAX_ADDR_NAME)
				continue;

			/*
			   [i_a] '@' is also accepted in the section before the '<email-address>', e.g.
			   "loony@toones <ano@box.xom>"

			   Besides, this code must be able to handle lines like
			   'from bla <mail@box.com>; via blub (mail@box.net); etc.'
			 */
			char const *lt_p = strchr(szBuffer, '<');
			char const *gt_p = (!lt_p ? lt_p : strchr(lt_p + 1, '>'));
			char const *pszAt = strchr(lt_p ? lt_p + 1 : szBuffer, '@');
			while (lt_p && gt_p && pszAt) {
				if (pszAt > gt_p) {
					lt_p = strchr(lt_p + 1, '<');
					gt_p = (!lt_p ? lt_p : strchr(lt_p + 1, '>'));
					pszAt = (!lt_p ? /* copout for bad line */ strchr(szBuffer, '@') : strchr(lt_p + 1, '@'));
				}
				else {
					break;
				}
			}

			if (pszAt == NULL)
				continue;

			char szRecipient[MAX_ADDR_NAME] = "";

			if (AddressFromAtPtr(pszAt, szBuffer, szRecipient,
					     sizeof(szRecipient)) != NULL) {
				fprintf(pMailFile, "rcpt to:<%s>\r\n", szRecipient);

				++iRcptCount;
			}
		}

		fclose(pRcptFile);
	}
	/* Check the number of recipients */
	if (iRcptCount == 0) {
		fprintf(stderr, "empty recipient list\n");
		fclose(pDataFile);
		remove(szDataFile);
		fclose(pMailFile);
		remove(szMailFile);
		return 7;
	}
	/* Empty line separator between maildrop header and data */
	fprintf(pMailFile, "\r\n");

	/* Append data file */
	rewind(pDataFile);

	unsigned int uReaded;

	do {
		if (((uReaded = (unsigned int)fread(szBuffer, 1, (int)sizeof(szBuffer), pDataFile)) != 0) &&
		    (fwrite(szBuffer, 1, uReaded, pMailFile) != uReaded)) {
			perror(szMailFile);
			fclose(pDataFile);
			remove(szDataFile);
			fclose(pMailFile);
			remove(szMailFile);
			return 8;
		}

	} while (uReaded == sizeof(szBuffer));

	fclose(pDataFile);
	remove(szDataFile);

	/* Sync and close the mail file */
	if (SysFileSync(pMailFile) < 0 || fclose(pMailFile)) {
		remove(szMailFile);
		fprintf(stderr, "cannot write file: %s\n", szMailFile);
		return 9;
	}
	/* Move the mail file */
	char szDropFile[SYS_MAX_PATH];

	SysSNPrintf(szDropFile, sizeof(szDropFile), "%s%s%lu000.%lu.%s",
		    szMailRoot, LOCAL_SUBPATH, (unsigned long) time(NULL),
		    SysGetProcessId(), szHostName);
	if (SysMoveFile(szMailFile, szDropFile) < 0) {
		remove(szMailFile);
		fprintf(stderr, "cannot move file: %s\n", szMailFile);
		return 10;
	}

	return 0;
}

