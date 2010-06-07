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
#include "AppDefines.h"

#define SYS_ADDR_FAMILY(a)	((struct sockaddr *) (a)->Addr)->sa_family
#define SYS_IN4(a)		((struct sockaddr_in *) (a)->Addr)
#define SYS_IN6(a)		((struct sockaddr_in6 *) (a)->Addr)


static int SysMapGetAddrInfoError(int iError);


int SysInetAnySetup(SYS_INET_ADDR &AddrInfo, int iFamily, int iPortNo)
{
	switch (iFamily) {
	case AF_INET:
		ZeroData(AddrInfo);
		SYS_IN4(&AddrInfo)->sin_family = iFamily;
		SYS_IN4(&AddrInfo)->sin_port = htons(iPortNo);
		SYS_IN4(&AddrInfo)->sin_addr.s_addr = INADDR_ANY;
		AddrInfo.iSize = sizeof(struct sockaddr_in);
		break;

	case AF_INET6:
		ZeroData(AddrInfo);
		SYS_IN6(&AddrInfo)->sin6_family = iFamily;
		SYS_IN6(&AddrInfo)->sin6_port = htons(iPortNo);
		SYS_IN6(&AddrInfo)->sin6_addr = in6addr_any;
		AddrInfo.iSize = sizeof(struct sockaddr_in6);
		break;

	default:
		ErrSetErrorCode(ERR_INVALID_INET_ADDR);
		return ERR_INVALID_INET_ADDR;
	}

	return 0;
}

int SysGetAddrFamily(SYS_INET_ADDR const &AddrInfo)
{
	return SYS_ADDR_FAMILY(&AddrInfo);
}

int SysGetAddrPort(SYS_INET_ADDR const &AddrInfo)
{
	switch (SysGetAddrFamily(AddrInfo)) {
	case AF_INET:
		return ntohs(SYS_IN4(&AddrInfo)->sin_port);

	case AF_INET6:
		return ntohs(SYS_IN6(&AddrInfo)->sin6_port);
	}

	ErrSetErrorCode(ERR_INVALID_INET_ADDR);
	return ERR_INVALID_INET_ADDR;
}

int SysSetAddrPort(SYS_INET_ADDR &AddrInfo, int iPortNo)
{
	switch (SysGetAddrFamily(AddrInfo)) {
	case AF_INET:
		SYS_IN4(&AddrInfo)->sin_port = htons((short) iPortNo);
		break;

	case AF_INET6:
		SYS_IN6(&AddrInfo)->sin6_port = htons((short) iPortNo);
		break;

	default:
		ErrSetErrorCode(ERR_INVALID_INET_ADDR);
		return ERR_INVALID_INET_ADDR;
	}

	return 0;
}

static int SysMapGetAddrInfoError(int iError)
{
	switch (iError) {
	case EAI_NONAME:
		return ERR_BAD_SERVER_ADDR;

#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
	case EAI_NODATA:
		return ERR_DNS_NOTFOUND;
#endif

	case EAI_MEMORY:
		return ERR_MEMORY;

	case EAI_BADFLAGS:
	case EAI_FAMILY:
		return ERR_INVALID_PARAMETER;
	}

	return ERR_NETWORK;
}

int SysGetHostByName(const char *pszName, int iFamily, SYS_INET_ADDR &AddrInfo)
{
	int iError;
	struct addrinfo *pCRes, *pRes, *pRes4 = NULL, *pRes6 = NULL;
	struct addrinfo AHints;

	ZeroData(AHints);
	AHints.ai_family = AF_UNSPEC;
	if ((iError = getaddrinfo(pszName, NULL, &AHints, &pRes)) != 0) {
		iError = SysMapGetAddrInfoError(iError);
		ErrSetErrorCode(iError, pszName);
		return iError;
	}
	for (pCRes = pRes, iError = ERR_BAD_SERVER_ADDR; pCRes != NULL;
	     pCRes = pCRes->ai_next) {
		if (pCRes->ai_addr->sa_family == AF_INET)
			pRes4 = pCRes;
		else if (pCRes->ai_addr->sa_family == AF_INET6)
			pRes6 = pCRes;
	}
	switch (iFamily) {
	case AF_INET:
		pCRes = pRes4;
		break;
	case AF_INET6:
		pCRes = pRes6;
		break;
	case SYS_INET46:
		if ((pCRes = pRes4) == NULL)
			pCRes = pRes6;
		break;
	case SYS_INET64:
		if ((pCRes = pRes6) == NULL)
			pCRes = pRes4;
		break;
	default:
		pCRes = NULL;
	}
	if (pCRes != NULL && sizeof(AddrInfo.Addr) >= pCRes->ai_addrlen) {
		ZeroData(AddrInfo);
		AddrInfo.iSize = pCRes->ai_addrlen;
		memcpy(AddrInfo.Addr, pCRes->ai_addr, pCRes->ai_addrlen);
	}
	freeaddrinfo(pRes);
	if (pCRes == NULL) {
		ErrSetErrorCode(ERR_BAD_SERVER_ADDR, pszName);
		return ERR_BAD_SERVER_ADDR;
	}

	return 0;
}

int SysGetHostByAddr(SYS_INET_ADDR const &AddrInfo, char *pszFQDN, int iSize)
{
	int iError;

	if ((iError = getnameinfo((struct sockaddr const *) AddrInfo.Addr, AddrInfo.iSize,
				  pszFQDN, iSize, NULL, 0, NI_NAMEREQD)) != 0) {
		char szIP[128];

		ErrSetErrorCode(ERR_GET_SOCK_HOST, SysInetNToA(AddrInfo, szIP, sizeof(szIP)));
		return ERR_GET_SOCK_HOST;
	}

	return 0;
}

int SysGetPeerInfo(SYS_SOCKET SockFD, SYS_INET_ADDR &AddrInfo)
{
	socklen_t InfoSize = sizeof(AddrInfo.Addr);

	ZeroData(AddrInfo);
	if (getpeername(SockFD, (struct sockaddr *) AddrInfo.Addr, &InfoSize) == -1) {
		ErrSetErrorCode(ERR_GET_PEER_INFO);
		return ERR_GET_PEER_INFO;
	}
	AddrInfo.iSize = (int) InfoSize;

	return 0;
}

int SysGetSockInfo(SYS_SOCKET SockFD, SYS_INET_ADDR &AddrInfo)
{
	socklen_t InfoSize = sizeof(AddrInfo.Addr);

	ZeroData(AddrInfo);
	if (getsockname(SockFD, (struct sockaddr *) AddrInfo.Addr, &InfoSize) == -1) {
		ErrSetErrorCode(ERR_GET_SOCK_INFO);
		return ERR_GET_SOCK_INFO;
	}
	AddrInfo.iSize = (int) InfoSize;

	return 0;
}

char *SysInetNToA(SYS_INET_ADDR const &AddrInfo, char *pszIP, int iSize)
{

	SetEmptyString(pszIP);
	if (getnameinfo((struct sockaddr const *) AddrInfo.Addr, AddrInfo.iSize,
			pszIP, iSize, NULL, 0, NI_NUMERICHOST) != 0)
		ErrSetErrorCode(ERR_INVALID_INET_ADDR);

	return pszIP;
}

char *SysInetRevNToA(SYS_INET_ADDR const &AddrInfo, char *pszRevIP, int iSize)
{
	int i;
	char *pszCur;
	SYS_UINT8 const *pAddr;

	SetEmptyString(pszRevIP);
	switch (SysGetAddrFamily(AddrInfo)) {
	case AF_INET:
		pAddr = (SYS_UINT8 const *) &SYS_IN4(&AddrInfo)->sin_addr;
		SysSNPrintf(pszRevIP, iSize, "%u.%u.%u.%u.",
			    pAddr[3], pAddr[2], pAddr[1], pAddr[0]);
		break;

	case AF_INET6:
		pAddr = (SYS_UINT8 const *) &SYS_IN6(&AddrInfo)->sin6_addr;
		for (i = 15, pszCur = pszRevIP; i >= 0 && iSize > 4;
		     i--, pszCur += 4, iSize -= 4)
			SysSNPrintf(pszCur, iSize, "%x.%x.", pAddr[i] & 0xf,
				    pAddr[i] >> 4);
		break;

	default:
		ErrSetErrorCode(ERR_INVALID_INET_ADDR);
	}

	return pszRevIP;
}

void const *SysInetAddrData(SYS_INET_ADDR const &AddrInfo, int *piSize)
{
	switch (SysGetAddrFamily(AddrInfo)) {
	case AF_INET:
		*piSize = (int) sizeof(SYS_IN4(&AddrInfo)->sin_addr);
		return &SYS_IN4(&AddrInfo)->sin_addr;

	case AF_INET6:
		*piSize = (int) sizeof(SYS_IN6(&AddrInfo)->sin6_addr);
		return &SYS_IN6(&AddrInfo)->sin6_addr;
	}

	ErrSetErrorCode(ERR_INVALID_INET_ADDR);
	return NULL;
}

int SysInetIPV6CompatIPV4(SYS_INET_ADDR const &Addr)
{
	int i, iASize;
	SYS_UINT8 const *pAData;

	if (SysGetAddrFamily(Addr) != AF_INET6 ||
	    (pAData = (SYS_UINT8 const *) SysInetAddrData(Addr, &iASize)) == NULL)
		return 0;
	/*
	 * First 80 bit must be zero ...
	 */
	for (i = 0; i < 10; i++)
		if (pAData[i])
			return 0;
	/*
	 * Then two 0xff must follow, or two 0x00
	 */
	return (pAData[i] == 0xff && pAData[i + 1] == 0xff) ||
		(pAData[i] == 0 && pAData[i + 1] == 0);
}

int SysInetIPV6ToIPV4(SYS_INET_ADDR const &SAddr, SYS_INET_ADDR &DAddr)
{
	if (!SysInetIPV6CompatIPV4(SAddr)) {
		ErrSetErrorCode(ERR_INVALID_INET_ADDR);
		return ERR_INVALID_INET_ADDR;
	}
	ZeroData(DAddr);
	SYS_IN4(&DAddr)->sin_family = AF_INET;
	SYS_IN4(&DAddr)->sin_port = SYS_IN6(&SAddr)->sin6_port;
	memcpy(&SYS_IN4(&DAddr)->sin_addr.s_addr,
	       (const char *) &SYS_IN6(&SAddr)->sin6_addr + 12, 4);
	DAddr.iSize = sizeof(struct sockaddr_in);

	return 0;
}

int SysInetAddrMatch(SYS_INET_ADDR const &Addr, SYS_UINT8 const *pMask, int iMaskSize,
		     SYS_INET_ADDR const &TestAddr)
{
	int i, iASize, iTASize, iAFamily, iTAFamily;
	SYS_UINT8 const *pAData, *pTAData;

	if ((pAData = (SYS_UINT8 const *) SysInetAddrData(Addr, &iASize)) == NULL ||
	    (pTAData = (SYS_UINT8 const *) SysInetAddrData(TestAddr, &iTASize)) == NULL ||
	    iMaskSize < iASize || iMaskSize < iTASize)
		return 0;
	iAFamily = SysGetAddrFamily(Addr);
	iTAFamily = SysGetAddrFamily(TestAddr);
	if (iAFamily != iTAFamily) {
		/*
		 * Need some marshaling here, since families are different.
		 * We only support IPV4 and IPV6, so it does not look that bad.
		 */
		if (iAFamily == AF_INET) {
			if (iTAFamily == AF_INET6) {
				if (!SysInetIPV6CompatIPV4(TestAddr))
					return 0;
				pTAData += iTASize - iASize;
			} else
				return 0;
		} else if (iAFamily == AF_INET6) {
			if (iTAFamily == AF_INET) {
				if (!SysInetIPV6CompatIPV4(Addr))
					return 0;
				pAData += iASize - iTASize;
				pMask += iASize - iTASize;
				iASize = iTASize;
			} else
				return 0;
		} else
			return 0;
	}
	for (i = 0; i < iASize; i++)
		if ((pAData[i] & pMask[i]) != (pTAData[i] & pMask[i]))
			return 0;

	return 1;
}

int SysInetAddrMatch(SYS_INET_ADDR const &Addr, SYS_INET_ADDR const &TestAddr)
{
	SYS_UINT8 Mask[sizeof(SYS_INET_ADDR)];

	memset(Mask, 0xff, sizeof(Mask));
	return SysInetAddrMatch(Addr, Mask, sizeof(Mask), TestAddr);
}

