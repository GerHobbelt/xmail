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
#include "ShBlocks.h"
#include "StrUtils.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "Base64Enc.h"

#define OK                  0
#define FAIL                -1
#define BUFOVER             -2

#define CHAR64(c)           (((c) < 0 || (c) > 127) ? -1: index_64[(c)])

static char basis_64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????";

static char index_64[128] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1
};

int Base64Encode(const char *pIn, int iInSize, char *pszOut, int *piOutSize)
{
	const unsigned char *in = (const unsigned char *) pIn;
	unsigned char *out = (unsigned char *) pszOut;
	unsigned char oval;
	char *blah;
	int olen, omax = *piOutSize;

	olen = (iInSize + 2) / 3 * 4;
	*piOutSize = olen;
	if (omax < olen)
		return BUFOVER;

	blah = (char *) out;
	while (iInSize >= 3) {
		/* user provided max buffer size; make sure we don't go over it */
		*out++ = basis_64[in[0] >> 2];
		*out++ = basis_64[((in[0] << 4) & 0x30) | (in[1] >> 4)];
		*out++ = basis_64[((in[1] << 2) & 0x3c) | (in[2] >> 6)];
		*out++ = basis_64[in[2] & 0x3f];
		in += 3;
		iInSize -= 3;
	}
	if (iInSize > 0) {
		/* user provided max buffer size; make sure we don't go over it */
		*out++ = basis_64[in[0] >> 2];
		oval = (in[0] << 4) & 0x30;
		if (iInSize > 1)
			oval |= in[1] >> 4;
		*out++ = basis_64[oval];
		*out++ = (iInSize < 2) ? '=' : basis_64[(in[1] << 2) & 0x3c];
		*out++ = '=';
	}

	if (olen < omax)
		*out = '\0';

	return OK;
}

int Base64Decode(const char *pszIn, int iInSize, char *pOut, int *piOutSize)
{
	int i, c1, c2, c3, c4, omax = *piOutSize - 1, len = 0;

	if (iInSize >= 2 && pszIn[0] == '+' && pszIn[1] == ' ')
		pszIn += 2, iInSize -= 2;

	if (*pszIn == '\0')
		return FAIL;

	for (i = 0; i < iInSize / 4; i++) {
		c1 = pszIn[0];
		if (CHAR64(c1) == -1)
			return FAIL;
		c2 = pszIn[1];
		if (CHAR64(c2) == -1)
			return FAIL;
		c3 = pszIn[2];
		if (c3 != '=' && CHAR64(c3) == -1)
			return FAIL;
		c4 = pszIn[3];
		if (c4 != '=' && CHAR64(c4) == -1)
			return FAIL;
		pszIn += 4;
		if (len >= omax)
			return FAIL;
		*pOut++ = (CHAR64(c1) << 2) | (CHAR64(c2) >> 4);
		++len;
		if (c3 != '=') {
			if (len >= omax)
				return FAIL;
			*pOut++ = ((CHAR64(c2) << 4) & 0xf0) | (CHAR64(c3) >> 2);
			++len;
			if (c4 != '=') {
				if (len >= omax)
					return FAIL;
				*pOut++ = ((CHAR64(c3) << 6) & 0xc0) | CHAR64(c4);
				++len;
			}
		}
	}
	*pOut = 0;
	*piOutSize = len;

	return OK;
}

