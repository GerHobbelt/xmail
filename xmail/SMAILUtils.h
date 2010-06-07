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

int USmlLoadSpoolFileHeader(char const *pszSpoolFile, SpoolFileHeader &SFH);
void USmlCleanupSpoolFileHeader(SpoolFileHeader &SFH);
char *USmlAddrConcat(char const *const *ppszStrings);
char *USmlBuildSendMailFrom(char const *const *ppszFrom, char const *const *ppszRcpt);
char *USmlBuildSendRcptTo(char const *const *ppszFrom, char const *const *ppszRcpt);
SPLF_HANDLE USmlCreateHandle(char const *pszMessFilePath);
void USmlCloseHandle(SPLF_HANDLE hFSpool);
int USmlReloadHandle(SPLF_HANDLE hFSpool);
char const *USmlGetRelayDomain(SPLF_HANDLE hFSpool);
char const *USmlGetSpoolFilePath(SPLF_HANDLE hFSpool);
char const *USmlGetSpoolFile(SPLF_HANDLE hFSpool);
char const *USmlGetSMTPDomain(SPLF_HANDLE hFSpool);
char const *USmlGetSmtpMessageID(SPLF_HANDLE hFSpool);
char const *const *USmlGetInfo(SPLF_HANDLE hFSpool);
char const *const *USmlGetMailFrom(SPLF_HANDLE hFSpool);
char const *USmlMailFrom(SPLF_HANDLE hFSpool);
char const *USmlSendMailFrom(SPLF_HANDLE hFSpool);
char const *const *USmlGetRcptTo(SPLF_HANDLE hFSpool);
char const *USmlRcptTo(SPLF_HANDLE hFSpool);
char const *USmlSendRcptTo(SPLF_HANDLE hFSpool);
SYS_OFF_T USmlMessageSize(SPLF_HANDLE hFSpool);
int USmlSyncChanges(SPLF_HANDLE hFSpool);
int USmlGetMsgFileSection(SPLF_HANDLE hFSpool, FileSection &FSect);
int USmlWriteMailFile(SPLF_HANDLE hFSpool, FILE *pMsgFile, bool bMBoxFile = false);
char *USmlGetTag(SPLF_HANDLE hFSpool, char const *pszTagName, TAG_POSITION &TagPosition);
int USmlAddTag(SPLF_HANDLE hFSpool, char const *pszTagName,
	       char const *pszTagData, int iUpdate = 0);
int USmlSetTagAddress(SPLF_HANDLE hFSpool, char const *pszTagName, char const *pszAddress);
int USmlMapAddress(char const *pszAddress, char *pszDomain, char *pszName);
int USmlCreateMBFile(UserInfo *pUI, char const *pszFileName, SPLF_HANDLE hFSpool);
int USmlVCreateSpoolFile(SPLF_HANDLE hFSpool, char const *pszFromUser,
			 char const *pszRcptUser, char const *pszFileName, va_list Headers);
int USmlCreateSpoolFile(SPLF_HANDLE hFSpool, char const *pszFromUser,
			char const *pszRcptUser, char const *pszFileName, ...);
int USmlProcessLocalUserMessage(SVRCFG_HANDLE hSvrConfig, UserInfo *pUI, SPLF_HANDLE hFSpool,
				QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				LocalMailProcConfig &LMPC);
int USmlGetDomainCustomDir(char *pszCustomDir, int iMaxPath, int iFinalSlash);
int USmlGetCmdAliasDir(char *pszAliasDir, int iMaxPath, int iFinalSlash);
int USmlGetCmdAliasFile(char const *pszDomain, char const *pszUser, char *pszAliasFile);
int USmlIsCmdAliasAccount(char const *pszDomain, char const *pszUser, char *pszAliasFile = NULL);
int USmlGetCmdAliasSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszAliasFilePath);
int USmlGetCmdAliasCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			      QMSG_HANDLE hMessage, char const *pszDomain, char const *pszUser,
			      char *pszAliasFilePath);
int USmlDomainCustomFileName(char const *pszDestDomain, char *pszCustFilePath);
int USmlGetDomainCustomFile(char const *pszDestDomain, char *pszCustFilePath);
int USmlCreateCmdAliasDomainDir(char const *pszDomain);
int USmlDeleteCmdAliasDomainDir(char const *pszDomain);
int USmlGetDomainCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
				 char *pszCustFilePath);
int USmlGetUserCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char *pszCustFilePath);
int USmlGetDomainMsgCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
			       QMSG_HANDLE hMessage, char const *pszDestDomain,
			       char *pszCustFilePath);
int USmlGetCustomDomainFile(char const *pszDestDomain, char const *pszCustFilePath);
int USmlSetCustomDomainFile(char const *pszDestDomain, char const *pszCustFilePath);
int USmlCustomizedDomain(char const *pszDestDomain);
int USmlLogMessage(SPLF_HANDLE hFSpool, char const *pszMedium, char const *pszRmtMsgID,
		   char const *pszParam);
char const *USmlDotAtom(char const *pszStr, char const *pszTop);
int USmlValidAddress(char const *pszAddress, char const *pszTop);
int USmlValidHost(char const *pszHost, char const *pszTop);
int USmlParseAddress(char const *pszAddress, char *pszPreAddr,
		     int iMaxPreAddress, char *pszEmailAddr, int iMaxAddress);
int USmlDeliverFetchedMsg(char const *pszSyncAddr, char const *pszFetchHdrTags,
			  char const *pszMailFile);
int USmlMailLoopCheck(SPLF_HANDLE hFSpool, SVRCFG_HANDLE hSvrConfig);
int USmlMessageAuth(SPLF_HANDLE hFSpool, char *pszAuthName, int iSize);

#endif
