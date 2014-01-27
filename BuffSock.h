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

#ifndef _BUFFSOCK_H
#define _BUFFSOCK_H

#define BSOCK_BIO_NAME "SOCK"
#define STD_SOCK_BUFFER_SIZE 4096

#define INVALID_BSOCK_HANDLE ((BSOCK_HANDLE) 0)

typedef struct BSOCK_HANDLE_struct {
} *BSOCK_HANDLE;

struct BSockLineBuffer {
	char *pszBuffer;
	int iSize;
};

struct BufSockIOOps {
	void *pPrivate;
	char const *(*pName)(void *);
	int (*pFree)(void *);
	int (*pRead)(void *, void *, int, int);
	int (*pWrite)(void *, void const *, int, int);
	int (*pSendFile)(void *, char const *, SYS_OFF_T, SYS_OFF_T, int);
};

BSOCK_HANDLE BSckAttach(SYS_SOCKET SockFD, int iBufferSize = STD_SOCK_BUFFER_SIZE);
SYS_SOCKET BSckDetach(BSOCK_HANDLE hBSock, int iCloseSocket = 0);
int BSckGetChar(BSOCK_HANDLE hBSock, int iTimeout);
char *BSckChGetString(BSOCK_HANDLE hBSock, char *pszBuffer, int iMaxChars, int iTimeout,
		      int *pLineLength = NULL, int *piGotNL = NULL);
char *BSckGetString(BSOCK_HANDLE hBSock, char *pszBuffer, int iMaxChars, int iTimeout,
		    int *pLineLength = NULL, int *piGotNL = NULL);
int BSckSendString(BSOCK_HANDLE hBSock, char const *pszBuffer, int iTimeout);
int BSckVSendString(BSOCK_HANDLE hBSock, int iTimeout, char const *pszFormat, ...);
int BSckSendData(BSOCK_HANDLE hBSock, char const *pszBuffer, int iSize, int iTimeout);
int BSckReadData(BSOCK_HANDLE hBSock, char *pszBuffer, int iSize, int iTimeout,
		 int iSizeFill = 0);
int BSckSendFile(BSOCK_HANDLE hBSock, char const *pszFilePath, SYS_OFF_T llBaseOffset,
		 SYS_OFF_T llEndOffset, int iTimeout);
SYS_SOCKET BSckGetAttachedSocket(BSOCK_HANDLE hBSock);
int BSckSetIOops(BSOCK_HANDLE hBSock, BufSockIOOps const *pIOops);
char const *BSckBioName(BSOCK_HANDLE hBSock);
int BSckBufferInit(BSockLineBuffer *pBLB, int iSize = -1);
void BSckBufferFree(BSockLineBuffer *pBLB);
char *BSckBufferGet(BSOCK_HANDLE hBSock, BSockLineBuffer *pBLB, int iTimeout,
		    int *piLnLength = NULL);

#endif
