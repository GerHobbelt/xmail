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

#ifdef USE_SSL

#include "SysInclude.h"
#include "SysDep.h"
#include "SSLSupport.h"

#ifdef _MSC_VER
#undef CDECL
#define CDECL __cdecl
#else
#define CDECL
#endif

#ifdef _WIN32
static CRITICAL_SECTION lock;
#else
static pthread_mutex_t lock;
#endif

static int iSSLInit = 0;
static SSL_CTX* pSSLCliCtx = NULL;
static SSL_CTX* pSSLSrvCtx = NULL;

#ifdef DEBUG_SSL
static void print_stats(void) { /* print statistics */
    printf("%4ld items in the session cache\r\n",
        SSL_CTX_sess_number(pSSLSrvCtx));
    printf("%4ld client connects (SSL_connect())\r\n",
        SSL_CTX_sess_connect(pSSLSrvCtx));
    printf("%4ld client connects that finished\r\n",
        SSL_CTX_sess_connect_good(pSSLSrvCtx));
#if SSLEAY_VERSION_NUMBER >= 0x0922
    printf("%4ld client renegotiatations requested\r\n",
        SSL_CTX_sess_connect_renegotiate(pSSLSrvCtx));
#endif
    printf("%4ld server connects (SSL_accept())\r\n",
        SSL_CTX_sess_accept(pSSLSrvCtx));
    printf("%4ld server connects that finished\r\n",
        SSL_CTX_sess_accept_good(pSSLSrvCtx));
#if SSLEAY_VERSION_NUMBER >= 0x0922
    printf("%4ld server renegotiatiations requested\r\n",
        SSL_CTX_sess_accept_renegotiate(pSSLSrvCtx));
#endif
    printf("%4ld session cache hits\r\n", SSL_CTX_sess_hits(pSSLSrvCtx));
    printf("%4ld session cache misses\r\n", SSL_CTX_sess_misses(pSSLSrvCtx));
    printf("%4ld session cache timeouts\r\n", SSL_CTX_sess_timeouts(pSSLSrvCtx));
}

#if SSLEAY_VERSION_NUMBER >= 0x00907000L
static void SSLInfoCallback(const SSL *s, int where, int ret) {
#else
static void SSLInfoCallback(SSL *s, int where, int ret) {
#endif
    if(where & SSL_CB_LOOP)
        printf("SSL state (%s): %s\r\n",
        where & SSL_ST_CONNECT ? "connect" :
        where & SSL_ST_ACCEPT ? "accept" :
        "undefined", SSL_state_string_long(s));
    else if(where & SSL_CB_ALERT)
        printf("SSL alert (%s): %s: %s\r\n",
            where & SSL_CB_READ ? "read" : "write",
            SSL_alert_type_string_long(ret),
            SSL_alert_desc_string_long(ret));
    else if(where==SSL_CB_HANDSHAKE_DONE)
        print_stats();
    else
        if (where & SSL_CB_EXIT)
        {
            if (ret == 0)
                printf("%failed in %s\r\n",
                SSL_state_string_long(s));
        }
}
#endif /*DEBUG_SSL*/

static void CDECL LockSSL(int iMode,
                          int iType,
                          const char* pszFileName,
                          int iLine)
{
    if(iMode & CRYPTO_LOCK)
    {
#ifdef _WIN32
        EnterCriticalSection(&lock);
#else
        pthread_mutex_lock(&lock);
#endif
    }
    else
    {
        if(iMode & CRYPTO_UNLOCK)
        {
#ifdef _WIN32
            LeaveCriticalSection(&lock);
#else
            pthread_mutex_unlock(&lock);
#endif
        }
    }
}

SSL_CTX* SSLMakeCtx(const SSLOptions& sslOpt,
                    int iServer)
{
    SSL_CTX* pSSLCtx;

    if(iServer)
    {
        pSSLCtx = SSL_CTX_new(SSLv23_server_method());
    }
    else
    {
        pSSLCtx = SSL_CTX_new(SSLv23_client_method());
    }

    if(!pSSLCtx)
    {
        SysLogMessage(LOG_LEV_ERROR,
                      "Cannot create SSL context, ssl support is disabled\r\n");
        return (NULL);
    }


    int iSSLOpt = SSL_OP_ALL;
#ifdef SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS
    iSSLOpt |= SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;
#endif
    SSL_CTX_set_options(pSSLCtx,
                        iSSLOpt);

#if SSLEAY_VERSION_NUMBER >= 0x00906000L
    SSL_CTX_set_mode(pSSLCtx,
                     SSL_MODE_ENABLE_PARTIAL_WRITE |
                     SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
#endif /* OpenSSL-0.9.6 */
/*
    SSL_CTX_set_session_cache_mode(pSSLCtx,
                                   SSL_SESS_CACHE_OFF);
*/
    SSL_CTX_set_session_cache_mode(pSSLCtx,
                                   SSL_SESS_CACHE_BOTH);
    SSL_CTX_sess_set_cache_size(pSSLCtx,
                                128);

    if(!sslOpt.pszCertFile)
    {
        SysLogMessage(LOG_LEV_ERROR,
                      "Certificate file not found, ssl support is disabled\r\n");
        SSL_CTX_free(pSSLCtx);
        return (NULL);
    }

    if(!SSL_CTX_use_certificate_chain_file(pSSLCtx,
                                           sslOpt.pszCertFile))
    {
        SysLogMessage(LOG_LEV_ERROR,
                      "Error reading certificate file: %s, ssl support is disabled\r\n",
                       sslOpt.pszCertFile);
        SSL_CTX_free(pSSLCtx);
        return (NULL);
    }

    SSL_CTX_use_RSAPrivateKey_file(pSSLCtx,
                                   sslOpt.pszCertFile,
                                   SSL_FILETYPE_PEM);

    if(!SSL_CTX_check_private_key(pSSLCtx))
    {
        SysLogMessage(LOG_LEV_ERROR,
                      "Private key does not match the certificate, ssl support is disabled\r\n");
        SSL_CTX_free(pSSLCtx);
        return (NULL);
    }

#ifdef DEBUG_SSL
    SSL_CTX_set_info_callback(pSSLCtx,
                              SSLInfoCallback);
#endif

    if(!SSL_CTX_set_cipher_list(pSSLCtx,
                                SSL_DEFAULT_CIPHER_LIST))
    {
        SysLogMessage(LOG_LEV_ERROR,
                      "Cannot set ciphers: %s, ssl support is disabled\r\n",
                      SSL_DEFAULT_CIPHER_LIST);
        SSL_CTX_free(pSSLCtx);
        return (NULL);
    }

    return (pSSLCtx);
}

int SSLInit(const SSLOptions& sslOpt)
{
    int iResult = 0;

    if(!iSSLInit)
    {
        iSSLInit = 1;

#ifdef _WIN32
        InitializeCriticalSection(&lock);
#else
        pthread_mutexattr_t mutexAttr;
        pthread_mutexattr_init(&mutexAttr);
        pthread_mutexattr_settype(&mutexAttr,
                                  PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&lock,
                           &mutexAttr);
#endif
        CRYPTO_set_locking_callback(LockSSL);

        SSLeay_add_ssl_algorithms();
        SSL_load_error_strings();

        COMP_METHOD* pCompressMethod = COMP_zlib();

        if(pCompressMethod != NULL)
        {
            SSL_COMP_add_compression_method(0xe0,
                                            pCompressMethod);
        }

        pCompressMethod = COMP_rle();

        if(pCompressMethod != NULL)
        {
            SSL_COMP_add_compression_method(0xe1,
                                            pCompressMethod);
        }

        pSSLCliCtx = SSLMakeCtx(sslOpt,
                                0);
        pSSLSrvCtx = SSLMakeCtx(sslOpt,
                                1);

        if(!pSSLCliCtx ||
           !pSSLSrvCtx)
        {
            SSLDone();
            iResult = -1;
        }
    }

    return (iResult);
}

void SSLDone()
{
    if(iSSLInit)
    {
        iSSLInit = 0;

        if(pSSLCliCtx != NULL)
        {
            SSL_CTX_free(pSSLCliCtx);
            pSSLCliCtx = NULL;
        }

        if(pSSLSrvCtx != NULL)
        {
            SSL_CTX_free(pSSLSrvCtx);
            pSSLSrvCtx = NULL;
        }

#ifdef _WIN32
        DeleteCriticalSection(&lock);
#else
        pthread_mutex_destroy(&lock);
#endif
    }
}

#ifdef DEBUG_SSL
void CDECL msg_cb(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
    {
    BIO *bio = (BIO*)arg;
    const char *str_write_p, *str_version, *str_content_type = "", *str_details1 = "", *str_details2= "";

    str_write_p = write_p ? ">>>" : "<<<";

    switch (version)
        {
    case SSL2_VERSION:
        str_version = "SSL 2.0";
        break;
    case SSL3_VERSION:
        str_version = "SSL 3.0 ";
        break;
    case TLS1_VERSION:
        str_version = "TLS 1.0 ";
        break;
    default:
        str_version = "???";
        }

    if (version == SSL2_VERSION)
        {
        str_details1 = "???";

        if (len > 0)
            {
            switch (((unsigned char*)buf)[0])
                {
                case 0:
                    str_details1 = ", ERROR:";
                    str_details2 = " ???";
                    if (len >= 3)
                        {
                        unsigned err = (((unsigned char*)buf)[1]<<8) + ((unsigned char*)buf)[2];

                        switch (err)
                            {
                        case 0x0001:
                            str_details2 = " NO-CIPHER-ERROR";
                            break;
                        case 0x0002:
                            str_details2 = " NO-CERTIFICATE-ERROR";
                            break;
                        case 0x0004:
                            str_details2 = " BAD-CERTIFICATE-ERROR";
                            break;
                        case 0x0006:
                            str_details2 = " UNSUPPORTED-CERTIFICATE-TYPE-ERROR";
                            break;
                            }
                        }

                    break;
                case 1:
                    str_details1 = ", CLIENT-HELLO";
                    break;
                case 2:
                    str_details1 = ", CLIENT-MASTER-KEY";
                    break;
                case 3:
                    str_details1 = ", CLIENT-FINISHED";
                    break;
                case 4:
                    str_details1 = ", SERVER-HELLO";
                    break;
                case 5:
                    str_details1 = ", SERVER-VERIFY";
                    break;
                case 6:
                    str_details1 = ", SERVER-FINISHED";
                    break;
                case 7:
                    str_details1 = ", REQUEST-CERTIFICATE";
                    break;
                case 8:
                    str_details1 = ", CLIENT-CERTIFICATE";
                    break;
                }
            }
        }

    if (version == SSL3_VERSION || version == TLS1_VERSION)
        {
        switch (content_type)
            {
        case 20:
            str_content_type = "ChangeCipherSpec";
            break;
        case 21:
            str_content_type = "Alert";
            break;
        case 22:
            str_content_type = "Handshake";
            break;
            }

        if (content_type == 21) /* Alert */
            {
            str_details1 = ", ???";

            if (len == 2)
                {
                switch (((unsigned char*)buf)[0])
                    {
                case 1:
                    str_details1 = ", warning";
                    break;
                case 2:
                    str_details1 = ", fatal";
                    break;
                    }

                str_details2 = " ???";
                switch (((unsigned char*)buf)[1])
                    {
                case 0:
                    str_details2 = " close_notify";
                    break;
                case 10:
                    str_details2 = " unexpected_message";
                    break;
                case 20:
                    str_details2 = " bad_record_mac";
                    break;
                case 21:
                    str_details2 = " decryption_failed";
                    break;
                case 22:
                    str_details2 = " record_overflow";
                    break;
                case 30:
                    str_details2 = " decompression_failure";
                    break;
                case 40:
                    str_details2 = " handshake_failure";
                    break;
                case 42:
                    str_details2 = " bad_certificate";
                    break;
                case 43:
                    str_details2 = " unsupported_certificate";
                    break;
                case 44:
                    str_details2 = " certificate_revoked";
                    break;
                case 45:
                    str_details2 = " certificate_expired";
                    break;
                case 46:
                    str_details2 = " certificate_unknown";
                    break;
                case 47:
                    str_details2 = " illegal_parameter";
                    break;
                case 48:
                    str_details2 = " unknown_ca";
                    break;
                case 49:
                    str_details2 = " access_denied";
                    break;
                case 50:
                    str_details2 = " decode_error";
                    break;
                case 51:
                    str_details2 = " decrypt_error";
                    break;
                case 60:
                    str_details2 = " export_restriction";
                    break;
                case 70:
                    str_details2 = " protocol_version";
                    break;
                case 71:
                    str_details2 = " insufficient_security";
                    break;
                case 80:
                    str_details2 = " internal_error";
                    break;
                case 90:
                    str_details2 = " user_canceled";
                    break;
                case 100:
                    str_details2 = " no_renegotiation";
                    break;
                    }
                }
            }

        if (content_type == 22) /* Handshake */
            {
            str_details1 = "???";

            if (len > 0)
                {
                switch (((unsigned char*)buf)[0])
                    {
                case 0:
                    str_details1 = ", HelloRequest";
                    break;
                case 1:
                    str_details1 = ", ClientHello";
                    break;
                case 2:
                    str_details1 = ", ServerHello";
                    break;
                case 11:
                    str_details1 = ", Certificate";
                    break;
                case 12:
                    str_details1 = ", ServerKeyExchange";
                    break;
                case 13:
                    str_details1 = ", CertificateRequest";
                    break;
                case 14:
                    str_details1 = ", ServerHelloDone";
                    break;
                case 15:
                    str_details1 = ", CertificateVerify";
                    break;
                case 16:
                    str_details1 = ", ClientKeyExchange";
                    break;
                case 20:
                    str_details1 = ", Finished";
                    break;
                    }
                }
            }
        }

    printf("%s %s%s [length %04lx]%s%s\n", str_write_p, str_version, str_content_type, (unsigned long)len, str_details1, str_details2);

    if (len > 0)
        {
        size_t num, i;

        printf("   ");
        num = len;
#if 0
        if (num > 16)
            num = 16;
#endif
        for (i = 0; i < num; i++)
            {
            if (i % 16 == 0 && i > 0)
                printf("\n   ");
            printf(" %02x", ((unsigned char*)buf)[i]);
            }
        if (i < len)
            printf(" ...");
        printf("\n");
        }
    BIO_flush(bio);
    }
#endif /*DEBUG_SSL*/

SSL* SSLMakeSession(SYS_SOCKET SockFD,
                    int iTimeOut,
                    int iIsServerCtx)
{
    char szSessionId[256];
    SYS_INET_ADDR addrInfo;
    const char* pszServerId = "Xmail_Server";
    const char* pszClientId = "Xmail_Client";
    const char* pszId = NULL;
    SSL_CTX* pSSLCtx = NULL;
    SSL* pSSLSession = NULL;

    ZeroData(addrInfo);

    SysGetPeerInfo(SockFD,
                   addrInfo);

    SysInetNToA(addrInfo,
                szSessionId, sizeof(szSessionId)); /* [i_a] */

    if(iSSLInit)
    {
        if(iIsServerCtx)
        {
            pSSLCtx = pSSLSrvCtx;
            pszId = pszServerId;
        }
        else
        {
            pSSLCtx = pSSLCliCtx;
            pszId = pszClientId;
        }

        StrNCat(szSessionId,
                pszId,
                CStringSize(szSessionId) - strlen(szSessionId));

        pSSLSession = SSL_new(pSSLCtx);

        if(pSSLSession != NULL)
        {
#ifdef DEBUG_SSL
            SSL_set_msg_callback(pSSLSession, msg_cb);
#endif
#if SSLEAY_VERSION_NUMBER >= 0x0922
            SSL_set_session_id_context(pSSLSession,
                                       (const unsigned char*)&szSessionId,
                                       (unsigned int)strlen(szSessionId));
#endif
            BIO_set_tcp_ndelay(SockFD, 1); /* [i_a] */
            SSL_set_fd(pSSLSession,
                       SockFD);

            if(iIsServerCtx)
            {
                SSL_set_accept_state(pSSLSession);
            }
            else
            {
                SSL_set_connect_state(pSSLSession);
            }

            time_t tTimeOut = time(NULL) + iTimeOut;

            for(;;)
            {
                int iResult;

                if(iIsServerCtx)
                {
                    iResult = SSL_accept(pSSLSession);
                }
                else
                {
                    iResult = SSL_connect(pSSLSession);
                }

                iResult = SSL_get_error(pSSLSession,
                                        iResult);
                switch(iResult)
                {
                case SSL_ERROR_NONE:

                    break;

                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                case SSL_ERROR_WANT_X509_LOOKUP:

                    if(time(NULL) < tTimeOut)
                    {
                        SysMsSleep(1);
                        continue;
                    }

                default:

                    SSL_free(pSSLSession);
                    pSSLSession = NULL;
                }

                break;
            }
        }
    }

    return (pSSLSession);
}

void SSLReleaseSession(SSL** pSSLSession)
{
    SSL_free(*pSSLSession);
    *pSSLSession = NULL;
}

int SSLRead(SSL* pSSLSession,
            char* pszBuffer,
            int iBufferSize,
            int iTimeOut)
{
    int iReadBytes = 0;
    time_t tTimeOut = time(NULL) + iTimeOut;

    for(;;)
    {
        iReadBytes = SSL_read(pSSLSession,
                              pszBuffer,
                              iBufferSize);

        switch(SSL_get_error(pSSLSession,
                             iReadBytes))
        {
        case SSL_ERROR_NONE:
        case SSL_ERROR_ZERO_RETURN:

            break;

        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_X509_LOOKUP:

            if(time(NULL) < tTimeOut)
            {
                SysMsSleep(1);
                continue;
            }

        default:

            iReadBytes = -1;
        }

        break;
    }

    return (iReadBytes);
}

int SSLWrite(SSL* pSSLSession,
             const char *pszBuffer,
             int iBufferSize,
             int iTimeOut)
{
    int iWrittenBytes = 0;
    int iSentBytes = 0;
    time_t tTimeOut = time(NULL) + iTimeOut;

    while(iSentBytes < iBufferSize)
    {
        iWrittenBytes = SSL_write(pSSLSession,
                                  pszBuffer + iSentBytes,
                                  iBufferSize - iSentBytes);

        switch(SSL_get_error(pSSLSession,
                             iWrittenBytes))
        {
        case SSL_ERROR_NONE:

            iSentBytes+=iWrittenBytes;
            tTimeOut = time(NULL) + iTimeOut;
            continue;

        case SSL_ERROR_ZERO_RETURN:

            break;

        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_X509_LOOKUP:

            if(time(NULL) < tTimeOut)
            {
                SysMsSleep(1);
                continue;
            }
        }

        break;
    }

    return (iSentBytes);
}

void SSLGetSessionCipher(SSL* pSSLSession,
                         char* pszBuffer,
                         int iBufferSize)
{
    SSL_CIPHER* pCipher = SSL_get_current_cipher(pSSLSession);
    SetEmptyString(pszBuffer);

    if(pCipher)
    {
        SysSNPrintf(pszBuffer,
                    iBufferSize,
                    "protocol=%s, cipher=%s(%d)",
                    SSL_CIPHER_get_version(pCipher),
                    SSL_CIPHER_get_name(pCipher),
                    SSL_CIPHER_get_bits(pCipher, NULL));
    }
}

#endif /*USE_SSL*/
