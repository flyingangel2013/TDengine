/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tsdb.h"

// =============== PAGE-WISE FILE ===============
static int32_t tsdbOpenFile(const char *path, int32_t szPage, int32_t flag, STsdbFD **ppFD) {
  int32_t  code = 0;
  STsdbFD *pFD;

  *ppFD = NULL;

  pFD = (STsdbFD *)taosMemoryCalloc(1, sizeof(*pFD) + strlen(path) + 1);
  if (pFD == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _exit;
  }

  pFD->path = (char *)&pFD[1];
  strcpy(pFD->path, path);
  pFD->szPage = szPage;
  pFD->flag = flag;
  pFD->pFD = taosOpenFile(path, flag);
  if (pFD->pFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _exit;
  }
  pFD->szPage = szPage;
  pFD->pgno = 0;
  pFD->pBuf = taosMemoryCalloc(1, szPage);
  if (pFD->pBuf == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    taosMemoryFree(pFD);
    goto _exit;
  }
  if (taosStatFile(path, &pFD->szFile, NULL) < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _exit;
  }
  ASSERT(pFD->szFile % szPage == 0);
  pFD->szFile = pFD->szFile / szPage;
  *ppFD = pFD;

_exit:
  return code;
}

static void tsdbCloseFile(STsdbFD **ppFD) {
  STsdbFD *pFD = *ppFD;
  taosMemoryFree(pFD->pBuf);
  taosCloseFile(&pFD->pFD);
  taosMemoryFree(pFD);
  *ppFD = NULL;
}

static int32_t tsdbWriteFilePage(STsdbFD *pFD) {
  int32_t code = 0;

  if (pFD->pgno > 0) {
    int64_t n = taosLSeekFile(pFD->pFD, PAGE_OFFSET(pFD->pgno, pFD->szPage), SEEK_SET);
    if (n < 0) {
      code = TAOS_SYSTEM_ERROR(errno);
      goto _exit;
    }

    taosCalcChecksumAppend(0, pFD->pBuf, pFD->szPage);

    n = taosWriteFile(pFD->pFD, pFD->pBuf, pFD->szPage);
    if (n < 0) {
      code = TAOS_SYSTEM_ERROR(errno);
      goto _exit;
    }

    if (pFD->szFile < pFD->pgno) {
      pFD->szFile = pFD->pgno;
    }
  }
  pFD->pgno = 0;

_exit:
  return code;
}

static int32_t tsdbReadFilePage(STsdbFD *pFD, int64_t pgno) {
  int32_t code = 0;

  ASSERT(pgno <= pFD->szFile);

  // seek
  int64_t offset = PAGE_OFFSET(pgno, pFD->szPage);
  int64_t n = taosLSeekFile(pFD->pFD, offset, SEEK_SET);
  if (n < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _exit;
  }

  // read
  n = taosReadFile(pFD->pFD, pFD->pBuf, pFD->szPage);
  if (n < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _exit;
  } else if (n < pFD->szPage) {
    code = TSDB_CODE_FILE_CORRUPTED;
    goto _exit;
  }

  // check
  if (!taosCheckChecksumWhole(pFD->pBuf, pFD->szPage)) {
    code = TSDB_CODE_FILE_CORRUPTED;
    goto _exit;
  }

  pFD->pgno = pgno;

_exit:
  return code;
}

static int32_t tsdbWriteFile(STsdbFD *pFD, int64_t offset, uint8_t *pBuf, int64_t size) {
  int32_t code = 0;
  int64_t fOffset = LOGIC_TO_FILE_OFFSET(offset, pFD->szPage);
  int64_t pgno = OFFSET_PGNO(fOffset, pFD->szPage);
  int64_t bOffset = fOffset % pFD->szPage;
  int64_t n = 0;

  do {
    if (pFD->pgno != pgno) {
      code = tsdbWriteFilePage(pFD);
      if (code) goto _exit;

      if (pgno < pFD->szFile) {
        code = tsdbReadFilePage(pFD, pgno);
        if (code) goto _exit;
      } else {
        pFD->pgno = pgno;
      }
    }

    int64_t nWrite = TMIN(PAGE_CONTENT_SIZE(pFD->szPage) - bOffset, size - n);
    memcpy(pFD->pBuf + bOffset, pBuf + n, nWrite);

    pgno++;
    bOffset = 0;
    n += nWrite;
  } while (n < size);

_exit:
  return code;
}

static int32_t tsdbReadFile(STsdbFD *pFD, int64_t offset, uint8_t *pBuf, int64_t size) {
  int32_t code = 0;
  int64_t n = 0;
  int64_t fOffset = LOGIC_TO_FILE_OFFSET(offset, pFD->szPage);
  int64_t pgno = OFFSET_PGNO(fOffset, pFD->szPage);
  int32_t szPgCont = PAGE_CONTENT_SIZE(pFD->szPage);
  int64_t bOffset = fOffset % pFD->szPage;

  ASSERT(pgno && pgno <= pFD->szFile);
  ASSERT(bOffset < szPgCont);

  while (n < size) {
    if (pFD->pgno != pgno) {
      code = tsdbReadFilePage(pFD, pgno);
      if (code) goto _exit;
    }

    int64_t nRead = TMIN(szPgCont - bOffset, size - n);
    memcpy(pBuf + n, pFD->pBuf + bOffset, nRead);

    n += nRead;
    pgno++;
    bOffset = 0;
  }

_exit:
  return code;
}

static int32_t tsdbFsyncFile(STsdbFD *pFD) {
  int32_t code = 0;

  code = tsdbWriteFilePage(pFD);
  if (code) goto _exit;

  if (taosFsyncFile(pFD->pFD) < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _exit;
  }

_exit:
  return code;
}

// SDataFWriter ====================================================
int32_t tsdbDataFWriterOpen(SDataFWriter **ppWriter, STsdb *pTsdb, SDFileSet *pSet) {
  int32_t       code = 0;
  int32_t       flag;
  int64_t       n;
  int32_t       szPage = TSDB_DEFAULT_PAGE_SIZE;
  SDataFWriter *pWriter = NULL;
  char          fname[TSDB_FILENAME_LEN];
  char          hdr[TSDB_FHDR_SIZE] = {0};

  // alloc
  pWriter = taosMemoryCalloc(1, sizeof(*pWriter));
  if (pWriter == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }
  pWriter->pTsdb = pTsdb;
  pWriter->wSet = (SDFileSet){.diskId = pSet->diskId,
                              .fid = pSet->fid,
                              .pHeadF = &pWriter->fHead,
                              .pDataF = &pWriter->fData,
                              .pSmaF = &pWriter->fSma,
                              .nSstF = pSet->nSstF};
  pWriter->fHead = *pSet->pHeadF;
  pWriter->fData = *pSet->pDataF;
  pWriter->fSma = *pSet->pSmaF;
  for (int8_t iSst = 0; iSst < pSet->nSstF; iSst++) {
    pWriter->wSet.aSstF[iSst] = &pWriter->fSst[iSst];
    pWriter->fSst[iSst] = *pSet->aSstF[iSst];
  }

  // head
  flag = TD_FILE_READ | TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC;
  tsdbHeadFileName(pTsdb, pWriter->wSet.diskId, pWriter->wSet.fid, &pWriter->fHead, fname);
  code = tsdbOpenFile(fname, szPage, flag, &pWriter->pHeadFD);
  if (code) goto _err;

  code = tsdbWriteFile(pWriter->pHeadFD, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;
  pWriter->fHead.size += TSDB_FHDR_SIZE;

  // data
  if (pWriter->fData.size == 0) {
    flag = TD_FILE_READ | TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC;
  } else {
    flag = TD_FILE_READ | TD_FILE_WRITE;
  }
  tsdbDataFileName(pTsdb, pWriter->wSet.diskId, pWriter->wSet.fid, &pWriter->fData, fname);
  code = tsdbOpenFile(fname, szPage, flag, &pWriter->pDataFD);
  if (code) goto _err;
  if (pWriter->fData.size == 0) {
    code = tsdbWriteFile(pWriter->pDataFD, 0, hdr, TSDB_FHDR_SIZE);
    if (code) goto _err;
    pWriter->fData.size += TSDB_FHDR_SIZE;
  }

  // sma
  if (pWriter->fSma.size == 0) {
    flag = TD_FILE_READ | TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC;
  } else {
    flag = TD_FILE_READ | TD_FILE_WRITE;
  }
  tsdbSmaFileName(pTsdb, pWriter->wSet.diskId, pWriter->wSet.fid, &pWriter->fSma, fname);
  code = tsdbOpenFile(fname, szPage, flag, &pWriter->pSmaFD);
  if (code) goto _err;
  if (pWriter->fSma.size == 0) {
    code = tsdbWriteFile(pWriter->pSmaFD, 0, hdr, TSDB_FHDR_SIZE);
    if (code) goto _err;

    pWriter->fSma.size += TSDB_FHDR_SIZE;
  }

  // sst
  ASSERT(pWriter->fSst[pSet->nSstF - 1].size == 0);
  flag = TD_FILE_READ | TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC;
  tsdbSstFileName(pTsdb, pWriter->wSet.diskId, pWriter->wSet.fid, &pWriter->fSst[pSet->nSstF - 1], fname);
  code = tsdbOpenFile(fname, szPage, flag, &pWriter->pSstFD);
  if (code) goto _err;
  code = tsdbWriteFile(pWriter->pSstFD, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;
  pWriter->fSst[pWriter->wSet.nSstF - 1].size += TSDB_FHDR_SIZE;

  *ppWriter = pWriter;
  return code;

_err:
  tsdbError("vgId:%d, tsdb data file writer open failed since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  *ppWriter = NULL;
  return code;
}

int32_t tsdbDataFWriterClose(SDataFWriter **ppWriter, int8_t sync) {
  int32_t code = 0;
  STsdb  *pTsdb = NULL;

  if (*ppWriter == NULL) goto _exit;

  pTsdb = (*ppWriter)->pTsdb;
  if (sync) {
    code = tsdbFsyncFile((*ppWriter)->pHeadFD);
    if (code) goto _err;

    code = tsdbFsyncFile((*ppWriter)->pDataFD);
    if (code) goto _err;

    code = tsdbFsyncFile((*ppWriter)->pSmaFD);
    if (code) goto _err;

    code = tsdbFsyncFile((*ppWriter)->pSstFD);
    if (code) goto _err;
  }

  tsdbCloseFile(&(*ppWriter)->pHeadFD);
  tsdbCloseFile(&(*ppWriter)->pDataFD);
  tsdbCloseFile(&(*ppWriter)->pSmaFD);
  tsdbCloseFile(&(*ppWriter)->pSstFD);

  for (int32_t iBuf = 0; iBuf < sizeof((*ppWriter)->aBuf) / sizeof(uint8_t *); iBuf++) {
    tFree((*ppWriter)->aBuf[iBuf]);
  }
  taosMemoryFree(*ppWriter);
_exit:
  *ppWriter = NULL;
  return code;

_err:
  tsdbError("vgId:%d, data file writer close failed since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbUpdateDFileSetHeader(SDataFWriter *pWriter) {
  int32_t code = 0;
  int64_t n;
  char    hdr[TSDB_FHDR_SIZE];

  // head ==============
  memset(hdr, 0, TSDB_FHDR_SIZE);
  tPutHeadFile(hdr, &pWriter->fHead);
  code = tsdbWriteFile(pWriter->pHeadFD, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;

  // data ==============
  memset(hdr, 0, TSDB_FHDR_SIZE);
  tPutDataFile(hdr, &pWriter->fData);
  code = tsdbWriteFile(pWriter->pDataFD, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;

  // sma ==============
  memset(hdr, 0, TSDB_FHDR_SIZE);
  tPutSmaFile(hdr, &pWriter->fSma);
  code = tsdbWriteFile(pWriter->pSmaFD, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;

  // sst ==============
  memset(hdr, 0, TSDB_FHDR_SIZE);
  tPutSstFile(hdr, &pWriter->fSst[pWriter->wSet.nSstF - 1]);
  code = tsdbWriteFile(pWriter->pSstFD, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;

  return code;

_err:
  tsdbError("vgId:%d, update DFileSet header failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbWriteBlockIdx(SDataFWriter *pWriter, SArray *aBlockIdx) {
  int32_t    code = 0;
  SHeadFile *pHeadFile = &pWriter->fHead;
  int64_t    size;
  int64_t    n;

  // check
  if (taosArrayGetSize(aBlockIdx) == 0) {
    pHeadFile->offset = pHeadFile->size;
    goto _exit;
  }

  // prepare
  size = 0;
  for (int32_t iBlockIdx = 0; iBlockIdx < taosArrayGetSize(aBlockIdx); iBlockIdx++) {
    size += tPutBlockIdx(NULL, taosArrayGet(aBlockIdx, iBlockIdx));
  }

  // alloc
  code = tRealloc(&pWriter->aBuf[0], size);
  if (code) goto _err;

  // build
  n = 0;
  for (int32_t iBlockIdx = 0; iBlockIdx < taosArrayGetSize(aBlockIdx); iBlockIdx++) {
    n += tPutBlockIdx(pWriter->aBuf[0] + n, taosArrayGet(aBlockIdx, iBlockIdx));
  }
  ASSERT(n == size);

  // write
  code = tsdbWriteFile(pWriter->pHeadFD, pHeadFile->size, pWriter->aBuf[0], size);
  if (code) goto _err;

  // update
  pHeadFile->offset = pHeadFile->size;
  pHeadFile->size += size;

_exit:
  tsdbTrace("vgId:%d write block idx, offset:%" PRId64 " size:%" PRId64 " nBlockIdx:%d", TD_VID(pWriter->pTsdb->pVnode),
            pHeadFile->offset, size, taosArrayGetSize(aBlockIdx));
  return code;

_err:
  tsdbError("vgId:%d, write block idx failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbWriteBlock(SDataFWriter *pWriter, SMapData *mBlock, SBlockIdx *pBlockIdx) {
  int32_t    code = 0;
  SHeadFile *pHeadFile = &pWriter->fHead;
  int64_t    size;
  int64_t    n;

  ASSERT(mBlock->nItem > 0);

  // alloc
  size = tPutMapData(NULL, mBlock);
  code = tRealloc(&pWriter->aBuf[0], size);
  if (code) goto _err;

  // build
  n = tPutMapData(pWriter->aBuf[0], mBlock);

  // write
  code = tsdbWriteFile(pWriter->pHeadFD, pHeadFile->size, pWriter->aBuf[0], size);
  if (code) goto _err;

  // update
  pBlockIdx->offset = pHeadFile->size;
  pBlockIdx->size = size;
  pHeadFile->size += size;

  tsdbTrace("vgId:%d, write block, file ID:%d commit ID:%d suid:%" PRId64 " uid:%" PRId64 " offset:%" PRId64
            " size:%" PRId64 " nItem:%d",
            TD_VID(pWriter->pTsdb->pVnode), pWriter->wSet.fid, pHeadFile->commitID, pBlockIdx->suid, pBlockIdx->uid,
            pBlockIdx->offset, pBlockIdx->size, mBlock->nItem);
  return code;

_err:
  tsdbError("vgId:%d, write block failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbWriteSstBlk(SDataFWriter *pWriter, SArray *aSstBlk) {
  int32_t   code = 0;
  SSstFile *pSstFile = &pWriter->fSst[pWriter->wSet.nSstF - 1];
  int64_t   size;
  int64_t   n;

  // check
  if (taosArrayGetSize(aSstBlk) == 0) {
    pSstFile->offset = pSstFile->size;
    goto _exit;
  }

  // size
  size = 0;
  for (int32_t iBlockL = 0; iBlockL < taosArrayGetSize(aSstBlk); iBlockL++) {
    size += tPutSstBlk(NULL, taosArrayGet(aSstBlk, iBlockL));
  }

  // alloc
  code = tRealloc(&pWriter->aBuf[0], size);
  if (code) goto _err;

  // encode
  n = 0;
  for (int32_t iBlockL = 0; iBlockL < taosArrayGetSize(aSstBlk); iBlockL++) {
    n += tPutSstBlk(pWriter->aBuf[0] + n, taosArrayGet(aSstBlk, iBlockL));
  }

  // write
  code = tsdbWriteFile(pWriter->pSstFD, pSstFile->size, pWriter->aBuf[0], size);
  if (code) goto _err;

  // update
  pSstFile->offset = pSstFile->size;
  pSstFile->size += size;

_exit:
  tsdbTrace("vgId:%d tsdb write sst block, loffset:%" PRId64 " size:%" PRId64, TD_VID(pWriter->pTsdb->pVnode),
            pSstFile->offset, size);
  return code;

_err:
  tsdbError("vgId:%d tsdb write blockl failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

static int32_t tsdbWriteBlockSma(SDataFWriter *pWriter, SBlockData *pBlockData, SSmaInfo *pSmaInfo) {
  int32_t code = 0;

  pSmaInfo->offset = 0;
  pSmaInfo->size = 0;

  // encode
  for (int32_t iColData = 0; iColData < taosArrayGetSize(pBlockData->aIdx); iColData++) {
    SColData *pColData = tBlockDataGetColDataByIdx(pBlockData, iColData);

    if ((!pColData->smaOn) || IS_VAR_DATA_TYPE(pColData->type)) continue;

    SColumnDataAgg sma;
    tsdbCalcColDataSMA(pColData, &sma);

    code = tRealloc(&pWriter->aBuf[0], pSmaInfo->size + tPutColumnDataAgg(NULL, &sma));
    if (code) goto _err;
    pSmaInfo->size += tPutColumnDataAgg(pWriter->aBuf[0] + pSmaInfo->size, &sma);
  }

  // write
  if (pSmaInfo->size) {
    code = tRealloc(&pWriter->aBuf[0], pSmaInfo->size);
    if (code) goto _err;

    code = tsdbWriteFile(pWriter->pSmaFD, pWriter->fSma.size, pWriter->aBuf[0], pSmaInfo->size);
    if (code) goto _err;

    pSmaInfo->offset = pWriter->fSma.size;
    pWriter->fSma.size += pSmaInfo->size;
  }

  return code;

_err:
  tsdbError("vgId:%d tsdb write block sma failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbWriteBlockData(SDataFWriter *pWriter, SBlockData *pBlockData, SBlockInfo *pBlkInfo, SSmaInfo *pSmaInfo,
                           int8_t cmprAlg, int8_t toLast) {
  int32_t code = 0;

  ASSERT(pBlockData->nRow > 0);

  if (toLast) {
    pBlkInfo->offset = pWriter->fSst[pWriter->wSet.nSstF - 1].size;
  } else {
    pBlkInfo->offset = pWriter->fData.size;
  }
  pBlkInfo->szBlock = 0;
  pBlkInfo->szKey = 0;

  int32_t aBufN[4] = {0};
  code = tCmprBlockData(pBlockData, cmprAlg, NULL, NULL, pWriter->aBuf, aBufN);
  if (code) goto _err;

  // write =================
  STsdbFD *pFD = toLast ? pWriter->pSstFD : pWriter->pDataFD;

  pBlkInfo->szKey = aBufN[3] + aBufN[2];
  pBlkInfo->szBlock = aBufN[0] + aBufN[1] + aBufN[2] + aBufN[3];

  int64_t offset = pBlkInfo->offset;
  code = tsdbWriteFile(pFD, offset, pWriter->aBuf[3], aBufN[3]);
  if (code) goto _err;
  offset += aBufN[3];

  code = tsdbWriteFile(pFD, offset, pWriter->aBuf[2], aBufN[2]);
  if (code) goto _err;
  offset += aBufN[2];

  if (aBufN[1]) {
    code = tsdbWriteFile(pFD, offset, pWriter->aBuf[1], aBufN[1]);
    if (code) goto _err;
    offset += aBufN[1];
  }

  if (aBufN[0]) {
    code = tsdbWriteFile(pFD, offset, pWriter->aBuf[0], aBufN[0]);
    if (code) goto _err;
  }

  // update info
  if (toLast) {
    pWriter->fSst[pWriter->wSet.nSstF - 1].size += pBlkInfo->szBlock;
  } else {
    pWriter->fData.size += pBlkInfo->szBlock;
  }

  // ================= SMA ====================
  if (pSmaInfo) {
    code = tsdbWriteBlockSma(pWriter, pBlockData, pSmaInfo);
    if (code) goto _err;
  }

_exit:
  tsdbTrace("vgId:%d tsdb write block data, suid:%" PRId64 " uid:%" PRId64 " nRow:%d, offset:%" PRId64 " size:%d",
            TD_VID(pWriter->pTsdb->pVnode), pBlockData->suid, pBlockData->uid, pBlockData->nRow, pBlkInfo->offset,
            pBlkInfo->szBlock);
  return code;

_err:
  tsdbError("vgId:%d tsdb write block data failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbDFileSetCopy(STsdb *pTsdb, SDFileSet *pSetFrom, SDFileSet *pSetTo) {
  int32_t   code = 0;
  int64_t   n;
  int64_t   size;
  TdFilePtr pOutFD = NULL;  // TODO
  TdFilePtr PInFD = NULL;   // TODO
  char      fNameFrom[TSDB_FILENAME_LEN];
  char      fNameTo[TSDB_FILENAME_LEN];

  // head
  tsdbHeadFileName(pTsdb, pSetFrom->diskId, pSetFrom->fid, pSetFrom->pHeadF, fNameFrom);
  tsdbHeadFileName(pTsdb, pSetTo->diskId, pSetTo->fid, pSetTo->pHeadF, fNameTo);

  pOutFD = taosOpenFile(fNameTo, TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC);
  if (pOutFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  PInFD = taosOpenFile(fNameFrom, TD_FILE_READ);
  if (PInFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  n = taosFSendFile(pOutFD, PInFD, 0, pSetFrom->pHeadF->size);
  if (n < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }
  taosCloseFile(&pOutFD);
  taosCloseFile(&PInFD);

  // data
  tsdbDataFileName(pTsdb, pSetFrom->diskId, pSetFrom->fid, pSetFrom->pDataF, fNameFrom);
  tsdbDataFileName(pTsdb, pSetTo->diskId, pSetTo->fid, pSetTo->pDataF, fNameTo);

  pOutFD = taosOpenFile(fNameTo, TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC);
  if (pOutFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  PInFD = taosOpenFile(fNameFrom, TD_FILE_READ);
  if (PInFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  n = taosFSendFile(pOutFD, PInFD, 0, pSetFrom->pDataF->size);
  if (n < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }
  taosCloseFile(&pOutFD);
  taosCloseFile(&PInFD);

  // sst
  tsdbSstFileName(pTsdb, pSetFrom->diskId, pSetFrom->fid, pSetFrom->aSstF[0], fNameFrom);
  tsdbSstFileName(pTsdb, pSetTo->diskId, pSetTo->fid, pSetTo->aSstF[0], fNameTo);

  pOutFD = taosOpenFile(fNameTo, TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC);
  if (pOutFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  PInFD = taosOpenFile(fNameFrom, TD_FILE_READ);
  if (PInFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  n = taosFSendFile(pOutFD, PInFD, 0, pSetFrom->aSstF[0]->size);
  if (n < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }
  taosCloseFile(&pOutFD);
  taosCloseFile(&PInFD);

  // sma
  tsdbSmaFileName(pTsdb, pSetFrom->diskId, pSetFrom->fid, pSetFrom->pSmaF, fNameFrom);
  tsdbSmaFileName(pTsdb, pSetTo->diskId, pSetTo->fid, pSetTo->pSmaF, fNameTo);

  pOutFD = taosOpenFile(fNameTo, TD_FILE_WRITE | TD_FILE_CREATE | TD_FILE_TRUNC);
  if (pOutFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  PInFD = taosOpenFile(fNameFrom, TD_FILE_READ);
  if (PInFD == NULL) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }

  n = taosFSendFile(pOutFD, PInFD, 0, pSetFrom->pSmaF->size);
  if (n < 0) {
    code = TAOS_SYSTEM_ERROR(errno);
    goto _err;
  }
  taosCloseFile(&pOutFD);
  taosCloseFile(&PInFD);

  return code;

_err:
  tsdbError("vgId:%d, tsdb DFileSet copy failed since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  return code;
}

// SDataFReader ====================================================
int32_t tsdbDataFReaderOpen(SDataFReader **ppReader, STsdb *pTsdb, SDFileSet *pSet) {
  int32_t       code = 0;
  SDataFReader *pReader;
  int32_t       szPage = TSDB_DEFAULT_PAGE_SIZE;
  char          fname[TSDB_FILENAME_LEN];

  // alloc
  pReader = (SDataFReader *)taosMemoryCalloc(1, sizeof(*pReader));
  if (pReader == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }
  pReader->pTsdb = pTsdb;
  pReader->pSet = pSet;

  // head
  tsdbHeadFileName(pTsdb, pSet->diskId, pSet->fid, pSet->pHeadF, fname);
  code = tsdbOpenFile(fname, szPage, TD_FILE_READ, &pReader->pHeadFD);
  if (code) goto _err;

  // data
  tsdbDataFileName(pTsdb, pSet->diskId, pSet->fid, pSet->pDataF, fname);
  code = tsdbOpenFile(fname, szPage, TD_FILE_READ, &pReader->pDataFD);
  if (code) goto _err;

  // sma
  tsdbSmaFileName(pTsdb, pSet->diskId, pSet->fid, pSet->pSmaF, fname);
  code = tsdbOpenFile(fname, szPage, TD_FILE_READ, &pReader->pSmaFD);
  if (code) goto _err;

  // sst
  for (int32_t iSst = 0; iSst < pSet->nSstF; iSst++) {
    tsdbSstFileName(pTsdb, pSet->diskId, pSet->fid, pSet->aSstF[iSst], fname);
    code = tsdbOpenFile(fname, szPage, TD_FILE_READ, &pReader->aSstFD[iSst]);
    if (code) goto _err;
  }

  *ppReader = pReader;
  return code;

_err:
  tsdbError("vgId:%d, tsdb data file reader open failed since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  *ppReader = NULL;
  return code;
}

int32_t tsdbDataFReaderClose(SDataFReader **ppReader) {
  int32_t code = 0;
  if (*ppReader == NULL) return code;

  // head
  tsdbCloseFile(&(*ppReader)->pHeadFD);

  // data
  tsdbCloseFile(&(*ppReader)->pDataFD);

  // sma
  tsdbCloseFile(&(*ppReader)->pSmaFD);

  // sst
  for (int32_t iSst = 0; iSst < (*ppReader)->pSet->nSstF; iSst++) {
    tsdbCloseFile(&(*ppReader)->aSstFD[iSst]);
  }

  for (int32_t iBuf = 0; iBuf < sizeof((*ppReader)->aBuf) / sizeof(uint8_t *); iBuf++) {
    tFree((*ppReader)->aBuf[iBuf]);
  }
  taosMemoryFree(*ppReader);
  *ppReader = NULL;
  return code;

_err:
  tsdbError("vgId:%d, data file reader close failed since %s", TD_VID((*ppReader)->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadBlockIdx(SDataFReader *pReader, SArray *aBlockIdx) {
  int32_t    code = 0;
  SHeadFile *pHeadFile = pReader->pSet->pHeadF;
  int64_t    offset = pHeadFile->offset;
  int64_t    size = pHeadFile->size - offset;

  taosArrayClear(aBlockIdx);
  if (size == 0) return code;

  // alloc
  code = tRealloc(&pReader->aBuf[0], size);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->pHeadFD, offset, pReader->aBuf[0], size);
  if (code) goto _err;

  // decode
  int64_t n = 0;
  while (n < size) {
    SBlockIdx blockIdx;
    n += tGetBlockIdx(pReader->aBuf[0] + n, &blockIdx);

    if (taosArrayPush(aBlockIdx, &blockIdx) == NULL) {
      code = TSDB_CODE_OUT_OF_MEMORY;
      goto _err;
    }
  }
  ASSERT(n == size);

  return code;

_err:
  tsdbError("vgId:%d, read block idx failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadSstBlk(SDataFReader *pReader, int32_t iSst, SArray *aSstBlk) {
  int32_t   code = 0;
  SSstFile *pSstFile = pReader->pSet->aSstF[iSst];
  int64_t   offset = pSstFile->offset;
  int64_t   size = pSstFile->size - offset;

  taosArrayClear(aSstBlk);
  if (size == 0) return code;

  // alloc
  code = tRealloc(&pReader->aBuf[0], size);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->aSstFD[iSst], offset, pReader->aBuf[0], size);
  if (code) goto _err;

  // decode
  int64_t n = 0;
  while (n < size) {
    SSstBlk sstBlk;
    n += tGetSstBlk(pReader->aBuf[0] + n, &sstBlk);

    if (taosArrayPush(aSstBlk, &sstBlk) == NULL) {
      code = TSDB_CODE_OUT_OF_MEMORY;
      goto _err;
    }
  }
  ASSERT(n == size);

  return code;

_err:
  tsdbError("vgId:%d read sst blk failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadBlock(SDataFReader *pReader, SBlockIdx *pBlockIdx, SMapData *mBlock) {
  int32_t code = 0;
  int64_t offset = pBlockIdx->offset;
  int64_t size = pBlockIdx->size;

  // alloc
  code = tRealloc(&pReader->aBuf[0], size);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->pHeadFD, offset, pReader->aBuf[0], size);
  if (code) goto _err;

  // decode
  int64_t n = tGetMapData(pReader->aBuf[0], mBlock);
  if (n < 0) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }
  ASSERT(n == size);

  return code;

_err:
  tsdbError("vgId:%d, read block failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadBlockSma(SDataFReader *pReader, SDataBlk *pDataBlk, SArray *aColumnDataAgg) {
  int32_t   code = 0;
  SSmaInfo *pSmaInfo = &pDataBlk->smaInfo;

  ASSERT(pSmaInfo->size > 0);

  taosArrayClear(aColumnDataAgg);

  // alloc
  code = tRealloc(&pReader->aBuf[0], pSmaInfo->size);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->pSmaFD, pSmaInfo->offset, pReader->aBuf[0], pSmaInfo->size);
  if (code) goto _err;

  // decode
  int32_t n = 0;
  while (n < pSmaInfo->size) {
    SColumnDataAgg sma;
    n += tGetColumnDataAgg(pReader->aBuf[0] + n, &sma);

    if (taosArrayPush(aColumnDataAgg, &sma) == NULL) {
      code = TSDB_CODE_OUT_OF_MEMORY;
      goto _err;
    }
  }
  ASSERT(n == pSmaInfo->size);
  return code;

_err:
  tsdbError("vgId:%d tsdb read block sma failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

static int32_t tsdbReadBlockDataImpl(SDataFReader *pReader, SBlockInfo *pBlkInfo, SBlockData *pBlockData) {
  int32_t code = 0;

  tBlockDataClear(pBlockData);

  STsdbFD *pFD = pReader->pDataFD;

  // uid + version + tskey
  code = tRealloc(&pReader->aBuf[0], pBlkInfo->szKey);
  if (code) goto _err;

  code = tsdbReadFile(pFD, pBlkInfo->offset, pReader->aBuf[0], pBlkInfo->szKey);
  if (code) goto _err;

  SDiskDataHdr hdr;
  uint8_t     *p = pReader->aBuf[0] + tGetDiskDataHdr(pReader->aBuf[0], &hdr);

  ASSERT(hdr.delimiter == TSDB_FILE_DLMT);
  ASSERT(pBlockData->suid == hdr.suid);
  ASSERT(pBlockData->uid == hdr.uid);

  pBlockData->nRow = hdr.nRow;

  // uid
  if (hdr.uid == 0) {
    ASSERT(hdr.szUid);
    code = tsdbDecmprData(p, hdr.szUid, TSDB_DATA_TYPE_BIGINT, hdr.cmprAlg, (uint8_t **)&pBlockData->aUid,
                          sizeof(int64_t) * hdr.nRow, &pReader->aBuf[1]);
    if (code) goto _err;
  } else {
    ASSERT(!hdr.szUid);
  }
  p += hdr.szUid;

  // version
  code = tsdbDecmprData(p, hdr.szVer, TSDB_DATA_TYPE_BIGINT, hdr.cmprAlg, (uint8_t **)&pBlockData->aVersion,
                        sizeof(int64_t) * hdr.nRow, &pReader->aBuf[1]);
  if (code) goto _err;
  p += hdr.szVer;

  // TSKEY
  code = tsdbDecmprData(p, hdr.szKey, TSDB_DATA_TYPE_TIMESTAMP, hdr.cmprAlg, (uint8_t **)&pBlockData->aTSKEY,
                        sizeof(TSKEY) * hdr.nRow, &pReader->aBuf[1]);
  if (code) goto _err;
  p += hdr.szKey;

  ASSERT(p - pReader->aBuf[0] == pBlkInfo->szKey);

  // read and decode columns
  if (taosArrayGetSize(pBlockData->aIdx) == 0) goto _exit;

  if (hdr.szBlkCol > 0) {
    int64_t offset = pBlkInfo->offset + pBlkInfo->szKey;
    code = tsdbReadFile(pFD, offset, pReader->aBuf[0], hdr.szBlkCol);
    if (code) goto _err;
  }

  SBlockCol  blockCol = {.cid = 0};
  SBlockCol *pBlockCol = &blockCol;
  int32_t    n = 0;

  for (int32_t iColData = 0; iColData < taosArrayGetSize(pBlockData->aIdx); iColData++) {
    SColData *pColData = tBlockDataGetColDataByIdx(pBlockData, iColData);

    while (pBlockCol && pBlockCol->cid < pColData->cid) {
      if (n < hdr.szBlkCol) {
        n += tGetBlockCol(pReader->aBuf[0] + n, pBlockCol);
      } else {
        ASSERT(n == hdr.szBlkCol);
        pBlockCol = NULL;
      }
    }

    if (pBlockCol == NULL || pBlockCol->cid > pColData->cid) {
      // add a lot of NONE
      for (int32_t iRow = 0; iRow < hdr.nRow; iRow++) {
        code = tColDataAppendValue(pColData, &COL_VAL_NONE(pColData->cid, pColData->type));
        if (code) goto _err;
      }
    } else {
      ASSERT(pBlockCol->type == pColData->type);
      ASSERT(pBlockCol->flag && pBlockCol->flag != HAS_NONE);

      if (pBlockCol->flag == HAS_NULL) {
        // add a lot of NULL
        for (int32_t iRow = 0; iRow < hdr.nRow; iRow++) {
          code = tColDataAppendValue(pColData, &COL_VAL_NULL(pBlockCol->cid, pBlockCol->type));
          if (code) goto _err;
        }
      } else {
        // decode from binary
        int64_t offset = pBlkInfo->offset + pBlkInfo->szKey + hdr.szBlkCol + pBlockCol->offset;
        int32_t size = pBlockCol->szBitmap + pBlockCol->szOffset + pBlockCol->szValue;

        code = tsdbReadFile(pFD, offset, pReader->aBuf[1], size);
        if (code) goto _err;

        code = tsdbDecmprColData(pReader->aBuf[1], pBlockCol, hdr.cmprAlg, hdr.nRow, pColData, &pReader->aBuf[2]);
        if (code) goto _err;
      }
    }
  }

_exit:
  return code;

_err:
  tsdbError("vgId:%d tsdb read block data impl failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadDataBlock(SDataFReader *pReader, SDataBlk *pDataBlk, SBlockData *pBlockData) {
  int32_t code = 0;

  code = tsdbReadBlockDataImpl(pReader, &pDataBlk->aSubBlock[0], pBlockData);
  if (code) goto _err;

  if (pDataBlk->nSubBlock > 1) {
    SBlockData bData1;
    SBlockData bData2;

    // create
    code = tBlockDataCreate(&bData1);
    if (code) goto _err;
    code = tBlockDataCreate(&bData2);
    if (code) goto _err;

    // init
    tBlockDataInitEx(&bData1, pBlockData);
    tBlockDataInitEx(&bData2, pBlockData);

    for (int32_t iSubBlock = 1; iSubBlock < pDataBlk->nSubBlock; iSubBlock++) {
      code = tsdbReadBlockDataImpl(pReader, &pDataBlk->aSubBlock[iSubBlock], &bData1);
      if (code) {
        tBlockDataDestroy(&bData1, 1);
        tBlockDataDestroy(&bData2, 1);
        goto _err;
      }

      code = tBlockDataCopy(pBlockData, &bData2);
      if (code) {
        tBlockDataDestroy(&bData1, 1);
        tBlockDataDestroy(&bData2, 1);
        goto _err;
      }

      code = tBlockDataMerge(&bData1, &bData2, pBlockData);
      if (code) {
        tBlockDataDestroy(&bData1, 1);
        tBlockDataDestroy(&bData2, 1);
        goto _err;
      }
    }

    tBlockDataDestroy(&bData1, 1);
    tBlockDataDestroy(&bData2, 1);
  }

  return code;

_err:
  tsdbError("vgId:%d tsdb read data block failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadSstBlock(SDataFReader *pReader, int32_t iSst, SSstBlk *pSstBlk, SBlockData *pBlockData) {
  int32_t code = 0;

  // alloc
  code = tRealloc(&pReader->aBuf[0], pSstBlk->bInfo.szBlock);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->aSstFD[iSst], pSstBlk->bInfo.offset, pReader->aBuf[0], pSstBlk->bInfo.szBlock);
  if (code) goto _err;

  // decmpr
  code = tDecmprBlockData(pReader->aBuf[0], pSstBlk->bInfo.szBlock, pBlockData, &pReader->aBuf[1]);
  if (code) goto _err;

  return code;

_err:
  tsdbError("vgId:%d tsdb read sst block failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

// SDelFWriter ====================================================
int32_t tsdbDelFWriterOpen(SDelFWriter **ppWriter, SDelFile *pFile, STsdb *pTsdb) {
  int32_t      code = 0;
  char         fname[TSDB_FILENAME_LEN];
  uint8_t      hdr[TSDB_FHDR_SIZE] = {0};
  SDelFWriter *pDelFWriter;
  int64_t      n;

  // alloc
  pDelFWriter = (SDelFWriter *)taosMemoryCalloc(1, sizeof(*pDelFWriter));
  if (pDelFWriter == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }
  pDelFWriter->pTsdb = pTsdb;
  pDelFWriter->fDel = *pFile;

  tsdbDelFileName(pTsdb, pFile, fname);
  code =
      tsdbOpenFile(fname, TSDB_DEFAULT_PAGE_SIZE, TD_FILE_READ | TD_FILE_WRITE | TD_FILE_CREATE, &pDelFWriter->pWriteH);
  if (code) goto _err;

  // update header
  code = tsdbWriteFile(pDelFWriter->pWriteH, 0, hdr, TSDB_FHDR_SIZE);
  if (code) goto _err;

  pDelFWriter->fDel.size = TSDB_FHDR_SIZE;
  pDelFWriter->fDel.offset = 0;

  *ppWriter = pDelFWriter;
  return code;

_err:
  tsdbError("vgId:%d, failed to open del file writer since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  *ppWriter = NULL;
  return code;
}

int32_t tsdbDelFWriterClose(SDelFWriter **ppWriter, int8_t sync) {
  int32_t      code = 0;
  SDelFWriter *pWriter = *ppWriter;
  STsdb       *pTsdb = pWriter->pTsdb;

  // sync
  if (sync) {
    code = tsdbFsyncFile(pWriter->pWriteH);
    if (code) goto _err;
  }

  // close
  tsdbCloseFile(&pWriter->pWriteH);

  for (int32_t iBuf = 0; iBuf < sizeof(pWriter->aBuf) / sizeof(uint8_t *); iBuf++) {
    tFree(pWriter->aBuf[iBuf]);
  }
  taosMemoryFree(pWriter);

  *ppWriter = NULL;
  return code;

_err:
  tsdbError("vgId:%d, failed to close del file writer since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbWriteDelData(SDelFWriter *pWriter, SArray *aDelData, SDelIdx *pDelIdx) {
  int32_t code = 0;
  int64_t size;
  int64_t n;

  // prepare
  size = 0;
  for (int32_t iDelData = 0; iDelData < taosArrayGetSize(aDelData); iDelData++) {
    size += tPutDelData(NULL, taosArrayGet(aDelData, iDelData));
  }

  // alloc
  code = tRealloc(&pWriter->aBuf[0], size);
  if (code) goto _err;

  // build
  n = 0;
  for (int32_t iDelData = 0; iDelData < taosArrayGetSize(aDelData); iDelData++) {
    n += tPutDelData(pWriter->aBuf[0] + n, taosArrayGet(aDelData, iDelData));
  }
  ASSERT(n == size);

  // write
  code = tsdbWriteFile(pWriter->pWriteH, pWriter->fDel.size, pWriter->aBuf[0], size);
  if (code) goto _err;

  // update
  pDelIdx->offset = pWriter->fDel.size;
  pDelIdx->size = size;
  pWriter->fDel.size += size;

  return code;

_err:
  tsdbError("vgId:%d, failed to write del data since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbWriteDelIdx(SDelFWriter *pWriter, SArray *aDelIdx) {
  int32_t  code = 0;
  int64_t  size;
  int64_t  n;
  SDelIdx *pDelIdx;

  // prepare
  size = 0;
  for (int32_t iDelIdx = 0; iDelIdx < taosArrayGetSize(aDelIdx); iDelIdx++) {
    size += tPutDelIdx(NULL, taosArrayGet(aDelIdx, iDelIdx));
  }

  // alloc
  code = tRealloc(&pWriter->aBuf[0], size);
  if (code) goto _err;

  // build
  n = 0;
  for (int32_t iDelIdx = 0; iDelIdx < taosArrayGetSize(aDelIdx); iDelIdx++) {
    n += tPutDelIdx(pWriter->aBuf[0] + n, taosArrayGet(aDelIdx, iDelIdx));
  }
  ASSERT(n == size);

  // write
  code = tsdbWriteFile(pWriter->pWriteH, pWriter->fDel.size, pWriter->aBuf[0], size);
  if (code) goto _err;

  // update
  pWriter->fDel.offset = pWriter->fDel.size;
  pWriter->fDel.size += size;

  return code;

_err:
  tsdbError("vgId:%d, write del idx failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbUpdateDelFileHdr(SDelFWriter *pWriter) {
  int32_t code = 0;
  char    hdr[TSDB_FHDR_SIZE] = {0};
  int64_t size = TSDB_FHDR_SIZE;
  int64_t n;

  // build
  tPutDelFile(hdr, &pWriter->fDel);

  // write
  code = tsdbWriteFile(pWriter->pWriteH, 0, hdr, size);
  if (code) goto _err;

  return code;

_err:
  tsdbError("vgId:%d, update del file hdr failed since %s", TD_VID(pWriter->pTsdb->pVnode), tstrerror(code));
  return code;
}
// SDelFReader ====================================================
struct SDelFReader {
  STsdb   *pTsdb;
  SDelFile fDel;
  STsdbFD *pReadH;
  uint8_t *aBuf[1];
};

int32_t tsdbDelFReaderOpen(SDelFReader **ppReader, SDelFile *pFile, STsdb *pTsdb) {
  int32_t      code = 0;
  char         fname[TSDB_FILENAME_LEN];
  SDelFReader *pDelFReader;
  int64_t      n;

  // alloc
  pDelFReader = (SDelFReader *)taosMemoryCalloc(1, sizeof(*pDelFReader));
  if (pDelFReader == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }

  // open impl
  pDelFReader->pTsdb = pTsdb;
  pDelFReader->fDel = *pFile;

  tsdbDelFileName(pTsdb, pFile, fname);
  code = tsdbOpenFile(fname, TSDB_DEFAULT_PAGE_SIZE, TD_FILE_READ, &pDelFReader->pReadH);
  if (code) goto _err;

  *ppReader = pDelFReader;
  return code;

_err:
  tsdbError("vgId:%d, del file reader open failed since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
  *ppReader = NULL;
  return code;
}

int32_t tsdbDelFReaderClose(SDelFReader **ppReader) {
  int32_t      code = 0;
  SDelFReader *pReader = *ppReader;

  if (pReader) {
    tsdbCloseFile(&pReader->pReadH);
    for (int32_t iBuf = 0; iBuf < sizeof(pReader->aBuf) / sizeof(uint8_t *); iBuf++) {
      tFree(pReader->aBuf[iBuf]);
    }
    taosMemoryFree(pReader);
  }
  *ppReader = NULL;

_exit:
  return code;
}

int32_t tsdbReadDelData(SDelFReader *pReader, SDelIdx *pDelIdx, SArray *aDelData) {
  int32_t code = 0;
  int64_t offset = pDelIdx->offset;
  int64_t size = pDelIdx->size;
  int64_t n;

  taosArrayClear(aDelData);

  // alloc
  code = tRealloc(&pReader->aBuf[0], size);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->pReadH, offset, pReader->aBuf[0], size);
  if (code) goto _err;

  // // decode
  n = 0;
  while (n < size) {
    SDelData delData;
    n += tGetDelData(pReader->aBuf[0] + n, &delData);

    if (taosArrayPush(aDelData, &delData) == NULL) {
      code = TSDB_CODE_OUT_OF_MEMORY;
      goto _err;
    }
  }
  ASSERT(n == size);

  return code;

_err:
  tsdbError("vgId:%d, read del data failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}

int32_t tsdbReadDelIdx(SDelFReader *pReader, SArray *aDelIdx) {
  int32_t code = 0;
  int32_t n;
  int64_t offset = pReader->fDel.offset;
  int64_t size = pReader->fDel.size - offset;

  taosArrayClear(aDelIdx);

  // alloc
  code = tRealloc(&pReader->aBuf[0], size);
  if (code) goto _err;

  // read
  code = tsdbReadFile(pReader->pReadH, offset, pReader->aBuf[0], size);
  if (code) goto _err;

  // decode
  n = 0;
  while (n < size) {
    SDelIdx delIdx;

    n += tGetDelIdx(pReader->aBuf[0] + n, &delIdx);

    if (taosArrayPush(aDelIdx, &delIdx) == NULL) {
      code = TSDB_CODE_OUT_OF_MEMORY;
      goto _err;
    }
  }

  ASSERT(n == size);

  return code;

_err:
  tsdbError("vgId:%d, read del idx failed since %s", TD_VID(pReader->pTsdb->pVnode), tstrerror(code));
  return code;
}