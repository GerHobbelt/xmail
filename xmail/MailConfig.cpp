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
#include "MessQueue.h"
#include "MailSvr.h"
#include "MailConfig.h"

char *CfgGetRootPath(char *pszPath, int iMaxPath)
{
	StrNCpy(pszPath, szMailPath, iMaxPath);

	return pszPath;
}

char *CfgGetBasedPath(const char *pszFullPath, char *pszBasePath, int iMaxPath)
{
	int iRootLength;
	char szRootPath[SYS_MAX_PATH] = "";

	CfgGetRootPath(szRootPath, sizeof(szRootPath));
	iRootLength = strlen(szRootPath);
	if (strncmp(pszFullPath, szRootPath, iRootLength) == 0)
		StrNCpy(pszBasePath, pszFullPath + iRootLength, iMaxPath);
	else
		StrNCpy(pszBasePath, pszFullPath, iMaxPath);

	return pszBasePath;
}

char *CfgGetFullPath(const char *pszRelativePath, char *pszFullPath, int iMaxPath)
{
	CfgGetRootPath(pszFullPath, iMaxPath);
	StrNCat(pszFullPath,
		(*pszRelativePath != SYS_SLASH_CHAR) ? pszRelativePath: pszRelativePath + 1,
		iMaxPath);

	return pszFullPath;
}
