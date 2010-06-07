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

#ifndef _POP3SVR_H
#define _POP3SVR_H

#define POP3_SERVER_NAME            "[" APP_NAME_VERSION_STR " POP3 Server]"
#define STD_POP3_PORT               110
#define POP3S_SERVER_NAME           "[" APP_NAME_VERSION_STR " POP3S Server]"
#define STD_POP3S_PORT              995
#define POP3_LISTEN_SIZE            64

#define POP3F_LOG_ENABLED           (1 << 0)
#define POP3F_HANG_ON_BADLOGIN      (1 << 1)

struct POP3Config {
	unsigned long ulFlags;
	long lThreadCount;
	long lMaxThreads;
	int iSessionTimeout;
	int iTimeout;
	int iBadLoginWait;
};

unsigned int POP3ClientThread(void *pThreadData);

#endif
