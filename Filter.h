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

#ifndef _FILTER_H
#define _FILTER_H

#define FILTER_MODE_INBOUND         "in"
#define FILTER_MODE_OUTBOUND        "out"

#define FILTER_PRIORITY             SYS_PRIORITY_NORMAL
#define FILTER_OUT_NNF_EXITCODE     4
#define FILTER_OUT_NN_EXITCODE      5
#define FILTER_OUT_EXITCODE         6
#define FILTER_MODIFY_EXITCODE      7
#define FILTER_FLAGS_BREAK          (1 << 4)
#define FILTER_FLAGS_MASK           FILTER_FLAGS_BREAK

#define FILTER_XFL_WHITELISTED      (1 << 0)


struct FilterLogInfo {
	char const *pszSender;
	char const *pszRecipient;
	SYS_INET_ADDR LocalAddr;
	SYS_INET_ADDR RemoteAddr;
	char const * const *ppszExec;
	int iExecResult;
	int iExitCode;
	char const *pszType;
	char const *pszInfo;
};

struct FilterTokens {
	char **ppszCmdTokens;
	int iTokenCount;
};

struct FilterExecCtx {
	FilterTokens *pToks;
	char const *pszAuthName;
	unsigned long ulFlags;
	int iTimeout;
};

enum FilterFields {
	filSender = 0,
	filRecipient,
	filRemoteAddr,
	filLocalAddr,
	filFileName,

	filMax
};

int FilLogFilter(FilterLogInfo const *pFLI);
char *FilGetFilterRejMessage(char const *pszSpoolFile);
int FilExecPreParse(FilterExecCtx *pCtx, char **ppszPEError);
int FilFilterMessage(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
		     QMSG_HANDLE hMessage, char const *pszMode, UserInfo *pUI);

#endif
