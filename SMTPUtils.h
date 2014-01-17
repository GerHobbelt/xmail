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

#ifndef _SMTPUTILS_H
#define _SMTPUTILS_H

#define INVALID_MXS_HANDLE          ((MXS_HANDLE) 0)
#define INVALID_SMTPCH_HANDLE       ((SMTPCH_HANDLE) 0)

#define SMTP_FATAL_ERROR            999

#define RECEIVED_TYPE_STD           0
#define RECEIVED_TYPE_VERBOSE       1
#define RECEIVED_TYPE_STRICT        2
#define RECEIVED_TYPE_AUTHSTD       3
#define RECEIVED_TYPE_AUTHVERBOSE   4

#define SMTP_ERROR_VARNAME          "SMTP-Error"
#define DEFAULT_SMTP_ERR            "417 Temporary delivery error"
#define SMTP_SERVER_VARNAME         "SMTP-Server"

#define SMTP_GWF_USE_TLS            (1 << 0)
#define SMTP_GWF_FORCE_TLS          (1 << 1)

typedef struct MXS_HANDLE_struct {
} *MXS_HANDLE;

typedef struct SMTPCH_HANDLE_struct {
} *SMTPCH_HANDLE;

struct SMTPError {
	char *pszServer;
	int iSTMPResponse;
	char *pszSTMPResponse;
};

struct SMTPGateway {
	char *pszHost;
	char *pszIFace;
	unsigned long ulFlags;
};

enum SmtpMsgInfo {
	smsgiClientDomain = 0,
	smsgiClientAddr,
	smsgiServerDomain,
	smsgiServerAddr,
	smsgiTime,
	smsgiSeverName,

	smsgiMax
};

enum SpoolMsgInfo {
	smiClientAddr,
	smiServerAddr,
	smiTime,

	smiMax
};

SMTPGateway **USmtpMakeGateways(char const * const *ppszGwHosts, char const **ppszOptions);
void USmtpFreeGateways(SMTPGateway **ppGws);
SMTPGateway **USmtpGetCfgGateways(SVRCFG_HANDLE hSvrConfig,  char const * const *ppszGwHosts,
				  const char *pszOptions);
SMTPGateway **USmtpGetFwdGateways(SVRCFG_HANDLE hSvrConfig, const char *pszDomain);
int USmtpGetGateway(SVRCFG_HANDLE hSvrConfig, const char *pszDomain, char *pszGateway,
		    int iSize);
int USmtpAddGateway(const char *pszDomain, const char *pszGateway);
int USmtpRemoveGateway(const char *pszDomain);
int USmtpIsAllowedRelay(const SYS_INET_ADDR & PeerInfo, SVRCFG_HANDLE hSvrConfig);
char **USmtpGetPathStrings(const char *pszMailCmd);
int USmtpSplitEmailAddr(const char *pszAddr, char *pszUser, char *pszDomain);
int USmtpCheckAddressPart(char const *pszName);
int USmtpCheckDomainPart(char const *pszName);
int USmtpCheckAddress(char const *pszAddress);
int USmtpInitError(SMTPError *pSMTPE);
int USmtpSetError(SMTPError *pSMTPE, int iSTMPResponse, char const *pszSTMPResponse,
		  char const *pszServer);
bool USmtpIsFatalError(SMTPError const *pSMTPE);
char const *USmtpGetErrorMessage(SMTPError const *pSMTPE);
int USmtpCleanupError(SMTPError *pSMTPE);
char *USmtpGetSMTPError(SMTPError *pSMTPE, char *pszError, int iMaxError);
char *USmtpGetSMTPRmtMsgID(char const *pszAckDATA, char *pszRmtMsgID, int iMaxMsg);
char const *USmtpGetErrorServer(SMTPError const *pSMTPE);
SMTPCH_HANDLE USmtpCreateChannel(SMTPGateway const *pGw, const char *pszDomain,
				 SMTPError *pSMTPE = NULL);
int USmtpCloseChannel(SMTPCH_HANDLE hSmtpCh, int iHardClose = 0, SMTPError *pSMTPE = NULL);
int USmtpChannelReset(SMTPCH_HANDLE hSmtpCh, SMTPError *pSMTPE = NULL);
int USmtpSendMail(SMTPCH_HANDLE hSmtpCh, const char *pszFrom, const char *pszRcpt,
		  FileSection const *pFS, SMTPError *pSMTPE = NULL);
int USmtpSendMail(SMTPGateway const *pGw, const char *pszDomain, const char *pszFrom,
		  const char *pszRcpt, FileSection const *pFS, SMTPError *pSMTPE = NULL);
int USmtpMailRmtDeliver(SVRCFG_HANDLE hSvrConfig, const char *pszServer, const char *pszDomain,
			const char *pszFrom, const char *pszRcpt, FileSection const *pFS,
			SMTPError *pSMTPE = NULL);
char *USmtpBuildRcptPath(char const *const *ppszRcptTo, SVRCFG_HANDLE hSvrConfig);
SMTPGateway **USmtpGetMailExchangers(SVRCFG_HANDLE hSvrConfig, const char *pszDomain);
int USmtpCheckMailDomain(SVRCFG_HANDLE hSvrConfig, char const *pszDomain);
MXS_HANDLE USmtpGetMXFirst(SVRCFG_HANDLE hSvrConfig, const char *pszDomain, char *pszMXHost);
int USmtpGetMXNext(MXS_HANDLE hMXSHandle, char *pszMXHost);
void USmtpMXSClose(MXS_HANDLE hMXSHandle);
int USmtpDnsMapsContained(SYS_INET_ADDR const &PeerInfo, char const *pszMapsServer);
int USmtpSpammerCheck(const SYS_INET_ADDR & PeerInfo, char *&pszInfo);
int USmtpSpamAddressCheck(char const *pszAddress);
int USmtpAddMessageInfo(FILE *pMsgFile, char const *pszClientDomain,
			SYS_INET_ADDR const &PeerInfo, char const *pszServerDomain,
			SYS_INET_ADDR const &SockInfo, char const *pszSmtpServerLogo);
int USmtpWriteInfoLine(FILE *pSpoolFile, char const *pszClientAddr,
		       char const *pszServerAddr, char const *pszTime);
char *USmtpGetReceived(int iType, char const *pszAuth, char const *const *ppszMsgInfo,
		       char const *pszMailFrom, char const *pszRcptTo, char const *pszMessageID);

#endif

