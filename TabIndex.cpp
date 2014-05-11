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
#include "Array.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "TabIndex.h"

/*
 * The index version MUST be incremented at every file format change!
 */
#define TAB_INDEX_CURR_VERSION      3

#define TAB_SAMPLE_LINES            32
#define TAB_MIN_HASH_SIZE           17
#define TAB_INDEX_DIR               "tabindex"
#define TAB_INDEX_MAGIC             (*(SYS_UINT32 *) "ABDL")
#define KEY_BUFFER_SIZE             1024
#define TAB_RECORD_BUFFER_SIZE      2048
#define TOKEN_SEP_STR               "\t"
#define TAB_INIT_RESSET_SIZE        128


struct TabHashLink {
    SysListHead Lnk;
    TabIdxUINT uOffset;
};

struct TabHashNode {
    SysListHead NodeList;
    TabIdxUINT uCount;
};

struct TabHashFileHeader {
    SYS_UINT32 uMagic;
    SYS_UINT32 uVersion;
    SYS_UINT32 uHashSize;
};

struct TabHashIndex {
    TabHashFileHeader HFH;
    FILE *pIdxFile;
};

struct IndexLookupData {
    ARRAY_HANDLE hArray;
    long lRecCount;
    FILE *pTabFile;
    TabIdxUINT *pHTblC;
    long lArIdx;
    long lHTblI;
};


static int TbixCalcHashSize(FILE *pTabFile, char *pszLnBuff, int iBufferSize)
{
    int iLineCnt = 0, iHashSize;
    SYS_OFF_T llOrigOffset, llOffset, llCurrOffset, llLineSize, llFileSize;

    llOrigOffset = Sys_ftell(pTabFile);
    rewind(pTabFile);
    llCurrOffset = llLineSize = 0;
    while (iLineCnt < TAB_SAMPLE_LINES) {
        if (MscGetString(pTabFile, pszLnBuff, iBufferSize - 1) == NULL)
            break;

        llOffset = Sys_ftell(pTabFile);
        if (!IsEmptyString(pszLnBuff) &&
            pszLnBuff[0] != TAB_COMMENT_CHAR) {
            llLineSize += llOffset - llCurrOffset;
            ++iLineCnt;
        }
        llCurrOffset = llOffset;
    }
    if (iLineCnt == 0) {
        Sys_fseek(pTabFile, llOrigOffset, SEEK_SET);
        return TAB_MIN_HASH_SIZE;
    }
    llLineSize /= iLineCnt;
    Sys_fseek(pTabFile, 0, SEEK_END);
    llFileSize = Sys_ftell(pTabFile);
    Sys_fseek(pTabFile, llOrigOffset, SEEK_SET);

    iHashSize = (int) (llFileSize / llLineSize) + TAB_MIN_HASH_SIZE;
    while (!IsPrimeNumber(iHashSize))
        ++iHashSize;

    return iHashSize;
}

char *TbixGetIndexFile(char const *pszTabFilePath, int const *piFieldsIdx, char *pszIdxFile)
{
    int i;
    char szFileDir[SYS_MAX_PATH], szFileName[SYS_MAX_PATH], szIndex[64];

    MscSplitPath(pszTabFilePath, szFileDir, sizeof(szFileDir),
             szFileName, sizeof(szFileName), NULL, 0);
    sprintf(pszIdxFile, "%s%s%s%s-", szFileDir, TAB_INDEX_DIR, SYS_SLASH_STR, szFileName);
    for (i = 0; piFieldsIdx[i] != INDEX_SEQUENCE_TERMINATOR; i++) {
        sprintf(szIndex, "%02d", piFieldsIdx[i]);
        strcat(pszIdxFile, szIndex);
    }
    strcat(pszIdxFile, ".hdx");

    return pszIdxFile;
}

static int TbixFreeHash(TabHashNode *pHash, int iHashSize)
{
    int i;
    SysListHead *pHead, *pPos;
    TabHashLink *pHL;

    for (i = 0; i < iHashSize; i++) {
        pHead = &pHash[i].NodeList;
        while ((pPos = SYS_LIST_FIRST(pHead)) != NULL) {
            pHL = SYS_LIST_ENTRY(pPos, TabHashLink, Lnk);
            SYS_LIST_DEL(&pHL->Lnk);
            SysFree(pHL);
        }
    }
    SysFree(pHash);

    return 0;
}

int TbixCreateIndex(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens,
            int (*pHashFunc) (char const *const *, int const *, unsigned long *,
                      bool))
{
    int i, iHashSize;
    FILE *pTabFile, *pIdxFile;
    TabHashNode *pHash;
    TabHashFileHeader HFH;
    char szIdxFile[SYS_MAX_PATH], szLnBuff[TAB_RECORD_BUFFER_SIZE];

    /* Adjust hash function */
    if (pHashFunc == NULL)
        pHashFunc = TbixCalculateHash;

    /* Build index file name */
    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIdxFile) < 0)
        return ErrGetErrorCode();

    if ((pTabFile = fopen(pszTabFilePath, "rb")) == NULL) {
        ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
        return ERR_FILE_OPEN;
    }

    /* Calculate file lookup hash size */
    iHashSize = TbixCalcHashSize(pTabFile, szLnBuff, sizeof(szLnBuff));

    /* Alloc and init hash records */
    if ((pHash = (TabHashNode *) SysAlloc(iHashSize * sizeof(TabHashNode))) == NULL) {
        fclose(pTabFile);
        return ErrGetErrorCode();
    }
    for (i = 0; i < iHashSize; i++)
        SYS_INIT_LIST_HEAD(&pHash[i].NodeList);

    /* Setup indexes records */
    for (;;) {
        /* Get current offset */
        unsigned long ulHashVal;
        TabIdxUINT uFileOffset;
        char **ppszToks;

        uFileOffset = (TabIdxUINT) Sys_ftell(pTabFile);
        if (MscGetString(pTabFile, szLnBuff, sizeof(szLnBuff) - 1) == NULL)
            break;
        if (szLnBuff[0] == TAB_COMMENT_CHAR ||
            (ppszToks = StrGetTabLineStrings(szLnBuff)) == NULL)
            continue;

        /* Calculate hash value */
        if ((*pHashFunc)(ppszToks, piFieldsIdx, &ulHashVal, bCaseSens) == 0) {
            int iHashIndex = (int) (ulHashVal % (unsigned int) iHashSize);
            TabHashLink *pHL;

            if ((pHL = (TabHashLink *) SysAlloc(sizeof(TabHashLink))) == NULL) {
                StrFreeStrings(ppszToks);
                TbixFreeHash(pHash, iHashSize);
                fclose(pTabFile);
                return ErrGetErrorCode();
            }
            pHL->uOffset = uFileOffset;
            SYS_LIST_ADDT(&pHL->Lnk, &pHash[iHashIndex].NodeList);
            pHash[iHashIndex].uCount++;
        }
        StrFreeStrings(ppszToks);
    }
    fclose(pTabFile);

    /* Write index file */
    if ((pIdxFile = fopen(szIdxFile, "wb")) == NULL) {
        TbixFreeHash(pHash, iHashSize);

        ErrSetErrorCode(ERR_FILE_CREATE, szIdxFile);
        return ERR_FILE_CREATE;
    }
    /* Write file header */
    ZeroData(HFH);
    HFH.uMagic = TAB_INDEX_MAGIC;
    HFH.uVersion = TAB_INDEX_CURR_VERSION;
    HFH.uHashSize = iHashSize;

    if (!fwrite(&HFH, sizeof(HFH), 1, pIdxFile)) {
        fclose(pIdxFile);
        SysRemove(szIdxFile);
        TbixFreeHash(pHash, iHashSize);

        ErrSetErrorCode(ERR_FILE_WRITE, szIdxFile); /* [i_a] */
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
        if (!fwrite(&uTableOffset, sizeof(uTableOffset), 1, pIdxFile)) {
            fclose(pIdxFile);
            SysRemove(szIdxFile);
            TbixFreeHash(pHash, iHashSize);

            ErrSetErrorCode(ERR_FILE_WRITE, szIdxFile); /* [i_a] */
            return ERR_FILE_WRITE;
        }
    }

    /* Dump hash tables */
    for (i = 0; i < iHashSize; i++) {
        TabIdxUINT uRecCount = pHash[i].uCount;

        if (uRecCount != 0) {
            SysListHead *pHead, *pPos;

            if (!fwrite(&uRecCount, sizeof(uRecCount), 1, pIdxFile)) {
                fclose(pIdxFile);
                SysRemove(szIdxFile);
                TbixFreeHash(pHash, iHashSize);

                ErrSetErrorCode(ERR_FILE_WRITE, szIdxFile); /* [i_a] */
                return ERR_FILE_WRITE;
            }
            pHead = &pHash[i].NodeList;
            SYS_LIST_FOR_EACH(pPos, pHead) {
                TabHashLink *pHL = SYS_LIST_ENTRY(pPos, TabHashLink,
                                  Lnk);
                TabIdxUINT uRecordOffset = pHL->uOffset;

                if (!fwrite(&uRecordOffset, sizeof(uRecordOffset), 1,
                        pIdxFile)) {
                    fclose(pIdxFile);
                    SysRemove(szIdxFile);
                    TbixFreeHash(pHash, iHashSize);

                    ErrSetErrorCode(ERR_FILE_WRITE, szIdxFile); /* [i_a] */
                    return ERR_FILE_WRITE;
                }
            }
        }
    }
    fclose(pIdxFile);
    TbixFreeHash(pHash, iHashSize);

    return 0;
}

static int TbixBuildKey(char *pszKey, va_list Args, bool bCaseSens)
{
    int i;
    char const *pszToken;

    SetEmptyString(pszKey);
    for (i = 0; (pszToken = va_arg(Args, char *)) != NULL; i++) {
        if (i > 0)
            strcat(pszKey, TOKEN_SEP_STR);
        strcat(pszKey, pszToken);
    }
    if (!bCaseSens)
        StrLower(pszKey);

    return 0;
}

static int TbixBuildKey(char *pszKey, char const *const *ppszToks,
            int const *piFieldsIdx, bool bCaseSens)
{
    int i, iFieldsCount = StrStringsCount(ppszToks);

    SetEmptyString(pszKey);
    for (i = 0; piFieldsIdx[i] != INDEX_SEQUENCE_TERMINATOR; i++) {
        if (piFieldsIdx[i] < 0 || piFieldsIdx[i] >= iFieldsCount) {
            ErrSetErrorCode(ERR_BAD_TAB_INDEX_FIELD);
            return ERR_BAD_TAB_INDEX_FIELD;
        }
        if (i > 0)
            strcat(pszKey, TOKEN_SEP_STR);
        strcat(pszKey, ppszToks[piFieldsIdx[i]]);
    }
    if (!bCaseSens)
        StrLower(pszKey);

    return 0;
}

int TbixCalculateHash(char const *const *ppszToks, int const *piFieldsIdx,
              unsigned long *pulHashVal, bool bCaseSens)
{
    char szKey[KEY_BUFFER_SIZE];

    if (TbixBuildKey(szKey, ppszToks, piFieldsIdx, bCaseSens) < 0)
        return ErrGetErrorCode();
    *pulHashVal = MscHashString(szKey, strlen(szKey));

    return 0;
}

static int TbixOpenIndex(char const *pszIdxFile, TabHashIndex &THI)
{
    FILE *pIdxFile = fopen(pszIdxFile, "rb");

    if (pIdxFile == NULL) {
        ErrSetErrorCode(ERR_FILE_OPEN, pszIdxFile);
        return ERR_FILE_OPEN;
    }
    /* Read header and check signature */
    ZeroData(THI);

    if (!fread(&THI.HFH, sizeof(THI.HFH), 1, pIdxFile)) {
        fclose(pIdxFile);

        ErrSetErrorCode(ERR_FILE_READ, pszIdxFile);
        return ERR_FILE_READ;
    }
    if (THI.HFH.uMagic != TAB_INDEX_MAGIC ||
        THI.HFH.uVersion != TAB_INDEX_CURR_VERSION) {
        fclose(pIdxFile);

        ErrSetErrorCode(ERR_BAD_INDEX_FILE, pszIdxFile);
        return ERR_BAD_INDEX_FILE;
    }
    THI.pIdxFile = pIdxFile;

    return 0;
}

static int TbixCloseIndex(TabHashIndex &THI)
{
    fclose(THI.pIdxFile);
    ZeroData(THI);

    return 0;
}

static int TbixCheckIndex(char const *pszIdxFile)
{
    TabHashIndex THI;

    if (TbixOpenIndex(pszIdxFile, THI) < 0)
        return ErrGetErrorCode();
    TbixCloseIndex(THI);

    return 0;
}

static TabIdxUINT *TbixReadTable(TabHashIndex &THI, /* TabIdxUINT */ unsigned long ulHashVal, char const *pszTabFilePath)
{
    unsigned long ulHashIndex;
    TabIdxUINT uTableOffset, uTableSize;
    SYS_OFF_T llTblOffset;
    TabIdxUINT *pOffTbl;

    ulHashIndex = ulHashVal % THI.HFH.uHashSize;
    llTblOffset = sizeof(TabHashFileHeader) + ulHashIndex * sizeof(TabIdxUINT);
    if (Sys_fseek(THI.pIdxFile, llTblOffset, SEEK_SET) != 0) {
        ErrSetErrorCode(ERR_BAD_INDEX_FILE, pszTabFilePath);
        return NULL;
    }
    if (!fread(&uTableOffset, sizeof(uTableOffset), 1, THI.pIdxFile)) {
        ErrSetErrorCode(ERR_FILE_READ, pszTabFilePath);
        return NULL;
    }
    if (uTableOffset == 0) {
        ErrSetErrorCode(ERR_RECORD_NOT_FOUND, pszTabFilePath);
        return NULL;
    }
    if (Sys_fseek(THI.pIdxFile, uTableOffset, SEEK_SET) != 0) {
        ErrSetErrorCode(ERR_BAD_INDEX_FILE, pszTabFilePath);
        return NULL;
    }
    if (!fread(&uTableSize, sizeof(uTableSize), 1, THI.pIdxFile)) {
        ErrSetErrorCode(ERR_FILE_READ, pszTabFilePath);
        return NULL;
    }
    pOffTbl = (TabIdxUINT *)SysAlloc((uTableSize + 1) * sizeof(TabIdxUINT));
    if (pOffTbl == NULL)
        return NULL;
    pOffTbl[0] = uTableSize;
    if (!fread(&pOffTbl[1], uTableSize * sizeof(TabIdxUINT), 1,
           THI.pIdxFile)) {
        SysFree(pOffTbl);
        ErrSetErrorCode(ERR_FILE_READ, pszTabFilePath);
        return NULL;
    }

    return pOffTbl;
}

static char **TbixLoadRecord(FILE *pTabFile, TabIdxUINT uOffset)
{
    char szLnBuff[TAB_RECORD_BUFFER_SIZE];

    if (Sys_fseek(pTabFile, uOffset, SEEK_SET) != 0) {
        ErrSetErrorCode(ERR_BAD_INDEX_FILE);
        return NULL;
    }
    if (MscGetString(pTabFile, szLnBuff,
             sizeof(szLnBuff) - 1) == NULL) {
        ErrSetErrorCode(ERR_FILE_READ);
        return NULL;
    }

    return StrGetTabLineStrings(szLnBuff);
}

char **TbixLookup(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens, ...)
{
    int i, iHashNodes;
    unsigned long ulHashVal;
    TabIdxUINT *pHashTable;
    FILE *pTabFile;
    va_list Args;
    TabHashIndex THI;
    char szIdxFile[SYS_MAX_PATH], szRefKey[KEY_BUFFER_SIZE];

    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIdxFile) < 0)
        return NULL;

    /* Calculate key & hash */
    va_start(Args, bCaseSens);
    if (TbixBuildKey(szRefKey, Args, bCaseSens) < 0) {
        va_end(Args);
        return NULL;
    }
    va_end(Args);

    /* Open index */
    ulHashVal = MscHashString(szRefKey, strlen(szRefKey));
    if (TbixOpenIndex(szIdxFile, THI) < 0)
        return NULL;

    /* Try to lookup records */
    TabIdxUINT *pHashTable = TbixReadTable(THI, ulHashVal, szIndexFile);

    TbixCloseIndex(THI);
    if (pHashTable == NULL)
        return NULL;

    /* Search for the matched one */
    if ((pTabFile = fopen(pszTabFilePath, "rb")) == NULL) {
        SysFree(pHashTable);
        ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
        return NULL;
    }

    iHashNodes = (int) pHashTable[0];
    for (i = 0; i < iHashNodes; i++) {
        char **ppszToks = TbixLoadRecord(pTabFile, pHashTable[i + 1]);
        char szKey[KEY_BUFFER_SIZE];

        if (ppszToks != NULL) {
            if (TbixBuildKey(szKey, ppszToks, piFieldsIdx, bCaseSens) == 0) {
                if (bCaseSens) {
                    if (strcmp(szKey, szRefKey) == 0) {
                        fclose(pTabFile);
                        SysFree(pHashTable);
                        return ppszToks;
                    }
                } else {
                    if (stricmp(szKey, szRefKey) == 0) {
                        fclose(pTabFile);
                        SysFree(pHashTable);
                        return ppszToks;
                    }
                }
            }
            StrFreeStrings(ppszToks);
        }
    }
    fclose(pTabFile);
    SysFree(pHashTable);

    ErrSetErrorCode(ERR_RECORD_NOT_FOUND);

    return NULL;
}

int TbixCheckIndex(char const *pszTabFilePath, int const *piFieldsIdx, bool bCaseSens,
           int (*pHashFunc) (char const *const *, int const *, unsigned long *,
                     bool))
{
    SYS_FILE_INFO FI_Tab, FI_Index;
    char szIdxFile[SYS_MAX_PATH];

    if (SysGetFileInfo(pszTabFilePath, FI_Tab) < 0 ||
        TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIdxFile) < 0)
        return ErrGetErrorCode();
    if (SysGetFileInfo(szIdxFile, FI_Index) < 0 || FI_Tab.tMod > FI_Index.tMod ||
        TbixCheckIndex(szIdxFile) < 0) {

        /* Rebuild the index */
        if (TbixCreateIndex(pszTabFilePath, piFieldsIdx, bCaseSens, pHashFunc) < 0)
            return ErrGetErrorCode();
    }

    return 0;
}

INDEX_HANDLE TbixOpenHandle(char const *pszTabFilePath, int const *piFieldsIdx,
                unsigned long const *pulHashVal, int iNumVals)
{
    int i;
    long lRecCount;
    ARRAY_HANDLE hArray;
    FILE *pTabFile;
    IndexLookupData *pILD;
    TabHashIndex THI;
    char szIdxFile[SYS_MAX_PATH];

    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIdxFile) < 0 ||
        (hArray = ArrayCreate(TAB_INIT_RESSET_SIZE)) == INVALID_ARRAY_HANDLE)
        return INVALID_INDEX_HANDLE;

    if (TbixOpenIndex(szIdxFile, THI) < 0) {
        ArrayFree(hArray, MscSysFreeCB, NULL);
        return INVALID_INDEX_HANDLE;
    }
    for (i = 0, lRecCount = 0; i < iNumVals; i++) {
        TabIdxUINT *pHashTable = TbixReadTable(THI, pulHashVal[i], szIndexFile);

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
        ErrSetErrorCode(ERR_RECORD_NOT_FOUND, pszTabFilePath);
        return INVALID_INDEX_HANDLE;
    }

    /* Open tab file */
    if ((pTabFile = fopen(pszTabFilePath, "rb")) == NULL) {
        ArrayFree(hArray, MscSysFreeCB, NULL);
        ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
        return INVALID_INDEX_HANDLE;
    }
    /* Setup lookup struct */
    if ((pILD = (IndexLookupData *) SysAlloc(sizeof(IndexLookupData))) == NULL) {
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

