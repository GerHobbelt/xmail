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
#include "Array.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "TabIndex.h"

/* The index version MUST be incremented at every file format change */
#define TAB_INDEX_CURR_VERSION      1

#define TAB_SAMPLE_LINES            32
#define TAB_MIN_HASH_SIZE           17
#define TAB_INDEX_DIR               "tabindex"
#define TAB_INDEX_MAGIC             (*(SYS_UINT32 *) "ABDL")
#define KEY_BUFFER_SIZE             1024
#define TAB_RECORD_BUFFER_SIZE      2048
#define TOKEN_SEP_STR               "\t"
#define TAB_INIT_RESSET_SIZE        128


struct HashLink {
	SysListHead LLink;
	TabIdxUINT uOffset;
};

struct HashNode {
	SysListHead NodeList;
	TabIdxUINT uCount;
};

struct HashFileHeader {
	SYS_UINT32 uMagic;
	SYS_UINT32 uVersion;
	TabIdxUINT uHashSize;
};

struct TabHashIndex {
	HashFileHeader HFH;
	FILE *pIndexFile;
};

struct IndexLookupData {
	ARRAY_HANDLE hArray;
	long lRecCount;
	FILE *pTabFile;
	TabIdxUINT *pHTblC;
	long lArIdx;
	long lHTblI;
};

static int TbixCalcHashSize(FILE *pTabFile, char *pszLineBuffer, int iBufferSize);
static int TbixFreeHash(HashNode *pHash, int iHashSize);
static int TbixBuildKey(char *pszKey, va_list Args, bool bCaseSens);
static int TbixBuildKey(char *pszKey, char const *const *ppszTabTokens,
			int const *piFieldsIdx, bool bCaseSens);
static TabIdxUINT TbixHashData(char const *pData, long lSize);
static int TbixOpenIndex(char const *pszIndexFile, TabHashIndex &THI);
static int TbixCloseIndex(TabHashIndex &THI);
static int TbixCheckIndex(char const *pszIndexFile);
static TabIdxUINT *TbixReadTable(TabHashIndex &THI, TabIdxUINT uHashVal);
static char **TbixLoadRecord(FILE *pTabFile, TabIdxUINT uOffset);

static int TbixCalcHashSize(FILE *pTabFile, char *pszLineBuffer, int iBufferSize)
{
	int iSampleLines = 0;
	unsigned long ulOrigOffset = (unsigned long) ftell(pTabFile);
	unsigned long ulCurrOffset = 0;
	unsigned long ulLineSize = 0;

	rewind(pTabFile);
	while (iSampleLines < TAB_SAMPLE_LINES) {
		if (MscGetString(pTabFile, pszLineBuffer, iBufferSize - 1) == NULL)
			break;

		unsigned long ulOffset = (unsigned long) ftell(pTabFile);

		if (!IsEmptyString(pszLineBuffer) &&
		    pszLineBuffer[0] != TAB_COMMENT_CHAR) {
			ulLineSize += ulOffset - ulCurrOffset;

			++iSampleLines;
		}
		ulCurrOffset = ulOffset;
	}
	if (iSampleLines == 0) {
		fseek(pTabFile, ulOrigOffset, SEEK_SET);
		return TAB_MIN_HASH_SIZE;
	}

	ulLineSize /= iSampleLines;

	fseek(pTabFile, 0, SEEK_END);

	unsigned long ulFileSize = (unsigned long) ftell(pTabFile);

	fseek(pTabFile, ulOrigOffset, SEEK_SET);

	int iHashSize = (int) (ulFileSize / ulLineSize) + TAB_MIN_HASH_SIZE;

	while (!IsPrimeNumber(iHashSize))
		++iHashSize;

	return iHashSize;
}

char *TbixGetIndexFile(char const *pszTabFilePath, int const *piFieldsIdx, char *pszIndexFile)
{
	char szFileDir[SYS_MAX_PATH] = "";
	char szFileName[SYS_MAX_PATH] = "";

	MscSplitPath(pszTabFilePath, szFileDir, sizeof(szFileDir),
		     szFileName, sizeof(szFileName), NULL, 0);
	sprintf(pszIndexFile, "%s%s%s%s-", szFileDir, TAB_INDEX_DIR, SYS_SLASH_STR, szFileName);

	for (int i = 0; piFieldsIdx[i] != INDEX_SEQUENCE_TERMINATOR; i++) {
		char szIndex[64] = "";

		sprintf(szIndex, "%02d", piFieldsIdx[i]);
		strcat(pszIndexFile, szIndex);
	}
	strcat(pszIndexFile, ".hdx");

	return pszIndexFile;
}

static int TbixFreeHash(HashNode *pHash, int iHashSize)
{
	for (int i = 0; i < iHashSize; i++) {
		SysListHead *pHead = &pHash[i].NodeList;
		SysListHead *pLLink;

		while ((pLLink = SYS_LIST_FIRST(pHead)) != NULL) {
			HashLink *pHL = SYS_LIST_ENTRY(pLLink, HashLink, LLink);

			SYS_LIST_DEL(&pHL->LLink);
			SysFree(pHL);
		}
	}
	SysFree(pHash);

	return 0;
}

int TbixCreateIndex(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens,
		    int (*pHashFunc) (char const *const *, int const *, TabIdxUINT *, bool))
{
	/* Adjust hash function */
	if (pHashFunc == NULL)
		pHashFunc = TbixCalculateHash;

	/* Build index file name */
	char szIndexFile[SYS_MAX_PATH] = "";

	if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
		return ErrGetErrorCode();

	FILE *pTabFile = fopen(pszTabFilePath, "rb");

	if (pTabFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
		return ERR_FILE_OPEN;
	}
	/* Calculate file lookup hash size */
	char szLineBuffer[TAB_RECORD_BUFFER_SIZE] = "";
	int iHashSize = TbixCalcHashSize(pTabFile, szLineBuffer, sizeof(szLineBuffer));

	/* Alloc and init hash records */
	HashNode *pHash = (HashNode *) SysAlloc(iHashSize * sizeof(HashNode));

	if (pHash == NULL) {
		fclose(pTabFile);
		return ErrGetErrorCode();
	}

	int i;

	for (i = 0; i < iHashSize; i++) {
		SYS_INIT_LIST_HEAD(&pHash[i].NodeList);
		pHash[i].uCount = 0;
	}

	/* Setup indexes records */
	for (;;) {
		/* Get current offset */
		TabIdxUINT uFileOffset = (TabIdxUINT) ftell(pTabFile);

		if (MscGetString(pTabFile, szLineBuffer, sizeof(szLineBuffer) - 1) == NULL)
			break;

		if (szLineBuffer[0] == TAB_COMMENT_CHAR)
			continue;

		char **ppszTabTokens = StrGetTabLineStrings(szLineBuffer);

		if (ppszTabTokens == NULL)
			continue;

		/* Calculate hash value */
		TabIdxUINT uHashVal = 0;

		if ((*pHashFunc)(ppszTabTokens, piFieldsIdx, &uHashVal, bCaseSens) == 0) {
			int iHashIndex = (int) (uHashVal % (TabIdxUINT) iHashSize);
			HashLink *pHL = (HashLink *) SysAlloc(sizeof(HashLink));

			if (pHL == NULL) {
				StrFreeStrings(ppszTabTokens);
				TbixFreeHash(pHash, iHashSize);
				fclose(pTabFile);
				return ErrGetErrorCode();
			}

			pHL->uOffset = uFileOffset;
			SYS_LIST_ADDT(&pHL->LLink, &pHash[iHashIndex].NodeList);
			++pHash[iHashIndex].uCount;
		}
		StrFreeStrings(ppszTabTokens);
	}
	fclose(pTabFile);

	/* Write index file */
	FILE *pIndexFile = fopen(szIndexFile, "wb");

	if (pIndexFile == NULL) {
		TbixFreeHash(pHash, iHashSize);

		ErrSetErrorCode(ERR_FILE_CREATE, szIndexFile);
		return ERR_FILE_CREATE;
	}
	/* Write file header */
	HashFileHeader HFH;

	ZeroData(HFH);
	HFH.uMagic = TAB_INDEX_MAGIC;
	HFH.uVersion = TAB_INDEX_CURR_VERSION;
	HFH.uHashSize = (TabIdxUINT) iHashSize;

	if (!fwrite(&HFH, sizeof(HFH), 1, pIndexFile)) {
		fclose(pIndexFile);
		SysRemove(szIndexFile);
		TbixFreeHash(pHash, iHashSize);

		ErrSetErrorCode(ERR_FILE_WRITE);
		return ERR_FILE_WRITE;
	}
	/* Dump main table */
	TabIdxUINT uCurrOffset = sizeof(HFH) + iHashSize * sizeof(TabIdxUINT);

	for (i = 0; i < iHashSize; i++) {
		TabIdxUINT uTableOffset = 0;

		if (pHash[i].uCount != 0) {
			uTableOffset = uCurrOffset;
			uCurrOffset += (pHash[i].uCount + 1) * sizeof(TabIdxUINT);
		}
		if (!fwrite(&uTableOffset, sizeof(uTableOffset), 1, pIndexFile)) {
			fclose(pIndexFile);
			SysRemove(szIndexFile);
			TbixFreeHash(pHash, iHashSize);

			ErrSetErrorCode(ERR_FILE_WRITE);
			return ERR_FILE_WRITE;
		}
	}

	/* Dump hash tables */
	for (i = 0; i < iHashSize; i++) {
		TabIdxUINT uRecCount = pHash[i].uCount;

		if (uRecCount != 0) {
			if (!fwrite(&uRecCount, sizeof(uRecCount), 1, pIndexFile)) {
				fclose(pIndexFile);
				SysRemove(szIndexFile);
				TbixFreeHash(pHash, iHashSize);

				ErrSetErrorCode(ERR_FILE_WRITE);
				return ERR_FILE_WRITE;
			}

			SysListHead *pHead = &pHash[i].NodeList;
			SysListHead *pLLink;

			SYS_LIST_FOR_EACH(pLLink, pHead) {
				HashLink *pHL = SYS_LIST_ENTRY(pLLink, HashLink, LLink);
				TabIdxUINT uRecordOffset = pHL->uOffset;

				if (!fwrite(&uRecordOffset, sizeof(uRecordOffset), 1, pIndexFile)) {
					fclose(pIndexFile);
					SysRemove(szIndexFile);
					TbixFreeHash(pHash, iHashSize);

					ErrSetErrorCode(ERR_FILE_WRITE);
					return ERR_FILE_WRITE;
				}
			}
		}
	}
	fclose(pIndexFile);
	TbixFreeHash(pHash, iHashSize);

	return 0;
}

static int TbixBuildKey(char *pszKey, va_list Args, bool bCaseSens)
{
	char const *pszToken;

	SetEmptyString(pszKey);
	for (int i = 0; (pszToken = va_arg(Args, char *)) != NULL; i++) {
		if (i > 0)
			strcat(pszKey, TOKEN_SEP_STR);
		strcat(pszKey, pszToken);
	}
	if (!bCaseSens)
		StrLower(pszKey);

	return 0;
}

static int TbixBuildKey(char *pszKey, char const *const *ppszTabTokens,
			int const *piFieldsIdx, bool bCaseSens)
{
	int iFieldsCount = StrStringsCount(ppszTabTokens);

	SetEmptyString(pszKey);
	for (int i = 0; piFieldsIdx[i] != INDEX_SEQUENCE_TERMINATOR; i++) {
		if (piFieldsIdx[i] < 0 || piFieldsIdx[i] >= iFieldsCount) {
			ErrSetErrorCode(ERR_BAD_TAB_INDEX_FIELD);
			return ERR_BAD_TAB_INDEX_FIELD;
		}
		if (i > 0)
			strcat(pszKey, TOKEN_SEP_STR);
		strcat(pszKey, ppszTabTokens[piFieldsIdx[i]]);
	}
	if (!bCaseSens)
		StrLower(pszKey);

	return 0;
}

static TabIdxUINT TbixHashData(char const *pData, long lSize)
{
	unsigned long ulHashVal = MscHashString(pData, lSize);

	/*
	 * We may want to use upper bits also, in architectures
	 * where sizeof(long) > 4 ...
	 */

	return (TabIdxUINT) ulHashVal;
}

int TbixCalculateHash(char const *const *ppszTabTokens, int const *piFieldsIdx,
		      TabIdxUINT *puHashVal, bool bCaseSens)
{
	char szKey[KEY_BUFFER_SIZE] = "";

	if (TbixBuildKey(szKey, ppszTabTokens, piFieldsIdx, bCaseSens) < 0)
		return ErrGetErrorCode();
	*puHashVal = TbixHashData(szKey, strlen(szKey));

	return 0;
}

static int TbixOpenIndex(char const *pszIndexFile, TabHashIndex &THI)
{
	FILE *pIndexFile = fopen(pszIndexFile, "rb");

	if (pIndexFile == NULL) {
		ErrSetErrorCode(ERR_FILE_OPEN, pszIndexFile);
		return ERR_FILE_OPEN;
	}
	/* Read header and check signature */
	ZeroData(THI);

	if (!fread(&THI.HFH, sizeof(THI.HFH), 1, pIndexFile)) {
		fclose(pIndexFile);

		ErrSetErrorCode(ERR_FILE_READ, pszIndexFile);
		return ERR_FILE_READ;
	}
	if (THI.HFH.uMagic != TAB_INDEX_MAGIC || THI.HFH.uVersion != TAB_INDEX_CURR_VERSION) {
		fclose(pIndexFile);

		ErrSetErrorCode(ERR_BAD_INDEX_FILE, pszIndexFile);
		return ERR_BAD_INDEX_FILE;
	}
	THI.pIndexFile = pIndexFile;

	return 0;
}

static int TbixCloseIndex(TabHashIndex &THI)
{
	fclose(THI.pIndexFile);
	ZeroData(THI);

	return 0;
}

static int TbixCheckIndex(char const *pszIndexFile)
{
	TabHashIndex THI;

	if (TbixOpenIndex(pszIndexFile, THI) < 0)
		return ErrGetErrorCode();

	TbixCloseIndex(THI);

	return 0;
}

static TabIdxUINT *TbixReadTable(TabHashIndex &THI, TabIdxUINT uHashVal)
{
	TabIdxUINT uHashIndex = uHashVal % THI.HFH.uHashSize, uTableOffset, uTableSize;
	unsigned long ulTableOffset = sizeof(HashFileHeader) + uHashIndex * sizeof(TabIdxUINT);

	if (fseek(THI.pIndexFile, ulTableOffset, SEEK_SET) != 0) {
		ErrSetErrorCode(ERR_BAD_INDEX_FILE);
		return NULL;
	}
	if (!fread(&uTableOffset, sizeof(uTableOffset), 1, THI.pIndexFile)) {
		ErrSetErrorCode(ERR_FILE_READ);
		return NULL;
	}
	if (uTableOffset == 0) {
		ErrSetErrorCode(ERR_RECORD_NOT_FOUND);
		return NULL;
	}
	if (fseek(THI.pIndexFile, uTableOffset, SEEK_SET) != 0) {
		ErrSetErrorCode(ERR_BAD_INDEX_FILE);
		return NULL;
	}
	if (!fread(&uTableSize, sizeof(uTableSize), 1, THI.pIndexFile)) {
		ErrSetErrorCode(ERR_FILE_READ);
		return NULL;
	}

	TabIdxUINT *pOffTable = (TabIdxUINT *) SysAlloc((uTableSize + 1) * sizeof(TabIdxUINT));

	if (pOffTable == NULL)
		return NULL;

	pOffTable[0] = uTableSize;
	if (!fread(&pOffTable[1], uTableSize * sizeof(TabIdxUINT), 1, THI.pIndexFile)) {
		SysFree(pOffTable);
		ErrSetErrorCode(ERR_FILE_READ);
		return NULL;
	}

	return pOffTable;
}

char **TbixLookup(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens, ...)
{
	/* Build index file name */
	char szIndexFile[SYS_MAX_PATH] = "";

	if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
		return NULL;

	/* Calculate key & hash */
	va_list Args;
	char szRefKey[KEY_BUFFER_SIZE] = "";

	va_start(Args, bCaseSens);
	if (TbixBuildKey(szRefKey, Args, bCaseSens) < 0) {
		va_end(Args);
		return NULL;
	}
	va_end(Args);

	TabIdxUINT uHashVal = TbixHashData(szRefKey, strlen(szRefKey));

	/* Open index */
	TabHashIndex THI;

	if (TbixOpenIndex(szIndexFile, THI) < 0)
		return NULL;

	/* Try to lookup records */
	TabIdxUINT *pHashTable = TbixReadTable(THI, uHashVal);

	TbixCloseIndex(THI);
	if (pHashTable == NULL)
		return NULL;

	/* Search for the matched one */
	FILE *pTabFile = fopen(pszTabFilePath, "rb");

	if (pTabFile == NULL) {
		SysFree(pHashTable);
		ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
		return NULL;
	}

	int iHashNodes = (int) pHashTable[0];

	for (int i = 0; i < iHashNodes; i++) {
		char **ppszTabTokens = TbixLoadRecord(pTabFile, pHashTable[i + 1]);

		if (ppszTabTokens != NULL) {
			char szKey[KEY_BUFFER_SIZE] = "";

			if (TbixBuildKey(szKey, ppszTabTokens, piFieldsIdx, bCaseSens) == 0) {
				if (bCaseSens) {
					if (strcmp(szKey, szRefKey) == 0) {
						fclose(pTabFile);
						SysFree(pHashTable);
						return ppszTabTokens;
					}
				} else {
					if (stricmp(szKey, szRefKey) == 0) {
						fclose(pTabFile);
						SysFree(pHashTable);
						return ppszTabTokens;
					}
				}
			}
			StrFreeStrings(ppszTabTokens);
		}
	}
	fclose(pTabFile);
	SysFree(pHashTable);

	ErrSetErrorCode(ERR_RECORD_NOT_FOUND);

	return NULL;
}

static char **TbixLoadRecord(FILE *pTabFile, TabIdxUINT uOffset)
{
	char szLineBuffer[TAB_RECORD_BUFFER_SIZE] = "";

	if (fseek(pTabFile, uOffset, SEEK_SET) != 0) {
		ErrSetErrorCode(ERR_BAD_INDEX_FILE);
		return NULL;
	}
	if (MscGetString(pTabFile, szLineBuffer, sizeof(szLineBuffer) - 1) == NULL) {
		ErrSetErrorCode(ERR_FILE_READ);
		return NULL;
	}

	return StrGetTabLineStrings(szLineBuffer);
}

int TbixCheckIndex(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens,
		   int (*pHashFunc) (char const *const *, int const *, TabIdxUINT *, bool))
{
	SYS_FILE_INFO FI_Tab;

	if (SysGetFileInfo(pszTabFilePath, FI_Tab) < 0)
		return ErrGetErrorCode();

	/* Build index file name */
	char szIndexFile[SYS_MAX_PATH] = "";

	if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
		return ErrGetErrorCode();

	/* Compare TAB <-> Index dates */
	SYS_FILE_INFO FI_Index;

	if (SysGetFileInfo(szIndexFile, FI_Index) < 0 || FI_Tab.tMod > FI_Index.tMod ||
	    TbixCheckIndex(szIndexFile) < 0) {

		/* Rebuild the index */
		if (TbixCreateIndex(pszTabFilePath, piFieldsIdx, bCaseSens, pHashFunc) < 0)
			return ErrGetErrorCode();
	}

	return 0;
}

INDEX_HANDLE TbixOpenHandle(char const *pszTabFilePath, int const *piFieldsIdx,
			    TabIdxUINT const *puHashVal, int iNumVals)
{
	int i;
	long lRecCount;
	ARRAY_HANDLE hArray;
	TabHashIndex THI;
	char szIndexFile[SYS_MAX_PATH] = "";

	if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0 ||
	    (hArray = ArrayCreate(TAB_INIT_RESSET_SIZE)) == INVALID_ARRAY_HANDLE)
		return INVALID_INDEX_HANDLE;

	if (TbixOpenIndex(szIndexFile, THI) < 0) {
		ArrayFree(hArray, MscSysFreeCB, NULL);
		return INVALID_INDEX_HANDLE;
	}
	for (i = 0, lRecCount = 0; i < iNumVals; i++) {
		TabIdxUINT *pHashTable = TbixReadTable(THI, puHashVal[i]);

		if (pHashTable != NULL) {
			if (ArrayAppend(hArray, pHashTable) < 0) {
				SysFree(pHashTable);
				ArrayFree(hArray, MscSysFreeCB, NULL);
				TbixCloseIndex(THI);
				return INVALID_INDEX_HANDLE;
			}
			lRecCount += (long) pHashTable[0];
		}
	}
	TbixCloseIndex(THI);
	if (lRecCount == 0) {
		ArrayFree(hArray, MscSysFreeCB, NULL);
		ErrSetErrorCode(ERR_RECORD_NOT_FOUND);
		return INVALID_INDEX_HANDLE;
	}

	/* Open tab file */
	FILE *pTabFile = fopen(pszTabFilePath, "rb");

	if (pTabFile == NULL) {
		ArrayFree(hArray, MscSysFreeCB, NULL);
		ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
		return INVALID_INDEX_HANDLE;
	}
	/* Setup lookup struct */
	IndexLookupData *pILD = (IndexLookupData *) SysAlloc(sizeof(IndexLookupData));

	if (pILD == NULL) {
		fclose(pTabFile);
		ArrayFree(hArray, MscSysFreeCB, NULL);
		return INVALID_INDEX_HANDLE;
	}
	pILD->hArray = hArray;
	pILD->lRecCount = lRecCount;
	pILD->pTabFile = pTabFile;

	return (INDEX_HANDLE) pILD;
}

int TbixCloseHandle(INDEX_HANDLE hIndexLookup)
{
	IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

	fclose(pILD->pTabFile);
	ArrayFree(pILD->hArray, MscSysFreeCB, NULL);
	SysFree(pILD);

	return 0;
}

long TbixLookedUpRecords(INDEX_HANDLE hIndexLookup)
{
	IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

	return pILD->lRecCount;
}

char **TbixFirstRecord(INDEX_HANDLE hIndexLookup)
{
	IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

	pILD->lArIdx = 0;
	pILD->lHTblI = 0;
	if ((pILD->pHTblC = (TabIdxUINT *) ArrayGet(pILD->hArray,
						    pILD->lArIdx)) == NULL) {
		ErrSetErrorCode(ERR_RECORD_NOT_FOUND);
		return NULL;
	}

	return TbixLoadRecord(pILD->pTabFile, pILD->pHTblC[++pILD->lHTblI]);
}

char **TbixNextRecord(INDEX_HANDLE hIndexLookup)
{
	IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

	if (pILD->pHTblC == NULL) {
		ErrSetErrorCode(ERR_BAD_SEQUENCE);
		return NULL;
	}
	if (pILD->lHTblI == (long) pILD->pHTblC[0]) {
		pILD->lArIdx++;
		if ((pILD->pHTblC = (TabIdxUINT *) ArrayGet(pILD->hArray,
							    pILD->lArIdx)) == NULL) {
			ErrSetErrorCode(ERR_RECORD_NOT_FOUND);
			return NULL;
		}
		pILD->lHTblI = 0;
	}

	return TbixLoadRecord(pILD->pTabFile, pILD->pHTblC[++pILD->lHTblI]);
}

