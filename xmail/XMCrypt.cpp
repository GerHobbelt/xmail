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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *StrCrypt(char const *pszString, char *pszCrypt)
{
	strcpy(pszCrypt, "");

	for (int ii = 0; pszString[ii] != '\0'; ii++) {
		unsigned int uChar = (unsigned int) pszString[ii];
		char szByte[32] = "";

		sprintf(szByte, "%02x", (uChar ^ 101) & 0xff);

		strcat(pszCrypt, szByte);
	}

	return pszCrypt;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("usage : %s  password\n", argv[0]);
		return 1;
	}

	char szCrypt[1024] = "";

	StrCrypt(argv[1], szCrypt);

	printf("%s\n", szCrypt);

	return 0;
}
