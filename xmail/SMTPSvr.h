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

#ifndef _SMTPSVR_H
#define _SMTPSVR_H

#define SMTP_SERVER_NAME           "[" APP_NAME_VERSION_STR " ESMTP Server]"
#define STD_SMTP_PORT               25
#define SMTPS_SERVER_NAME          "[" APP_NAME_VERSION_STR " ESMTPS Server]"
#define STD_SMTPS_PORT              465
#define SMTP_LISTEN_SIZE            64

#define SMTPF_LOG_ENABLED           (1 << 0)

struct SMTPConfig {
	unsigned long ulFlags;
	long lThreadCount;
	long lMaxThreads;
	int iSessionTimeout;
	int iTimeout;
	int iMaxRcpts;
	unsigned int uPopAuthExpireTime;
};

unsigned int SMTPClientThread(void *pThreadData);

#endif
