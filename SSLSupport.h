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
 * SSL support for XMail by Eugene Vasilkov <Eugene@godeltech.com>
 * Thanks to the stunnel project <http://www.stunnel.org>,
 * which is a good sample of OpenSSL usage <http://www.openssl.org>
 *
 */

#ifndef __SSL_SUPPORT_H__
#define __SSL_SUPPORT_H__

#ifdef USE_SSL

#include <openssl/ssl.h>

typedef struct
{
    const char* pszCertFile;
} SSLOptions;

int SSLInit(const SSLOptions& sslOpt);

void SSLDone();

SSL* SSLMakeSession(SYS_SOCKET SockFD,
                    int iTimeOut,
                    int iIsServerCtx);

void SSLReleaseSession(SSL** pSSLSession);

int SSLRead(SSL* pSSLSession,
            char* pszBuffer,
            int iBufferSize,
            int iTimeOut);

int SSLWrite(SSL* pSSLSession,
             const char* pszBuffer,
             int iBufferSize,
             int iTimeOut);

void SSLGetSessionCipher(SSL* pSSLSession,
                         char* pszBuffer,
                         int iBufferSize);

#endif

#endif /*__SSL_SUPPORT_H__*/
