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

#define BSOCK_EOF                   INT_MIN
#define BSOCK_STD_BUFFER_SIZE       1024

#define BSOCK_NAME(p) (*(p)->IOops.pName)((p)->IOops.pPrivate)
#define BSOCK_FREE(p) (*(p)->IOops.pFree)((p)->IOops.pPrivate)
#define BSOCK_READ(p, d, n, t) (*(p)->IOops.pRead)((p)->IOops.pPrivate, d, n, t)
#define BSOCK_WRITE(p, d, n, t) (*(p)->IOops.pWrite)((p)->IOops.pPrivate, d, n, t)
#define BSOCK_SENDFILE(p, f, b, e, t) (*(p)->IOops.pSendFile)((p)->IOops.pPrivate, f, b, e, t)


struct BuffSocketData {
	SYS_SOCKET SockFD;
	int iBufferSize;
	char *pszBuffer;
	int iBytesInBuffer;
	int iReadIndex;
	BufSockIOOps IOops;
};


static int BSckReadLL(BuffSocketData *pBSD, void *pData, int iSize, int iTimeout)
{
	int iCount = 0;
	int iCRead;

	while (iCount < iSize) {
		iCRead = BSOCK_READ(pBSD, (char *) pData + iCount,
				    iSize - iCount, iTimeout);
		if (iCRead <= 0)
			return iCount;
		iCount += iCRead;
	}

	return iCount;
}

static int BSckWriteLL(BuffSocketData *pBSD, void const *pData, int iSize, int iTimeout)
{
	int iCount = 0;
	int iCWrite;

	while (iCount < iSize) {
		iCWrite = BSOCK_WRITE(pBSD, (char const *) pData + iCount,
				      iSize - iCount, iTimeout);
		if (iCWrite <= 0)
			return iCount;
		iCount += iCWrite;
	}

	return iCount;
}

static char const *BSckSock_Name(PrivateDataRef pPrivate)
{
	return BSOCK_BIO_NAME;
}

static int BSckSock_Free(PrivateDataRef pPrivate)
{
	return 0;
}

static int BSckSock_Read(PrivateDataRef pPrivate, void *pData, int iSize, int iTimeout)
{
	return SysRecvData(pPrivate.handle, (char *) pData, iSize, iTimeout);
}

static int BSckSock_Write(PrivateDataRef pPrivate, void const *pData, int iSize, int iTimeout)
{
	return SysSendData(pPrivate.handle, (char const *) pData, iSize, iTimeout);
}

static int BSckSock_SendFile(PrivateDataRef pPrivate, char const *pszFilePath, SYS_OFF_T llBaseOffset,
			     SYS_OFF_T llEndOffset, int iTimeout)
{
	return SysSendFile(pPrivate.handle, pszFilePath, llBaseOffset,
			   llEndOffset, iTimeout);
}

BSOCK_HANDLE BSckAttach(SYS_SOCKET SockFD, int iBufferSize)
{
	BuffSocketData *pBSD = (BuffSocketData *) SysAlloc(sizeof(BuffSocketData));

	if (pBSD == NULL)
		return INVALID_BSOCK_HANDLE;

	char *pszBuffer = (char *) SysAlloc(iBufferSize);

	if (pszBuffer == NULL) {
		SysFree(pBSD);
		return INVALID_BSOCK_HANDLE;
	}

	pBSD->SockFD = SockFD;
	pBSD->iBufferSize = iBufferSize;
	pBSD->pszBuffer = pszBuffer;
	pBSD->iBytesInBuffer = 0;
	pBSD->iReadIndex = 0;
	pBSD->IOops.pPrivate.handle = SockFD;
	pBSD->IOops.pName = BSckSock_Name;
	pBSD->IOops.pFree = BSckSock_Free;
	pBSD->IOops.pRead = BSckSock_Read;
	pBSD->IOops.pWrite = BSckSock_Write;
	pBSD->IOops.pSendFile = BSckSock_SendFile;

	return (BSOCK_HANDLE) pBSD;
}

SYS_SOCKET BSckDetach(BSOCK_HANDLE hBSock, int iCloseSocket)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;
	SYS_SOCKET SockFD = SYS_INVALID_SOCKET;

	if (pBSD != NULL) {
		SockFD = pBSD->SockFD;
		BSOCK_FREE(pBSD);
		SysFree(pBSD->pszBuffer);
		SysFree(pBSD);
		if (iCloseSocket) {
			SysCloseSocket(SockFD);
			return SYS_INVALID_SOCKET;
		}
	}

	return SockFD;
}

static int BSckFetchData(BuffSocketData *pBSD, int iTimeout)
{
	int iRdBytes;

	pBSD->iReadIndex = 0;
	if ((iRdBytes = BSOCK_READ(pBSD, pBSD->pszBuffer, pBSD->iBufferSize,
				   iTimeout)) <= 0) {
		ErrSetErrorCode(ERR_SOCK_NOMORE_DATA);
		return iRdBytes;
	}
	pBSD->iBytesInBuffer = iRdBytes;

	return iRdBytes;
}

int BSckGetChar(BSOCK_HANDLE hBSock, int iTimeout)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	if ((pBSD->iBytesInBuffer == 0) && (BSckFetchData(pBSD, iTimeout) <= 0))
		return BSOCK_EOF;

	int iChar = pBSD->pszBuffer[pBSD->iReadIndex];

	pBSD->iReadIndex = INext(pBSD->iReadIndex, pBSD->iBufferSize);
	--pBSD->iBytesInBuffer;

	return iChar;
}

char *BSckChGetString(BSOCK_HANDLE hBSock, char *pszBuffer, int iMaxChars, int iTimeout,
		      int *pLineLength, int *piGotNL)
{
	int i;
	int iChar;

	for (i = 0, iMaxChars--; i < iMaxChars; i++) {
		iChar = BSckGetChar(hBSock, iTimeout);
		if (iChar == BSOCK_EOF)
			return NULL;

		if (iChar == '\n') {
			for (; (i > 0) && (pszBuffer[i - 1] == '\r'); i--);
			pszBuffer[i] = '\0';
			if (pLineLength != NULL)
				*pLineLength = i;
			if (piGotNL != NULL)
				*piGotNL = 1;

			return pszBuffer;
		} else
			pszBuffer[i] = (char) iChar;
	}
	pszBuffer[i] = '\0';
	if (pLineLength != NULL)
		*pLineLength = i;
	if (piGotNL != NULL) {
		*piGotNL = 0;
		return pszBuffer;
	}

	ErrSetErrorCode(ERR_LINE_TOO_LONG);

	return NULL;
}

char *BSckGetString(BSOCK_HANDLE hBSock, char *pszBuffer, int iMaxChars, int iTimeout,
		    int *pLineLength, int *piGotNL)
{
	int i;
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	for (i = 0, iMaxChars--; i < iMaxChars;) {
		/* Verify to have something to read */
		if (pBSD->iBytesInBuffer == 0 && BSckFetchData(pBSD, iTimeout) <= 0)
			return NULL;

		int iBytesLookup = Min(pBSD->iBytesInBuffer, iMaxChars - i);

		if (iBytesLookup > 0) {
			char *pszNL = (char *) memchr(pBSD->pszBuffer + pBSD->iReadIndex, '\n',
						      iBytesLookup);

			if (pszNL != NULL) {
				int iCopySize = (int) (pszNL - (pBSD->pszBuffer + pBSD->iReadIndex));

				memcpy(pszBuffer + i, pBSD->pszBuffer + pBSD->iReadIndex,
				       iCopySize);
				i += iCopySize;
				pBSD->iReadIndex += iCopySize + 1;
				pBSD->iBytesInBuffer -= iCopySize + 1;

				/* Line cleanup */
				for (; i > 0 && pszBuffer[i - 1] == '\r'; i--);
				pszBuffer[i] = '\0';
				if (pLineLength != NULL)
					*pLineLength = i;
				if (piGotNL != NULL)
					*piGotNL = 1;

				SysLogMessage(LOG_LEV_DEBUG, "socket read line: [%s]\n", pszBuffer);

				return pszBuffer;
			} else {
				memcpy(pszBuffer + i, pBSD->pszBuffer + pBSD->iReadIndex,
				       iBytesLookup);
				i += iBytesLookup;
				pBSD->iReadIndex += iBytesLookup;
				pBSD->iBytesInBuffer -= iBytesLookup;
			}
		}
	}
	pszBuffer[i] = '\0';
	if (pLineLength != NULL)
		*pLineLength = i;
	if (piGotNL != NULL) {
		*piGotNL = 0;

		SysLogMessage(LOG_LEV_DEBUG, "socket read line: [%s]\n", pszBuffer);

		return pszBuffer;
	}

	ErrSetErrorCode(ERR_LINE_TOO_LONG);

	return NULL;
}

int BSckSendString(BSOCK_HANDLE hBSock, char const *pszBuffer, int iTimeout)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;
	char *pszSendBuffer = (char *) SysAlloc(strlen(pszBuffer) + 3);

	if (pszSendBuffer == NULL)
		return ErrGetErrorCode();

	SysLogMessage(LOG_LEV_DEBUG, "socket write line: [%s]\n", pszBuffer);

	sprintf(pszSendBuffer, "%s\r\n", pszBuffer);

	int iSendLength = (int)strlen(pszSendBuffer);

	if (BSckWriteLL(pBSD, pszSendBuffer, iSendLength, iTimeout) != iSendLength) {
		SysFree(pszSendBuffer);
		return ErrGetErrorCode();
	}
	SysFree(pszSendBuffer);

	return iSendLength;
}

int BSckVSendString(BSOCK_HANDLE hBSock, int iTimeout, char const *pszFormat, ...)
{
	char *pszBuffer = NULL;

	StrVSprint(pszBuffer, pszFormat, pszFormat);

	if (pszBuffer == NULL)
		return ErrGetErrorCode();
	if (BSckSendString(hBSock, pszBuffer, iTimeout) < 0) {
		ErrorPush();
		SysFree(pszBuffer);
		return ErrorPop();
	}
	SysFree(pszBuffer);

	return 0;
}

int BSckSendData(BSOCK_HANDLE hBSock, char const *pszBuffer, int iSize, int iTimeout)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	SysLogMessage(LOG_LEV_DEBUG, "socket write data (len = %d)\n", iSize);

	if (BSckWriteLL(pBSD, pszBuffer, iSize, iTimeout) != iSize)
		return ErrGetErrorCode();

	return iSize;
}

int BSckReadData(BSOCK_HANDLE hBSock, char *pszBuffer, int iSize, int iTimeout, int iSizeFill)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;
	int iRdBytes = 0, iBufRdBytes = Min(iSize, pBSD->iBytesInBuffer);

	if (iBufRdBytes > 0) {
		memcpy(pszBuffer, pBSD->pszBuffer + pBSD->iReadIndex, iBufRdBytes);
		pBSD->iReadIndex += iBufRdBytes;
		pBSD->iBytesInBuffer -= iBufRdBytes;
		iRdBytes = iBufRdBytes;
	}
	if (iRdBytes == 0 || (iSizeFill && iRdBytes < iSize)) {
		int iRdSize = BSckReadLL(pBSD, pszBuffer + iRdBytes,
					 iSize - iRdBytes, iTimeout);

		if (iRdSize > 0)
			iRdBytes += iRdSize;
	}

	return iRdBytes;
}

int BSckSendFile(BSOCK_HANDLE hBSock, char const *pszFilePath, SYS_OFF_T llBaseOffset,
		 SYS_OFF_T llEndOffset, int iTimeout)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	return BSOCK_SENDFILE(pBSD, pszFilePath, llBaseOffset, llEndOffset, iTimeout);
}

SYS_SOCKET BSckGetAttachedSocket(BSOCK_HANDLE hBSock)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	return pBSD->SockFD;
}

int BSckSetIOops(BSOCK_HANDLE hBSock, BufSockIOOps const *pIOops)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	pBSD->IOops = *pIOops;

	return 0;
}

char const *BSckBioName(BSOCK_HANDLE hBSock)
{
	BuffSocketData *pBSD = (BuffSocketData *) hBSock;

	return BSOCK_NAME(pBSD);
}

int BSckBufferInit(BSockLineBuffer *pBLB, int iSize)
{
	if (iSize <= 0)
		iSize = BSOCK_STD_BUFFER_SIZE;

	if ((pBLB->pszBuffer = (char *) SysAlloc(iSize)) == NULL)
		return ErrGetErrorCode();
	pBLB->iSize = iSize;

	return 0;
}

void BSckBufferFree(BSockLineBuffer *pBLB)
{
	if (pBLB->pszBuffer != NULL)
		SysFree(pBLB->pszBuffer);
}

char *BSckBufferGet(BSOCK_HANDLE hBSock, BSockLineBuffer *pBLB, int iTimeout, int *piLnLength)
{
	int iLnLength = 0;
	int iCurrLength;
	int iGotNL;

	do {
		if (BSckGetString(hBSock, pBLB->pszBuffer + iLnLength,
				  pBLB->iSize - 1 - iLnLength, iTimeout, &iCurrLength,
				  &iGotNL) == NULL)
			return NULL;
		if (!iGotNL) {
			int iNewSize = 2 * pBLB->iSize + 1;
			char *pszBuffer = (char *) SysRealloc(pBLB->pszBuffer,
							      (unsigned int) iNewSize);

			if (pszBuffer == NULL)
				return NULL;
			pBLB->pszBuffer = pszBuffer;
			pBLB->iSize = iNewSize;
		}
		iLnLength += iCurrLength;
	} while (!iGotNL);
	if (piLnLength != NULL)
		*piLnLength = iLnLength;

	return pBLB->pszBuffer;
}

