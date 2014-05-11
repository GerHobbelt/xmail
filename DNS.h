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

#ifndef _DNS_H
#define _DNS_H

#define DNS_QUERY_TCP           1
#define DNS_QUERY_UDP           2

#define DNS_STD_MAXDEPTH        32

#define QTYPE_A                 1
#define QTYPE_NS                2
#define QTYPE_MD                3
#define QTYPE_MF                4
#define QTYPE_CNAME             5
#define QTYPE_SOA               6
#define QTYPE_MB                7
#define QTYPE_MG                8
#define QTYPE_MR                9
#define QTYPE_NULL              10
#define QTYPE_WKS               11
#define QTYPE_PTR               12
#define QTYPE_HINFO             13
#define QTYPE_MINFO             14
#define QTYPE_MX                15
#define QTYPE_TXT               16

#define QTYPE_AAAA              28
#define QTYPE_ANSWER_MAX        29

#define QTYPE_AXFR              252
#define QTYPE_MAILB             253
#define QTYPE_MAILA             254
#define QTYPE_ALL               255

#define QCLASS_IN               1
#define QCLASS_CS               2
#define QCLASS_CH               3
#define QCLASS_HS               4

#define QCLASS_ALL              255

#define RCODE_FORMAT            1
#define RCODE_SVRFAIL           2
#define RCODE_NXDOMAIN          3
#define RCODE_NOTSUPPORTED      4
#define RCODE_REFUSED           5

struct DNS_HEADER {
    SYS_UINT16 Id;
#ifdef BIG_ENDIAN_BITFIELD
    SYS_UINT8 QR:1, OpCode:4, AA:1, TC:1, RD:1;
    SYS_UINT8 RA:1, Z:3, RCode:4;
#else
    SYS_UINT8 RD:1, TC:1, AA:1, OpCode:4, QR:1;
    SYS_UINT8 RCode:4, Z:3, RA:1;
#endif              // #ifdef BIG_ENDIAN_BITFIELD
    SYS_UINT16 QDCount;
    SYS_UINT16 ANCount;
    SYS_UINT16 NSCount;
    SYS_UINT16 ARCount;
};

struct DNSRecord {
    struct SysListHead Lnk;
    char szName[MAX_HOST_NAME];
    SYS_UINT32 TTL;
    SYS_UINT32 Class;
    union {
        struct {
            char szName[MAX_HOST_NAME];
        } NAME;
        struct {
            char szName[MAX_HOST_NAME];
            SYS_UINT16 Pref;
        } MX;
        struct {
            SYS_UINT32 IAddr4;
        } A;
        struct {
            SYS_UINT8 IAddr6[16];
        } AAAA;
        struct {
            char szName[MAX_HOST_NAME];
            char szAddr[MAX_ADDR_NAME];
            SYS_UINT32 Serial;
            SYS_UINT32 Refresh;
            SYS_UINT32 Retry;
            SYS_UINT32 Expire;
            SYS_UINT32 MinTTL;
        } SOA;
    } U;
};

struct DNSAnswer {
    int iQDCount;
    int iANCount;
    int iNSCount;
    int iARCount;
    int iAuth;
    struct SysListHead RecsLst[QTYPE_ANSWER_MAX];
};

void DNS_InitAnswer(DNSAnswer *pAns);
void DNS_FreeRecList(SysListHead *pHead);
void DNS_FreeAnswer(DNSAnswer *pAns);
int DNS_FatalError(int iError);
int DNS_Query(char const *pszName, unsigned int uQType, DNSAnswer *pAns,
          int iMaxDepth = DNS_STD_MAXDEPTH);
int DNS_QueryDirect(char const *pszDNSServer, char const *pszName,
            unsigned int uQType, int iQuerySockType, DNSAnswer *pAns);

#endif

