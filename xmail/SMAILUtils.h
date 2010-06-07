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

#ifndef _SMAILUTILS_H
#define _SMAILUTILS_H

#define MAX_SPOOL_LINE                  1537

#define MAIL_FROM_STR                   "MAIL FROM:"
#define RCPT_TO_STR                     "RCPT TO:"
#define SPOOL_FILE_DATA_START           "<<MAIL-DATA>>"

#define INVALID_SPLF_HANDLE             ((SPLF_HANDLE) 0)

#define TAG_POSITION_INIT               ((TAG_POSITION) -1)

#define LMPCF_LOG_ENABLED               (1 << 0)

struct SpoolFileHeader {
	char szSpoolFile[SYS_MAX_PATH];
	char szSMTPDomain[MAX_ADDR_NAME];
	char szMessageID[128];
	char **ppszInfo;
	char **ppszFrom;
	char **ppszRcpt;
};

typedef struct SPLF_HANDLE_struct {
} *SPLF_HANDLE;

struct LocalMailProcConfig {
	unsigned long ulFlags;

};

typedef void *TAG_POSITION;

int USmlLoadSpoolFileHeader(const char *pszSpoolFile, SpoolFileHeader &SFH);
void USmlCleanupSpoolFileHeader(SpoolFileHeader &SFH);
char *USmlAddrConcat(const char *const *ppszStrings);
char *USmlBuildSendMailFrom(const char *const *ppszFrom, const char *const *ppszRcpt);
char *USmlBuildSendRcptTo(const char *const *ppszFrom, const char *const *ppszRcpt);
SPLF_HANDLE USmlCreateHandle(const char *pszMessFilePath);
void USmlCloseHandle(SPLF_HANDLE hFSpool);
int USmlReloadHandle(SPLF_HANDLE hFSpool);
const char *USmlGetRelayDomain(SPLF_HANDLE hFSpool);
const char *USmlGetSpoolFilePath(SPLF_HANDLE hFSpool);
const char *USmlGetSpoolFile(SPLF_HANDLE hFSpool);
const char *USmlGetSMTPDomain(SPLF_HANDLE hFSpool);
const char *USmlGetSmtpMessageID(SPLF_HANDLE hFSpool);
const char *const *USmlGetInfo(SPLF_HANDLE hFSpool);
const char *const *USmlGetMailFrom(SPLF_HANDLE hFSpool);
const char *USmlMailFrom(SPLF_HANDLE hFSpool);
const char *USmlSendMailFrom(SPLF_HANDLE hFSpool);
const char *const *USmlGetRcptTo(SPLF_HANDLE hFSpool);
const char *USmlRcptTo(SPLF_HANDLE hFSpool);
const char *USmlSendRcptTo(SPLF_HANDLE hFSpool);
SYS_OFF_T USmlMessageSize(SPLF_HANDLE hFSpool);
int USmlSyncChanges(SPLF_HANDLE hFSpool);
int USmlGetMsgFileSection(SPLF_HANDLE hFSpool, FileSection &FSect);
int USmlWriteMailFile(SPLF_HANDLE hFSpool, FILE *pMsgFile);
char *USmlGetTag(SPLF_HANDLE hFSpool, const char *pszTagName, TAG_POSITION &TagPosition);
int USmlAddTag(SPLF_HANDLE hFSpool, const char *pszTagName,
	       const char *pszTagData, int iUpdate = 0);
int USmlSetTagAddress(SPLF_HANDLE hFSpool, const char *pszTagName, const char *pszAddress);
int USmlMapAddress(const char *pszAddress, char *pszDomain, char *pszName);
int USmlCreateMBFile(UserInfo *pUI, const char *pszFileName, SPLF_HANDLE hFSpool);
int USmlVCreateSpoolFile(SPLF_HANDLE hFSpool, const char *pszFromUser,
			 const char *pszRcptUser, const char *pszFileName, va_list Headers);
int USmlCreateSpoolFile(SPLF_HANDLE hFSpool, const char *pszFromUser,
			const char *pszRcptUser, const char *pszFileName, ...);
int USmlProcessLocalUserMessage(SVRCFG_HANDLE hSvrConfig, UserInfo *pUI, SPLF_HANDLE hFSpool,
				QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				LocalMailProcConfig &LMPC);
int USmlGetDomainCustomDir(char *pszCustomDir, int iMaxPath, int iFinalSlash);
int USmlGetCmdAliasDir(char *pszAliasDir, int iMaxPath, int iFinalSlash);
int USmlGetCmdAliasFile(const char *pszDomain, const char *pszUser, char *pszAliasFile);
int USmlIsCmdAliasAccount(const char *pszDomain, const char *pszUser, char *pszAliasFile = NULL);
int USmlGetCmdAliasSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszAliasFilePath);
int USmlGetCmdAliasCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			      QMSG_HANDLE hMessage, const char *pszDomain, const char *pszUser,
			      char *pszAliasFilePath);
int USmlDomainCustomFileName(const char *pszDestDomain, char *pszCustFilePath);
int USmlGetDomainCustomFile(const char *pszDestDomain, char *pszCustFilePath);
int USmlCreateCmdAliasDomainDir(const char *pszDomain);
int USmlDeleteCmdAliasDomainDir(const char *pszDomain);
int USmlGetDomainCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				 char *pszCustFilePath);
int USmlGetUserCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszCustFilePath);
int USmlGetDomainMsgCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			       QMSG_HANDLE hMessage, const char *pszDestDomain,
			       char *pszCustFilePath);
int USmlGetCustomDomainFile(const char *pszDestDomain, const char *pszCustFilePath);
int USmlSetCustomDomainFile(const char *pszDestDomain, const char *pszCustFilePath);
int USmlCustomizedDomain(const char *pszDestDomain);
int USmlLogMessage(SPLF_HANDLE hFSpool, const char *pszMedium, const char *pszRmtMsgID,
		   const char *pszParam);
const char *USmlDotAtom(const char *pszStr, const char *pszTop);
int USmlValidAddress(const char *pszAddress, const char *pszTop);
int USmlValidHost(const char *pszHost, const char *pszTop);
int USmlParseAddress(const char *pszAddress, char *pszPreAddr,
		     int iMaxPreAddress, char *pszEmailAddr, int iMaxAddress);
int USmlDeliverFetchedMsg(const char *pszSyncAddr, const char *pszFetchHdrTags,
			  const char *pszMailFile);
int USmlMailLoopCheck(SPLF_HANDLE hFSpool, SVRCFG_HANDLE hSvrConfig);
int USmlMessageAuth(SPLF_HANDLE hFSpool, char *pszAuthName, int iSize);

#endif
