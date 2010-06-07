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
#include "ShBlocks.h"
#include "StrUtils.h"
#include "BuffSock.h"
#include "SList.h"
#include "MailConfig.h"
#include "MessQueue.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"
#include "DNS.h"

#define DNS_PORTNO              53
#define DNS_SOCKET_TIMEOUT      16
#define DNS_QUERY_EXTRA         1024
#define DNS_MAX_RESP_PACKET     1024
#define DNS_SEND_RETRIES        3
#define DNS_MAX_RR_DATA         256
#define DNS_RESPDATA_EXTRA      (2 * sizeof(int))

#if defined(BIG_ENDIAN_CPU)
#define DNS_LABEL_LEN_MASK      0x3fff
#else /* BIG_ENDIAN_CPU */
#define DNS_LABEL_LEN_MASK      0xff3f
#endif /* BIG_ENDIAN_CPU */

#define DNS_LABEL_LEN_INVMASK   0xc0

#define ROOTS_FILE              "dnsroots"

struct DNSQuery {
	DNS_HEADER DNSH;
	SYS_UINT8 QueryData[DNS_QUERY_EXTRA];
};

struct DNSResourceRecord {
	char szName[MAX_HOST_NAME];
	SYS_UINT16 Type;
	SYS_UINT16 Class;
	SYS_UINT32 TTL;
	SYS_UINT16 Lenght;
	SYS_UINT8 const *pBaseData;
	SYS_UINT8 const *pRespData;
};

static int DNS_GetResourceRecord(SYS_UINT8 const *pBaseData, SYS_UINT8 const *pRespData,
				 DNSResourceRecord *pRR = NULL, int *piRRLength = NULL);
static int DNS_GetText(SYS_UINT8 const *pBaseData, SYS_UINT8 const *pRespData,
		       char *pszText = NULL, int iMaxText = 0, int *piRRLength = NULL);
static int DNS_GetQuery(SYS_UINT8 const *pBaseData, SYS_UINT8 const *pRespData,
			char *pszInetName = NULL, int iMaxName = 0, SYS_UINT16 *pType = NULL,
			SYS_UINT16 *pClass = NULL, int *piRRLength = NULL);
static SYS_UINT8 *DNS_QueryExec(char const *pszDNSServer, int iPortNo, int iTimeout,
				unsigned int uOpCode, unsigned int uQType,
				char const *pszInetName, int iAskQR = 0);

int DNS_InitAnswer(DNSAnswer *pAns)
{
	int i;

	pAns->iQDCount = pAns->iANCount = pAns->iNSCount = pAns->iARCount = 0;
	for (i = 0; i < QTYPE_ANSWER_MAX; i++)
		SYS_INIT_LIST_HEAD(&pAns->RecsLst[i]);

	return 0;
}

void DNS_FreeRecList(SysListHead *pHead)
{
	SysListHead *pLnk;

	while ((pLnk = SYS_LIST_FIRST(pHead)) != NULL) {
		DNSRecord *pRec = SYS_LIST_ENTRY(pLnk, DNSRecord, Lnk);

		SYS_LIST_DEL(pLnk);
		SysFree(pRec);
	}
}

void DNS_FreeAnswer(DNSAnswer *pAns)
{
	int i;

	for (i = 0; i < QTYPE_ANSWER_MAX; i++)
		DNS_FreeRecList(&pAns->RecsLst[i]);
}

static DNSRecord *DNS_AllocRec(DNSAnswer *pAns, int iType,
			       DNSResourceRecord const *pRR)
{
	DNSRecord *pRec = (struct DNSRecord *) SysAlloc(sizeof(DNSRecord));

	if (pRec == NULL)
		return NULL;
	SYS_LIST_ADDT(&pRec->Lnk, &pAns->RecsLst[iType]);
	pRec->TTL = pRR->TTL;
	pRec->Class = pRR->Class;

	return pRec;
}

static SYS_UINT8 *DNS_AllocRespData(int iSize)
{
	SYS_UINT8 *pBaseData = (SYS_UINT8 *) SysAlloc(iSize + DNS_RESPDATA_EXTRA);

	if (pBaseData == NULL)
		return NULL;
	*(int *) pBaseData = iSize;

	return pBaseData + DNS_RESPDATA_EXTRA;
}

static void DNS_FreeRespData(SYS_UINT8 *pRespData)
{
	if (pRespData != NULL)
		SysFree(pRespData - DNS_RESPDATA_EXTRA);
}

static int DNS_RespDataSize(SYS_UINT8 const *pRespData)
{
	SYS_UINT8 const *pBaseData = pRespData - DNS_RESPDATA_EXTRA;

	return *(int *) pBaseData;
}

static int DNS_GetResourceRecord(SYS_UINT8 const *pBaseData, SYS_UINT8 const *pRespData,
				 DNSResourceRecord *pRR, int *piRRLength)
{
	int iRRLen;
	char *pszName;

	if (pRR != NULL) {
		ZeroData(*pRR);
		pRR->pBaseData = pBaseData;
		pszName = pRR->szName;
	} else
		pszName = NULL;

	/* Read name field */
	if (DNS_GetText(pBaseData, pRespData, pszName,
			pszName != NULL ? sizeof(pRR->szName): 0, &iRRLen) < 0)
		return ErrGetErrorCode();
	pRespData += iRRLen;

	/* Read type field */
	if (pRR != NULL)
		pRR->Type = ntohs(MscReadUint16(pRespData));
	pRespData += sizeof(SYS_UINT16);
	iRRLen += sizeof(SYS_UINT16);

	/* Read class field */
	if (pRR != NULL)
		pRR->Class = ntohs(MscReadUint16(pRespData));
	pRespData += sizeof(SYS_UINT16);
	iRRLen += sizeof(SYS_UINT16);

	/* Read TTL field */
	if (pRR != NULL)
		pRR->TTL = ntohl(MscReadUint32(pRespData));
	pRespData += sizeof(SYS_UINT32);
	iRRLen += sizeof(SYS_UINT32);

	/* Read lenght field */
	SYS_UINT16 Lenght = ntohs(MscReadUint16(pRespData));

	if (pRR != NULL)
		pRR->Lenght = Lenght;
	pRespData += sizeof(SYS_UINT16);
	iRRLen += sizeof(SYS_UINT16);

	/* Read RR data */
	if (pRR != NULL)
		pRR->pRespData = pRespData;
	iRRLen += (int) Lenght;
	if (piRRLength != NULL)
		*piRRLength = iRRLen;

	return 0;
}

static int DNS_GetText(SYS_UINT8 const *pBaseData, SYS_UINT8 const *pRespData,
		       char *pszText, int iMaxText, int *piRRLength)
{
	int iBaseLength = DNS_RespDataSize(pBaseData);
	int iCurrOffset = (int) (pRespData - pBaseData);
	int iDataLength = 0, iTextLength = 0, iBackLink = 0, iLabelLength;

	while (*pRespData != 0) {
		if (*pRespData & DNS_LABEL_LEN_INVMASK) {
			/* Got displacement from base-data (boundary checked) */
			if ((iCurrOffset = (int)
			     ntohs(MscReadUint16(pRespData) & DNS_LABEL_LEN_MASK)) >= iBaseLength) {
				ErrSetErrorCode(ERR_BAD_DNS_NAME_RECORD);
				return ERR_BAD_DNS_NAME_RECORD;
			}
			pRespData = pBaseData + iCurrOffset;
			if (!iBackLink)
				iDataLength += sizeof(SYS_UINT16), ++iBackLink;

			continue;
		}
		/* Extract label length (boundary checked) */
		iLabelLength = (int) *pRespData;
		if ((iMaxText > 0 && (iTextLength + iLabelLength + 2 >= iMaxText)) ||
		    (iCurrOffset + iLabelLength + 1 >= iBaseLength)) {
			ErrSetErrorCode(ERR_BAD_DNS_NAME_RECORD);
			return ERR_BAD_DNS_NAME_RECORD;
		}
		if (pszText != NULL) {
			/* Append to name and update pointers */
			memcpy(pszText, pRespData + 1, iLabelLength);
			pszText[iLabelLength] = '.';
			pszText += iLabelLength + 1;
		}
		iTextLength += iLabelLength + 1;

		/* If we've not got a back-jump, update the data length */
		if (!iBackLink)
			iDataLength += iLabelLength + 1;

		/* Move pointers */
		iCurrOffset += iLabelLength + 1;
		pRespData += iLabelLength + 1;
	}
	if (pszText != NULL)
		*pszText = '\0';
	if (piRRLength != NULL)
		*piRRLength = (!iBackLink) ? iDataLength + 1: iDataLength;

	return 0;
}

static int DNS_GetQuery(SYS_UINT8 const *pBaseData, SYS_UINT8 const *pRespData,
			char *pszInetName, int iMaxName, SYS_UINT16 *pType,
			SYS_UINT16 *pClass, int *piRRLength)
{
	/* Read name field */
	int iQueryLen = 0;

	if (DNS_GetText(pBaseData, pRespData, pszInetName, iMaxName,
			&iQueryLen) < 0)
		return ErrGetErrorCode();

	pRespData += iQueryLen;

	/* Read type field */
	if (pType != NULL)
		*pType = ntohs(MscReadUint16(pRespData));

	pRespData += sizeof(SYS_UINT16);
	iQueryLen += sizeof(SYS_UINT16);

	/* Read class field */
	if (pClass != NULL)
		*pClass = ntohs(MscReadUint16(pRespData));

	pRespData += sizeof(SYS_UINT16);
	iQueryLen += sizeof(SYS_UINT16);

	if (piRRLength != NULL)
		*piRRLength = iQueryLen;

	return 0;
}

static int DNS_NameCopy(SYS_UINT8 *pDNSQName, char const *pszInetName)
{
	int iNameLen = 0, iTokLen;
	char *pszToken, *pszSavePtr, *pszNameCopy;

	if ((pszNameCopy = SysStrDup(pszInetName)) == NULL)
		return ErrGetErrorCode();
	pszToken = SysStrTok(pszNameCopy, ".", &pszSavePtr);
	while (pszToken != NULL) {
		iTokLen = strlen(pszToken);

		*pDNSQName = (SYS_UINT8) iTokLen;

		strcpy((char *) pDNSQName + 1, pszToken);

		pDNSQName += iTokLen + 1;
		iNameLen += iTokLen + 1;

		pszToken = SysStrTok(NULL, ".", &pszSavePtr);
	}
	*pDNSQName = 0;
	SysFree(pszNameCopy);

	return iNameLen + 1;
}

static SYS_UINT16 DNS_GetUniqueQueryId(void)
{
	static SYS_UINT16 uDnsQueryId = 0;

	return (SYS_UINT16) (++uDnsQueryId * SysGetCurrentThreadId());
}

static int DNS_RequestSetup(DNSQuery **ppDNSQ, unsigned int uOpCode,
			    unsigned int uQType, char const *pszInetName,
			    int *piQLength, int iAskQR)
{
	int iNameLen;
	DNSQuery *pDNSQ;
	SYS_UINT8 *pQueryData;

	if ((pDNSQ = (DNSQuery *) SysAlloc(sizeof(DNSQuery))) == NULL)
		return ErrGetErrorCode();

	/* Setup query header */
	pDNSQ->DNSH.Id = DNS_GetUniqueQueryId();
	pDNSQ->DNSH.QR = 0;
	pDNSQ->DNSH.RD = (iAskQR) ? 1: 0;
	pDNSQ->DNSH.OpCode = uOpCode;
	pDNSQ->DNSH.QDCount = htons(1);
	pDNSQ->DNSH.ANCount = htons(0);
	pDNSQ->DNSH.NSCount = htons(0);
	pDNSQ->DNSH.ARCount = htons(0);

	pQueryData = pDNSQ->QueryData;

	/* Copy name to query */
	if ((iNameLen = DNS_NameCopy(pQueryData, pszInetName)) < 0)
		return ErrGetErrorCode();
	pQueryData += iNameLen;

	/* Set query type */
	MscWriteUint16(pQueryData, (SYS_UINT16) htons(uQType));
	pQueryData += sizeof(SYS_UINT16);

	/* Set query class */
	MscWriteUint16(pQueryData, (SYS_UINT16) htons(QCLASS_IN));
	pQueryData += sizeof(SYS_UINT16);

	*ppDNSQ = pDNSQ;
	*piQLength = (int) (pQueryData - (SYS_UINT8 *) pDNSQ);

	return 0;
}

static SYS_UINT8 *DNS_QuerySendStream(char const *pszDNSServer, int iPortNo, int iTimeout,
				      DNSQuery const *pDNSQ, int iQLenght)
{
	/* Open DNS server socket */
	SYS_SOCKET SockFD;
	SYS_INET_ADDR SvrAddr;
	SYS_INET_ADDR SockAddr;

	if (MscCreateClientSocket(pszDNSServer, iPortNo, SOCK_STREAM, &SockFD, &SvrAddr,
				  &SockAddr, iTimeout) < 0)
		return NULL;

	SYS_UINT16 QLenght = (SYS_UINT16) htons(iQLenght);

	/* Send packet lenght */
	if (SysSend(SockFD, (char *) &QLenght, sizeof(QLenght),
		    iTimeout) != sizeof(QLenght)) {
		ErrorPush();
		SysCloseSocket(SockFD);
		ErrSetErrorCode(ErrorFetch());
		return NULL;
	}
	/* Send packet */
	if (SysSend(SockFD, (char const *) pDNSQ, iQLenght, iTimeout) != iQLenght) {
		ErrorPush();
		SysCloseSocket(SockFD);
		ErrSetErrorCode(ErrorFetch());
		return NULL;
	}
	/* Receive packet lenght */
	if (SysRecv(SockFD, (char *) &QLenght, sizeof(QLenght),
		    iTimeout) != sizeof(QLenght)) {
		ErrorPush();
		SysCloseSocket(SockFD);
		ErrSetErrorCode(ErrorFetch());
		return NULL;
	}

	int iPacketLenght = (int) ntohs(QLenght);
	SYS_UINT8 *pRespData = DNS_AllocRespData(iPacketLenght + 1);

	if (pRespData == NULL) {
		ErrorPush();
		SysCloseSocket(SockFD);
		ErrSetErrorCode(ErrorFetch());
		return NULL;
	}
	/* Receive packet */
	if (SysRecv(SockFD, (char *) pRespData, iPacketLenght,
		    iTimeout) != iPacketLenght) {
		ErrorPush();
		DNS_FreeRespData(pRespData);
		SysCloseSocket(SockFD);
		ErrSetErrorCode(ErrorFetch());
		return NULL;
	}
	SysCloseSocket(SockFD);

	DNS_HEADER *pDNSH = (DNS_HEADER *) pRespData;

	if (pDNSQ->DNSH.RD && !pDNSH->RA) {
		DNS_FreeRespData(pRespData);
		ErrSetErrorCode(ERR_DNS_RECURSION_NOT_AVAILABLE);
		return NULL;
	}

	return pRespData;
}

static SYS_UINT8 *DNS_QuerySendDGram(char const *pszDNSServer, int iPortNo, int iTimeout,
				     DNSQuery const *pDNSQ, int iQLenght, int *piTrunc)
{
	int i, iPktSize;
	DNS_HEADER *pDNSH;
	SYS_UINT8 *pRespData;
	SYS_SOCKET SockFD;
	SYS_INET_ADDR SvrAddr, SockAddr, RecvAddr;
	SYS_UINT8 RespBuffer[1024];

	*piTrunc = 0;
	if (MscCreateClientSocket(pszDNSServer, iPortNo, SOCK_DGRAM, &SockFD, &SvrAddr,
				  &SockAddr, iTimeout) < 0)
		return NULL;

	for (i = 0; i < DNS_SEND_RETRIES; i++) {
		if (SysSendData(SockFD, (char const *) pDNSQ, iQLenght,
				iTimeout) != iQLenght)
			continue;

		ZeroData(RecvAddr);
		if ((iPktSize = SysRecvDataFrom(SockFD, &RecvAddr,
						(char *) RespBuffer, sizeof(RespBuffer),
						iTimeout)) < (int) sizeof(DNS_HEADER))
			continue;

		pDNSH = (DNS_HEADER *) RespBuffer;
		if (pDNSH->Id != pDNSQ->DNSH.Id)
			continue;

		if (pDNSQ->DNSH.RD && !pDNSH->RA) {
			SysCloseSocket(SockFD);
			ErrSetErrorCode(ERR_DNS_RECURSION_NOT_AVAILABLE);
			return NULL;
		}
		if (pDNSH->TC) {
			(*piTrunc)++;

			SysCloseSocket(SockFD);
			ErrSetErrorCode(ERR_TRUNCATED_DGRAM_DNS_RESPONSE);
			return NULL;
		}
		if ((pRespData = DNS_AllocRespData(iPktSize + 1)) == NULL) {
			ErrorPush();
			SysCloseSocket(SockFD);
			ErrSetErrorCode(ErrorFetch());
			return NULL;
		}
		memcpy(pRespData, RespBuffer, iPktSize);
		SysCloseSocket(SockFD);

		return pRespData;
	}
	SysCloseSocket(SockFD);

	ErrSetErrorCode(ERR_NO_DGRAM_DNS_RESPONSE);
	return NULL;
}

static SYS_UINT8 *DNS_QueryExec(char const *pszDNSServer, int iPortNo, int iTimeout,
				unsigned int uOpCode, unsigned int uQType,
				char const *pszInetName, int iAskQR)
{
	int iTrunc = 0;
	int iQLenght;
	DNSQuery *pDNSQ;
	SYS_UINT8 *pRespData;

	if (DNS_RequestSetup(&pDNSQ, uOpCode, uQType, pszInetName, &iQLenght,
			     iAskQR) < 0)
		return NULL;

	pRespData = DNS_QuerySendDGram(pszDNSServer, iPortNo, iTimeout,
				       pDNSQ, iQLenght, &iTrunc);
	if (pRespData == NULL && iTrunc)
		pRespData = DNS_QuerySendStream(pszDNSServer, iPortNo, iTimeout,
						pDNSQ, iQLenght);
	SysFree(pDNSQ);

	return pRespData;
}

static int DNS_DecodeRecord(DNSResourceRecord const *pRR, DNSAnswer *pAns)
{
	SYS_UINT8 const *pRRData = pRR->pRespData;
	DNSRecord *pRec;

	if (pRR->Type == QTYPE_MX) {
		if ((pRec = DNS_AllocRec(pAns, pRR->Type, pRR)) == NULL)
			return ErrGetErrorCode();

		pRec->U.MX.Pref = ntohs(MscReadUint16(pRRData));
		pRRData += sizeof(SYS_UINT16);

		if (DNS_GetText(pRR->pBaseData, pRRData, pRec->szName,
				sizeof(pRec->szName)) < 0)
			return ErrGetErrorCode();
	} else if (pRR->Type == QTYPE_CNAME || pRR->Type == QTYPE_PTR ||
		   pRR->Type == QTYPE_NS) {
		if ((pRec = DNS_AllocRec(pAns, pRR->Type, pRR)) == NULL)
			return ErrGetErrorCode();
		if (DNS_GetText(pRR->pBaseData, pRRData, pRec->szName,
				sizeof(pRec->szName)) < 0)
			return ErrGetErrorCode();
	} else if (pRR->Type == QTYPE_A) {
		if ((pRec = DNS_AllocRec(pAns, pRR->Type, pRR)) == NULL)
			return ErrGetErrorCode();
		strcpy(pRec->szName, pRR->szName);
		pRec->U.A.IAddr4 = MscReadUint32(pRRData);
	} else if (pRR->Type == QTYPE_AAAA) {
		if ((pRec = DNS_AllocRec(pAns, pRR->Type, pRR)) == NULL)
			return ErrGetErrorCode();
		strcpy(pRec->szName, pRR->szName);
		memcpy(pRec->U.AAAA.IAddr6, pRRData, sizeof(pRec->U.AAAA.IAddr6));
	} else if (pRR->Type == QTYPE_SOA) {
		int iRRLen;

		if ((pRec = DNS_AllocRec(pAns, pRR->Type, pRR)) == NULL)
			return ErrGetErrorCode();

		if (DNS_GetText(pRR->pBaseData, pRRData, pRec->szName,
				sizeof(pRec->szName), &iRRLen) < 0)
			return ErrGetErrorCode();
		pRRData += iRRLen;

		if (DNS_GetText(pRR->pBaseData, pRRData, pRec->U.SOA.szAddr,
				sizeof(pRec->U.SOA.szAddr), &iRRLen) < 0)
			return ErrGetErrorCode();
		pRRData += iRRLen;

		pRec->U.SOA.Serial = ntohl(MscReadUint32(pRRData));
		pRRData += sizeof(SYS_UINT32);

		pRec->U.SOA.Refresh = ntohl(MscReadUint32(pRRData));
		pRRData += sizeof(SYS_UINT32);

		pRec->U.SOA.Retry = ntohl(MscReadUint32(pRRData));
		pRRData += sizeof(SYS_UINT32);

		pRec->U.SOA.Expire = ntohl(MscReadUint32(pRRData));
		pRRData += sizeof(SYS_UINT32);

		pRec->U.SOA.MinTTL = ntohl(MscReadUint32(pRRData));
	} else {
		SysLogMessage(LOG_LEV_MESSAGE, "Unknown DNS record type %d\n",
			      (int) pRR->Type);

	}

	return 0;
}

static int DNS_MapRCodeError(unsigned int uRCode)
{
	switch (uRCode) {
	case RCODE_NXDOMAIN:
		return ERR_DNS_NXDOMAIN;
	case RCODE_FORMAT:
		return ERR_DNS_FORMAT;
	case RCODE_SVRFAIL:
		return ERR_DNS_SVRFAIL;
	case RCODE_NOTSUPPORTED:
		return ERR_DNS_NOTSUPPORTED;
	case RCODE_REFUSED:
		return ERR_DNS_REFUSED;
	}
	return ERR_BAD_DNS_RESPONSE;
}

static int DNS_DecodeResponse(SYS_UINT8 *pRespData, DNSAnswer *pAns)
{
	SYS_UINT8 *pBaseData = pRespData;
	DNSQuery *pDNSQ = (DNSQuery *) pRespData;
	int i, iRRLenght, iQLenght;
	SYS_UINT16 Type, Class;
	DNSResourceRecord RR;
	char szInetName[MAX_HOST_NAME];

	if (pDNSQ->DNSH.RCode != 0) {
		int iError = DNS_MapRCodeError(pDNSQ->DNSH.RCode);

		ErrSetErrorCode(iError);
		return iError;
	}

	pAns->iQDCount = ntohs(pDNSQ->DNSH.QDCount);
	pAns->iANCount = ntohs(pDNSQ->DNSH.ANCount);
	pAns->iNSCount = ntohs(pDNSQ->DNSH.NSCount);
	pAns->iARCount = ntohs(pDNSQ->DNSH.ARCount);

	pRespData = pDNSQ->QueryData;

	/* Scan query data */
	for (i = 0; i < pAns->iQDCount; i++) {
		if (DNS_GetQuery(pBaseData, pRespData, szInetName, sizeof(szInetName),
				 &Type, &Class, &iQLenght) < 0)
			return ErrGetErrorCode();
		pRespData += iQLenght;
	}

	/* Scan answer data */
	for (i = 0; i < pAns->iANCount; i++) {
		if (DNS_GetResourceRecord(pBaseData, pRespData, &RR, &iRRLenght) < 0)
			return ErrGetErrorCode();
		pRespData += iRRLenght;
		DNS_DecodeRecord(&RR, pAns);
	}

	/* Scan name servers data */
	for (i = 0; i < pAns->iNSCount; i++) {
		if (DNS_GetResourceRecord(pBaseData, pRespData, &RR, &iRRLenght) < 0)
			return ErrGetErrorCode();

		pRespData += iRRLenght;
		DNS_DecodeRecord(&RR, pAns);
	}

	/* Scan additional records data */
	for (i = 0; i < pAns->iARCount; i++) {
		if (DNS_GetResourceRecord(pBaseData, pRespData, &RR, &iRRLenght) < 0)
			return ErrGetErrorCode();

		pRespData += iRRLenght;
		DNS_DecodeRecord(&RR, pAns);
	}

	return 0;
}

int DNS_FatalError(int iError)
{
	switch (iError) {
	case ERR_DNS_NXDOMAIN:
	case ERR_DNS_MAXDEPTH:
		return 1;
	}

	return 0;
}

static int DNS_RecurseQuery(SysListHead *pNsHead, char const *pszName,
			    unsigned int uQType, DNSAnswer *pAns, int iDepth,
			    int iMaxDepth)
{
	int iError;
	SYS_UINT8 *pRespData;
	SysListHead *pLnk;
	SysListHead LstNS;

	if (iDepth > iMaxDepth) {
		SysLogMessage(LOG_LEV_MESSAGE, "Maximum DNS query depth %d exceeded ('%s')\n",
			      iMaxDepth, pszName);

		ErrSetErrorCode(ERR_DNS_MAXDEPTH, pszName);
		return ERR_DNS_MAXDEPTH;
	}
	iError = ERR_DNS_NOTFOUND;
	for (pLnk = SYS_LIST_FIRST(pNsHead); pLnk != NULL;
	     pLnk = SYS_LIST_NEXT(pLnk, pNsHead)) {
		DNSRecord *pRec = SYS_LIST_ENTRY(pLnk, DNSRecord, Lnk);

		if (DNS_InitAnswer(pAns) < 0)
			return ErrGetErrorCode();
		if ((pRespData = DNS_QueryExec(pRec->szName, DNS_PORTNO,
					       DNS_SOCKET_TIMEOUT, 0, uQType,
					       pszName)) == NULL) {
			iError = ErrGetErrorCode();
			DNS_FreeAnswer(pAns);
			continue;
		}
		iError = DNS_DecodeResponse(pRespData, pAns);
		DNS_FreeRespData(pRespData);
		if (DNS_FatalError(iError)) {
			DNS_FreeAnswer(pAns);
			return iError;
		}
		if (iError != 0) {
			DNS_FreeAnswer(pAns);
			continue;
		}
		if (pAns->iANCount > 0)
			return 0;

		/*
		 * We've got no answers, but we may have had authority (NS) records.
		 * Steal the NS list from the DNSAnswer structure, so that we can
		 * free the strcture itself, and cycle through the stolen NS list.
		 * If the NS list is empty, it means we reached an authority SOA,
		 * that gave us no answers. That'd be the time to return NOTFOUND.
		 */
		SYS_INIT_LIST_HEAD(&LstNS);
		SYS_LIST_SPLICE(&pAns->RecsLst[QTYPE_NS], &LstNS);
		DNS_FreeAnswer(pAns);
		if (SYS_LIST_EMTPY(&LstNS)) {
			iError = ERR_DNS_NOTFOUND;
			break;
		}

		iError = DNS_RecurseQuery(&LstNS, pszName, uQType, pAns,
					  iDepth + 1, iMaxDepth);

		DNS_FreeRecList(&LstNS);
		if (iError == 0 || DNS_FatalError(iError) ||
		    iError == ERR_DNS_NOTFOUND)
			return iError;
	}

	ErrSetErrorCode(iError);
	return iError;
}

static char *DNS_GetRootsFile(char *pszRootsFilePath, int iMaxPath)
{
	CfgGetRootPath(pszRootsFilePath, iMaxPath);
	StrNCat(pszRootsFilePath, ROOTS_FILE, iMaxPath);

	return pszRootsFilePath;
}

static int DNS_LoadRoots(SysListHead *pHead)
{
	FILE *pFile;
	DNSRecord *pRec;
	char szRootsFile[SYS_MAX_PATH];
	char szHost[MAX_HOST_NAME];

	DNS_GetRootsFile(szRootsFile, sizeof(szRootsFile));
	if ((pFile = fopen(szRootsFile, "rt")) == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, szRootsFile);
		return ERR_FILE_OPEN;
	}
	SYS_INIT_LIST_HEAD(pHead);
	while (MscFGets(szHost, sizeof(szHost) - 1, pFile) != NULL) {
		if ((pRec = (struct DNSRecord *)
		     SysAlloc(sizeof(DNSRecord))) == NULL) {
			DNS_FreeRecList(pHead);
			fclose(pFile);
			return ErrGetErrorCode();
		}
		SYS_LIST_ADDT(&pRec->Lnk, pHead);
		strcpy(pRec->szName, szHost);
	}
	fclose(pFile);

	return 0;
}

int DNS_Query(char const *pszName, unsigned int uQType, DNSAnswer *pAns,
	      int iMaxDepth)
{
	int iError;
	SysListHead LstNS;

	if (DNS_LoadRoots(&LstNS) < 0)
		return ErrGetErrorCode();

	iError = DNS_RecurseQuery(&LstNS, pszName, uQType, pAns, 0, iMaxDepth);

	DNS_FreeRecList(&LstNS);

	return iError;
}

int DNS_QueryDirect(char const *pszDNSServer, char const *pszName,
		    unsigned int uQType, int iQuerySockType, DNSAnswer *pAns)
{
	int iQLenght, iError, iTrunc = 0;
	DNSQuery *pDNSQ;
	SYS_UINT8 *pRespData;

	/* Setup DNS MX query with recursion requested */
	if (DNS_RequestSetup(&pDNSQ, 0, uQType, pszName, &iQLenght, 1) < 0)
		return ErrGetErrorCode();

	switch (iQuerySockType) {
	case DNS_QUERY_TCP:
		pRespData = DNS_QuerySendStream(pszDNSServer, DNS_PORTNO,
						DNS_SOCKET_TIMEOUT,
						pDNSQ, iQLenght);
		break;

	case DNS_QUERY_UDP:
	default:
		/* Try needed UDP query first, if it's truncated switch to TCP query */
		if ((pRespData = DNS_QuerySendDGram(pszDNSServer, DNS_PORTNO,
						    DNS_SOCKET_TIMEOUT,
						    pDNSQ, iQLenght,
						    &iTrunc)) == NULL && iTrunc)
			pRespData = DNS_QuerySendStream(pszDNSServer, DNS_PORTNO,
							DNS_SOCKET_TIMEOUT, pDNSQ,
							iQLenght);
	}
	SysFree(pDNSQ);
	if (pRespData == NULL)
		return ErrGetErrorCode();
	if (DNS_InitAnswer(pAns) < 0) {
		DNS_FreeRespData(pRespData);
		return ErrGetErrorCode();
	}

	iError = DNS_DecodeResponse(pRespData, pAns);

	DNS_FreeRespData(pRespData);

	return iError;
}

