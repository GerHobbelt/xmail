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

#ifndef _SYSMACROS_H
#define _SYSMACROS_H

#define SRand()                 srand((unsigned int) (SysMsTime() * SysGetCurrentThreadId()))
#define MaxSignedType(t)        (((t) -1) & ~(((t) 1) << (CHAR_BIT * sizeof(t) - 1)))
#define NbrCeil(n, a)           ((((n) + (a) - 1) / (a)) * (a))
#define NbrFloor(n, a)          (((n) / (a)) * (a))
#define Sign(v)                 (((v) < 0) ? -1: +1)
#define Min(a, b)               (((a) < (b)) ? (a): (b))
#define Max(a, b)               (((a) > (b)) ? (a): (b))
#define Abs(v)                  (((v) > 0) ? (v): -(v))
#define INext(i, n)             ((((i) + 1) < (n)) ? ((i) + 1): 0)
#define IPrev(i, n)             (((i) > 0) ? ((i) - 1): ((n) - 1))
#define LIndex2D(i, j, n)       ((i) * (n) + (j))
#define ZeroData(d)             memset(&(d), 0, sizeof(d))
#define CountOf(t)              (sizeof(t) / sizeof((t)[0]))
#define SetEmptyString(s)       (s)[0] = '\0'
#define IsEmptyString(s)        (*(s) == '\0')
#define CStringSize(s)          (sizeof(s) - 1)
#define StrCmdMatch(s, c)       StrNCmdMatch(s, c, CStringSize(c))
#define StrSkipSpaces(p)        for (; (*(p) == ' ') || (*(p) == '\t'); (p)++)
#define CharISame(a, b)         (tolower(a) == tolower(b))
#define StrINComp(s, t)         strnicmp(s, t, strlen(t))
#define StrNComp(s, t)          strncmp(s, t, strlen(t))
#define StrSINComp(s, t)        strnicmp(s, t, CStringSize(t))
#define StrSNComp(s, t)         strncmp(s, t, CStringSize(t))
#define StrNCpy(t, s, n)        do { strncpy(t, s, n); (t)[(n) - 1] = '\0'; } while (0)
#define StrSNCpy(t, s)          StrNCpy(t, s, sizeof(t))
#define StrSNCat(t, s)          StrNCat(t, s, sizeof(t))
#define Cpy2Sz(d, s, n)         do { memcpy(d, s, (n) * sizeof(*(s))); (d)[n] = 0; } while (0)
#define StrEnd(s)               ((char *) (s) + strlen(s))
#define CheckRemoveFile(fp)     ((SysExistFile(fp)) ? SysRemove(fp) : 0)
#define SysFreeNullify(p)       do { SysFree(p), (p) = NULL; } while(0)
#define FCloseNullify(f)        do { if ((f) != NULL) fclose(f); (f) = NULL; } while (0)
#define ErrorSet(e)             (ErrSetErrorCode(e), e)
#define ErrorPush()             int __iPushedError = ErrGetErrorCode()
#define ErrorPop()              ErrorSet(__iPushedError)
#define ErrorFetch()            __iPushedError
#define DatumStrSet(d, s)       do { (d)->pData = (char *) (s); (d)->lSize = strlen(s); } while (0)
#define IsDotFilename(f)        ((f)[0] == '.')
#define IsEmailAddress(a)       (strchr((a), '@') != NULL)
#define MemMatch(s, n, m, l)    ((n) == (l) && memcmp(s, m, n) == 0)
#define EquivDatum(a, b)        ((a)->lSize == (b)->lSize && memcmp((a)->pData, (b)->pData, (a)->lSize) == 0)
#define ArrayInit(a, v)         do {			\
		unsigned int __i;			\
		for (__i = 0; __i < CountOf(a); __i++)	\
			(a)[__i] = (v);			\
	} while (0)
#define StrVSprint(r, l, f) do {					\
		int iPSize, iCurrSize = 256;				\
		va_list Args;						\
		for (;;) {						\
			if ((r = (char *) SysAlloc(iCurrSize)) == NULL)	\
				break;					\
			va_start(Args, l);				\
			if ((iPSize = SysVSNPrintf(r, iCurrSize - 1, f, Args)) >= 0 && \
			    iPSize < iCurrSize) {			\
				va_end(Args);				\
				break;					\
			}						\
			va_end(Args);					\
			if (iPSize > 0)					\
				iCurrSize = (4 * iPSize) / 3 + 2;	\
			else						\
				iCurrSize *= 2;				\
			SysFree(r);					\
		}							\
	} while (0)



inline char *StrNCat(char *pszDest, char const *pszSrc, int iMaxSize)
{
	int iDestLength = strlen(pszDest);

	if (iDestLength < iMaxSize)
		StrNCpy(pszDest + iDestLength, pszSrc, iMaxSize - iDestLength);

	return pszDest;
}

inline int StrNCmdMatch(char const *pszCmdLine, char const *pszCmd, int iCmdLength)
{
	return strnicmp(pszCmdLine, pszCmd, iCmdLength) == 0 &&
		(pszCmdLine[iCmdLength] == '\0' || strchr(" \r\n\t", pszCmdLine[iCmdLength]) != NULL);
}

inline char *AppendChar(char *pszString, int iChar)
{
	int iStrLength = strlen(pszString);

	if (iStrLength == 0 || pszString[iStrLength - 1] != iChar) {
		pszString[iStrLength] = iChar;
		pszString[iStrLength + 1] = '\0';
	}

	return pszString;
}

inline char *AppendSlash(char *pszPath)
{
	return AppendChar(pszPath, SYS_SLASH_CHAR);
}

inline char *DelFinalChar(char *pszString, int iChar)
{
	int iStrLength = strlen(pszString);

	if (iStrLength > 0 && pszString[iStrLength - 1] == iChar)
		pszString[iStrLength - 1] = '\0';

	return pszString;
}

inline char *DelFinalSlash(char *pszPath)
{
	return DelFinalChar(pszPath, SYS_SLASH_CHAR);
}

inline int ToUpper(int iChar)
{
	return (iChar >= 'a' && iChar <= 'z') ? 'A' + (iChar - 'a'): iChar;
}

inline int ToLower(int iChar)
{
	return (iChar >= 'A' && iChar <= 'Z') ? 'a' + (iChar - 'A'): iChar;
}

inline int IsPrimeNumber(long lNumber)
{
	if (lNumber > 3) {
		if (lNumber & 1) {
			long i, lHalfNumber;

			for (i = 3, lHalfNumber = lNumber / 2; i < lHalfNumber;
			     i += 2)
				if ((lNumber % i) == 0)
					return 0;
		} else
			return 0;
	}

	return 1;
}

inline char *ClearEOL(char *pszBuffer)
{
	int iSize = strlen(pszBuffer);

	for (; iSize > 0 && (pszBuffer[iSize - 1] == '\r' || pszBuffer[iSize - 1] == '\n');
	     iSize--);
	pszBuffer[iSize] = '\0';

	return pszBuffer;
}

#endif

