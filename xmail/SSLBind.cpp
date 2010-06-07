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
#include "BuffSock.h"
#include "SSLBind.h"

#include "openssl/bio.h"
#include "openssl/rsa.h"
#include "openssl/crypto.h"
#include "openssl/x509.h"
#include "openssl/pem.h"
#include "openssl/ssl.h"
#include "openssl/evp.h"
#include "openssl/err.h"
#ifndef OPENSSL_NO_ENGINE
#include "openssl/engine.h"
#endif


#define BSSL_WRITE_BLKSIZE (1024 * 64)


struct SslBindCtx {
	BufSockIOOps IOOps;
	SYS_SOCKET SockFD;
	SSL_CTX *pSCtx;
	SSL *pSSL;
};


static SYS_MUTEX *pSslMtxs;


static void BSslLockingCB(int iMode, int iType, const char *pszFile, int iLine)
{
	if (pSslMtxs != NULL) {
		if (iMode & CRYPTO_LOCK)
			SysLockMutex(pSslMtxs[iType], SYS_INFINITE_TIMEOUT);
		else
			SysUnlockMutex(pSslMtxs[iType]);
	}
}

static void BSslThreadExit(void *pPrivate, SYS_THREAD ThreadID, int iMode)
{
	if (iMode == SYS_THREAD_DETACH) {
		/*
		 * This needs to be called at every thread exit, in order to give
		 * a chance to OpenSSL to free its internal state. We do not need
		 * to call ERR_remove_state() if ThreadID is SYS_INVALID_THREAD,
		 * since in such case we would be called from the main thread,
		 * and BSslCleanup() (in BSslFreeOSSL()) takes care of that.
		 */
		if (ThreadID != SYS_INVALID_THREAD)
			ERR_remove_state(0);
	}
}

static void BSslFreeOSSL(void)
{
	ERR_remove_state(0);
#ifndef OPENSSL_NO_ENGINE
	ENGINE_cleanup();
#endif
	CRYPTO_cleanup_all_ex_data();
	ERR_free_strings();
	EVP_cleanup();
}

int BSslInit(void)
{
	int i, iNumLocks = CRYPTO_num_locks();

	if ((pSslMtxs = (SYS_MUTEX *) SysAlloc(iNumLocks * sizeof(SYS_MUTEX))) == NULL)
		return ErrGetErrorCode();
	for (i = 0; i < iNumLocks; i++) {
		if ((pSslMtxs[i] = SysCreateMutex()) == SYS_INVALID_MUTEX) {
			ErrorPush();
			for (i--; i >= 0; i--)
				SysCloseMutex(pSslMtxs[i]);
			SysFreeNullify(pSslMtxs);
			return ErrorPop();
		}
	}
	SSL_load_error_strings();
	SSLeay_add_ssl_algorithms();
	CRYPTO_set_id_callback(SysGetCurrentThreadId);
	CRYPTO_set_locking_callback(BSslLockingCB);
	SysAddThreadExitHook(BSslThreadExit, NULL);

	return 0;
}

void BSslCleanup(void)
{
	int i, iNumLocks = CRYPTO_num_locks();

#ifdef DEBUG_OSSL
	BIO *pErrBIO = BIO_new_fp(stderr, BIO_NOCLOSE);
#endif

	BSslFreeOSSL();

#ifdef DEBUG_OSSL
	if (pErrBIO != NULL) {
		CRYPTO_mem_leaks(pErrBIO);
		BIO_free(pErrBIO);
	}
#endif
	/*
	 * This must be done as the last operation, since OpenSSL cleanup code
	 * mayb end up calling BSslLockingCB() during its operations.
	 */
	for (i = 0; i < iNumLocks; i++)
		SysCloseMutex(pSslMtxs[i]);
	SysFreeNullify(pSslMtxs);
}

static int BSslHandleAsync(SslBindCtx *pCtx, int iCode, int iDefError, int iTimeo)
{
	int iError, iResult = 0;
	SYS_fd_set FdSet;

	if ((iError = SSL_get_error(pCtx->pSSL, iCode)) != SSL_ERROR_NONE) {
		SYS_FD_ZERO(&FdSet);
		SYS_FD_SET(pCtx->SockFD, &FdSet);
		if (iError == SSL_ERROR_WANT_READ) {
			if (SysSelect((long) pCtx->SockFD + 1, &FdSet, NULL,
				      NULL, iTimeo) < 0)
				return ErrGetErrorCode();
			iResult = 1;
		} else if (iError == SSL_ERROR_WANT_WRITE) {
			if (SysSelect((long) pCtx->SockFD + 1, NULL, &FdSet,
				      NULL, iTimeo) < 0)
				return ErrGetErrorCode();
			iResult = 1;
		} else {
			ErrSetErrorCode(iDefError);
			iResult = iDefError;
		}
	}

	return iResult;
}

static int BSslReadLL(SslBindCtx *pCtx, void *pData, int iSize, int iTimeo)
{
	int iRead, iError;

	for (;;) {
		iRead = SSL_read(pCtx->pSSL, pData, iSize);
		if ((iError = BSslHandleAsync(pCtx, iRead, ERR_SSL_READ, iTimeo)) < 0)
			return iError;
		if (iError == 0)
			break;
	}

	return iRead;
}

static int BSslWriteLL(SslBindCtx *pCtx, void const *pData, int iSize, int iTimeo)
{
	int iWrite, iError;

	for (;;) {
		iWrite = SSL_write(pCtx->pSSL, pData, iSize);
		if ((iError = BSslHandleAsync(pCtx, iWrite, ERR_SSL_WRITE, iTimeo)) < 0)
			return iError;
		if (iError == 0)
			break;
	}

	return iWrite;
}

static int BSslShutdown(SslBindCtx *pCtx)
{

	/*
	 * OpenSSL SSL_shutdown() is broken, and does not support non-blocking
	 * BIOs at this time. We need to set the socket back to blocking mode,
	 * in order for SSL_shutdown() to perform correctly. If we do not do
	 * this, TCP/IP connections will be terminated with RST packets, instead
	 * of proper FIN/ACK exchanges.
	 * This sucks, but the problem has been posted by others to OpenSSL dev
	 * mailing lists, and they did not fix it.
	 */
	SysBlockSocket(pCtx->SockFD, 1);
	if (SSL_shutdown(pCtx->pSSL) == 0) {
		SysShutdownSocket(pCtx->SockFD, SYS_SHUT_WR);
		SSL_shutdown(pCtx->pSSL);
	}

	return 0;
}

static char const *BSslCtx__Name(void *pPrivate)
{
	return BSSL_BIO_NAME;
}

static int BSslCtx__Free(void *pPrivate)
{
	SslBindCtx *pCtx = (SslBindCtx *) pPrivate;

	BSslShutdown(pCtx);
	SSL_free(pCtx->pSSL);
	SSL_CTX_free(pCtx->pSCtx);

	/*
	 * Restore default system blocking mode (-1)
	 */
	SysBlockSocket(pCtx->SockFD, -1);
	SysFree(pCtx);

	return 0;
}

static int BSslCtx__Read(void *pPrivate, void *pData, int iSize, int iTimeo)
{
	SslBindCtx *pCtx = (SslBindCtx *) pPrivate;

	return BSslReadLL(pCtx, pData, iSize, iTimeo);
}

static int BSslCtx__Write(void *pPrivate, void const *pData, int iSize, int iTimeo)
{
	SslBindCtx *pCtx = (SslBindCtx *) pPrivate;

	return BSslWriteLL(pCtx, pData, iSize, iTimeo);
}

static int BSslCtx__SendFile(void *pPrivate, char const *pszFilePath, SYS_OFF_T llOffStart,
			     SYS_OFF_T llOffEnd, int iTimeo)
{
	SslBindCtx *pCtx = (SslBindCtx *) pPrivate;
	SYS_MMAP hMap;
	SYS_OFF_T llSize, llAlnOff;
	void *pMapAddr;
	char *pCurAddr;

	if ((hMap = SysCreateMMap(pszFilePath, SYS_MMAP_READ)) == SYS_INVALID_MMAP)
		return ErrGetErrorCode();
	llSize = SysMMapSize(hMap);
	if (llOffEnd == -1)
		llOffEnd = llSize;
	if (llOffStart > llSize || llOffEnd > llSize || llOffStart > llOffEnd) {
		SysCloseMMap(hMap);
		ErrSetErrorCode(ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}
	llAlnOff = SysMMapOffsetAlign(hMap, llOffStart);
	if ((pMapAddr = SysMapMMap(hMap, llAlnOff,
				   (SYS_SIZE_T) (llOffEnd - llAlnOff))) == NULL) {
		ErrorPush();
		SysCloseMMap(hMap);
		return ErrorPop();
	}
	pCurAddr = (char *) pMapAddr + (llOffStart - llAlnOff);
	while (llOffStart < llOffEnd) {
		int iSize = (int) Min(BSSL_WRITE_BLKSIZE, llOffEnd - llOffStart), iWrite;

		if ((iWrite = BSslWriteLL(pCtx, pCurAddr, iSize, iTimeo)) < 0) {
			ErrorPush();
			SysUnmapMMap(hMap, pMapAddr, (SYS_SIZE_T) (llOffEnd - llAlnOff));
			SysCloseMMap(hMap);
			return ErrorPop();
		}
		pCurAddr += iWrite;
		llOffStart += iWrite;
	}
	SysUnmapMMap(hMap, pMapAddr, (SYS_SIZE_T) (llOffEnd - llAlnOff));
	SysCloseMMap(hMap);

	return 0;
}

static int BSslAllocCtx(SslBindCtx **ppCtx, SYS_SOCKET SockFD, SSL_CTX *pSCtx, SSL *pSSL)
{
	SslBindCtx *pCtx;

	if ((pCtx = (SslBindCtx *) SysAlloc(sizeof(SslBindCtx))) == NULL)
		return ErrGetErrorCode();
	pCtx->IOOps.pPrivate = pCtx;
	pCtx->IOOps.pName = BSslCtx__Name;
	pCtx->IOOps.pFree = BSslCtx__Free;
	pCtx->IOOps.pRead = BSslCtx__Read;
	pCtx->IOOps.pWrite = BSslCtx__Write;
	pCtx->IOOps.pSendFile = BSslCtx__SendFile;
	pCtx->SockFD = SockFD;
	pCtx->pSCtx = pSCtx;
	pCtx->pSSL = pSSL;

	*ppCtx = pCtx;

	return 0;
}

static int BSslEnvExport(SSL_CTX *pSCtx, SSL *pSSL, X509 *pCert,
			 int (*pfEnvCB)(void *, int, void const *), void *pPrivate)
{
	char *pszVal;

	pszVal = X509_NAME_oneline(X509_get_issuer_name(pCert), 0, 0);
	(*pfEnvCB)(pPrivate, BSSL_CERT_ISSUER, pszVal);
	OPENSSL_free(pszVal);

	pszVal = X509_NAME_oneline(X509_get_subject_name(pCert), 0, 0);
	(*pfEnvCB)(pPrivate, BSSL_CERT_SUBJECT, pszVal);
	OPENSSL_free(pszVal);

	return 0;
}

static int BSslCertVerifyCB(int iOK, X509_STORE_CTX *pXsCtx)
{
	int iError, iDepth;
	SSL *pSSL;
	SSL_CTX *pSCtx;
	SslServerBind const *pSSLB;
	X509 *pCert;

	iError = X509_STORE_CTX_get_error(pXsCtx);
	iDepth = X509_STORE_CTX_get_error_depth(pXsCtx);
	if ((pSSL = (SSL *)
	     X509_STORE_CTX_get_ex_data(pXsCtx, SSL_get_ex_data_X509_STORE_CTX_idx())) == NULL)
		return iOK;
	pSCtx = SSL_get_SSL_CTX(pSSL);
	pSSLB = (SslServerBind const *) SSL_CTX_get_app_data(pSCtx);
	pCert = X509_STORE_CTX_get_current_cert(pXsCtx);

#ifdef DEBUG_OSSL
	char *pszVal;

	pszVal = X509_NAME_oneline(X509_get_issuer_name(pCert), 0, 0);
	SysLogMessage(LOG_LEV_MESSAGE, "CERT Issuer: %s\n", pszVal);
	OPENSSL_free(pszVal);

	pszVal = X509_NAME_oneline(X509_get_subject_name(pCert), 0, 0);
	SysLogMessage(LOG_LEV_MESSAGE, "CERT Subject: %s\n", pszVal);
	OPENSSL_free(pszVal);
#endif

	if (!iOK) {
		SysLogMessage(LOG_LEV_MESSAGE, "CERT verify error: depth = %d error = '%s'\n",
			      iDepth, X509_verify_cert_error_string(iError));
		if (iError == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT &&
		    pSSLB->ulFlags & BSSLF_ALLOW_SEFLSIGNED) {
			SysLogMessage(LOG_LEV_MESSAGE, "Self signed CERT allowed (override)\n");
			iOK = 1;
		}
	}

	return iOK;
}

static int BSslSetupVerify(SSL_CTX *pSCtx, SslServerBind const *pSSLB)
{
	char const *pszKeyFile = pSSLB->pszKeyFile;

	if (pSSLB->pszCertFile != NULL) {
		if (SSL_CTX_use_certificate_file(pSCtx, pSSLB->pszCertFile,
						 SSL_FILETYPE_PEM) <= 0) {
			ErrSetErrorCode(ERR_SSL_SETCERT, pSSLB->pszCertFile);
			return ERR_SSL_SETCERT;
		}
		if (pszKeyFile == NULL)
			pszKeyFile = pSSLB->pszCertFile;
	}
	if (pszKeyFile != NULL) {
		if (SSL_CTX_use_PrivateKey_file(pSCtx, pszKeyFile,
						SSL_FILETYPE_PEM) <= 0) {
			ErrSetErrorCode(ERR_SSL_SETKEY, pszKeyFile);
			return ERR_SSL_SETKEY;
		}
		if (!SSL_CTX_check_private_key(pSCtx)) {
			ErrSetErrorCode(ERR_SSL_CHECKKEY, pszKeyFile);
			return ERR_SSL_CHECKKEY;
		}
	}
	if (pSSLB->ulFlags & BSSLF_WANT_VERIFY) {
		int iVerMode = SSL_VERIFY_PEER;

		if (pSSLB->ulFlags & BSSLF_WANT_CERT)
			iVerMode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
		SSL_CTX_set_verify(pSCtx, iVerMode, BSslCertVerifyCB);
		if (pSSLB->iMaxDepth > 0)
			SSL_CTX_set_verify_depth(pSCtx, pSSLB->iMaxDepth);
		SSL_CTX_set_app_data(pSCtx, pSSLB);

		if (pSSLB->pszCAFile != NULL || pSSLB->pszCAPath != NULL) {
			if (!SSL_CTX_load_verify_locations(pSCtx, pSSLB->pszCAFile,
							   pSSLB->pszCAPath)) {
				ErrSetErrorCode(ERR_SSL_VERPATHS);
				return ERR_SSL_VERPATHS;
			}
		} else if (!SSL_CTX_set_default_verify_paths(pSCtx)) {
			ErrSetErrorCode(ERR_SSL_VERPATHS);
			return ERR_SSL_VERPATHS;
		}
	}

	return 0;
}

int BSslBindClient(BSOCK_HANDLE hBSock, SslServerBind const *pSSLB,
		   int (*pfEnvCB)(void *, int, void const *), void *pPrivate)
{
	int iError;
	SYS_SOCKET SockFD;
	SSL_METHOD const *pMethod;
	SSL_CTX *pSCtx;
	SSL *pSSL;
	X509 *pCert;
	SslBindCtx *pCtx;

	pMethod = SSLv23_client_method();
	if ((pSCtx = SSL_CTX_new((SSL_METHOD *) pMethod)) == NULL) {
		ErrSetErrorCode(ERR_SSLCTX_CREATE);
		return ERR_SSLCTX_CREATE;
	}
	SSL_CTX_set_session_cache_mode(pSCtx, SSL_SESS_CACHE_OFF);
	/*
	 * Client may not supply a certificate.
	 */
	if (pSSLB != NULL &&
	    BSslSetupVerify(pSCtx, pSSLB) < 0) {
		SSL_CTX_free(pSCtx);
		return ErrGetErrorCode();
	}
	if ((pSSL = SSL_new(pSCtx)) == NULL) {
		SSL_CTX_free(pSCtx);
		ErrSetErrorCode(ERR_SSL_CREATE);
		return ERR_SSL_CREATE;
	}
	SockFD = BSckGetAttachedSocket(hBSock);
	/*
	 * We want blocking sockets during the initial SSL negotiation.
	 */
	SysBlockSocket(SockFD, 1);
	SSL_set_fd(pSSL, SockFD);
	if (SSL_connect(pSSL) == -1) {
		SysBlockSocket(SockFD, -1);
		SSL_free(pSSL);
		SSL_CTX_free(pSCtx);
		ErrSetErrorCode(ERR_SSL_CONNECT);
		return ERR_SSL_CONNECT;
	}
	SSL_CTX_set_app_data(pSCtx, NULL);
	/*
	 * Server must supply a certificate.
	 */
	if ((pCert = SSL_get_peer_certificate(pSSL)) == NULL) {
		SysBlockSocket(SockFD, -1);
		SSL_free(pSSL);
		SSL_CTX_free(pSCtx);
		ErrSetErrorCode(ERR_SSL_NOCERT);
		return ERR_SSL_NOCERT;
	}
	iError = (pfEnvCB != NULL) ? BSslEnvExport(pSCtx, pSSL, pCert,
						   pfEnvCB, pPrivate): 0;
	X509_free(pCert);
	if (iError < 0 ||
	    BSslAllocCtx(&pCtx, SockFD, pSCtx, pSSL) < 0) {
		ErrorPush();
		SysBlockSocket(SockFD, -1);
		SSL_free(pSSL);
		SSL_CTX_free(pSCtx);
		return ErrorPop();
	}
	/*
	 * Need to use non-blocking socket to implement read/write timeouts.
	 */
	SysBlockSocket(SockFD, 0);
	BSckSetIOops(hBSock, &pCtx->IOOps);

	return 0;
}

int BSslBindServer(BSOCK_HANDLE hBSock, SslServerBind const *pSSLB,
		   int (*pfEnvCB)(void *, int, void const *), void *pPrivate)
{
	int iError;
	SYS_SOCKET SockFD;
	SSL_METHOD const *pMethod;
	SSL_CTX *pSCtx;
	SSL *pSSL;
	X509 *pCert;
	SslBindCtx *pCtx;

	pMethod = SSLv23_server_method();
	if ((pSCtx = SSL_CTX_new((SSL_METHOD *) pMethod)) == NULL) {
		ErrSetErrorCode(ERR_SSLCTX_CREATE);
		return ERR_SSLCTX_CREATE;
	}
	SSL_CTX_set_session_cache_mode(pSCtx, SSL_SESS_CACHE_OFF);
	if (BSslSetupVerify(pSCtx, pSSLB) < 0) {
		SSL_CTX_free(pSCtx);
		return ErrGetErrorCode();
	}
	if ((pSSL = SSL_new(pSCtx)) == NULL) {
		SSL_CTX_free(pSCtx);
		ErrSetErrorCode(ERR_SSL_CREATE);
		return ERR_SSL_CREATE;
	}
	SockFD = BSckGetAttachedSocket(hBSock);
	/*
	 * We want blocking sockets during the initial SSL negotiation.
	 */
	SysBlockSocket(SockFD, 1);
	SSL_set_fd(pSSL, SockFD);
	if (SSL_accept(pSSL) == -1) {
		SysBlockSocket(SockFD, -1);
		SSL_free(pSSL);
		SSL_CTX_free(pSCtx);
		ErrSetErrorCode(ERR_SSL_ACCEPT);
		return ERR_SSL_ACCEPT;
	}
	SSL_CTX_set_app_data(pSCtx, NULL);
	/*
	 * Client may not supply a certificate.
	 */
	iError = 0;
	if (pfEnvCB != NULL &&
	    (pCert = SSL_get_peer_certificate(pSSL)) != NULL) {
		iError = BSslEnvExport(pSCtx, pSSL, pCert, pfEnvCB, pPrivate);
		X509_free(pCert);
	}
	if (iError < 0 ||
	    BSslAllocCtx(&pCtx, SockFD, pSCtx, pSSL) < 0) {
		ErrorPush();
		SysBlockSocket(SockFD, -1);
		SSL_free(pSSL);
		SSL_CTX_free(pSCtx);
		return ErrorPop();
	}
	/*
	 * Need to use non-blocking socket to implement read/write timeouts.
	 */
	SysBlockSocket(SockFD, 0);
	BSckSetIOops(hBSock, &pCtx->IOOps);

	return 0;
}

