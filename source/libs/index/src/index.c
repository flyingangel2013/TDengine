/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3 * or later ("AGPL"), as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "index.h"
#include "indexCache.h"
#include "indexComm.h"
#include "indexInt.h"
#include "indexTfile.h"
#include "indexUtil.h"
#include "tcoding.h"
#include "tdataformat.h"
#include "tdef.h"
#include "tref.h"
#include "tsched.h"

#define INDEX_NUM_OF_THREADS     5
#define INDEX_MAX_NUM_OF_THREADS 10

#define INDEX_QUEUE_SIZE 200

#define INDEX_DATA_BOOL_NULL      0x02
#define INDEX_DATA_TINYINT_NULL   0x80
#define INDEX_DATA_SMALLINT_NULL  0x8000
#define INDEX_DATA_INT_NULL       0x80000000LL
#define INDEX_DATA_BIGINT_NULL    0x8000000000000000LL
#define INDEX_DATA_TIMESTAMP_NULL TSDB_DATA_BIGINT_NULL

#define INDEX_DATA_FLOAT_NULL    0x7FF00000            // it is an NAN
#define INDEX_DATA_DOUBLE_NULL   0x7FFFFF0000000000LL  // an NAN
#define INDEX_DATA_NCHAR_NULL    0xFFFFFFFF
#define INDEX_DATA_BINARY_NULL   0xFF
#define INDEX_DATA_JSON_NULL     0xFFFFFFFF
#define INDEX_DATA_JSON_null     0xFFFFFFFE
#define INDEX_DATA_JSON_NOT_NULL 0x01

#define INDEX_DATA_UTINYINT_NULL  0xFF
#define INDEX_DATA_USMALLINT_NULL 0xFFFF
#define INDEX_DATA_UINT_NULL      0xFFFFFFFF
#define INDEX_DATA_UBIGINT_NULL   0xFFFFFFFFFFFFFFFFL

#define INDEX_DATA_NULL_STR   "NULL"
#define INDEX_DATA_NULL_STR_L "null"

void*   indexQhandle = NULL;
int32_t indexRefMgt;

int32_t indexThreads = 5;

static void indexDestroy(void* sIdx);

void indexInit(int32_t threadNum) {
  indexThreads = threadNum;
  if (indexThreads <= 1) indexThreads = INDEX_NUM_OF_THREADS;
  if (indexThreads >= INDEX_MAX_NUM_OF_THREADS) indexThreads = INDEX_MAX_NUM_OF_THREADS;
}
void indexEnvInit() {
  // refactor later
  indexQhandle = taosInitScheduler(INDEX_QUEUE_SIZE, indexThreads, "index", NULL);
  indexRefMgt = taosOpenRef(1000, indexDestroy);
}
void indexCleanup() {
  // refacto later
  taosCleanUpScheduler(indexQhandle);
  taosMemoryFreeClear(indexQhandle);
  taosCloseRef(indexRefMgt);
}

typedef struct SIdxColInfo {
  int colId;  // generated by index internal
  int version;
} SIdxColInfo;

static TdThreadOnce isInit = PTHREAD_ONCE_INIT;
// static void           indexInit();
static int idxTermSearch(SIndex* sIdx, SIndexTermQuery* term, SArray** result);

static void    idxInterRsltDestroy(SArray* results);
static int32_t idxMergeFinalResults(SArray* in, EIndexOperatorType oType, SArray* out);

static int32_t idxGenTFile(SIndex* index, IndexCache* cache, SArray* batch);

// merge cache and tfile by opera type
static void idxMergeCacheAndTFile(SArray* result, IterateValue* icache, IterateValue* iTfv, SIdxTRslt* helper);

// static int32_t indexSerialTermKey(SIndexTerm* itm, char* buf);
// int32_t        indexSerialKey(ICacheKey* key, char* buf);

static void idxPost(void* idx) {
  SIndex* pIdx = idx;
  (void)tsem_post(&pIdx->sem);
}
static void indexWait(void* idx) {
  SIndex* pIdx = idx;
  (void)tsem_wait(&pIdx->sem);
}

int32_t indexOpen(SIndexOpts* opts, const char* path, SIndex** index) {
  (void)taosThreadOnce(&isInit, indexEnvInit);

  int     code = TSDB_CODE_SUCCESS;
  SIndex* idx = taosMemoryCalloc(1, sizeof(SIndex));
  if (idx == NULL) {
    TAOS_CHECK_GOTO(TSDB_CODE_OUT_OF_MEMORY, NULL, END);
  }

  idx->lru = taosLRUCacheInit(opts->cacheSize, -1, .5);
  if (idx->lru == NULL) {
    TAOS_CHECK_GOTO(TSDB_CODE_OUT_OF_MEMORY, NULL, END);
  }
  taosLRUCacheSetStrictCapacity(idx->lru, false);

  idx->tindex = idxTFileCreate(idx, path);
  if (idx->tindex == NULL) {
    TAOS_CHECK_GOTO(TSDB_CODE_OUT_OF_MEMORY, NULL, END);
  }

  idx->colObj = taosHashInit(8, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BINARY), true, HASH_ENTRY_LOCK);
  if (idx->colObj == NULL) {
    TAOS_CHECK_GOTO(TSDB_CODE_OUT_OF_MEMORY, NULL, END);
  }

  idx->version = 1;
  idx->path = taosStrdup(path);
  if (idx->path == NULL) {
    TAOS_CHECK_GOTO(TSDB_CODE_OUT_OF_MEMORY, NULL, END);
  }

  (void)taosThreadMutexInit(&idx->mtx, NULL);
  (void)tsem_init(&idx->sem, 0, 0);

  idx->refId = idxAddRef(idx);
  idx->opts = *opts;
  idxAcquireRef(idx->refId);

  *index = idx;
  return code;

END:
  if (idx != NULL) {
    indexDestroy(idx);
  }
  *index = NULL;
  return code;
}

void indexDestroy(void* handle) {
  if (handle == NULL) return;
  SIndex* idx = handle;
  (void)taosThreadMutexDestroy(&idx->mtx);
  (void)tsem_destroy(&idx->sem);
  idxTFileDestroy(idx->tindex);
  taosMemoryFree(idx->path);

  SLRUCache* lru = idx->lru;
  if (lru != NULL) {
    taosLRUCacheEraseUnrefEntries(lru);
    taosLRUCacheCleanup(lru);
  }
  idx->lru = NULL;
  taosMemoryFree(idx);
  return;
}
void indexClose(SIndex* sIdx) {
  bool ref = 0;
  if (sIdx->colObj != NULL) {
    void* iter = taosHashIterate(sIdx->colObj, NULL);
    while (iter) {
      IndexCache** pCache = iter;
      idxCacheForceToMerge((void*)(*pCache));
      indexInfo("%s wait to merge", (*pCache)->colName);
      indexWait((void*)(sIdx));
      indexInfo("%s finish to wait", (*pCache)->colName);
      iter = taosHashIterate(sIdx->colObj, iter);
      idxCacheUnRef(*pCache);
    }
    taosHashCleanup(sIdx->colObj);
    sIdx->colObj = NULL;
  }

  idxReleaseRef(sIdx->refId);
  (void)idxRemoveRef(sIdx->refId);
}
int64_t idxAddRef(void* p) {
  // impl
  return taosAddRef(indexRefMgt, p);
}
int32_t idxRemoveRef(int64_t ref) {
  // impl later
  return taosRemoveRef(indexRefMgt, ref);
}

void idxAcquireRef(int64_t ref) {
  // impl
  (void)taosAcquireRef(indexRefMgt, ref);
}
void idxReleaseRef(int64_t ref) {
  // impl
  (void)taosReleaseRef(indexRefMgt, ref);
}

int32_t indexPut(SIndex* index, SIndexMultiTerm* fVals, uint64_t uid) {
  // TODO(yihao): reduce the lock range
  (void)taosThreadMutexLock(&index->mtx);
  for (int i = 0; i < taosArrayGetSize(fVals); i++) {
    SIndexTerm* p = taosArrayGetP(fVals, i);

    char      buf[128] = {0};
    ICacheKey key = {.suid = p->suid, .colName = p->colName, .nColName = strlen(p->colName), .colType = p->colType};
    int32_t   sz = idxSerialCacheKey(&key, buf);

    IndexCache** cache = taosHashGet(index->colObj, buf, sz);
    if (cache == NULL) {
      IndexCache* pCache = idxCacheCreate(index, p->suid, p->colName, p->colType);
      (void)taosHashPut(index->colObj, buf, sz, &pCache, sizeof(void*));
    }
  }
  (void)taosThreadMutexUnlock(&index->mtx);

  for (int i = 0; i < taosArrayGetSize(fVals); i++) {
    SIndexTerm* p = taosArrayGetP(fVals, i);

    char      buf[128] = {0};
    ICacheKey key = {.suid = p->suid, .colName = p->colName, .nColName = strlen(p->colName), .colType = p->colType};
    int32_t   sz = idxSerialCacheKey(&key, buf);
    indexDebug("w suid:%" PRIu64 ", colName:%s, colType:%d", key.suid, key.colName, key.colType);

    IndexCache** cache = taosHashGet(index->colObj, buf, sz);
    ASSERTS(*cache != NULL, "index-cache already release");
    if (*cache == NULL) return -1;

    int ret = idxCachePut(*cache, p, uid);
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}
int32_t indexSearch(SIndex* index, SIndexMultiTermQuery* multiQuerys, SArray* result) {
  EIndexOperatorType opera = multiQuerys->opera;  // relation of querys

  SArray* iRslts = taosArrayInit(4, POINTER_BYTES);
  int     nQuery = taosArrayGetSize(multiQuerys->query);
  for (size_t i = 0; i < nQuery; i++) {
    SIndexTermQuery* qterm = taosArrayGet(multiQuerys->query, i);
    SArray*          trslt = NULL;
    (void)idxTermSearch(index, qterm, &trslt);
    (void)taosArrayPush(iRslts, (void*)&trslt);
  }
  (void)idxMergeFinalResults(iRslts, opera, result);
  idxInterRsltDestroy(iRslts);
  return 0;
}

int indexDelete(SIndex* index, SIndexMultiTermQuery* query) { return 1; }
// int indexRebuild(SIndex* index, SIndexOpts* opts) { return 0; }

SIndexOpts* indexOptsCreate(int32_t cacheSize) {
  SIndexOpts* opts = taosMemoryCalloc(1, sizeof(SIndexOpts));
  opts->cacheSize = cacheSize;
  return opts;
}
void indexOptsDestroy(SIndexOpts* opts) { return taosMemoryFree(opts); }
/*
 * @param: oper
 *
 */
SIndexMultiTermQuery* indexMultiTermQueryCreate(EIndexOperatorType opera) {
  SIndexMultiTermQuery* mtq = (SIndexMultiTermQuery*)taosMemoryMalloc(sizeof(SIndexMultiTermQuery));
  if (mtq == NULL) {
    return NULL;
  }
  mtq->opera = opera;
  mtq->query = taosArrayInit(4, sizeof(SIndexTermQuery));
  return mtq;
}
void indexMultiTermQueryDestroy(SIndexMultiTermQuery* pQuery) {
  for (int i = 0; i < taosArrayGetSize(pQuery->query); i++) {
    SIndexTermQuery* p = (SIndexTermQuery*)taosArrayGet(pQuery->query, i);
    indexTermDestroy(p->term);
  }
  taosArrayDestroy(pQuery->query);
  taosMemoryFree(pQuery);
};
int32_t indexMultiTermQueryAdd(SIndexMultiTermQuery* pQuery, SIndexTerm* term, EIndexQueryType qType) {
  SIndexTermQuery q = {.qType = qType, .term = term};
  (void)taosArrayPush(pQuery->query, &q);
  return 0;
}

SIndexTerm* indexTermCreate(int64_t suid, SIndexOperOnColumn oper, uint8_t colType, const char* colName,
                            int32_t nColName, const char* colVal, int32_t nColVal) {
  SIndexTerm* tm = (SIndexTerm*)taosMemoryCalloc(1, (sizeof(SIndexTerm)));
  if (tm == NULL) {
    return NULL;
  }

  tm->suid = suid;
  tm->operType = oper;
  tm->colType = colType;

  tm->colName = (char*)taosMemoryCalloc(1, nColName + 1);
  memcpy(tm->colName, colName, nColName);
  tm->nColName = nColName;

  char*   buf = NULL;
  int32_t len = 0;
  if (colVal != NULL && nColVal != 0) {
    len = idxConvertDataToStr((void*)colVal, IDX_TYPE_GET_TYPE(colType), (void**)&buf);
  } else if (colVal == NULL) {
    buf = strndup(INDEX_DATA_NULL_STR, (int32_t)strlen(INDEX_DATA_NULL_STR));
    len = (int32_t)strlen(INDEX_DATA_NULL_STR);
  } else {
    static const char* emptyStr = " ";
    buf = strndup(emptyStr, (int32_t)strlen(emptyStr));
    len = (int32_t)strlen(emptyStr);
  }
  tm->colVal = buf;
  tm->nColVal = len;

  return tm;
}

void indexTermDestroy(SIndexTerm* p) {
  taosMemoryFree(p->colName);
  taosMemoryFree(p->colVal);
  taosMemoryFree(p);
}

SIndexMultiTerm* indexMultiTermCreate() { return taosArrayInit(4, sizeof(SIndexTerm*)); }

int32_t indexMultiTermAdd(SIndexMultiTerm* terms, SIndexTerm* term) {
  (void)taosArrayPush(terms, &term);
  return 0;
}
void indexMultiTermDestroy(SIndexMultiTerm* terms) {
  for (int32_t i = 0; i < taosArrayGetSize(terms); i++) {
    SIndexTerm* p = taosArrayGetP(terms, i);
    indexTermDestroy(p);
  }
  taosArrayDestroy(terms);
}

/*
 * rebuild index
 */

static void idxSchedRebuildIdx(SSchedMsg* msg) {
  // TODO, no need rebuild index
  SIndex* idx = msg->ahandle;

  int8_t st = kFinished;
  atomic_store_8(&idx->status, st);
  idxReleaseRef(idx->refId);
}
void indexRebuild(SIndexJson* idx, void* iter) {
  // set up rebuild status
  int8_t st = kRebuild;
  atomic_store_8(&idx->status, st);

  // task put into BG thread
  SSchedMsg schedMsg = {0};
  schedMsg.fp = idxSchedRebuildIdx;
  schedMsg.ahandle = idx;
  idxAcquireRef(idx->refId);
  (void)taosScheduleTask(indexQhandle, &schedMsg);
}

/*
 * check index json status
 **/
bool indexIsRebuild(SIndex* idx) {
  // idx rebuild or not
  return ((SIdxStatus)atomic_load_8(&idx->status)) == kRebuild ? true : false;
}
/*
 * rebuild index
 */
void indexJsonRebuild(SIndexJson* idx, void* iter) {
  // idx rebuild or not
  indexRebuild(idx, iter);
}

/*
 * check index json status
 **/
bool indexJsonIsRebuild(SIndexJson* idx) {
  // load idx rebuild or not
  return ((SIdxStatus)atomic_load_8(&idx->status)) == kRebuild ? true : false;
}

static int32_t idxTermSearch(SIndex* sIdx, SIndexTermQuery* query, SArray** result) {
  SIndexTerm* term = query->term;
  const char* colName = term->colName;
  int32_t     nColName = term->nColName;

  // Get col info
  IndexCache* cache = NULL;

  char      buf[128] = {0};
  ICacheKey key = {
      .suid = term->suid, .colName = term->colName, .nColName = strlen(term->colName), .colType = term->colType};
  indexDebug("r suid:%" PRIu64 ", colName:%s, colType:%d", key.suid, key.colName, key.colType);

  int32_t sz = idxSerialCacheKey(&key, buf);

  (void)taosThreadMutexLock(&sIdx->mtx);
  IndexCache** pCache = taosHashGet(sIdx->colObj, buf, sz);
  cache = (pCache == NULL) ? NULL : *pCache;
  (void)taosThreadMutexUnlock(&sIdx->mtx);

  *result = taosArrayInit(4, sizeof(uint64_t));
  if (*result == NULL) {
    return TSDB_CODE_OUT_OF_MEMORY;
  }
  // TODO: iterator mem and tidex

  STermValueType s = kTypeValue;

  int64_t st = taosGetTimestampUs();

  SIdxTRslt* tr = idxTRsltCreate();
  if (0 == idxCacheSearch(cache, query, tr, &s)) {
    if (s == kTypeDeletion) {
      indexInfo("col: %s already drop by", term->colName);
      // coloum already drop by other oper, no need to query tindex
      return 0;
    } else {
      st = taosGetTimestampUs();
      if (0 != idxTFileSearch(sIdx->tindex, query, tr)) {
        indexError("corrupt at index(TFile) col:%s val: %s", term->colName, term->colVal);
        goto END;
      }
      int64_t tfCost = taosGetTimestampUs() - st;
      indexInfo("tfile search cost: %" PRIu64 "us", tfCost);
    }
  } else {
    indexError("corrupt at index(cache) col:%s val: %s", term->colName, term->colVal);
    goto END;
  }
  int64_t cost = taosGetTimestampUs() - st;
  indexInfo("search cost: %" PRIu64 "us", cost);

  idxTRsltMergeTo(tr, *result);

  idxTRsltDestroy(tr);
  return 0;
END:
  idxTRsltDestroy(tr);
  return 0;
}
static void idxInterRsltDestroy(SArray* results) {
  if (results == NULL) {
    return;
  }

  size_t sz = taosArrayGetSize(results);
  for (size_t i = 0; i < sz; i++) {
    SArray* p = taosArrayGetP(results, i);
    taosArrayDestroy(p);
  }
  taosArrayDestroy(results);
}

static int32_t idxMergeFinalResults(SArray* in, EIndexOperatorType oType, SArray* out) {
  // refactor, merge interResults into fResults by oType
  for (int i = 0; i < taosArrayGetSize(in); i++) {
    SArray* t = taosArrayGetP(in, i);
    taosArraySort(t, uidCompare);
    taosArrayRemoveDuplicate(t, uidCompare, NULL);
  }

  if (oType == MUST) {
    iIntersection(in, out);
  } else if (oType == SHOULD) {
    iUnion(in, out);
  } else if (oType == NOT) {
    // just one column index, enhance later
    // taosArrayAddAll(fResults, interResults);
    // not use currently
  }
  return 0;
}

static void idxMayMergeTempToFinalRslt(SArray* result, TFileValue* tfv, SIdxTRslt* tr) {
  int32_t sz = taosArrayGetSize(result);
  if (sz > 0) {
    TFileValue* lv = taosArrayGetP(result, sz - 1);
    if (tfv != NULL && strcmp(lv->colVal, tfv->colVal) != 0) {
      idxTRsltMergeTo(tr, lv->tableId);
      idxTRsltClear(tr);

      (void)taosArrayPush(result, &tfv);
    } else if (tfv == NULL) {
      // handle last iterator
      idxTRsltMergeTo(tr, lv->tableId);
    } else {
      tfileValueDestroy(tfv);
    }
  } else {
    (void)taosArrayPush(result, &tfv);
  }
}
static void idxMergeCacheAndTFile(SArray* result, IterateValue* cv, IterateValue* tv, SIdxTRslt* tr) {
  char*       colVal = (cv != NULL) ? cv->colVal : tv->colVal;
  TFileValue* tfv = tfileValueCreate(colVal);

  idxMayMergeTempToFinalRslt(result, tfv, tr);

  if (cv != NULL) {
    uint64_t id = *(uint64_t*)taosArrayGet(cv->val, 0);
    uint32_t ver = cv->ver;
    if (cv->type == ADD_VALUE) {
      INDEX_MERGE_ADD_DEL(tr->del, tr->add, id)
    } else if (cv->type == DEL_VALUE) {
      INDEX_MERGE_ADD_DEL(tr->add, tr->del, id)
    }
  }
  if (tv != NULL) {
    (void)taosArrayAddAll(tr->total, tv->val);
  }
}
static void idxDestroyFinalRslt(SArray* result) {
  int32_t sz = result ? taosArrayGetSize(result) : 0;
  for (size_t i = 0; i < sz; i++) {
    TFileValue* tv = taosArrayGetP(result, i);
    tfileValueDestroy(tv);
  }
  taosArrayDestroy(result);
}

int32_t idxFlushCacheToTFile(SIndex* sIdx, void* cache, bool quit) {
  if (sIdx == NULL) {
    return TSDB_CODE_INVALID_PTR;
  }
  indexInfo("suid %" PRIu64 " merge cache into tindex", sIdx->suid);

  int64_t st = taosGetTimestampUs();

  IndexCache* pCache = (IndexCache*)cache;

  do {
  } while (quit && atomic_load_32(&pCache->merging) == 1);

  TFileReader* pReader = tfileGetReaderByCol(sIdx->tindex, pCache->suid, pCache->colName);
  if (pReader == NULL) {
    indexWarn("empty tfile reader found");
  }
  // handle flush
  Iterate* cacheIter = idxCacheIteratorCreate(pCache);
  if (cacheIter == NULL) {
    indexError("%p immtable is empty, ignore merge opera", pCache);
    idxCacheDestroyImm(pCache);
    tfileReaderUnRef(pReader);
    atomic_store_32(&pCache->merging, 0);
    if (quit) {
      idxPost(sIdx);
    }
    idxReleaseRef(sIdx->refId);
    return 0;
  }

  Iterate* tfileIter = tfileIteratorCreate(pReader);
  if (tfileIter == NULL) {
    indexWarn("empty tfile reader iterator");
  }

  SArray* result = taosArrayInit(1024, sizeof(void*));

  bool cn = cacheIter ? cacheIter->next(cacheIter) : false;
  bool tn = tfileIter ? tfileIter->next(tfileIter) : false;

  SIdxTRslt* tr = idxTRsltCreate();
  if (tr == NULL) {
  }
  while (cn == true || tn == true) {
    IterateValue* cv = (cn == true) ? cacheIter->getValue(cacheIter) : NULL;
    IterateValue* tv = (tn == true) ? tfileIter->getValue(tfileIter) : NULL;

    int comp = 0;
    if (cn == true && tn == true) {
      comp = strcmp(cv->colVal, tv->colVal);
    } else if (cn == true) {
      comp = -1;
    } else {
      comp = 1;
    }
    if (comp == 0) {
      idxMergeCacheAndTFile(result, cv, tv, tr);
      cn = cacheIter->next(cacheIter);
      tn = tfileIter->next(tfileIter);
    } else if (comp < 0) {
      idxMergeCacheAndTFile(result, cv, NULL, tr);
      cn = cacheIter->next(cacheIter);
    } else {
      idxMergeCacheAndTFile(result, NULL, tv, tr);
      tn = tfileIter->next(tfileIter);
    }
  }
  idxMayMergeTempToFinalRslt(result, NULL, tr);
  idxTRsltDestroy(tr);

  int ret = idxGenTFile(sIdx, pCache, result);
  if (ret != 0) {
    indexError("failed to merge");
  } else {
    int64_t cost = taosGetTimestampUs() - st;
    indexInfo("success to merge , time cost: %" PRId64 "ms", cost / 1000);
  }
  idxDestroyFinalRslt(result);

  idxCacheDestroyImm(pCache);

  idxCacheIteratorDestroy(cacheIter);
  tfileIteratorDestroy(tfileIter);

  tfileReaderUnRef(pReader);
  idxCacheUnRef(pCache);

  atomic_store_32(&pCache->merging, 0);
  if (quit) {
    idxPost(sIdx);
  }
  idxReleaseRef(sIdx->refId);

  return ret;
}
void iterateValueDestroy(IterateValue* value, bool destroy) {
  if (destroy) {
    taosArrayDestroy(value->val);
    value->val = NULL;
  } else {
    if (value->val != NULL) {
      taosArrayClear(value->val);
    }
  }
  taosMemoryFree(value->colVal);
  value->colVal = NULL;
}

static int64_t idxGetAvailableVer(SIndex* sIdx, IndexCache* cache) {
  ICacheKey key = {.suid = cache->suid, .colName = cache->colName, .nColName = strlen(cache->colName)};
  int64_t   ver = CACHE_VERSION(cache);

  IndexTFile* tf = (IndexTFile*)(sIdx->tindex);

  (void)taosThreadMutexLock(&tf->mtx);
  TFileReader* rd = tfileCacheGet(tf->cache, &key);
  (void)taosThreadMutexUnlock(&tf->mtx);

  if (rd != NULL) {
    ver = (ver > rd->header.version ? ver : rd->header.version) + 1;
    indexInfo("header: %" PRId64 ", ver: %" PRId64 "", rd->header.version, ver);
  }
  tfileReaderUnRef(rd);
  return ver;
}
static int32_t idxGenTFile(SIndex* sIdx, IndexCache* cache, SArray* batch) {
  int32_t code = 0;

  int64_t version = idxGetAvailableVer(sIdx, cache);
  indexInfo("file name version: %" PRId64 "", version);

  TFileWriter* tw = NULL;

  code = tfileWriterOpen(sIdx->path, cache->suid, version, cache->colName, cache->type, &tw);
  if (code != 0) {
    indexError("failed to open file to write since %s", tstrerror(code));
  }

  code = tfileWriterPut(tw, batch, true);
  if (code != 0) {
    indexError("failed to write into tindex since %s", tstrerror(code));
    goto END;
  }
  tfileWriterClose(tw);

  TFileReader* reader = NULL;
  code = tfileReaderOpen(sIdx, cache->suid, version, cache->colName, &reader);
  if (code != 0) {
    goto END;
  }
  indexInfo("success to create tfile, reopen it, %s", reader->ctx->file.buf);

  IndexTFile* tf = (IndexTFile*)sIdx->tindex;

  TFileHeader* header = &reader->header;
  ICacheKey    key = {.suid = cache->suid, .colName = header->colName, .nColName = strlen(header->colName)};

  (void)taosThreadMutexLock(&tf->mtx);
  code = tfileCachePut(tf->cache, &key, reader);
  (void)taosThreadMutexUnlock(&tf->mtx);

  return code;

END:
  if (tw != NULL) {
    idxFileCtxDestroy(tw->ctx, true);
    taosMemoryFree(tw);
  }
  return code;
}

int32_t idxSerialCacheKey(ICacheKey* key, char* buf) {
  bool hasJson = IDX_TYPE_CONTAIN_EXTERN_TYPE(key->colType, TSDB_DATA_TYPE_JSON);

  char* p = buf;
  char  tbuf[65] = {0};
  (void)idxInt2str((int64_t)key->suid, tbuf, 0);

  SERIALIZE_STR_VAR_TO_BUF(buf, tbuf, strlen(tbuf));
  SERIALIZE_VAR_TO_BUF(buf, '_', char);
  if (hasJson) {
    SERIALIZE_STR_VAR_TO_BUF(buf, JSON_COLUMN, strlen(JSON_COLUMN));
  } else {
    SERIALIZE_STR_MEM_TO_BUF(buf, key, colName, key->nColName);
  }
  return buf - p;
}
