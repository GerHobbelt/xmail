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
#include "StrUtils.h"

struct ErrorStrings {
	int iErrorCode;
	char const *pszError;
};

struct ErrorEnv {
	int iErrorNo;
	char *pszInfo[1];
};

static void ErrFreeEnv(void *pData);
static void ErrOnceSetup(void);
static ErrorEnv *ErrSetupEnv(void);

static SYS_THREAD_ONCE OnceSetup = SYS_THREAD_ONCE_INIT;
static SYS_TLSKEY ErrTlsKey;
static ErrorStrings Errors[] = {
	{ ERR_SUCCESS, "Success" },
	{ ERR_SERVER_SHUTDOWN, "Server shutdown" },
	{ ERR_MEMORY, "Memory allocation error" },
	{ ERR_NETWORK, "Network kernel error" },
	{ ERR_SOCKET_CREATE, "Cannot create socket" },
	{ ERR_TIMEOUT, "Timeout error" },
	{ ERR_LOCKED, "Already locked" },
	{ ERR_SOCKET_BIND, "Socket bind error" },
	{ ERR_CONF_PATH, "Mail root path not found" },
	{ ERR_USERS_FILE_NOT_FOUND, "Users table file not found" },
	{ ERR_FILE_CREATE, "Unable to create file" },
	{ ERR_USER_NOT_FOUND, "User not found" },
	{ ERR_USER_EXIST, "User already exist" },
	{ ERR_WRITE_USERS_FILE, "Error writing users file" },
	{ ERR_NO_USER_PRFILE, "User profile file not found" },
	{ ERR_FILE_DELETE, "Unable to remove file" },
	{ ERR_DIR_CREATE, "Unable to create directory" },
	{ ERR_DIR_DELETE, "Unable to remove directory" },
	{ ERR_FILE_OPEN, "Unable to open file" },
	{ ERR_INVALID_FILE, "Invalid file structure" },
	{ ERR_FILE_WRITE, "Unable to write file" },
	{ ERR_MSG_NOT_IN_RANGE, "Message out of range" },
	{ ERR_MSG_DELETED, "Message deleted" },
	{ ERR_INVALID_PASSWORD, "Invalid password" },
	{ ERR_ALIAS_FILE_NOT_FOUND, "Users alias file not found" },
	{ ERR_ALIAS_EXIST, "Alias already exist" },
	{ ERR_WRITE_ALIAS_FILE, "Error writing alias file" },
	{ ERR_ALIAS_NOT_FOUND, "Alias not found" },
	{ ERR_SVR_PRFILE_NOT_LOCKED, "Server profile not locked" },
	{ ERR_GET_PEER_INFO, "Error getting peer address info" },
	{ ERR_SMTP_PATH_PARSE_ERROR, "Error parsing SMTP command line" },
	{ ERR_BAD_RETURN_PATH, "Bad return path syntax" },
	{ ERR_BAD_EMAIL_ADDR, "Bad email address" },
	{ ERR_RELAY_NOT_ALLOWED, "Relay not allowed" },
	{ ERR_BAD_FORWARD_PATH, "Bad forward path syntax" },
	{ ERR_GET_SOCK_INFO, "Error getting sock address info" },
	{ ERR_GET_SOCK_HOST, "Error getting sock host name" },
	{ ERR_NO_DOMAIN, "Unable to know server domain" },
	{ ERR_USER_NOT_LOCAL, "User not local" },
	{ ERR_BAD_SERVER_ADDR, "Invalid server address" },
	{ ERR_BAD_SERVER_RESPONSE, "Bad server response" },
	{ ERR_INVALID_POP3_RESPONSE, "Invalid POP3 response" },
	{ ERR_LINKS_FILE_NOT_FOUND, "POP3 links file not found" },
	{ ERR_LINK_EXIST, "POP3 link already exist" },
	{ ERR_WRITE_LINKS_FILE, "Error writing POP3 links file" },
	{ ERR_LINK_NOT_FOUND, "POP3 link not found" },
	{ ERR_SMTPGW_FILE_NOT_FOUND, "SMTP gateway file not found" },
	{ ERR_GATEWAY_ALREADY_EXIST, "SMTP gateway already exist" },
	{ ERR_GATEWAY_NOT_FOUND, "SMTP gateway not found" },
	{ ERR_USER_NOT_MAILINGLIST, "User is not a mailing list" },
	{ ERR_NO_USER_MLTABLE_FILE, "Mailing list users table file not found" },
	{ ERR_MLUSER_ALREADY_EXIST, "Mailing list user already exist" },
	{ ERR_MLUSER_NOT_FOUND, "Mailing list user not found" },
	{ ERR_SPOOL_FILE_NOT_FOUND, "Spool file not found" },
	{ ERR_INVALID_SPOOL_FILE, "Invalid spool file" },
	{ ERR_SPOOL_FILE_EXPIRED, "Spool file has reached max retry ops" },
	{ ERR_SMTPRELAY_FILE_NOT_FOUND, "SMTP relay file not found" },
	{ ERR_DOMAINS_FILE_NOT_FOUND, "POP3 domains file not found" },
	{ ERR_DOMAIN_NOT_HANDLED, "POP3 domain not handled" },
	{ ERR_BAD_SMTP_RESPONSE, "Bad SMTP response" },
	{ ERR_CFG_VAR_NOT_FOUND, "Config variable not found" },
	{ ERR_BAD_DNS_RESPONSE, "Bad DNS response" },
	{ ERR_SMTPGW_NOT_FOUND, "SMTP gateway not found" },
	{ ERR_INCOMPLETE_CONFIG, "Incomplete server configuration file" },
	{ ERR_MAIL_ERROR_LOOP, "Mail error loop detected" },
	{ ERR_EXTALIAS_FILE_NOT_FOUND, "External-Alias file not found" },
	{ ERR_EXTALIAS_EXIST, "External alias already exist" },
	{ ERR_WRITE_EXTALIAS_FILE, "Error writing External-Alias file" },
	{ ERR_EXTALIAS_NOT_FOUND, "External alias not found" },
	{ ERR_NO_USER_DEFAULT_PRFILE, "Unable to open default user profile" },
	{ ERR_FINGER_QUERY_FORMAT, "Error in FINGER query" },
	{ ERR_LOCKED_RESOURCE, "Resource already locked" },
	{ ERR_NO_PREDEFINED_MX, "No predefined mail exchanger" },
	{ ERR_NO_MORE_MXRECORDS, "No more MX records" },
	{ ERR_INVALID_MESSAGE_FORMAT, "Invalid message format" },
	{ ERR_SMTP_BAD_MAIL_FROM, "[MAIL FROM:] not permitted by remote SMTP server" },
	{ ERR_SMTP_BAD_RCPT_TO, "[RCPT TO:] not permitted by remote SMTP server" },
	{ ERR_SMTP_BAD_DATA, "[DATA] not permitted by remote SMTP server" },
	{ ERR_INVALID_MXRECS_STRING, "Invalid MX records string format" },
	{ ERR_SETSOCKOPT, "Error in function {setsockopt}" },
	{ ERR_CREATEEVENT, "Error in function {CreateEvent}" },
	{ ERR_CREATESEMAPHORE, "Error in function {CreateSemaphore}" },
	{ ERR_CLOSEHANDLE, "Error in function {CloseHandle}" },
	{ ERR_RELEASESEMAPHORE, "Error in function {ReleaseSemaphore}" },
	{ ERR_BEGINTHREADEX, "Error in function {_beginthreadex}" },
	{ ERR_CREATEFILEMAPPING, "Error in function {CreateFileMapping}" },
	{ ERR_MAPVIEWOFFILE, "Error in function {MapViewOfFile}" },
	{ ERR_UNMAPVIEWOFFILE, "Error in function {UnmapViewOfFile}" },
	{ ERR_SEMGET, "Error in function {semget}" },
	{ ERR_SEMCTL, "Error in function {semctl}" },
	{ ERR_SEMOP, "Error in function {semop}" },
	{ ERR_FORK, "Error in function {fork}" },
	{ ERR_SHMGET, "Error in function {shmget}" },
	{ ERR_SHMCTL, "Error in function {shmctl}" },
	{ ERR_SHMAT, "Error in function {shmat}" },
	{ ERR_SHMDT, "Error in function {shmdt}" },
	{ ERR_OPENDIR, "Error in function {opendir}" },
	{ ERR_STAT, "Error in function {stat}" },
	{ ERR_SMTP_BAD_CMD_SEQUENCE, "Bad SMTP command sequence" },
	{ ERR_NO_ROOT_DOMAIN_VAR, "RootDomain config var not found" },
	{ ERR_NS_NOT_FOUND, "Name Server for domain not found" },
	{ ERR_NO_DEFINED_MXS_FOR_DOMAIN, "No MX records defined for domain" },
	{ ERR_BAD_CTRL_COMMAND, "Bad CTRL command syntax" },
	{ ERR_DOMAIN_ALREADY_HANDLED, "Domain already exist" },
	{ ERR_BAD_CTRL_LOGIN, "Bad controller login" },
	{ ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND, "Controller accounts file not found" },
	{ ERR_SPAMMER_IP, "Server registered spammer IP" },
	{ ERR_TRUNCATED_DGRAM_DNS_RESPONSE, "Truncated UDP DNS response" },
	{ ERR_NO_DGRAM_DNS_RESPONSE, "Unable to get UDP DNS response" },
	{ ERR_DNS_NOTFOUND, "Requested DNS record not found" },
	{ ERR_BAD_SMARTDNSHOST_SYNTAX, "Bad SmartDNSHost config syntax" },
	{ ERR_MAILBOX_SIZE, "User maximum mailbox size reached" },
	{ ERR_DYNDNS_CONFIG, "Bad \"DynDnsSetup\" config syntax" },
	{ ERR_PROCESS_EXECUTE, "Error executing external process" },
	{ ERR_BAD_MAILPROC_CMD_SYNTAX, "Bad mailproc.tab command syntax" },
	{ ERR_NO_MAILPROC_FILE, "User mail processing file not present" },
	{ ERR_DNS_RECURSION_NOT_AVAILABLE, "DNS recursion not available" },
	{ ERR_POP3_EXTERNAL_LINK_DISABLED, "External POP3 link disabled" },
	{ ERR_BAD_DOMAIN_PROC_CMD_SYNTAX, "Error in custom domain processing file syntax" },
	{ ERR_NOT_A_CUSTOM_DOMAIN, "Not a custom domain" },
	{ ERR_NO_MORE_TOKENS, "No more tokens" },
	{ ERR_SELECT, "Error in function {select}" },
	{ ERR_REGISTER_EVENT_SOURCE, "Error in function {RegisterEventSource}" },
	{ ERR_NOMORESEMS, "No more semaphores available" },
	{ ERR_INVALID_SEMAPHORE, "Invalid semaphore" },
	{ ERR_SHMEM_ALREADY_EXIST, "Shared memory already exist" },
	{ ERR_SHMEM_NOT_EXIST, "Shared memory not exist" },
	{ ERR_SEM_NOT_EXIST, "Semaphore not exist" },
	{ ERR_SERVER_BUSY, "Server too busy, retry later" },
	{ ERR_IP_NOT_ALLOWED, "Server does not like Your IP" },
	{ ERR_FILE_EOF, "End of file reached" },
	{ ERR_BAD_TAG_ADDRESS, "Bad tag ( From: , etc ... ) address" },
	{ ERR_MAILFROM_UNKNOWN, "Unable to extract \"MAIL FROM: <>\" address" },
	{ ERR_FILTERED_MESSAGE, "Message rejected by server filters" },
	{ ERR_NO_DOMAIN_FILTER, "Domain filter not defined" },
	{ ERR_POP3_RETR_BROKEN, "POP3 RETR operation broken in data retrieval" },
	{ ERR_CCLN_INVALID_RESPONSE, "Invalid controller response" },
	{ ERR_CCLN_ERROR_RESPONSE, "Controller response error" },
	{ ERR_INCOMPLETE_PROCESSING, "Custom domain processing incomplete" },
	{ ERR_NO_EXTERNAL_AUTH_DEFINED, "No external auth defined for requested username@domain" },
	{ ERR_EXTERNAL_AUTH_FAILURE, "External authentication error" },
	{ ERR_MD5_AUTH_FAILED, "MD5 authentication failed" },
	{ ERR_NO_SMTP_AUTH_CONFIG, "SMTP authentication config not found" },
	{ ERR_UNKNOWN_SMTP_AUTH, "Unknown SMTP authentication mode" },
	{ ERR_BAD_SMTP_AUTH_CONFIG, "Bad SMTP authentication config" },
	{ ERR_BAD_EXTRNPRG_EXITCODE, "Bad external program exit code" },
	{ ERR_BAD_SMTP_CMD_SYNTAX, "Bad SMTP command syntax" },
	{ ERR_SMTP_AUTH_FAILED, "SMTP client authentication failed" },
	{ ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE, "Bad external SMTP auth response file syntax" },
	{ ERR_SMTP_USE_FORBIDDEN, "Server use is forbidden" },
	{ ERR_SPAM_ADDRESS, "Server registered spammer domain" },
	{ ERR_SOCK_NOMORE_DATA, "End of socket stream data" },
	{ ERR_BAD_TAB_INDEX_FIELD, "Bad TAB field index" },
	{ ERR_FILE_READ, "Error reading file" },
	{ ERR_BAD_INDEX_FILE, "Bad index file format" },
	{ ERR_INDEX_HASH_NOT_FOUND, "Record hash not found in index" },
	{ ERR_RECORD_NOT_FOUND, "Record not found in TAB file" },
	{ ERR_HEAP_ALLOC, "Heap block alloc error" },
	{ ERR_HEAP_FREE, "Heap block free error" },
	{ ERR_RESOURCE_NOT_LOCKED, "Trying to unlock an unlocked resource" },
	{ ERR_LOCK_ENTRY_NOT_FOUND, "Resource lock entry not found" },
	{ ERR_LINE_TOO_LONG, "Stream line too long" },
	{ ERR_MAIL_LOOP_DETECTED, "Mail loop detected" },
	{ ERR_FILE_MOVE, "Unable to move file" },
	{ ERR_INVALID_MAILDIR_SUBPATH, "Error invalid Maildir sub path" },
	{ ERR_SMTP_TOO_MANY_RECIPIENTS, "Too many SMTP recipients" },
	{ ERR_DNS_CACHE_FILE_FMT, "Error in DNS cache file format" },
	{ ERR_DNS_CACHE_FILE_EXPIRED, "DNS cache file expired" },
	{ ERR_MMAP, "Error in function {mmap}" },
	{ ERR_NOT_LOCKED, "Not locked" },
	{ ERR_SMTPFWD_FILE_NOT_FOUND, "SMTP forward gateway file not found" },
	{ ERR_SMTPFWD_NOT_FOUND, "SMTP forward gateway not found" },
	{ ERR_USER_BREAK, "Operation interrupted" },
	{ ERR_SET_THREAD_PRIORITY, "Error setting thread priority" },
	{ ERR_NULL_SENDER, "Empty message sender" },
	{ ERR_RCPTTO_UNKNOWN, "Mail tag \"To:\" missing" },
	{ ERR_LOADMODULE, "Error moading dynamic module" },
	{ ERR_LOADMODULESYMBOL, "Error moading dynamic module symbol" },
	{ ERR_NOMORE_TLSKEYS, "No more TLS keys are available" },
	{ ERR_INVALID_TLSKEY, "Invalid TLS key" },
	{ ERR_ERRORINIT_FAILED, "Error initialization failed" },
	{ ERR_SENDFILE, "Error in function {sendfile}" },
	{ ERR_MUTEXINIT, "Error in function {pthread_mutex_init}" },
	{ ERR_CONDINIT, "Error in function {pthread_cond_init}" },
	{ ERR_THREADCREATE, "Error in function {pthread_create}" },
	{ ERR_CREATEMUTEX, "Error in function {CreateMutex}" },
	{ ERR_NO_LOCAL_SPOOL_FILES, "Local spool empty" },
	{ ERR_NO_HANDLED_DOMAIN, "Unable to retrieve an handled domain from peer IP" },
	{ ERR_INVALID_MAIL_DOMAIN, "Remote domain has no DNS/MX entries" },
	{ ERR_BAD_CMDSTR_CHARS, "Bad characters in command line" },
	{ ERR_FETCHMSG_UNDELIVERED, "POP3 fetched message failed delivery" },
	{ ERR_USER_VAR_NOT_FOUND, "User configuration variable not found" },
	{ ERR_NO_POP3_IP, "Invalid or not available POP3 connection IP" },
	{ ERR_NO_MESSAGE_FILE, "Message file not existent" },
	{ ERR_GET_DISK_SPACE_INFO, "Error getting disk space info" },
	{ ERR_GET_MEMORY_INFO, "Error getting memory info" },
	{ ERR_LOW_DISK_SPACE, "System low in disk space" },
	{ ERR_LOW_VM_SPACE, "System low in virtual memory" },
	{ ERR_USER_DISABLED, "Account disabled" },
	{ ERR_BAD_DNS_NAME_RECORD, "Bad format for DNS name record" },
	{ ERR_MESSAGE_SIZE, "Message exceeds fixed maximum message size" },
	{ ERR_SMTPSRV_MSG_SIZE, "Message too big for the remote SMTP server" },
	{ ERR_MAPS_CONTAINED, "The peer IP is mapped" },
	{ ERR_ADOMAIN_FILE_NOT_FOUND, "Domain aliases file not found" },
	{ ERR_ADOMAIN_EXIST, "Domain alias already exist" },
	{ ERR_ADOMAIN_NOT_FOUND, "Domain alias not found" },
	{ ERR_NOT_A_CMD_ALIAS, "Cmd alias not found" },
	{ ERR_GETSOCKOPT, "Error in function {getsockopt}" },
	{ ERR_NO_HDR_FETCH_TAGS, "No fetch headers tags string supplied" },
	{ ERR_SET_FILE_TIME, "Error setting file times" },
	{ ERR_LISTDIR_NOT_FOUND, "Listing directory not found" },
	{ ERR_DUPLICATE_HANDLE, "Error in function {DuplicateHandle}" },
	{ ERR_EMPTY_LOG, "Log file is empty" },
	{ ERR_BAD_RELATIVE_PATH, "Error in relative path syntax" },
	{ ERR_DNS_NXDOMAIN, "DNS name not exist" },
	{ ERR_BAD_RFCNAME, "Name does not respect RFC822" },
	{ ERR_CONNECT, "Error connecting to remote address" },
	{ ERR_MESSAGE_DELETED, "Message marked for deletion" },
	{ ERR_PIPE, "Pipe creation error" },
	{ ERR_WAITPID, "Error in function {waitpid}" },
	{ ERR_MUNMAP, "Error in function {munmap}" },
	{ ERR_INVALID_MMAP_OFFSET, "Invalid memory map offset" },
	{ ERR_UNMAPFILEVIEW, "File view unmap failed" },
	{ ERR_INVALID_IMAP_LINE, "Invalid IMAP syntax" },
	{ ERR_IMAP_RESP_NO, "IMAP response NO" },
	{ ERR_IMAP_RESP_BAD, "IMAP response BAD" },
	{ ERR_IMAP_RESP_BYE, "IMAP response BYE" },
	{ ERR_IMAP_UNKNOWN_AUTH, "Unknown IMAP authentication method" },
	{ ERR_NO_MESSAGE_AUTH, "Message authentication not found" },
	{ ERR_INVALID_PARAMETER, "Invalid parameter" },
	{ ERR_ALREADY_EXIST, "Already exist" },
	{ ERR_SSLCTX_CREATE, "Error creating SSL context" },
	{ ERR_SSL_CREATE, "Error creating SSL session" },
	{ ERR_SSL_CONNECT, "Error establishing SSL connection (connect)" },
	{ ERR_SSL_SETCERT, "Error setting the SSL certificate file" },
	{ ERR_SSL_SETKEY, "Error setting the SSL key file" },
	{ ERR_SSL_READ, "SSL read error" },
	{ ERR_SSL_WRITE, "SSL write error" },
	{ ERR_SSL_CERT_VALIDATE, "SSL certificate validation failed" },
	{ ERR_SSL_NOCERT, "SSL certificate missing" },
	{ ERR_NO_REMOTE_SSL, "Remote server does not support TLS" },
	{ ERR_SSL_ALREADY_ACTIVE, "TLS link already active" },
	{ ERR_SSL_CHECKKEY, "SSL private key check failed" },
	{ ERR_SSL_VERPATHS, "SSL verify-paths load failed" },
	{ ERR_TLS_MODE_REQUIRED, "TLS required for this session" },
	{ ERR_NOT_FOUND, "Not found" },
	{ ERR_NOREMOTE_POP3_UIDL, "Remote POP3 server does not support UIDL" },
	{ ERR_CORRUPTED, "Input data corrupted" },
	{ ERR_SSL_DISABLED, "TLS service disabled" },
	{ ERR_SSL_ACCEPT, "Error establishing SSL connection (accept)" },
	{ ERR_BAD_SEQUENCE, "Wrong sequence of commands" },
	{ ERR_EMPTY_ADDRESS, "Empty email address" },
	{ ERR_DNS_MAXDEPTH, "Maximum DNS quesry depth exceeded" },
	{ ERR_THREAD_SETSTACK, "Unable to set thread stack" },
	{ ERR_DNS_FORMAT, "Bad DNS query format" },
	{ ERR_DNS_SVRFAIL, "Remote DNS server failed" },
	{ ERR_DNS_NOTSUPPORTED, "DNS query not supported" },
	{ ERR_DNS_REFUSED, "DNS query refused" },
	{ ERR_INVALID_RELAY_ADDRESS, "Invalid relay address" },
	{ ERR_INVALID_HOSTNAME, "Invalid host name" },
	{ ERR_INVALID_INET_ADDR, "Invalid INET address" },
	{ ERR_SOCKET_SHUTDOWN, "Connection shutdown error" },
	{ ERR_SSL_SHUTDOWN, "SSL connection shutdown error" },
	{ ERR_TOO_MANY_ELEMENTS, "Too many elements" },

};

static char const *pszErrors[ERROR_COUNT];


static void ErrFreeEnv(void *pData)
{
	ErrorEnv *pEV = (ErrorEnv *) pData;

	if (pEV != NULL) {
		char **ppszInfo = pEV->pszInfo;

		for (int i = 0; i < CountOf(Errors); i++, ppszInfo++)
			if (*ppszInfo != NULL)
				SysFree(*ppszInfo), *ppszInfo = NULL;
		SysFree(pEV);
	}
}

static void ErrOnceSetup(void)
{
	int i, iIdx;

	SysCreateTlsKey(ErrTlsKey, ErrFreeEnv);
	for (i = 0; i < CountOf(Errors); i++) {
		iIdx = -Errors[i].iErrorCode;
		if (iIdx >= 0 && iIdx < ERROR_COUNT)
			pszErrors[iIdx] = Errors[i].pszError;
	}
}

static ErrorEnv *ErrSetupEnv(void)
{
	SysThreadOnce(&OnceSetup, ErrOnceSetup);

	ErrorEnv *pEV = (ErrorEnv *) SysGetTlsKeyData(ErrTlsKey);

	if (pEV == NULL) {
		if ((pEV = (ErrorEnv *) SysAlloc(sizeof(ErrorEnv) +
						 ERROR_COUNT * sizeof(char *))) == NULL)
			return NULL;

		pEV->iErrorNo = ERR_SUCCESS;

		char **ppszInfo = pEV->pszInfo;

		for (int i = 0; i < ERROR_COUNT; i++, ppszInfo++)
			*ppszInfo = NULL;

		if (SysSetTlsKeyData(ErrTlsKey, pEV) < 0) {
			SysFree(pEV);
			return NULL;
		}
	}

	return pEV;
}

int ErrGetErrorCode(void)
{
	ErrorEnv *pEV = ErrSetupEnv();

	if (pEV == NULL)
		return ERR_ERRORINIT_FAILED;

	return pEV->iErrorNo;
}

int ErrSetErrorCode(int iError, char const *pszInfo, int iSize)
{
	ErrorEnv *pEV = ErrSetupEnv();

	if (pEV == NULL)
		return ERR_ERRORINIT_FAILED;

	pEV->iErrorNo = iError;
	if (pszInfo != NULL) {
		iError = -iError;
		if (iError >= 0 && iError < ERROR_COUNT) {
			if (pEV->pszInfo[iError] != NULL)
				SysFree(pEV->pszInfo[iError]);
			pEV->pszInfo[iError] = (char *) StrMemDup(pszInfo, iSize, 0);
		}
	}

	return 0;
}

const char *ErrGetErrorString(int iError)
{
	iError = -iError;

	return (iError >= 0 && iError < ERROR_COUNT && pszErrors[iError] != NULL) ?
		pszErrors[iError]: "Unknown error code";
}

const char *ErrGetErrorString(void)
{
	ErrorEnv *pEV = ErrSetupEnv();

	if (pEV == NULL)
		return ErrGetErrorString(ERR_ERRORINIT_FAILED);

	return ErrGetErrorString(pEV->iErrorNo);
}

char *ErrGetErrorStringInfo(int iError)
{
	ErrorEnv *pEV = ErrSetupEnv();

	if (pEV == NULL)
		return SysStrDup(ErrGetErrorString(iError));

	int iErrIndex = -iError;

	if (iErrIndex < 0 || iErrIndex >= ERROR_COUNT)
		return SysStrDup("Unknown error code");

	int iInfoLength = (pEV->pszInfo[iErrIndex] != NULL) ? strlen(pEV->pszInfo[iErrIndex]): 0;
	char const *pszError = pszErrors[iErrIndex] != NULL ? pszErrors[iErrIndex]: "Unknown error code";
	char *pszErrorInfo = (char *) SysAlloc(strlen(pszError) + iInfoLength + 256);

	if (pszErrorInfo == NULL)
		return NULL;

	if (pEV->pszInfo[iErrIndex] != NULL)
		sprintf(pszErrorInfo,
			"ErrCode   = %d\n"
			"ErrString = %s\n"
			"ErrInfo   = %s", iError, pszError, pEV->pszInfo[iErrIndex]);
	else
		sprintf(pszErrorInfo,
			"ErrCode   = %d\n" "ErrString = %s", iError, pszError);

	return pszErrorInfo;
}

int ErrLogMessage(int iLogLevel, char const *pszFormat, ...)
{
	char *pszErrorInfo = ErrGetErrorStringInfo(ErrGetErrorCode());

	if (pszErrorInfo == NULL)
		return ErrGetErrorCode();

	char *pszUserMessage = NULL;

	StrVSprint(pszUserMessage, pszFormat, pszFormat);
	if (pszUserMessage == NULL) {
		SysFree(pszErrorInfo);
		return ErrGetErrorCode();
	}

	SysLogMessage(iLogLevel, "<<\n" "%s\n" "%s" ">>\n", pszErrorInfo, pszUserMessage);

	SysFree(pszUserMessage);
	SysFree(pszErrorInfo);

	return 0;
}

int ErrFileLogString(char const *pszFileName, char const *pszMessage)
{
	char *pszErrorInfo = ErrGetErrorStringInfo(ErrGetErrorCode());

	if (pszErrorInfo == NULL)
		return ErrGetErrorCode();

	FILE *pLogFile = fopen(pszFileName, "a+t");

	if (pLogFile == NULL) {
		SysFree(pszErrorInfo);

		ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
		return ERR_FILE_CREATE;
	}

	fprintf(pLogFile, "<<\n" "%s\n" "%s" ">>\n", pszErrorInfo, pszMessage);

	fclose(pLogFile);
	SysFree(pszErrorInfo);

	return 0;
}

