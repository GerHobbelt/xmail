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
#include "StrUtils.h"
#include "SList.h"
#include "ShBlocks.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MailConfig.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "ExtAliases.h"
#include "MiscUtils.h"
#include "MailDomains.h"
#include "Filter.h"
#include "SMTPSvr.h"
#include "SMTPUtils.h"
#include "POP3Utils.h"
#include "AppDefines.h"
#include "MailSvr.h"

#define SFF_HEADER_MODIFIED             (1 << 0)

#define STD_TAG_BUFFER_LENGTH           1024
#define CUSTOM_CMD_LINE_MAX             512
#define SMAIL_DOMAIN_PROC_DIR           "custdomains"
#define SMAIL_CMDALIAS_DIR              "cmdaliases"
#define SMAIL_DEFAULT_FILTER            ".tab"
#define SMAIL_LOG_FILE                  "smail"
#define MAX_MTA_OPS                     16
#define ADDRESS_TOKENIZER               ","
#define SMAIL_EXTERNAL_EXIT_BREAK       16
#define SMAIL_STOP_PROCESSING           3111965L


struct SpoolFileData {
	char **ppszInfo;
	char **ppszFrom;
	char *pszMailFrom;
	char *pszSendMailFrom;
	char **ppszRcpt;
	char *pszRcptTo;
	char *pszSendRcptTo;
	char *pszRelayDomain;
	char szSMTPDomain[MAX_ADDR_NAME];
	char szMessageID[128];
	char szMessFilePath[SYS_MAX_PATH];
	SYS_OFF_T llMessageOffset;
	SYS_OFF_T llMailDataOffset;
	SYS_OFF_T llMessageSize;
	char szSpoolFile[SYS_MAX_PATH];
	HSLIST hTagList;
	unsigned long ulFlags;
};

struct MessageTagData {
	LISTLINK LL;
	char *pszTagName;
	char *pszTagData;
};

struct MacroSubstCtx {
	SPLF_HANDLE hFSpool;
	UserInfo *pUI;
	FileSection FSect;
};

static int USmlAddTag(HSLIST &hTagList, char const *pszTagName,
		      char const *pszTagData, int iUpdate = 0);


int USmlLoadSpoolFileHeader(char const *pszSpoolFile, SpoolFileHeader &SFH)
{
	ZeroData(SFH);

	FILE *pSpoolFile = fopen(pszSpoolFile, "rb");

	if (pSpoolFile == NULL) {
		ErrSetErrorCode(ERR_SPOOL_FILE_NOT_FOUND, pszSpoolFile);
		return ERR_SPOOL_FILE_NOT_FOUND;
	}
	/* Build spool file name */
	char szFName[SYS_MAX_PATH] = "";
	char szExt[SYS_MAX_PATH] = "";
	char szSpoolLine[MAX_SPOOL_LINE] = "";

	MscSplitPath(pszSpoolFile, NULL, 0, szFName, sizeof(szFName), szExt, sizeof(szExt));
	SysSNPrintf(SFH.szSpoolFile, sizeof(SFH.szSpoolFile) - 1, "%s%s", szFName, szExt);

	/* Read info ( 1st row of the spool file ) */
	if (MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL ||
	    (SFH.ppszInfo = StrTokenize(szSpoolLine, ";")) == NULL ||
	    StrStringsCount(SFH.ppszInfo) < smiMax) {
		if (SFH.ppszInfo != NULL)
			StrFreeStrings(SFH.ppszInfo);
		fclose(pSpoolFile);
		ZeroData(SFH);

		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
		return ERR_SPOOL_FILE_NOT_FOUND;
	}
	/* Read SMTP domain ( 2nd row of the spool file ) */
	if (MscGetString(pSpoolFile, SFH.szSMTPDomain, sizeof(SFH.szSMTPDomain) - 1) == NULL) {
		StrFreeStrings(SFH.ppszInfo);
		fclose(pSpoolFile);
		ZeroData(SFH);

		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
		return ERR_SPOOL_FILE_NOT_FOUND;
	}
	/* Read message ID ( 3rd row of the spool file ) */
	if (MscGetString(pSpoolFile, SFH.szMessageID, sizeof(SFH.szMessageID) - 1) == NULL) {
		StrFreeStrings(SFH.ppszInfo);
		fclose(pSpoolFile);
		ZeroData(SFH);

		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
		return ERR_SPOOL_FILE_NOT_FOUND;
	}
	/* Read "MAIL FROM:" ( 4th row of the spool file ) */
	if (MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL ||
	    StrINComp(szSpoolLine, MAIL_FROM_STR) != 0 ||
	    (SFH.ppszFrom = USmtpGetPathStrings(szSpoolLine)) == NULL) {
		StrFreeStrings(SFH.ppszInfo);
		fclose(pSpoolFile);
		ZeroData(SFH);

		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read "RCPT TO:" ( 5th row of the spool file ) */
	if (MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL ||
	    StrINComp(szSpoolLine, RCPT_TO_STR) != 0 ||
	    (SFH.ppszRcpt = USmtpGetPathStrings(szSpoolLine)) == NULL) {
		StrFreeStrings(SFH.ppszFrom);
		StrFreeStrings(SFH.ppszInfo);
		fclose(pSpoolFile);
		ZeroData(SFH);

		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
		return ERR_INVALID_SPOOL_FILE;
	}
	fclose(pSpoolFile);

	return 0;
}

void USmlCleanupSpoolFileHeader(SpoolFileHeader &SFH)
{
	if (SFH.ppszInfo != NULL)
		StrFreeStrings(SFH.ppszInfo);
	if (SFH.ppszRcpt != NULL)
		StrFreeStrings(SFH.ppszRcpt);
	if (SFH.ppszFrom != NULL)
		StrFreeStrings(SFH.ppszFrom);
	ZeroData(SFH);
}

static MessageTagData *USmlAllocTag(char const *pszTagName, char const *pszTagData)
{
	MessageTagData *pMTD = (MessageTagData *) SysAlloc(sizeof(MessageTagData));

	if (pMTD == NULL)
		return NULL;

	ListLinkInit(pMTD);
	pMTD->pszTagName = SysStrDup(pszTagName);
	pMTD->pszTagData = SysStrDup(pszTagData);

	return pMTD;
}

static void USmlFreeTag(MessageTagData *pMTD)
{
	SysFree(pMTD->pszTagName);
	SysFree(pMTD->pszTagData);
	SysFree(pMTD);
}

static MessageTagData *USmlFindTag(HSLIST &hTagList, char const *pszTagName,
				   TAG_POSITION &TagPosition)
{
	MessageTagData *pMTD = (TagPosition == TAG_POSITION_INIT) ?
		(MessageTagData *) ListFirst(hTagList): (MessageTagData *) TagPosition;

	for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
		     ListNext(hTagList, (PLISTLINK) pMTD)) {
		if (pszTagName == NULL || stricmp(pMTD->pszTagName, pszTagName) == 0) {
			TagPosition = (TAG_POSITION) ListNext(hTagList, (PLISTLINK) pMTD);
			return pMTD;
		}
	}

	TagPosition = (TAG_POSITION) INVALID_SLIST_PTR;

	return NULL;
}

static int USmlAddTag(HSLIST &hTagList, char const *pszTagName,
		      char const *pszTagData, int iUpdate)
{
	if (!iUpdate) {
		MessageTagData *pMTD = USmlAllocTag(pszTagName, pszTagData);

		if (pMTD == NULL)
			return ErrGetErrorCode();

		ListAddTail(hTagList, (PLISTLINK) pMTD);
	} else {
		TAG_POSITION TagPosition = TAG_POSITION_INIT;
		MessageTagData *pMTD = USmlFindTag(hTagList, pszTagName, TagPosition);

		if (pMTD != NULL) {
			SysFree(pMTD->pszTagData);

			pMTD->pszTagData = SysStrDup(pszTagData);
		} else {
			if ((pMTD = USmlAllocTag(pszTagName, pszTagData)) == NULL)
				return ErrGetErrorCode();

			ListAddTail(hTagList, (PLISTLINK) pMTD);
		}
	}

	return 0;
}

static void USmlFreeTagsList(HSLIST &hTagList)
{
	MessageTagData *pMTD;

	while ((pMTD = (MessageTagData *) ListRemove(hTagList)) != INVALID_SLIST_PTR)
		USmlFreeTag(pMTD);
}

static int USmlLoadTags(FILE *pSpoolFile, HSLIST &hTagList)
{
	int iPrevGotNL, iGotNL;
	unsigned long ulFilePos;
	DynString TagDS;
	char szTagName[256] = "", szSpoolLine[MAX_SPOOL_LINE];

	ulFilePos = (unsigned long) ftell(pSpoolFile);
	StrDynInit(&TagDS);

	for (iPrevGotNL = 1;
	     MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1,
			  &iGotNL) != NULL; iPrevGotNL = iGotNL) {
		if (IsEmptyString(szSpoolLine)) {
			if (StrDynSize(&TagDS) > 0) {
				if (USmlAddTag(hTagList, szTagName, StrDynGet(&TagDS)) < 0) {
					ErrorPush();
					StrDynFree(&TagDS);
					fseek(pSpoolFile, ulFilePos, SEEK_SET);
					return ErrorPop();
				}

				SetEmptyString(szTagName);
				StrDynTruncate(&TagDS);
			}
			break;
		}
		if (szSpoolLine[0] == ' ' || szSpoolLine[0] == '\t' ||
		    !iPrevGotNL) {
			if (IsEmptyString(szTagName)) {
				StrDynFree(&TagDS);
				fseek(pSpoolFile, ulFilePos, SEEK_SET);

				ErrSetErrorCode(ERR_INVALID_MESSAGE_FORMAT);
				return ERR_INVALID_MESSAGE_FORMAT;
			}
			if ((iPrevGotNL && StrDynAdd(&TagDS, "\r\n") < 0) ||
			    StrDynAdd(&TagDS, szSpoolLine) < 0) {
				ErrorPush();
				StrDynFree(&TagDS);
				fseek(pSpoolFile, ulFilePos, SEEK_SET);
				return ErrorPop();
			}
		} else {
			if (StrDynSize(&TagDS) > 0) {
				if (USmlAddTag(hTagList, szTagName, StrDynGet(&TagDS)) < 0) {
					ErrorPush();
					StrDynFree(&TagDS);
					fseek(pSpoolFile, ulFilePos, SEEK_SET);
					return ErrorPop();
				}

				SetEmptyString(szTagName);
				StrDynTruncate(&TagDS);
			}

			char *pszEndTag = strchr(szSpoolLine, ':');

			if (pszEndTag == NULL) {
				StrDynFree(&TagDS);
				fseek(pSpoolFile, ulFilePos, SEEK_SET);

				ErrSetErrorCode(ERR_INVALID_MESSAGE_FORMAT);
				return ERR_INVALID_MESSAGE_FORMAT;
			}

			int iNameLength = Min((int) (pszEndTag - szSpoolLine),
					      sizeof(szTagName) - 1);
			char *pszTagValue = pszEndTag + 1;

			strncpy(szTagName, szSpoolLine, iNameLength);
			szTagName[iNameLength] = '\0';

			StrSkipSpaces(pszTagValue);
			if (StrDynAdd(&TagDS, pszTagValue) < 0) {
				ErrorPush();
				StrDynFree(&TagDS);
				fseek(pSpoolFile, ulFilePos, SEEK_SET);
				return ErrorPop();
			}
		}
		ulFilePos = (unsigned long) ftell(pSpoolFile);
	}
	StrDynFree(&TagDS);

	return 0;
}

static int USmlDumpHeaders(FILE *pMsgFile, HSLIST &hTagList, char const *pszLF)
{
	MessageTagData *pMTD = (MessageTagData *) ListFirst(hTagList);

	for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
		     ListNext(hTagList, (PLISTLINK) pMTD)) {
		fprintf(pMsgFile, "%s: %s%s", pMTD->pszTagName, pMTD->pszTagData,
			pszLF);
	}

	return 0;
}

static void USmlFreeData(SpoolFileData *pSFD)
{
	USmlFreeTagsList(pSFD->hTagList);

	if (pSFD->ppszInfo != NULL)
		StrFreeStrings(pSFD->ppszInfo);

	if (pSFD->ppszFrom != NULL)
		StrFreeStrings(pSFD->ppszFrom);

	SysFree(pSFD->pszMailFrom);
	SysFree(pSFD->pszSendMailFrom);

	if (pSFD->ppszRcpt != NULL)
		StrFreeStrings(pSFD->ppszRcpt);

	SysFree(pSFD->pszRcptTo);
	SysFree(pSFD->pszSendRcptTo);
	SysFree(pSFD->pszRelayDomain);
}

char *USmlAddrConcat(char const *const *ppszStrings)
{
	int i;
	int iStrCount = StrStringsCount(ppszStrings);
	int iSumLength = 0;

	for (i = 0; i < iStrCount; i++)
		iSumLength += (int)strlen(ppszStrings[i]) + 1;

	char *pszConcat = (char *) SysAlloc(iSumLength + 1);

	if (pszConcat != NULL) {
		SetEmptyString(pszConcat);
		for (i = 0; i < iStrCount; i++) {
			if (i > 0)
				strcat(pszConcat, (i == iStrCount - 1) ? ":": ",");
			strcat(pszConcat, ppszStrings[i]);
		}
	}

	return pszConcat;
}

char *USmlBuildSendMailFrom(char const *const *ppszFrom, char const *const *ppszRcpt)
{
	int iRcptCount = StrStringsCount(ppszRcpt);
	int iFromCount = StrStringsCount(ppszFrom);

	if (iRcptCount == 0) {
		ErrSetErrorCode(ERR_BAD_FORWARD_PATH);
		return NULL;
	}

	if (iRcptCount == 1)
		return USmlAddrConcat(ppszFrom);

	int i, iSumLength = (int)strlen(ppszRcpt[0]) + 1;

	for (i = 0; i < iFromCount; i++)
		iSumLength += (int)strlen(ppszFrom[i]) + 1;

	char *pszConcat = (char *) SysAlloc(iSumLength + 1);

	if (pszConcat == NULL)
		return NULL;

	strcpy(pszConcat, ppszRcpt[0]);
	for (i = 0; i < iFromCount; i++) {
		strcat(pszConcat, (i == (iFromCount - 1)) ? ":": ",");
		strcat(pszConcat, ppszFrom[i]);
	}

	return pszConcat;
}

char *USmlBuildSendRcptTo(char const *const *ppszFrom, char const *const *ppszRcpt)
{
	int iRcptCount = StrStringsCount(ppszRcpt);
	int iFromCount = StrStringsCount(ppszFrom);

	if (iRcptCount == 0) {
		ErrSetErrorCode(ERR_BAD_FORWARD_PATH);
		return NULL;
	}

	if (iRcptCount == 1)
		return USmlAddrConcat(ppszRcpt);

	int i;
	int iSumLength = 0;

	for (i = 1; i < iRcptCount; i++)
		iSumLength += (int)strlen(ppszRcpt[i]) + 1;

	char *pszConcat = (char *) SysAlloc(iSumLength + 1);

	if (pszConcat == NULL)
		return NULL;

	SetEmptyString(pszConcat);
	for (i = 1; i < iRcptCount; i++) {
		if (i > 1)
			strcat(pszConcat, (i == (iRcptCount - 1)) ? ":": ",");
		strcat(pszConcat, ppszRcpt[i]);
	}

	return pszConcat;
}

static int USmlLoadHandle(SpoolFileData *pSFD, char const *pszMessFilePath)
{
	char szFName[SYS_MAX_PATH] = "";
	char szExt[SYS_MAX_PATH] = "";

	StrSNCpy(pSFD->szMessFilePath, pszMessFilePath);
	MscSplitPath(pszMessFilePath, NULL, 0, szFName, sizeof(szFName), szExt, sizeof(szExt));
	SysSNPrintf(pSFD->szSpoolFile, sizeof(pSFD->szSpoolFile) - 1, "%s%s", szFName, szExt);

	FILE *pSpoolFile = fopen(pszMessFilePath, "rb");

	if (pSpoolFile == NULL) {
		ErrSetErrorCode(ERR_SPOOL_FILE_NOT_FOUND, pszMessFilePath);
		return ERR_SPOOL_FILE_NOT_FOUND;
	}

	char szSpoolLine[MAX_SPOOL_LINE] = "";

	/* Read info ( 1st row of the spool file ) */
	if (MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL ||
	    (pSFD->ppszInfo = StrTokenize(szSpoolLine, ";")) == NULL ||
	    StrStringsCount(pSFD->ppszInfo) < smiMax) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read SMTP domain ( 2nd row of the spool file ) */
	if (MscGetString(pSpoolFile, pSFD->szSMTPDomain,
			 sizeof(pSFD->szSMTPDomain) - 1) == NULL) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read message ID ( 3rd row of the spool file ) */
	if (MscGetString(pSpoolFile, pSFD->szMessageID, sizeof(pSFD->szMessageID) - 1) == NULL) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read "MAIL FROM:" ( 4th row of the spool file ) */
	if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
	    (StrINComp(szSpoolLine, MAIL_FROM_STR) != 0)) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}

	if ((pSFD->ppszFrom = USmtpGetPathStrings(szSpoolLine)) == NULL) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Read "RCPT TO:" ( 5th row of the spool file ) */
	if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
	    (StrINComp(szSpoolLine, RCPT_TO_STR) != 0)) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}

	if ((pSFD->ppszRcpt = USmtpGetPathStrings(szSpoolLine)) == NULL) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Check the presence of the init data mark ( 5th row of the spool file ) */
	if (MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL ||
	    strncmp(szSpoolLine, SPOOL_FILE_DATA_START, strlen(SPOOL_FILE_DATA_START)) != 0) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Get real message position */
	pSFD->llMessageOffset = (SYS_OFF_T) ftell(pSpoolFile);

	/* Build address strings */
	if ((pSFD->pszMailFrom = USmlAddrConcat(pSFD->ppszFrom)) == NULL ||
	    (pSFD->pszSendMailFrom = USmlBuildSendMailFrom(pSFD->ppszFrom,
							   pSFD->ppszRcpt)) == NULL ||
	    (pSFD->pszRcptTo = (char *) USmlAddrConcat(pSFD->ppszRcpt)) == NULL ||
	    (pSFD->pszSendRcptTo = USmlBuildSendRcptTo(pSFD->ppszFrom, pSFD->ppszRcpt)) == NULL) {
		fclose(pSpoolFile);
		ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
		return ERR_INVALID_SPOOL_FILE;
	}
	/* Check if it's a relay message */
	if (StrStringsCount(pSFD->ppszRcpt) > 1) {
		char const *pszHost = pSFD->ppszRcpt[0];

		if (*pszHost++ != '@') {
			fclose(pSpoolFile);
			ErrSetErrorCode(ERR_INVALID_RELAY_ADDRESS, pSFD->ppszRcpt[0]);
			return ERR_INVALID_SPOOL_FILE;
		}
		if (USmlValidHost(pszHost, StrEnd(pszHost)) < 0) {
			fclose(pSpoolFile);
			return ERR_INVALID_SPOOL_FILE;
		}
		pSFD->pszRelayDomain = SysStrDup(pszHost);
	}
	/* Load message tags */
	if (USmlLoadTags(pSpoolFile, pSFD->hTagList) < 0)
		SysLogMessage(LOG_LEV_MESSAGE, "Invalid headers section : %s\n",
			      pSFD->szSpoolFile);

	/* Get spool file position */
	pSFD->llMailDataOffset = (SYS_OFF_T) ftell(pSpoolFile);

	fseek(pSpoolFile, 0, SEEK_END);

	pSFD->llMessageSize = (SYS_OFF_T) ftell(pSpoolFile) - pSFD->llMessageOffset;

	fclose(pSpoolFile);

	return 0;
}

static void USmlInitHandle(SpoolFileData *pSFD)
{
	pSFD->ppszInfo = NULL;
	pSFD->ppszFrom = NULL;
	pSFD->pszMailFrom = NULL;
	pSFD->pszSendMailFrom = NULL;
	pSFD->ppszRcpt = NULL;
	pSFD->pszRcptTo = NULL;
	pSFD->pszSendRcptTo = NULL;
	pSFD->pszRelayDomain = NULL;
	SetEmptyString(pSFD->szSMTPDomain);
	pSFD->ulFlags = 0;
	ListInit(pSFD->hTagList);
}

static SpoolFileData *USmlAllocEmptyHandle(void)
{
	/* Structure allocation and initialization */
	SpoolFileData *pSFD = (SpoolFileData *) SysAlloc(sizeof(SpoolFileData));

	if (pSFD != NULL)
		USmlInitHandle(pSFD);

	return pSFD;
}

SPLF_HANDLE USmlCreateHandle(char const *pszMessFilePath)
{
	/* Structure allocation and initialization */
	SpoolFileData *pSFD = USmlAllocEmptyHandle();

	if (pSFD == NULL)
		return INVALID_SPLF_HANDLE;

	if (USmlLoadHandle(pSFD, pszMessFilePath) < 0) {
		USmlFreeData(pSFD);
		SysFree(pSFD);
		return INVALID_SPLF_HANDLE;
	}

	return (SPLF_HANDLE) pSFD;
}

void USmlCloseHandle(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	USmlFreeData(pSFD);
	SysFree(pSFD);
}

int USmlReloadHandle(SPLF_HANDLE hFSpool)
{
	/* Structure allocation and initialization */
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;
	SpoolFileData *pNewSFD = USmlAllocEmptyHandle();

	if (pNewSFD == NULL)
		return ErrGetErrorCode();

	/* Load the new spool file data */
	if (USmlLoadHandle(pNewSFD, pSFD->szMessFilePath) < 0) {
		ErrorPush();
		USmlFreeData(pNewSFD);
		SysFree(pNewSFD);
		return ErrorPop();
	}
	/* Free the original structure data and load the new one */
	USmlFreeData(pSFD);

	*pSFD = *pNewSFD;

	/* We don't have to call USmlFreeData() since its content has been tranfered */
	/* to the original structure to replace the old information */
	SysFree(pNewSFD);

	return 0;
}

char const *USmlGetRelayDomain(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->pszRelayDomain;
}

char const *USmlGetSpoolFilePath(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->szMessFilePath;
}

char const *USmlGetSpoolFile(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->szSpoolFile;
}

char const *USmlGetSMTPDomain(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->szSMTPDomain;
}

char const *USmlGetSmtpMessageID(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->szMessageID;
}

char const *const *USmlGetInfo(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->ppszInfo;
}

char const *const *USmlGetMailFrom(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->ppszFrom;
}

char const *USmlMailFrom(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->pszMailFrom;
}

char const *USmlSendMailFrom(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->pszSendMailFrom;
}

char const *const *USmlGetRcptTo(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->ppszRcpt;
}

char const *USmlRcptTo(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->pszRcptTo;
}

char const *USmlSendRcptTo(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->pszSendRcptTo;
}

SYS_OFF_T USmlMessageSize(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	return pSFD->llMessageSize;
}

static int USmlFlushMessageFile(SpoolFileData *pSFD)
{
	char szTmpMsgFile[SYS_MAX_PATH] = "";

	SysSNPrintf(szTmpMsgFile, sizeof(szTmpMsgFile) - 1, "%s.flush", pSFD->szMessFilePath);

	/* Create temporary file */
	FILE *pMsgFile = fopen(szTmpMsgFile, "wb");

	if (pMsgFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, szTmpMsgFile);
		return ERR_FILE_CREATE;
	}
	/* Open message file */
	FILE *pMessFile = fopen(pSFD->szMessFilePath, "rb");

	if (pMessFile == NULL) {
		fclose(pMsgFile);
		CheckRemoveFile(szTmpMsgFile);

		ErrSetErrorCode(ERR_FILE_OPEN, pSFD->szMessFilePath);
		return ERR_FILE_OPEN;
	}
	/* Dump info section ( start = 0 - bytes = ulMessageOffset ) */
	if (MscCopyFile(pMsgFile, pMessFile, 0, pSFD->llMessageOffset) < 0) {
		ErrorPush();
		fclose(pMessFile);
		fclose(pMsgFile);
		CheckRemoveFile(szTmpMsgFile);
		return ErrorPop();
	}
	/* Dump message headers */
	if (USmlDumpHeaders(pMsgFile, pSFD->hTagList, "\r\n") < 0) {
		ErrorPush();
		fclose(pMessFile);
		fclose(pMsgFile);
		CheckRemoveFile(szTmpMsgFile);
		return ErrorPop();
	}

	fprintf(pMsgFile, "\r\n");

	/* Get the new message body offset */
	SYS_OFF_T llMailDataOffset = (SYS_OFF_T) ftell(pMsgFile);

	/* Dump message data ( start = ulMailDataOffset - bytes = -1 [EOF] ) */
	if (MscCopyFile(pMsgFile, pMessFile, pSFD->llMailDataOffset,
			(SYS_OFF_T) -1) < 0) {
		ErrorPush();
		fclose(pMessFile);
		fclose(pMsgFile);
		CheckRemoveFile(szTmpMsgFile);
		return ErrorPop();
	}
	fclose(pMessFile);

	if (SysFileSync(pMsgFile) < 0) {
		ErrorPush();
		fclose(pMsgFile);
		CheckRemoveFile(szTmpMsgFile);
		return ErrorPop();
	}
	pSFD->llMessageSize = (SYS_OFF_T) ftell(pMsgFile) - pSFD->llMessageOffset;

	if (fclose(pMsgFile)) {
		CheckRemoveFile(szTmpMsgFile);
		ErrSetErrorCode(ERR_FILE_WRITE, szTmpMsgFile);
		return ERR_FILE_WRITE;
	}
	/* Move the file */
	if (SysRemove(pSFD->szMessFilePath) < 0 ||
	    SysMoveFile(szTmpMsgFile, pSFD->szMessFilePath) < 0) {
		ErrorPush();
		CheckRemoveFile(szTmpMsgFile);
		return ErrorPop();
	}
	/* Set the new message body offset */
	pSFD->llMailDataOffset = llMailDataOffset;

	return 0;
}

int USmlSyncChanges(SPLF_HANDLE hFSpool)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	if (pSFD->ulFlags & SFF_HEADER_MODIFIED) {
		if (USmlFlushMessageFile(pSFD) == 0)
			pSFD->ulFlags &= ~SFF_HEADER_MODIFIED;

	}

	return 0;
}

int USmlGetMsgFileSection(SPLF_HANDLE hFSpool, FileSection &FSect)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	/* Sync message file */
	if (USmlSyncChanges(hFSpool) < 0)
		return ErrGetErrorCode();

	/* Setup file section fields */
	ZeroData(FSect);
	StrSNCpy(FSect.szFilePath, pSFD->szMessFilePath);
	FSect.llStartOffset = pSFD->llMessageOffset;
	FSect.llEndOffset = (SYS_OFF_T) -1;

	return 0;
}

int USmlWriteMailFile(SPLF_HANDLE hFSpool, FILE *pMsgFile, bool bMBoxFile)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;
	char const *pszLF = bMBoxFile ? SYS_EOL: "\r\n";

	/* Dump message tags */
	if (USmlDumpHeaders(pMsgFile, pSFD->hTagList, pszLF) < 0)
		return ErrGetErrorCode();

	fputs(pszLF, pMsgFile);

	/* Dump message data */
	FILE *pMessFile = fopen(pSFD->szMessFilePath, "rb");

	if (pMessFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pSFD->szMessFilePath);
		return ERR_FILE_OPEN;
	}

	bool bWantCRLF = !bMBoxFile;

#ifdef SYS_CRLF_EOL
	if (!bWantCRLF)
		bWantCRLF = true;
#endif
	if (bWantCRLF) {
		if (MscCopyFile(pMsgFile, pMessFile, pSFD->llMailDataOffset,
				(SYS_OFF_T) -1) < 0) {
			fclose(pMessFile);
			return ErrGetErrorCode();
		}
	} else {
		Sys_fseek(pMessFile, pSFD->llMailDataOffset, SEEK_SET);
		if (MscDos2UnixFile(pMsgFile, pMessFile) < 0) {
			fclose(pMessFile);
			return ErrGetErrorCode();
		}
	}
	fclose(pMessFile);

	return 0;
}

char *USmlGetTag(SPLF_HANDLE hFSpool, char const *pszTagName, TAG_POSITION &TagPosition)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;
	MessageTagData *pMTD = USmlFindTag(pSFD->hTagList, pszTagName, TagPosition);

	return (pMTD != NULL) ? SysStrDup(pMTD->pszTagData): NULL;
}

int USmlAddTag(SPLF_HANDLE hFSpool, char const *pszTagName, char const *pszTagData, int iUpdate)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	if (USmlAddTag(pSFD->hTagList, pszTagName, pszTagData, iUpdate) < 0)
		return ErrGetErrorCode();

	pSFD->ulFlags |= SFF_HEADER_MODIFIED;

	return 0;
}

int USmlSetTagAddress(SPLF_HANDLE hFSpool, char const *pszTagName, char const *pszAddress)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	TAG_POSITION TagPosition = TAG_POSITION_INIT;
	char *pszOldAddress = USmlGetTag(hFSpool, pszTagName, TagPosition);

	if (pszOldAddress == NULL) {
		char szTagData[512] = "";

		SysSNPrintf(szTagData, sizeof(szTagData) - 1, "<%s>", pszAddress);
		if (USmlAddTag(pSFD->hTagList, pszTagName, szTagData, 1) < 0)
			return ErrGetErrorCode();

	} else {
		char *pszOpen = strrchr(pszOldAddress, '<');

		if (pszOpen != NULL) {
			/* Case : NAME <ADDRESS> */
			char *pszClose = strrchr(pszOpen + 1, '>');

			if (pszClose == NULL) {
				SysFree(pszOldAddress);
				ErrSetErrorCode(ERR_INVALID_MESSAGE_FORMAT);
				return ERR_INVALID_MESSAGE_FORMAT;
			}

			DynString DynS;

			StrDynInit(&DynS);
			StrDynAdd(&DynS, pszOldAddress, (int) (pszOpen - pszOldAddress) + 1);
			StrDynAdd(&DynS, pszAddress);
			StrDynAdd(&DynS, pszClose);

			SysFree(pszOldAddress);
			if (USmlAddTag(pSFD->hTagList, pszTagName, StrDynGet(&DynS), 1) < 0) {
				ErrorPush();
				StrDynFree(&DynS);
				return ErrorPop();
			}
			StrDynFree(&DynS);
		} else {
			/* Case : ADDRESS */
			SysFree(pszOldAddress);
			if (USmlAddTag(pSFD->hTagList, pszTagName, pszAddress, 1) < 0)
				return ErrGetErrorCode();
		}
	}

	return 0;
}

int USmlMapAddress(char const *pszAddress, char *pszDomain, char *pszName)
{
	char szRmtDomain[MAX_ADDR_NAME] = "";
	char szRmtName[MAX_ADDR_NAME] = "";

	if (USmtpSplitEmailAddr(pszAddress, szRmtName, szRmtDomain) < 0)
		return ErrGetErrorCode();

	ExtAlias *pExtAlias = ExAlGetAlias(szRmtDomain, szRmtName);

	if (pExtAlias == NULL)
		return ErrGetErrorCode();

	strcpy(pszDomain, pExtAlias->pszDomain);
	strcpy(pszName, pExtAlias->pszName);

	ExAlFreeAlias(pExtAlias);

	return 0;
}

int USmlCreateMBFile(UserInfo *pUI, char const *pszFileName, SPLF_HANDLE hFSpool)
{
	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);

	FILE *pMBFile = fopen(pszFileName, "wb");

	if (pMBFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
		return ERR_FILE_CREATE;
	}
	/* Check the existence of the return path string ( PSYNC messages have ) */
	TAG_POSITION TagPosition = TAG_POSITION_INIT;
	char *pszReturnPath = USmlGetTag(hFSpool, "Return-Path", TagPosition);

	if (pszReturnPath == NULL) {
		/* Build return path string */
		int iFromDomains = StrStringsCount(ppszFrom);
		char szDomain[MAX_ADDR_NAME] = "";
		char szName[MAX_ADDR_NAME] = "";
		char szReturnPath[1024] = "Return-Path: <>";

		if (iFromDomains == 0 ||
		    USmlMapAddress(ppszFrom[iFromDomains - 1], szDomain, szName) < 0) {
			char *pszRetPath = USmlAddrConcat(ppszFrom);

			if (pszRetPath != NULL) {
				int iRetLength = (int)strlen(pszRetPath);
				int iExtraLength = CStringSize("Return-Path: <>");

				if (iRetLength > (int) (sizeof(szReturnPath) - iExtraLength - 2))
					pszRetPath[sizeof(szReturnPath) - iExtraLength - 2] =
						'\0';

				SysSNPrintf(szReturnPath, sizeof(szReturnPath) - 1,
					    "Return-Path: <%s>", pszRetPath);

				SysFree(pszRetPath);
			}
		} else {
			SysSNPrintf(szReturnPath, sizeof(szReturnPath) - 1,
				    "Return-Path: <%s@%s>", szName, szDomain);

			char szAddress[MAX_ADDR_NAME] = "";

			SysSNPrintf(szAddress, sizeof(szAddress) - 1, "%s@%s", szName, szDomain);

			USmlSetTagAddress(hFSpool, "Reply-To", szAddress);
		}

		fprintf(pMBFile, "%s" SYS_EOL, szReturnPath);

		/* Add "Delivered-To:" tag */
		char szUserAddress[MAX_ADDR_NAME] = "";

		UsrGetAddress(pUI, szUserAddress);
		fprintf(pMBFile, "Delivered-To: %s" SYS_EOL, szUserAddress);
	} else
		SysFree(pszReturnPath);

	/* Write mail file */
	if (USmlWriteMailFile(hFSpool, pMBFile, true) < 0) {
		ErrorPush();
		fclose(pMBFile);
		SysRemove(pszFileName);
		return ErrorPop();
	}
	fclose(pMBFile);

	return 0;
}

int USmlVCreateSpoolFile(SPLF_HANDLE hFSpool, char const *pszFromUser,
			 char const *pszRcptUser, char const *pszFileName, va_list Headers)
{
	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
	char const *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
	char const *const *ppszInfo = USmlGetInfo(hFSpool);
	char const *const *ppszFrom = USmlGetMailFrom(hFSpool);
	char const *const *ppszRcpt = USmlGetRcptTo(hFSpool);

	FILE *pSpoolFile = fopen(pszFileName, "wb");

	if (pSpoolFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszFileName); /* [i_a] */
		return ERR_FILE_CREATE;
	}
	/* Write info line */
	USmtpWriteInfoLine(pSpoolFile, ppszInfo[smiClientAddr],
			   ppszInfo[smiServerAddr], ppszInfo[smiTime]);

	/* Write SMTP domain */
	fprintf(pSpoolFile, "%s\r\n", pszSMTPDomain);

	/* Write message ID */
	fprintf(pSpoolFile, "%s\r\n", pszSmtpMessageID);

	/* Write "MAIL FROM:" */
	char const *pszMailFrom = USmlMailFrom(hFSpool);

	fprintf(pSpoolFile, "MAIL FROM: <%s>\r\n",
		(pszFromUser != NULL) ? pszFromUser: pszMailFrom);

	/* Write "RCPT TO:" */
	char const *pszRcptTo = USmlRcptTo(hFSpool);

	fprintf(pSpoolFile, "RCPT TO: <%s>\r\n", (pszRcptUser != NULL) ? pszRcptUser: pszRcptTo);

	/* Write SPOOL_FILE_DATA_START */
	fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

	/* Write extra RFC822 headers */
	char const *pszHeader = NULL;

	while ((pszHeader = va_arg(Headers, char *)) != NULL) {
		char const *pszValue = va_arg(Headers, char *);

		if (pszValue == NULL)
			break;
		if (!IsEmptyString(pszHeader))
			fprintf(pSpoolFile, "%s: %s\r\n", pszHeader, pszValue);
	}

	/* Than write mail data */
	if (USmlWriteMailFile(hFSpool, pSpoolFile) < 0) {
		ErrorPush();
		fclose(pSpoolFile);
		SysRemove(pszFileName);
		return ErrorPop();
	}

	fclose(pSpoolFile);

	return 0;
}

static int USmlCreateSpoolFile(FILE *pMailFile, char const *const *ppszInfo,
			       char const *pszMailFrom, char const *pszRcptTo,
			       char const *pszSpoolFile)
{
	FILE *pSpoolFile = fopen(pszSpoolFile, "wb");

	if (pSpoolFile == NULL) {
		ErrSetErrorCode(ERR_FILE_CREATE, pszSpoolFile);
		return ERR_FILE_CREATE;
	}
	/* Write info line */
	if (ppszInfo != NULL)
		USmtpWriteInfoLine(pSpoolFile, ppszInfo[smsgiClientAddr],
				   ppszInfo[smsgiServerAddr], ppszInfo[smsgiTime]);
	else {
		char szTime[256] = "";

		MscGetTimeStr(szTime, sizeof(szTime) - 1);
		USmtpWriteInfoLine(pSpoolFile, LOCAL_ADDRESS_SQB ":0",
				   LOCAL_ADDRESS_SQB ":0", szTime);
	}

	/* Write SMTP domain */
	char szSmtpDomain[MAX_HOST_NAME] = "";

	if (USmtpSplitEmailAddr(pszRcptTo, NULL, szSmtpDomain) < 0) {
		ErrorPush();
		fclose(pSpoolFile);
		SysRemove(pszSpoolFile);
		return ErrorPop();
	}

	fprintf(pSpoolFile, "%s\r\n", szSmtpDomain);

	/* Write message ID */
	SYS_UINT64 ullMessageID = 0;

	if (SvrGetMessageID(&ullMessageID) < 0) {
		ErrorPush();
		fclose(pSpoolFile);
		SysRemove(pszSpoolFile);
		return ErrorPop();
	}

	fprintf(pSpoolFile, "P" SYS_LLX_FMT "\r\n", ullMessageID);

	/* Write "MAIL FROM:" */
	fprintf(pSpoolFile, "MAIL FROM: <%s>\r\n", pszMailFrom);

	/* Write "RCPT TO:" */
	fprintf(pSpoolFile, "RCPT TO: <%s>\r\n", pszRcptTo);

	/* Write SPOOL_FILE_DATA_START */
	fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

	/* Write message body */
	if (MscCopyFile(pSpoolFile, pMailFile, 0, (SYS_OFF_T) -1) < 0) {
		ErrorPush();
		fclose(pSpoolFile);
		SysRemove(pszSpoolFile);
		return ErrorPop();
	}
	fclose(pSpoolFile);

	return 0;
}

int USmlCreateSpoolFile(SPLF_HANDLE hFSpool, char const *pszFromUser,
			char const *pszRcptUser, char const *pszFileName, ...)
{
	va_list Headers;

	va_start(Headers, pszFileName);

	int iCreateResult = USmlVCreateSpoolFile(hFSpool, pszFromUser,
						 pszRcptUser, pszFileName, Headers);

	va_end(Headers);

	return iCreateResult;
}

static int USmlGetMailProcessFile(UserInfo *pUI, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				  char *pszMPFilePath)
{
	/* Get the custom spool file associated with this message */
	if (USmlGetUserCustomSpoolFile(hQueue, hMessage, pszMPFilePath) < 0)
		return ErrGetErrorCode();

	/* If the file already exist inside the spool, just return the latest */
	if (SysExistFile(pszMPFilePath))
		return 0;

	/*
	 * Try to get a new copy from the user one. It'll fail if the account is not
	 * handled with a custom mail processing
	 */
	return UsrGetMailProcessFile(pUI, pszMPFilePath, GMPROC_USER | GMPROC_DOMAIN);
}

static int USmlLocalDelivery(SVRCFG_HANDLE hSvrConfig, UserInfo *pUI, SPLF_HANDLE hFSpool,
			     QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	/* Apply filters ... */
	if (FilFilterMessage(hFSpool, hQueue, hMessage, FILTER_MODE_INBOUND, pUI) < 0)
		return ErrGetErrorCode();

	/*
	 * Check if the mailbox can store the current message, according
	 * to the account's storage policies. Do not use the probe message size
	 * here (as second parameter to UPopCheckMailboxSize()), since at SMTP
	 * level we do not use it. And using it here will lead to EFULL messages
	 * never returned at SMTP level.
	 */
	if (UPopCheckMailboxSize(pUI) < 0) {
		ErrorPush();
		QueUtErrLogMessage(hQueue, hMessage, "Message rejected due to account mailbox quota\n");
		QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool,
					   ErrGetErrorString(ErrorFetch()),
					   NULL, true);
		return ErrorPop();
	}

	/* Create mailbox file ... */
	char szMBFile[SYS_MAX_PATH] = "";

	if (UsrGetTmpFile(pUI->pszDomain, szMBFile, sizeof(szMBFile)) < 0)
		return ErrGetErrorCode();

	if (USmlCreateMBFile(pUI, szMBFile, hFSpool) < 0)
		return ErrGetErrorCode();

	/* and send it home */
	char const *pszMessageID = USmlGetSpoolFile(hFSpool);

	if (UsrMoveToMailBox(pUI, szMBFile, pszMessageID) < 0) {
		ErrorPush();
		SysRemove(szMBFile);
		return ErrorPop();
	}
	/* Log operation */
	if (LMPC.ulFlags & LMPCF_LOG_ENABLED) {
		char szLocalAddress[MAX_ADDR_NAME] = "";

		USmlLogMessage(hFSpool, "LOCAL", NULL, UsrGetAddress(pUI, szLocalAddress));
	}

	return 0;
}

static char *USmlMacroLkupProc(void *pPrivate, char const *pszName, int iSize)
{
	MacroSubstCtx *pMSC = (MacroSubstCtx *) pPrivate;

	if (MemMatch(pszName, iSize, "FROM", 4)) {
		char const *const *ppszFrom = USmlGetMailFrom(pMSC->hFSpool);
		int iFromDomains = StrStringsCount(ppszFrom);

		return SysStrDup((iFromDomains > 0) ? ppszFrom[iFromDomains - 1]: "");
	} else if (MemMatch(pszName, iSize, "RCPT", 4)) {
		char const *const *ppszRcpt = USmlGetRcptTo(pMSC->hFSpool);
		int iRcptDomains = StrStringsCount(ppszRcpt);

		return SysStrDup((iRcptDomains > 0) ? ppszRcpt[iRcptDomains - 1]: "");
	} else if (MemMatch(pszName, iSize, "RRCPT", 5)) {
		char szUserAddress[MAX_ADDR_NAME] = "";

		UsrGetAddress(pMSC->pUI, szUserAddress);

		return SysStrDup(szUserAddress);
	} else if (MemMatch(pszName, iSize, "FILE", 4)) {

		return SysStrDup(pMSC->FSect.szFilePath);
	} else if (MemMatch(pszName, iSize, "MSGID", 5)) {

		return SysStrDup(USmlGetSpoolFile(pMSC->hFSpool));
	} else if (MemMatch(pszName, iSize, "MSGREF", 6)) {

		return SysStrDup(USmlGetSmtpMessageID(pMSC->hFSpool));
	} else if (MemMatch(pszName, iSize, "LOCALADDR", 9)) {
		char const *const *ppszInfo = USmlGetInfo(pMSC->hFSpool);

		return SysStrDup(ppszInfo[smiServerAddr]);
	} else if (MemMatch(pszName, iSize, "REMOTEADDR", 10)) {
		char const *const *ppszInfo = USmlGetInfo(pMSC->hFSpool);

		return SysStrDup(ppszInfo[smiClientAddr]);
	} else if (MemMatch(pszName, iSize, "TMPFILE", 7)) {
		char szTmpFile[SYS_MAX_PATH] = "";

		MscSafeGetTmpFile(szTmpFile, sizeof(szTmpFile));
		if (MscCopyFile(szTmpFile, pMSC->FSect.szFilePath) < 0) {
			CheckRemoveFile(szTmpFile);
			return NULL;
		}

		return SysStrDup(szTmpFile);
	} else if (MemMatch(pszName, iSize, "USERAUTH", 8)) {
		char szAuthName[MAX_ADDR_NAME] = "-";

		USmlMessageAuth(pMSC->hFSpool, szAuthName, sizeof(szAuthName) - 1);

		return SysStrDup(szAuthName);
	}

	return SysStrDup("");
}

static int USmlCmdMacroSubstitutes(char **ppszCmdTokens, UserInfo *pUI, SPLF_HANDLE hFSpool)
{
	MacroSubstCtx MSC;

	MSC.hFSpool = hFSpool;
	MSC.pUI = pUI;
	/*
	 * This function retrieve the spool file message section and sync the content.
	 * This is necessary before passing the file name to external programs.
	 */
	if (USmlGetMsgFileSection(hFSpool, MSC.FSect) < 0)
		return ErrGetErrorCode();

	return MscReplaceTokens(ppszCmdTokens, USmlMacroLkupProc, &MSC);
}

static int USmlCmd_external(char **ppszCmdTokens, int iNumTokens, SVRCFG_HANDLE hSvrConfig,
			    UserInfo *pUI, SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			    QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	/* Apply filters ... */
	if (FilFilterMessage(hFSpool, hQueue, hMessage, FILTER_MODE_INBOUND, pUI) < 0)
		return ErrGetErrorCode();

	if (iNumTokens < 5) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}

	int iPriority = atoi(ppszCmdTokens[1]);
	int iWaitTimeout = atoi(ppszCmdTokens[2]) * 1000;
	int iExitStatus = 0;

	if (SysExec(ppszCmdTokens[3], &ppszCmdTokens[3], iWaitTimeout, iPriority,
		    &iExitStatus) < 0) {
		ErrorPush();

		char const *pszMailFrom = USmlMailFrom(hFSpool);
		char const *pszRcptTo = USmlRcptTo(hFSpool);

		ErrLogMessage(LOG_LEV_MESSAGE,
			      "USMAIL EXTRN-Send Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
			      ppszCmdTokens[3], pszMailFrom, pszRcptTo);

		QueUtErrLogMessage(hQueue, hMessage,
				   "USMAIL EXTRN-Send Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
				   ppszCmdTokens[3], pszMailFrom, pszRcptTo);

		return ErrorPop();
	}
	/* Log operation */
	if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
		USmlLogMessage(hFSpool, "EXTRN", NULL, ppszCmdTokens[3]);

	return (iExitStatus == SMAIL_EXTERNAL_EXIT_BREAK) ? SMAIL_STOP_PROCESSING: 0;
}

static int USmlCmd_filter(char **ppszCmdTokens, int iNumTokens, SVRCFG_HANDLE hSvrConfig,
			  UserInfo *pUI, SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			  QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	if (iNumTokens < 5) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}

	int iPriority = atoi(ppszCmdTokens[1]);
	int iWaitTimeout = atoi(ppszCmdTokens[2]) * 1000;
	int iExitStatus = 0;

	if (SysExec(ppszCmdTokens[3], &ppszCmdTokens[3], iWaitTimeout, iPriority,
		    &iExitStatus) < 0) {
		ErrorPush();

		char const *pszMailFrom = USmlMailFrom(hFSpool);
		char const *pszRcptTo = USmlRcptTo(hFSpool);

		ErrLogMessage(LOG_LEV_MESSAGE,
			      "USMAIL FILTER Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
			      ppszCmdTokens[3], pszMailFrom, pszRcptTo);

		QueUtErrLogMessage(hQueue, hMessage,
				   "USMAIL FILTER Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
				   ppszCmdTokens[3], pszMailFrom, pszRcptTo);

		return ErrorPop();
	}

	/* Log operation */
	if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
		USmlLogMessage(hFSpool, "FILTER", NULL, ppszCmdTokens[3]);

	/* Separate code from flags */
	int iExitFlags = iExitStatus & FILTER_FLAGS_MASK;

	iExitStatus &= ~FILTER_FLAGS_MASK;

	if (iExitStatus == FILTER_OUT_EXITCODE ||
	    iExitStatus == FILTER_OUT_NN_EXITCODE ||
	    iExitStatus == FILTER_OUT_NNF_EXITCODE) {
		/* Filter out message */
		char *pszRejMsg = FilGetFilterRejMessage(USmlGetSpoolFilePath(hFSpool));

		if (iExitStatus == FILTER_OUT_EXITCODE)
			QueUtNotifyPermErrDelivery(hQueue, hMessage, NULL,
						   (pszRejMsg != NULL) ? pszRejMsg :
						   ErrGetErrorString(ERR_FILTERED_MESSAGE),
						   NULL, true);
		else if (iExitStatus == FILTER_OUT_NN_EXITCODE)
			QueCleanupMessage(hQueue, hMessage,
					  !QueUtRemoveSpoolErrors());
		else
			QueCleanupMessage(hQueue, hMessage, false);

		SysFree(pszRejMsg);

		ErrSetErrorCode(ERR_FILTERED_MESSAGE);
		return ERR_FILTERED_MESSAGE;
	} else if (iExitStatus == FILTER_MODIFY_EXITCODE) {
		/* Filter modified the message, we need to reload the spool handle */
		if (USmlReloadHandle(hFSpool) < 0) {
			ErrorPush();

			char const *pszMailFrom = USmlMailFrom(hFSpool);
			char const *pszRcptTo = USmlRcptTo(hFSpool);

			SysLogMessage(LOG_LEV_MESSAGE,
				      "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
				      pszMailFrom, pszRcptTo,
				      ppszCmdTokens[3]);

			QueUtErrLogMessage(hQueue, hMessage,
					   "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
					   pszMailFrom, pszRcptTo,
					   ppszCmdTokens[3]);

			QueCleanupMessage(hQueue, hMessage, true);

			return ErrorPop();
		}
	}

	return (iExitFlags & FILTER_FLAGS_BREAK) ? SMAIL_STOP_PROCESSING: 0;
}

static int USmlCmd_mailbox(char **ppszCmdTokens, int iNumTokens, SVRCFG_HANDLE hSvrConfig,
			   UserInfo *pUI, SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			   QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	if (iNumTokens != 1) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}
	/*
	 * Deliver the file locally.
	 */
	if (USmlLocalDelivery(hSvrConfig, pUI, hFSpool, hQueue, hMessage, LMPC) < 0)
		return ErrGetErrorCode();

	return 0;
}

static int USmlCmd_redirect(char **ppszCmdTokens, int iNumTokens, SVRCFG_HANDLE hSvrConfig,
			    UserInfo *pUI, SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			    QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	if (iNumTokens < 2) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}

	char szUserAddress[MAX_ADDR_NAME] = "";

	UsrGetAddress(pUI, szUserAddress);

	/* Redirection loop */
	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);

	for (int i = 1; ppszCmdTokens[i] != NULL; i++) {
		/* Get message handle */
		QMSG_HANDLE hRedirMessage = QueCreateMessage(hSpoolQueue);

		if (hRedirMessage == INVALID_QMSG_HANDLE)
			return ErrGetErrorCode();

		char szQueueFilePath[SYS_MAX_PATH] = "";

		QueGetFilePath(hSpoolQueue, hRedirMessage, szQueueFilePath);

		char szAliasAddr[MAX_ADDR_NAME] = "";

		if (strchr(ppszCmdTokens[i], '@') == NULL)
			SysSNPrintf(szAliasAddr, sizeof(szAliasAddr) - 1, "%s@%s",
				    pUI->pszName, ppszCmdTokens[i]);
		else
			StrSNCpy(szAliasAddr, ppszCmdTokens[i]);

		if (USmlCreateSpoolFile(hFSpool, NULL, szAliasAddr, szQueueFilePath,
					"X-Deliver-To", szUserAddress, NULL) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hRedirMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
		/* Log the redir operation */
		if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
			USmlLogMessage(hFSpool, "REDIR", NULL, ppszCmdTokens[i]);
	}

	return 0;
}

static int USmlCmd_lredirect(char **ppszCmdTokens, int iNumTokens, SVRCFG_HANDLE hSvrConfig,
			     UserInfo *pUI, SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			     QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	if (iNumTokens < 2) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}

	char szUserAddress[MAX_ADDR_NAME] = "";

	UsrGetAddress(pUI, szUserAddress);

	/* Redirection loop */
	for (int i = 1; ppszCmdTokens[i] != NULL; i++) {
		/* Get message handle */
		QMSG_HANDLE hRedirMessage = QueCreateMessage(hSpoolQueue);

		if (hRedirMessage == INVALID_QMSG_HANDLE)
			return ErrGetErrorCode();

		char szQueueFilePath[SYS_MAX_PATH] = "";

		QueGetFilePath(hSpoolQueue, hRedirMessage, szQueueFilePath);

		char szAliasAddr[MAX_ADDR_NAME] = "";

		if (strchr(ppszCmdTokens[i], '@') == NULL)
			SysSNPrintf(szAliasAddr, sizeof(szAliasAddr) - 1, "%s@%s",
				    pUI->pszName, ppszCmdTokens[i]);
		else
			StrSNCpy(szAliasAddr, ppszCmdTokens[i]);

		if (USmlCreateSpoolFile(hFSpool, szUserAddress, szAliasAddr, szQueueFilePath,
					"X-Deliver-To", szUserAddress, NULL) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hRedirMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hRedirMessage);
			QueCloseMessage(hSpoolQueue, hRedirMessage);
			return ErrorPop();
		}
		/* Log the redir operation */
		if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
			USmlLogMessage(hFSpool, "LREDIR", NULL, ppszCmdTokens[i]);
	}

	return 0;
}

static int USmlCmd_smtprelay(char **ppszCmdTokens, int iNumTokens, SVRCFG_HANDLE hSvrConfig,
			     UserInfo *pUI, SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			     QMSG_HANDLE hMessage, LocalMailProcConfig &LMPC)
{
	/* Apply filters ... */
	if (FilFilterMessage(hFSpool, hQueue, hMessage, FILTER_MODE_OUTBOUND, pUI) < 0)
		return ErrGetErrorCode();

	if (iNumTokens < 2) {
		ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
		return ERR_BAD_MAILPROC_CMD_SYNTAX;
	}

	char **ppszRelays = NULL;

	if (ppszCmdTokens[1][0] == '#') {
		if ((ppszRelays = StrTokenize(ppszCmdTokens[1] + 1, ";")) != NULL)
			MscRandomizeStringsOrder(ppszRelays);
	} else
		ppszRelays = StrTokenize(ppszCmdTokens[1], ";");

	if (ppszRelays == NULL)
		return ErrGetErrorCode();

	SMTPGateway **ppGws = USmtpGetCfgGateways(hSvrConfig, ppszRelays,
						  iNumTokens > 2 ? ppszCmdTokens[2]: NULL);

	StrFreeStrings(ppszRelays);
	if (ppGws == NULL)
		return ErrGetErrorCode();

	/* This function retrieve the spool file message section and sync the content. */
	/* This is necessary before sending the file */
	FileSection FSect;

	if (USmlGetMsgFileSection(hFSpool, FSect) < 0) {
		ErrorPush();
		USmtpFreeGateways(ppGws);
		return ErrorPop();
	}
	/* Get spool file infos */
	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
	char const *pszMailFrom = USmlMailFrom(hFSpool);
	char const *pszRcptTo = USmlRcptTo(hFSpool);
	char const *pszSendMailFrom = USmlSendMailFrom(hFSpool);
	char const *pszSendRcptTo = USmlSendRcptTo(hFSpool);
	char const *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);

	/* Get HELO domain */
	char szHeloDomain[MAX_HOST_NAME] = "";

	SvrConfigVar("HeloDomain", szHeloDomain, sizeof(szHeloDomain) - 1, hSvrConfig, "");

	char const *pszHeloDomain = IsEmptyString(szHeloDomain) ? NULL: szHeloDomain;

	SMTPError SMTPE;

	USmtpInitError(&SMTPE);

	/*
	 * By initializing this to zero makes XMail to discharge all mail for domains
	 * that have an empty relay list
	 */
	int iReturnCode = 0;

	for (int i = 0; ppGws[i] != NULL && iReturnCode >= 0; i++) {
		SysLogMessage(LOG_LEV_MESSAGE,
			      "USMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
			      ppGws[i]->pszHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

		USmtpCleanupError(&SMTPE);

		if (USmtpSendMail(ppGws[i], pszHeloDomain, pszSendMailFrom,
				  pszSendRcptTo, &FSect, &SMTPE) == 0) {
			/* Log Mailer operation */
			if (LMPC.ulFlags & LMPCF_LOG_ENABLED) {
				char szRmtMsgID[256] = "";

				USmtpGetSMTPRmtMsgID(USmtpGetErrorMessage(&SMTPE), szRmtMsgID,
						     sizeof(szRmtMsgID));
				USmlLogMessage(hFSpool, "RLYS", szRmtMsgID, ppGws[i]->pszHost);
			}
			USmtpCleanupError(&SMTPE);
			USmtpFreeGateways(ppGws);

			return 0;
		}

		int iErrorCode = ErrGetErrorCode();
		char szSmtpError[512] = "";

		USmtpGetSMTPError(&SMTPE, szSmtpError, sizeof(szSmtpError));

		ErrLogMessage(LOG_LEV_MESSAGE,
			      "USMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n"
			      "%s = \"%s\"\n", ppGws[i]->pszHost, pszSMTPDomain, pszMailFrom,
			      pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError);

		QueUtErrLogMessage(hQueue, hMessage,
				   "USMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n"
				   "%s = \"%s\"\n", ppGws[i]->pszHost, pszSMTPDomain, pszMailFrom,
				   pszRcptTo, SMTP_ERROR_VARNAME, szSmtpError);

		/* If a permanent SMTP error has been detected, then notify the message sender */
		if (USmtpIsFatalError(&SMTPE))
			QueUtNotifyPermErrDelivery(hQueue, hMessage, hFSpool,
						   USmtpGetErrorMessage(&SMTPE),
						   USmtpGetErrorServer(&SMTPE), false);

		iReturnCode = USmtpIsFatalError(&SMTPE) ? iErrorCode: -iErrorCode;
	}
	USmtpCleanupError(&SMTPE);
	USmtpFreeGateways(ppGws);

	return iReturnCode;
}

static int USmlProcessCustomMailingFile(SVRCFG_HANDLE hSvrConfig, UserInfo *pUI,
					SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
					QMSG_HANDLE hMessage, char const *pszMPFile,
					LocalMailProcConfig &LMPC)
{
	/* Open the mail processing file */
	FILE *pMPFile = fopen(pszMPFile, "rt");

	if (pMPFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszMPFile);
		return ERR_FILE_OPEN;
	}
	/* Create pushback command file */
	char szTmpFile[SYS_MAX_PATH] = "";

	UsrGetTmpFile(NULL, szTmpFile, sizeof(szTmpFile));

	FILE *pPushBFile = fopen(szTmpFile, "wt");

	if (pPushBFile == NULL) {
		fclose(pMPFile);

		ErrSetErrorCode(ERR_FILE_CREATE, szTmpFile);
		return ERR_FILE_CREATE;
	}

	int iPushBackCmds = 0;
	char szCmdLine[CUSTOM_CMD_LINE_MAX] = "";

	while (MscGetConfigLine(szCmdLine, sizeof(szCmdLine) - 1, pMPFile) != NULL) {
		char **ppszCmdTokens = StrGetTabLineStrings(szCmdLine);

		if (ppszCmdTokens == NULL)
			continue;

		int iFieldsCount = StrStringsCount(ppszCmdTokens);

		if (iFieldsCount > 0) {
			/* Do command line macro substitution */
			USmlCmdMacroSubstitutes(ppszCmdTokens, pUI, hFSpool);

			int iCmdResult = 0;

			if (stricmp(ppszCmdTokens[0], "external") == 0)
				iCmdResult =
					USmlCmd_external(ppszCmdTokens, iFieldsCount, hSvrConfig, pUI,
							 hFSpool, hQueue, hMessage, LMPC);
			else if (stricmp(ppszCmdTokens[0], "filter") == 0)
				iCmdResult =
					USmlCmd_filter(ppszCmdTokens, iFieldsCount, hSvrConfig, pUI,
						       hFSpool, hQueue, hMessage, LMPC);
			else if (stricmp(ppszCmdTokens[0], "mailbox") == 0)
				iCmdResult =
					USmlCmd_mailbox(ppszCmdTokens, iFieldsCount, hSvrConfig, pUI,
							hFSpool, hQueue, hMessage, LMPC);
			else if (stricmp(ppszCmdTokens[0], "redirect") == 0)
				iCmdResult =
					USmlCmd_redirect(ppszCmdTokens, iFieldsCount, hSvrConfig, pUI,
							 hFSpool, hQueue, hMessage, LMPC);
			else if (stricmp(ppszCmdTokens[0], "lredirect") == 0)
				iCmdResult =
					USmlCmd_lredirect(ppszCmdTokens, iFieldsCount, hSvrConfig,
							  pUI, hFSpool, hQueue, hMessage, LMPC);
			else if (stricmp(ppszCmdTokens[0], "smtprelay") == 0)
				iCmdResult =
					USmlCmd_smtprelay(ppszCmdTokens, iFieldsCount, hSvrConfig,
							  pUI, hFSpool, hQueue, hMessage, LMPC);
			else {
				SysLogMessage(LOG_LEV_ERROR,
					      "Invalid command \"%s\" in file \"%s\"\n",
					      ppszCmdTokens[0], pszMPFile);

			}

			/* Check for the stop-processing error code */
			if (iCmdResult == SMAIL_STOP_PROCESSING) {
				StrFreeStrings(ppszCmdTokens);
				break;
			}
			/* Test if we must save a failed command */
			/* <0 = Error ; ==0 = Success ; >0 = Transient error ( save the command ) */
			if (iCmdResult > 0) {
				fprintf(pPushBFile, "%s\n", szCmdLine);

				++iPushBackCmds;
			}
			/* An error code might result if filters blocked the message. If this is the */
			/* case QueCheckMessage() will return error and we MUST stop processing */
			if (iCmdResult < 0 &&
			    QueCheckMessage(hQueue, hMessage) < 0) {
				ErrorPush();
				StrFreeStrings(ppszCmdTokens);
				fclose(pPushBFile);
				fclose(pMPFile);
				SysRemove(szTmpFile);
				return ErrorPop();
			}
		}
		StrFreeStrings(ppszCmdTokens);
	}
	fclose(pPushBFile);
	fclose(pMPFile);
	SysRemove(pszMPFile);

	if (iPushBackCmds > 0) {
		/* If commands left out of processing, push them into the custom file */
		if (MscMoveFile(szTmpFile, pszMPFile) < 0)
			return ErrGetErrorCode();

		ErrSetErrorCode(ERR_INCOMPLETE_PROCESSING);
		return ERR_INCOMPLETE_PROCESSING;
	}
	SysRemove(szTmpFile);

	return 0;
}

int USmlProcessLocalUserMessage(SVRCFG_HANDLE hSvrConfig, UserInfo *pUI, SPLF_HANDLE hFSpool,
				QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				LocalMailProcConfig &LMPC)
{
	/* Exist user custom message processing ? */
	char szMPFile[SYS_MAX_PATH] = "";

	if (USmlGetMailProcessFile(pUI, hQueue, hMessage, szMPFile) < 0) {
		/*
		 * Deliver the file locally.
		 */
		if (USmlLocalDelivery(hSvrConfig, pUI, hFSpool, hQueue, hMessage, LMPC) < 0)
			return ErrGetErrorCode();
	} else {
		/* Process custom mailings */
		if (USmlProcessCustomMailingFile(hSvrConfig, pUI, hFSpool, hQueue, hMessage,
						 szMPFile, LMPC) < 0)
			return ErrGetErrorCode();

	}

	return 0;
}

int USmlGetDomainCustomDir(char *pszCustomDir, int iMaxPath, int iFinalSlash)
{
	CfgGetRootPath(pszCustomDir, iMaxPath);

	StrNCat(pszCustomDir, SMAIL_DOMAIN_PROC_DIR, iMaxPath);
	if (iFinalSlash)
		AppendSlash(pszCustomDir);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at directory: '%s'\n", pszCustomDir);

	return 0;
}

int USmlGetCmdAliasDir(char *pszAliasDir, int iMaxPath, int iFinalSlash)
{
	CfgGetRootPath(pszAliasDir, iMaxPath);

	StrNCat(pszAliasDir, SMAIL_CMDALIAS_DIR, iMaxPath);
	if (iFinalSlash)
		AppendSlash(pszAliasDir);

	SysLogMessage(LOG_LEV_DEBUG, "Going to look at directory: '%s'\n", pszAliasDir);

	return 0;
}

int USmlGetCmdAliasFile(char const *pszDomain, char const *pszUser, char *pszAliasFile)
{
	char szAliasDir[SYS_MAX_PATH] = "";

	USmlGetCmdAliasDir(szAliasDir, sizeof(szAliasDir), 1);

	SysSNPrintf(pszAliasFile, SYS_MAX_PATH - 1, "%s%s%s%s.tab",
		    szAliasDir, pszDomain, SYS_SLASH_STR, pszUser);

	StrLower(pszAliasFile + strlen(szAliasDir));

	return 0;
}

int USmlIsCmdAliasAccount(char const *pszDomain, char const *pszUser, char *pszAliasFile)
{
	char szAliasFile[SYS_MAX_PATH] = "";

	if (pszAliasFile == NULL)
		pszAliasFile = szAliasFile;

	if (USmlGetCmdAliasFile(pszDomain, pszUser, pszAliasFile) < 0)
		return ErrGetErrorCode();

	if (!SysExistFile(pszAliasFile)) {
		ErrSetErrorCode(ERR_NOT_A_CMD_ALIAS);
		return ERR_NOT_A_CMD_ALIAS;
	}

	return 0;
}

int USmlCreateCmdAliasDomainDir(char const *pszDomain)
{
	char szAliasDir[SYS_MAX_PATH] = "";
	char szDomainAliasDir[SYS_MAX_PATH] = "";

	USmlGetCmdAliasDir(szAliasDir, sizeof(szAliasDir), 1);

	SysSNPrintf(szDomainAliasDir, sizeof(szDomainAliasDir) - 1, "%s%s",
		    szAliasDir, pszDomain);

	StrLower(szDomainAliasDir + strlen(szAliasDir));

	if (SysMakeDir(szDomainAliasDir) < 0)
		return ErrGetErrorCode();

	return 0;
}

int USmlDeleteCmdAliasDomainDir(char const *pszDomain)
{
	char szAliasDir[SYS_MAX_PATH] = "";
	char szDomainAliasDir[SYS_MAX_PATH] = "";

	USmlGetCmdAliasDir(szAliasDir, sizeof(szAliasDir), 1);

	SysSNPrintf(szDomainAliasDir, sizeof(szDomainAliasDir) - 1, "%s%s",
		    szAliasDir, pszDomain);
	StrLower(szDomainAliasDir + strlen(szAliasDir));
	if (SysExistDir(szDomainAliasDir)) {
		if (MscClearDirectory(szDomainAliasDir) < 0)
			return ErrGetErrorCode();
		if (SysRemoveDir(szDomainAliasDir) < 0)
			return ErrGetErrorCode();
	}

	return 0;
}

int USmlGetCmdAliasSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszAliasFilePath)
{
	return QueGetFilePath(hQueue, hMessage, pszAliasFilePath, QUEUE_CUST_DIR);
}

int USmlGetCmdAliasCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			      QMSG_HANDLE hMessage, char const *pszDomain, char const *pszUser,
			      char *pszAliasFilePath)
{
	/* Check if exist a spooled copy */
	char const *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);

	USmlGetCmdAliasSpoolFile(hQueue, hMessage, pszAliasFilePath);

	if (SysExistFile(pszAliasFilePath))
		return 0;

	/* Check if this is a cmd alias */
	char szAliasFile[SYS_MAX_PATH] = "";

	if (USmlIsCmdAliasAccount(pszDomain, pszUser, szAliasFile) < 0)
		return ErrGetErrorCode();

	RLCK_HANDLE hResLock = RLckLockSH(szAliasFile);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* Make a copy into the spool */
	if (MscCopyFile(pszAliasFilePath, szAliasFile) < 0) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}

	RLckUnlockSH(hResLock);

	return 0;
}

int USmlDomainCustomFileName(char const *pszDestDomain, char *pszCustFilePath)
{
	/* Make domain name lower case */
	char szDestDomain[MAX_HOST_NAME] = "";

	StrSNCpy(szDestDomain, pszDestDomain);
	StrLower(szDestDomain);

	/* Build file name */
	char szCustomDir[SYS_MAX_PATH] = "";

	USmlGetDomainCustomDir(szCustomDir, sizeof(szCustomDir), 1);

	SysSNPrintf(pszCustFilePath, SYS_MAX_PATH - 1, "%s%s.tab", szCustomDir, szDestDomain);

	return 0;
}

int USmlGetDomainCustomFile(char const *pszDestDomain, char *pszCustFilePath)
{
	/* Make domain name lower case */
	char szDestDomain[MAX_HOST_NAME] = "";

	StrSNCpy(szDestDomain, pszDestDomain);
	StrLower(szDestDomain);

	/* Lookup custom files */
	char szCustomDir[SYS_MAX_PATH] = "";

	USmlGetDomainCustomDir(szCustomDir, sizeof(szCustomDir), 1);

	for (char const *pszSubDom = szDestDomain; pszSubDom != NULL;
	     pszSubDom = strchr(pszSubDom + 1, '.')) {
		SysSNPrintf(pszCustFilePath, SYS_MAX_PATH - 1, "%s%s.tab", szCustomDir,
			    pszSubDom);

		if (SysExistFile(pszCustFilePath))
			return 0;
	}

	SysSNPrintf(pszCustFilePath, SYS_MAX_PATH - 1, "%s.tab", szCustomDir);
	if (SysExistFile(pszCustFilePath))
		return 0;

	ErrSetErrorCode(ERR_NOT_A_CUSTOM_DOMAIN);
	return ERR_NOT_A_CUSTOM_DOMAIN;
}

int USmlGetDomainCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszCustFilePath)
{
	return QueGetFilePath(hQueue, hMessage, pszCustFilePath, QUEUE_CUST_DIR);
}

int USmlGetUserCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszCustFilePath)
{
	return QueGetFilePath(hQueue, hMessage, pszCustFilePath, QUEUE_MPRC_DIR);
}

int USmlGetDomainMsgCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			       QMSG_HANDLE hMessage, char const *pszDestDomain,
			       char *pszCustFilePath)
{
	/* Check if exist a spooled copy */
	char const *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);

	USmlGetDomainCustomSpoolFile(hQueue, hMessage, pszCustFilePath);

	if (SysExistFile(pszCustFilePath))
		return 0;

	/* Check if this is a custom domain */
	char szCustDomainFile[SYS_MAX_PATH] = "";

	if (USmlGetDomainCustomFile(pszDestDomain, szCustDomainFile) < 0)
		return ErrGetErrorCode();

	RLCK_HANDLE hResLock = RLckLockSH(szCustDomainFile);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* Make a copy into the spool */
	if (MscCopyFile(pszCustFilePath, szCustDomainFile) < 0) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}

	RLckUnlockSH(hResLock);

	return 0;
}

int USmlGetCustomDomainFile(char const *pszDestDomain, char const *pszCustFilePath)
{
	/* Check if this is a custom domain */
	char szCustDomainFile[SYS_MAX_PATH] = "";

	if (USmlGetDomainCustomFile(pszDestDomain, szCustDomainFile) < 0)
		return ErrGetErrorCode();

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockSH(CfgGetBasedPath(szCustDomainFile, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	/* Make a copy onto user supplied file */
	if (MscCopyFile(pszCustFilePath, szCustDomainFile) < 0) {
		ErrorPush();
		RLckUnlockSH(hResLock);
		return ErrorPop();
	}

	RLckUnlockSH(hResLock);

	return 0;
}

int USmlSetCustomDomainFile(char const *pszDestDomain, char const *pszCustFilePath)
{
	/* Check if this is a custom domain */
	char szCustDomainFile[SYS_MAX_PATH] = "";

	USmlDomainCustomFileName(pszDestDomain, szCustDomainFile);

	char szResLock[SYS_MAX_PATH] = "";
	RLCK_HANDLE hResLock = RLckLockEX(CfgGetBasedPath(szCustDomainFile, szResLock,
							  sizeof(szResLock)));

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	if (pszCustFilePath != NULL) {
		/* Overwrite current file */
		if (MscCopyFile(szCustDomainFile, pszCustFilePath) < 0) {
			ErrorPush();
			RLckUnlockEX(hResLock);
			return ErrorPop();
		}
	} else
		SysRemove(szCustDomainFile);

	RLckUnlockEX(hResLock);

	return 0;
}

int USmlCustomizedDomain(char const *pszDestDomain)
{
	char szCustFilePath[SYS_MAX_PATH] = "";

	return USmlGetDomainCustomFile(pszDestDomain, szCustFilePath);
}

static int USmlLogMessage(char const *pszSMTPDomain, char const *pszMessageID,
			  char const *pszSmtpMessageID, char const *pszFrom, char const *pszRcpt,
			  char const *pszMedium, char const *pszParam, char const *pszRmtMsgID)
{
	char szTime[256] = "";

	MscGetTimeNbrString(szTime, sizeof(szTime) - 1);

	RLCK_HANDLE hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR SMAIL_LOG_FILE);

	if (hResLock == INVALID_RLCK_HANDLE)
		return ErrGetErrorCode();

	MscFileLog(SMAIL_LOG_FILE, "\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\t\"%s\""
		   "\n", pszSMTPDomain, pszMessageID, pszSmtpMessageID, pszFrom, pszRcpt,
		   pszMedium, pszParam, szTime, pszRmtMsgID);

	RLckUnlockEX(hResLock);

	return 0;
}

int USmlLogMessage(SPLF_HANDLE hFSpool, char const *pszMedium, char const *pszRmtMsgID,
		   char const *pszParam)
{
	char const *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
	char const *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
	char const *pszMessageID = USmlGetSpoolFile(hFSpool);
	char const *pszMailFrom = USmlMailFrom(hFSpool);
	char const *pszRcptTo = USmlRcptTo(hFSpool);

	return USmlLogMessage(pszSMTPDomain, pszMessageID, pszSmtpMessageID, pszMailFrom,
			      pszRcptTo, pszMedium, pszParam,
			      pszRmtMsgID != NULL ? pszRmtMsgID: "");
}

static int USmlRFC822_ATEXT(int c)
{
	return isalpha(c) || isdigit(c) || strchr("!#$%&'*+-/=?^_`{|}~", c) != NULL;
}

char const *USmlDotAtom(char const *pszStr, char const *pszTop)
{
	for (;;) {
		char const *pszAtom = pszStr;

		for (; pszStr < pszTop && USmlRFC822_ATEXT(*pszStr); pszStr++);
		if (pszAtom == pszStr)
			return NULL;
		if (pszStr == pszTop || *pszStr != '.')
			break;
		pszStr++;
	}

	return pszStr;
}

static char const *USmlIPDomain(char const *pszAddress, char const *pszTop)
{
	int iSize;
	char const *pszNext;
	SYS_INET_ADDR Addr;
	char szIP[256];

	if (*pszAddress != '[' ||
	    (pszNext = (char const *) memchr(pszAddress, ']',
					     (int) (pszTop - pszAddress))) == NULL ||
	    (iSize = (int) (pszNext - pszAddress) - 1) >= (int) sizeof(szIP)) {
		ErrSetErrorCode(ERR_BAD_TAG_ADDRESS, pszAddress);
		return NULL;
	}
	Cpy2Sz(szIP, pszAddress + 1, iSize);
	if (SysGetHostByName(szIP, -1, Addr) < 0)
		return NULL;

	return pszNext + 1;
}

char const *USmlParseHost(char const *pszHost, char const *pszTop)
{
	char const *pszNext;

	if (*pszHost == '[')
		pszNext = USmlIPDomain(pszHost, pszTop);
	else if ((pszNext = USmlDotAtom(pszHost, pszTop)) == NULL)
		ErrSetErrorCode(ERR_BAD_TAG_ADDRESS, pszHost, (int) (pszTop - pszHost));

	return pszNext;
}

int USmlValidHost(char const *pszHost, char const *pszTop)
{
	char const *pszEOH;

	if ((pszEOH = USmlParseHost(pszHost, pszTop)) == NULL)
		return ErrGetErrorCode();
	if (pszEOH != pszTop) {
		ErrSetErrorCode(ERR_INVALID_HOSTNAME, pszHost,
				(int) (pszTop - pszHost));
		return ERR_INVALID_HOSTNAME;
	}

	return 0;
}

int USmlValidAddress(char const *pszAddress, char const *pszTop)
{
	char const *pszAddr = pszAddress;

	if (*pszAddr == '@') {
		for (;;) {
			if (*pszAddr++ != '@' ||
			    (pszAddr = USmlParseHost(pszAddr, pszTop)) == NULL)
				return ErrGetErrorCode();
			if (*pszAddr != ',')
				break;
			pszAddr++;
		}
		if (*pszAddr++ != ':') {
			ErrSetErrorCode(ERR_BAD_TAG_ADDRESS, pszAddress,
					(int) (pszTop - pszAddress));
			return ERR_BAD_TAG_ADDRESS;
		}
	}
	if ((pszAddr = USmlDotAtom(pszAddr, pszTop)) == NULL ||
	    *pszAddr++ != '@') {
		ErrSetErrorCode(ERR_BAD_TAG_ADDRESS, pszAddress,
				(int) (pszTop - pszAddress));
		return ERR_BAD_TAG_ADDRESS;
	}

	return USmlValidHost(pszAddr, pszTop);
}

int USmlParseAddress(char const *pszAddress, char *pszPreAddr,
		     int iMaxPreAddress, char *pszEmailAddr, int iMaxAddress)
{
	StrSkipSpaces(pszAddress);
	if (*pszAddress == '\0') {
		ErrSetErrorCode(ERR_EMPTY_ADDRESS);
		return ERR_EMPTY_ADDRESS;
	}

	char const *pszOpen = strrchr(pszAddress, '<');

	if (pszOpen != NULL) {
		char const *pszClose = strrchr(pszOpen + 1, '>');

		if (pszClose == NULL) {
			ErrSetErrorCode(ERR_BAD_TAG_ADDRESS);
			return ERR_BAD_TAG_ADDRESS;
		}
		if (pszClose == pszOpen + 1) {
			ErrSetErrorCode(ERR_EMPTY_ADDRESS);
			return ERR_EMPTY_ADDRESS;
		}
		if (pszPreAddr != NULL) {
			int iPreCount = Min((int) (pszOpen - pszAddress), iMaxPreAddress - 1);

			strncpy(pszPreAddr, pszAddress, iPreCount);
			pszPreAddr[iPreCount] = '\0';
			StrTrim(pszPreAddr, " \t");
		}
		if (USmlValidAddress(pszOpen + 1, pszClose) < 0)
			return ErrGetErrorCode();
		if (pszEmailAddr != NULL) {
			int iEmailCount = (int) Min((pszClose - pszOpen) - 1, iMaxAddress - 1);

			if (iEmailCount < 0) {
				ErrSetErrorCode(ERR_BAD_TAG_ADDRESS);
				return ERR_BAD_TAG_ADDRESS;
			}
			strncpy(pszEmailAddr, pszOpen + 1, iEmailCount);
			pszEmailAddr[iEmailCount] = '\0';
			StrTrim(pszEmailAddr, " \t");
		}
	} else {
		int iAddrLen = (int)strlen(pszAddress);

		if (iAddrLen == 0) {
			ErrSetErrorCode(ERR_EMPTY_ADDRESS);
			return ERR_EMPTY_ADDRESS;
		}
		if (USmlValidAddress(pszAddress, pszAddress + iAddrLen) < 0)
			return ErrGetErrorCode();
		if (pszPreAddr != NULL)
			SetEmptyString(pszPreAddr);
		if (pszEmailAddr != NULL) {
			strncpy(pszEmailAddr, pszAddress, iMaxAddress - 1);
			pszEmailAddr[iMaxAddress - 1] = '\0';
			StrTrim(pszEmailAddr, " \t");
		}
	}

	return 0;
}

static int USmlExtractFromAddress(HSLIST &hTagList, char *pszFromAddr, int iMaxAddress)
{
	/* Try to discover the "Return-Path" ( or eventually "From" ) tag to setup */
	/* the "MAIL FROM: <>" part of the spool message */
	TAG_POSITION TagPosition = TAG_POSITION_INIT;
	MessageTagData *pMTD = USmlFindTag(hTagList, "Return-Path", TagPosition);

	if (pMTD != NULL &&
	    USmlParseAddress(pMTD->pszTagData, NULL, 0, pszFromAddr, iMaxAddress) == 0)
		return 0;

	TagPosition = TAG_POSITION_INIT;

	if ((pMTD = USmlFindTag(hTagList, "From", TagPosition)) != NULL &&
	    USmlParseAddress(pMTD->pszTagData, NULL, 0, pszFromAddr, iMaxAddress) == 0)
		return 0;

	ErrSetErrorCode(ERR_MAILFROM_UNKNOWN);
	return ERR_MAILFROM_UNKNOWN;
}

/* [i_a] -- routine is replicated in Sendmail:AddressFromAtPtr() */
static char const *USmlAddressFromAtPtr(char const *pszAt, char const *pszBase,
					char *pszAddress, int iMaxAddress)
{
	char const *pszStart = pszAt;

	for (; (pszStart >= pszBase) && (strchr("<> \t,\":;'\r\n", *pszStart) == NULL);
	     pszStart--);

	++pszStart;

	char const *pszEnd = pszAt + 1;

	for (; (*pszEnd != '\0') && (strchr("<> \t,\":;'\r\n", *pszEnd) == NULL); pszEnd++);

	int iAddrLength = Min((int) (pszEnd - pszStart), iMaxAddress - 1);

	Cpy2Sz(pszAddress, pszStart, iAddrLength);

	return pszEnd;
}

static char const *USmlAddSingleAddress(char const *pszCurr, char const *pszBase,
					DynString *pAddrDS, char const *const *ppszMatchDomains,
					int *piAdded)
{
	*piAdded = 0;

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
		return NULL;

	char szAddress[MAX_SMTP_ADDRESS] = "";
	char szDomain[MAX_ADDR_NAME] = "";

	if ((pszCurr = USmlAddressFromAtPtr(pszAt, pszBase, szAddress,
					    sizeof(szAddress) - 1)) != NULL &&
	    USmtpSplitEmailAddr(szAddress, NULL, szDomain) == 0 &&
	    (ppszMatchDomains == NULL || ppszMatchDomains[0] == NULL || StrStringsIMatch(ppszMatchDomains, szDomain)) &&  /* [i_a] */
	    StrIStr(StrDynGet(pAddrDS), szAddress) == NULL) {
		if (StrDynSize(pAddrDS) > 0)
			StrDynAdd(pAddrDS, ADDRESS_TOKENIZER);

		StrDynAdd(pAddrDS, szAddress);
		++(*piAdded);
	}

	return pszCurr;
}

static int USmlAddAddresses(char const *pszAddrList, DynString *pAddrDS,
			    char const *const *ppszMatchDomains)
{
	int iAddrAdded = 0;
	int iAdded;
	char const *pszCurr = pszAddrList;

	for (; (pszCurr != NULL) && (*pszCurr != '\0');) {
		pszCurr = USmlAddSingleAddress(pszCurr, pszAddrList, pAddrDS, ppszMatchDomains,
					       &iAdded);
		if (iAdded)
			++iAddrAdded;
	}

	return iAddrAdded;
}

static char **USmlGetAddressList(HSLIST &hTagList, char const *const *ppszMatchDomains,
				 char const *const *ppszAddrTags)
{
	DynString AddrDS;

	StrDynInit(&AddrDS);
	for (int i = 0; ppszAddrTags[i] != NULL; i++) {
		char const *pszHdrTag = (strchr("+", ppszAddrTags[i][0]) != NULL) ?
			ppszAddrTags[i] + 1: ppszAddrTags[i];
		TAG_POSITION TagPosition = TAG_POSITION_INIT;
		MessageTagData *pMTD = USmlFindTag(hTagList, pszHdrTag, TagPosition);

		for (; pMTD != NULL; pMTD = USmlFindTag(hTagList, pszHdrTag, TagPosition)) {
			int iAddrAdded = USmlAddAddresses(pMTD->pszTagData, &AddrDS,
							  ppszMatchDomains);

			/* Exclusive tag detected, stop the scan */
			if (iAddrAdded > 0 && ppszAddrTags[i][0] == '+')
				goto BuildAddrList;
		}
	}

	/* Yes, i know, goto's might be bad. Not in this case though ... */
BuildAddrList:

	char **ppszAddresses = StrTokenize(StrDynGet(&AddrDS), ADDRESS_TOKENIZER);

	StrDynFree(&AddrDS);

	return ppszAddresses;
}

static int USmlExtractToAddress(HSLIST &hTagList, char *pszToAddr, int iMaxAddress)
{
	/* Try to extract the "To:" tag from the mail headers */
	TAG_POSITION TagPosition = TAG_POSITION_INIT;
	MessageTagData *pMTD = USmlFindTag(hTagList, "To", TagPosition);

	if (pMTD == NULL ||
	    USmlParseAddress(pMTD->pszTagData, NULL, 0, pszToAddr, iMaxAddress) < 0) {
		ErrSetErrorCode(ERR_RCPTTO_UNKNOWN);
		return ERR_RCPTTO_UNKNOWN;
	}

	return 0;
}

static char **USmlBuildTargetRcptList(char const *pszRcptTo, HSLIST &hTagList,
				      char const *pszFetchHdrTags)
{
	char **ppszRcptList = NULL;
	char **ppszAddrTags = NULL;

	if (pszFetchHdrTags != NULL &&
	    (ppszAddrTags = StrTokenize(pszFetchHdrTags, ",")) == NULL)
		return NULL;

	if (pszRcptTo == NULL) {
		if (ppszAddrTags == NULL) {
			ErrSetErrorCode(ERR_NO_HDR_FETCH_TAGS);
			return NULL;
		}
		/* If the recipient is NULL try to extract addresses from the message using */
		/* the supplied tag string */
		if ((ppszRcptList = USmlGetAddressList(hTagList, NULL, ppszAddrTags)) == NULL) {
			StrFreeStrings(ppszAddrTags);
			return NULL;
		}
	} else if (*pszRcptTo == '?') {
		if (ppszAddrTags == NULL) {
			ErrSetErrorCode(ERR_NO_HDR_FETCH_TAGS);
			return NULL;
		}
		/* Extract matching domains */
		char **ppszDomains = StrTokenize(pszRcptTo, ",");

		if (ppszDomains == NULL) {
			StrFreeStrings(ppszAddrTags);
			return NULL;
		}
		/* We need to masquerade incoming domain. In this case "pszRcptTo" is made by */
		/* "?" + masquerade-domain */
		if ((ppszRcptList = USmlGetAddressList(hTagList, &ppszDomains[1],
						       ppszAddrTags)) == NULL) {
			StrFreeStrings(ppszDomains);
			StrFreeStrings(ppszAddrTags);
			return NULL;
		}

		int iAddrCount = StrStringsCount(ppszRcptList);

		for (int i = 0; i < iAddrCount; i++) {
			char szToUser[MAX_ADDR_NAME] = "";
			char szRecipient[MAX_ADDR_NAME] = "";

			if (USmtpSplitEmailAddr(ppszRcptList[i], szToUser, NULL) == 0) {
				SysSNPrintf(szRecipient, sizeof(szRecipient) - 1, "%s@%s",
					    szToUser, ppszDomains[0] + 1);

				SysFree(ppszRcptList[i]);

				ppszRcptList[i] = SysStrDup(szRecipient);
			}
		}
		StrFreeStrings(ppszDomains);
	} else if (*pszRcptTo == '&') {
		if (ppszAddrTags == NULL) {
			ErrSetErrorCode(ERR_NO_HDR_FETCH_TAGS);
			return NULL;
		}
		/* Extract matching domains */
		char **ppszDomains = StrTokenize(pszRcptTo, ",");

		if (ppszDomains == NULL) {
			StrFreeStrings(ppszAddrTags);
			return NULL;
		}
		/*
		 * We need to masquerade incoming domain. In this case "pszRcptTo" is made by
		 * "&" + add-domain + match-domains.
		 */
		if ((ppszRcptList = USmlGetAddressList(hTagList, &ppszDomains[1],
						       ppszAddrTags)) == NULL) {
			StrFreeStrings(ppszDomains);
			StrFreeStrings(ppszAddrTags);
			return NULL;
		}

		int iAddrCount = StrStringsCount(ppszRcptList);

		for (int i = 0; i < iAddrCount; i++) {
			char szRecipient[MAX_ADDR_NAME] = "";

			SysSNPrintf(szRecipient, sizeof(szRecipient) - 1, "%s%s",
				    ppszRcptList[i], ppszDomains[0] + 1);

			SysFree(ppszRcptList[i]);
			ppszRcptList[i] = SysStrDup(szRecipient);
		}
		StrFreeStrings(ppszDomains);
	} else
		ppszRcptList = StrBuildList(pszRcptTo, NULL);

	if (ppszAddrTags != NULL)
		StrFreeStrings(ppszAddrTags);

	return ppszRcptList;
}

int USmlDeliverFetchedMsg(char const *pszSyncAddr, char const *pszFetchHdrTags,
			  char const *pszMailFile)
{
	FILE *pMailFile = fopen(pszMailFile, "rb");

	if (pMailFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszMailFile); /* [i_a] */
		return ERR_FILE_OPEN;
	}
	/* Load message tags */
	HSLIST hTagList;

	ListInit(hTagList);

	USmlLoadTags(pMailFile, hTagList);

	/* Extract "MAIL FROM: <>" address */
	char szFromAddr[MAX_SMTP_ADDRESS] = "";

	USmlExtractFromAddress(hTagList, szFromAddr, sizeof(szFromAddr) - 1);

	/* Extract recipient list */
	char **ppszRcptList = USmlBuildTargetRcptList(pszSyncAddr, hTagList, pszFetchHdrTags);

	if (ppszRcptList == NULL) {
		ErrorPush();
		USmlFreeTagsList(hTagList);
		fclose(pMailFile);
		return ErrorPop();
	}

	USmlFreeTagsList(hTagList);

	/* Loop through extracted recipients and deliver */
	int iDeliverCount = 0;
	int iAddrCount = StrStringsCount(ppszRcptList);

	for (int i = 0; i < iAddrCount; i++) {
		/* Check address validity and skip invalid ( or not handled ) ones */
		char szDestDomain[MAX_HOST_NAME] = "";

		if (USmtpSplitEmailAddr(ppszRcptList[i], NULL, szDestDomain) < 0 ||
		    (MDomIsHandledDomain(szDestDomain) < 0 &&
		     USmlCustomizedDomain(szDestDomain) < 0))
			continue;

		/* Get message handle */
		QMSG_HANDLE hMessage = QueCreateMessage(hSpoolQueue);

		if (hMessage == INVALID_QMSG_HANDLE) {
			ErrorPush();
			StrFreeStrings(ppszRcptList);
			fclose(pMailFile);
			return ErrorPop();
		}

		char szQueueFilePath[SYS_MAX_PATH] = "";

		QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

		if (USmlCreateSpoolFile(pMailFile, NULL, szFromAddr,
					ppszRcptList[i], szQueueFilePath) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszRcptList);
			fclose(pMailFile);
			return ErrorPop();
		}
		/* Transfer file to the spool */
		if (QueCommitMessage(hSpoolQueue, hMessage) < 0) {
			ErrorPush();
			QueCleanupMessage(hSpoolQueue, hMessage);
			QueCloseMessage(hSpoolQueue, hMessage);
			StrFreeStrings(ppszRcptList);
			fclose(pMailFile);
			return ErrorPop();
		}

		++iDeliverCount;
	}

	StrFreeStrings(ppszRcptList);
	fclose(pMailFile);

	/* Check if the message has been delivered at least one time */
	if (iDeliverCount == 0) {
		ErrSetErrorCode(ERR_FETCHMSG_UNDELIVERED);
		return ERR_FETCHMSG_UNDELIVERED;
	}

	return 0;
}

int USmlMailLoopCheck(SPLF_HANDLE hFSpool, SVRCFG_HANDLE hSvrConfig)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	/* Count MTA ops */
	int iLoopsCount = 0;
	int iMaxMTAOps = SvrGetConfigInt("MaxMTAOps", MAX_MTA_OPS, hSvrConfig);
	MessageTagData *pMTD = (MessageTagData *) ListFirst(pSFD->hTagList);

	for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
		     ListNext(pSFD->hTagList, (PLISTLINK) pMTD))
		if (stricmp(pMTD->pszTagName, "Received") == 0 ||
		    stricmp(pMTD->pszTagName, "X-Deliver-To") == 0)
			++iLoopsCount;

	/* Check MTA count */
	if (iLoopsCount > iMaxMTAOps) {
		ErrSetErrorCode(ERR_MAIL_LOOP_DETECTED);
		return ERR_MAIL_LOOP_DETECTED;
	}

	return 0;
}

int USmlMessageAuth(SPLF_HANDLE hFSpool, char *pszAuthName, int iSize)
{
	SpoolFileData *pSFD = (SpoolFileData *) hFSpool;

	/* Looks for the "X-AuthUser" tag, that has to happen *before* the first */
	/* "Received" tag (to prevent forging) */
	MessageTagData *pMTD = (MessageTagData *) ListFirst(pSFD->hTagList);

	for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
		     ListNext(pSFD->hTagList, (PLISTLINK) pMTD)) {
		if (stricmp(pMTD->pszTagName, "Received") == 0)
			break;
		if (stricmp(pMTD->pszTagName, "X-AuthUser") == 0) {
			if (pszAuthName != NULL) {
				StrNCpy(pszAuthName, pMTD->pszTagData, iSize);
				StrTrim(pszAuthName, " \t\r\n");
			}
			return 0;
		}
	}

	ErrSetErrorCode(ERR_NO_MESSAGE_AUTH);
	return ERR_NO_MESSAGE_AUTH;
}

