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

#ifndef _TABINDEX_H
#define _TABINDEX_H

#define INDEX_SEQUENCE_TERMINATOR    (-1)

#define INVALID_INDEX_HANDLE         ((INDEX_HANDLE) 0)

/*
 * Support only files up to 4GB for now. Should be plenty!
 */
typedef SYS_UINT32 TabIdxUINT;

typedef struct INDEX_HANDLE_struct {
} *INDEX_HANDLE;

char *TbixGetIndexFile(char const *pszTabFilePath, int const *piFieldsIdx, char *pszIdxFile);
int TbixCreateIndex(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens,
		    int (*pHashFunc) (char const *const *, int const *, unsigned long *,
				      bool) = NULL);
int TbixCalculateHash(char const *const *ppszToks, int const *piFieldsIdx,
		      unsigned long *pulHashVal, bool bCaseSens);
char **TbixLookup(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens, ...);
int TbixCheckIndex(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens,
		   int (*pHashFunc) (char const *const *, int const *, unsigned long *,
				     bool) = NULL);
INDEX_HANDLE TbixOpenHandle(char const *pszTabFilePath, int const *piFieldsIdx,
			    unsigned long const *pulHashVal, int iNumVals);
int TbixCloseHandle(INDEX_HANDLE hIndexLookup);
long TbixLookedUpRecords(INDEX_HANDLE hIndexLookup);
char **TbixFirstRecord(INDEX_HANDLE hIndexLookup);
char **TbixNextRecord(INDEX_HANDLE hIndexLookup);

#endif

