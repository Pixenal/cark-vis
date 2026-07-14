#define STUC_WINDOW_BITS 31 //15 (+16 as using gzip)

#include <zlib.h>

#include <cark_vis.h>

typedef int8_t I8;
typedef int32_t I32;
typedef uint32_t U32;
typedef int64_t I64;

static const I8 typeSizeArr[] = {
	0,
	8,
	8,
	16,
	16,
	32,
	32,
	64,
	64,
	32,
	64
};

PixErr carkInit(
	const PixalcFPtrs *pAlloc,
	const PixioFPtrs *pIo,
	I32 threadCount,
	CarkCtx *pCtx
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT(
		"",
		sizeof(typeSizeArr) / sizeof(typeSizeArr[0]) == CARK_TYPE_ENUM_COUNT
	);
	*pCtx = (CarkCtx) {
		.alloc = *pAlloc,
		.io = *pIo,
		.threadCount = threadCount,
		.pThreadArr = pAlloc->fpCalloc(threadCount, sizeof(CarkThread))
	};
	pixalcLinAllocInit(pAlloc, &pCtx->compAlloc, sizeof(CarkCompInfo), 8, false);
	pixalcLinAllocInit(pAlloc, &pCtx->structAlloc, sizeof(CarkStruct), 4, false);
	return err;
}

static
PixErr nameCpy(char *pDest, const char *pSrc) {
	PixErr err = PIX_ERR_SUCCESS;
	I32 nameLen = strnlen(pSrc, CARK_NAME_LEN_MAX + 1);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		nameLen > 0 && nameLen < CARK_NAME_LEN_MAX + 1,
		"name length is invalid"
	);
	memcpy(pDest, pSrc, nameLen);
	return err;
}

PixErr carkStageInit(
	CarkCtx *pCtx,
	const char *pName,
	const CarkStructInfoArr *pStructArr,
	CarkStage *pHandle
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pStructArr->size > 0,
		"stage must have at least one struct"
	);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pCtx && pName && pStructArr && pHandle,
		"one or more args are null"
	);

	*pHandle = (CarkStage){.structCount = pStructArr->size};
	pixalcLinAlloc(&pCtx->structAlloc, &pHandle->pStructArr, pStructArr->size);
	for (I32 i = 0; i < pStructArr->size; ++i) {
		CarkStruct *pStruct = pHandle->pStructArr + i;
		*pStruct = (CarkStruct){pStruct->info = pStructArr->pArr[i]};
		pixalcLinAlloc(&pCtx->compAlloc, &pStruct->info.pCompArr, pStruct->info.compCount);
		for (I32 j = 0; j < pStruct->info.compCount; ++j) {
			const CarkCompInfo *pComp = pStructArr->pArr[i].pCompArr + j;
			pStruct->byteSize += typeSizeArr[pComp->type] / 8;
			pStruct->info.pCompArr[j] = *pComp;
		}
	}

	err = nameCpy(pHandle->name, pName);
	PIX_ERR_RETURN_IFNOT(err, "failed to copy name");
	I32 structArrByteSize = CARK_CACHELINE_SIZE + sizeof(CarkStructLog) * pStructArr->size;
	pHandle->pStructMem = pCtx->alloc.fpCalloc(pCtx->threadCount, structArrByteSize);
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		//stage-arr is not written to by threads, so no padding, unlike struct-arr
		//TODO rename stageArr & structArr to include 'log', to avoid confusion
		PIXALC_DYN_ARR_RESIZE(
			CarkStageLog,
			&pCtx->alloc,
			&pCtx->pThreadArr[i].stageArr,
			pCtx->stageCount ? pCtx->stageCount + 1 : 4
		);
		CarkStageLog *pStageLog = pCtx->pThreadArr[i].stageArr.pArr + pCtx->stageCount;
		*pStageLog = (CarkStageLog) {
			.stage = pCtx->stageCount,
			.structCount = pStructArr->size,
			.pStructArr = (CarkStructLog *)(pHandle->pStructMem + structArrByteSize * i)
		};
	}
	++pCtx->stageCount;
	return err;
}

PixErr carkLogStart(
	CarkCtx *pCtx,
	I32 thread,
	const CarkStage *pStage,
	I32 structIdx,
	I32 idx,
	CarkLog *pLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pStage->idx >= 0 && pStage->idx < pCtx->stageCount,
		"invalid stage"
	);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		thread >= 0 && thread < pCtx->threadCount,
		"invalid thread"
	);
	CarkThread *pThread = pCtx->pThreadArr + thread;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		!pThread->activeLog,
		"logging already active for this thead, was log-end called?"
	);
	PIX_ERR_ASSERT("invalid state", pThread->activeLogStage == -1);
	pThread->activeLog = true;
	pThread->activeLogStage = pStage->idx;
	*pLog = (CarkLog){
		.pCtx = pCtx,
		.pStage = pStage,
		.structIdx = structIdx,
		.thread = thread
	};
	CarkStageLog *pStageLog = pThread->stageArr.pArr + pLog->pStage->idx;
	CarkStructLog *pStructLog = pStageLog->pStructArr + structIdx;
	I32 timestamp = 0;//TODO get actual time
	pixioByteArrWrite(&pLog->pCtx->alloc, &pStructLog->data, &idx, sizeof(I32));
	pixioByteArrWrite(&pLog->pCtx->alloc, &pStructLog->data, &timestamp, sizeof(I32));
	return err;
}

PixErr carkLogComp(CarkLog *pLog, I32 compIdx, void *pVal) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkThread *pThread = pLog->pCtx->pThreadArr + pLog->thread;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pThread->activeLog,
		"logging is not active. was log-start called?"
	);
	PIX_ERR_ASSERT("invalid state", pThread->activeLogStage == pLog->pStage->idx);
	const CarkStructInfo *pStructInfo = &pLog->pStage->pStructArr[pLog->structIdx].info;
	const CarkCompInfo *pComp = pStructInfo->pCompArr + compIdx;
	CarkStructLog *pStructLog =
		pThread->stageArr.pArr[pLog->pStage->idx].pStructArr + pLog->structIdx;
	PIX_ERR_ASSERT(
		"invalid component type",
		pComp->type > CARK_TYPE_NONE && pComp->type < CARK_TYPE_ENUM_COUNT
	);
	I32 compByteSize = typeSizeArr[pComp->type];
	pixioByteArrWrite(&pLog->pCtx->alloc, &pStructLog->data, pVal, compByteSize);
	++pLog->compCount;
	return err;
}

PixErr carkLogEnd(CarkLog *pLog) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkThread *pThread = pLog->pCtx->pThreadArr + pLog->thread;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pThread->activeLog,
		"log-end called, but logging is not active. was log-start called?"
	);
	PIX_ERR_ASSERT("invalid state", pThread->activeLogStage == pLog->pStage->idx);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pLog->compCount == pLog->pStage->pStructArr[pLog->structIdx].info.compCount,
		"all components in a struct must be logged"
	);
	++pThread->stageArr.pArr[pLog->pStage->idx].pStructArr[pLog->structIdx].count;
	pThread->activeLog = false;
	pThread->activeLogStage = -1;
	return err;
}

typedef enum Compare {
	STUC_COMPARE_LESS,
	STUC_COMPARE_EQUAL,
	STUC_COMPARE_GREAT
} Compare;

static
void insertionSort(
	I32 *pIdxArr,
	I32 count,
	const void *pData,
	Compare (*fpCmp)(const void *, I32, I32)
) {
	bool order = fpCmp(pData, 0, 1) == STUC_COMPARE_LESS;
	pIdxArr[0] = !order;
	pIdxArr[1] = order;
	I32 bufSize = 2;
	for (I32 i = bufSize; i < count; ++i) {
		bool insert = false;
		I32 j;
		for (j = bufSize - 1; j >= 0; --j) {
			insert =
				fpCmp(pData, i, pIdxArr[j]) == STUC_COMPARE_LESS &&
				fpCmp(pData, i, pIdxArr[j - 1]) == STUC_COMPARE_GREAT;
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIdxArr[bufSize] = i;
		}
		else {
			for (I32 m = bufSize; m > j; --m) {
				pIdxArr[m] = pIdxArr[m - 1];
				PIX_ERR_ASSERT("", m <= bufSize && m > j);
			}
			pIdxArr[j] = i;
		}
		bufSize++;
	}
}

static
I32 structCountGet(
	const CarkCtx *pCtx,
	const CarkStage *pStage,
	I32 structIdx
) {
	I32 total = 0;
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		const CarkStructLog *pStructLog =
			pCtx->pThreadArr[i].stageArr.pArr[pStage->idx].pStructArr + structIdx;
		total += pStructLog->count;
	}
	return total;
}

static
I32 structByteSize(const CarkStruct *pStruct) {
	return sizeof(I32) * 2 + pStruct->byteSize;
}

static
const void *structDataGet(
	const CarkCtx *pCtx,
	const CarkStage *pStage,
	I32 structIdx,
	I32 idx
) {
	PIX_ERR_ASSERT("", idx >= 0);
	I32 total = 0;
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		const CarkStructLog *pStructLog =
			pCtx->pThreadArr[i].stageArr.pArr[pStage->idx].pStructArr + structIdx;
		I32 idxLocal = idx - total;
		total += pStructLog->count;
		if (idx < total) {
			//idx + timestamp + struct components
			I64 byteSize = structByteSize(pStage->pStructArr + structIdx);
			return pStructLog->data.pArr + (I64)idxLocal * (I64)byteSize;
		}
	}
	PIX_ERR_ASSERT("idx out of bounds", false);
	return NULL;
}

typedef struct SortCtx {
	const CarkCtx *pCtx;
	const CarkStage *pStage;
	I32 structIdx;
} SortCtx;

static
Compare structIdxCmp(const void *pData, I32 aIdx, I32 bIdx) {
	const SortCtx *pSortCtx = pData;
	I32 structIdx = pSortCtx->structIdx;
	I32 a = *(I32 *)structDataGet(pSortCtx->pCtx, pSortCtx->pStage, structIdx, aIdx);
	I32 b = *(I32 *)structDataGet(pSortCtx->pCtx, pSortCtx->pStage, structIdx, bIdx);
	return b >= a ? STUC_COMPARE_GREAT : STUC_COMPARE_LESS;
}

static
void byteArrDestroy(const CarkCtx *pCtx, PixioByteArr *pArr) {
	if (pArr->pArr) {
		pCtx->alloc.fpFree(pArr->pArr);
	}
	*pArr = (PixioByteArr){0};
}

static
PixErr compileStructLogs(
	CarkCtx *pCtx,
	const CarkStage *pStage,
	I32 structIdx,
	I32 count,
	PixioByteArr *pStageData
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", count > 0);
	I32 *pIdxArrMem = pCtx->alloc.fpCalloc(count + 1, sizeof(I32));
	I32 *pIdxArr = pIdxArrMem + 1;
	insertionSort(
		pIdxArr,
		count,
		&(SortCtx){.pCtx = pCtx, .pStage = pStage, .structIdx = structIdx},
		structIdxCmp
	);
	I32 byteSize = structByteSize(pStage->pStructArr + structIdx);
	I64 byteTotal = (I64)count * (I64)byteSize;
	PIXALC_DYN_ARR_RESIZE(U8, &pCtx->alloc, pStageData, pStageData->size + byteTotal);
	for (I32 j = 0; j < count; ++j) {
		memcpy(
			pStageData->pArr + pStageData->byteIdx,
			structDataGet(pCtx, pStage, structIdx, pIdxArr[j]),
			byteSize
		);
	}
	return err;
}

static
void *mallocZlibWrap(void *pOpaque, U32 count, U32 typeSize) {
	return ((PixalcFPtrs *)pOpaque)->fpMalloc((I32)(count * typeSize));
}

static
void freeZlibWrap(void *pOpaque, void *pPtr) {
	((PixalcFPtrs *)pOpaque)->fpFree(pPtr);
}

static
PixErr checkZlibErr(I32 success, I32 zErr) {
	PixErr err = PIX_ERR_SUCCESS;
	if (zErr == success) {
		return err;
	}
	switch (zErr) {
		case Z_OK:
			PIX_ERR_RETURN(err, "zlib did not complete");
		case Z_MEM_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_MEM_ERROR");
		case Z_BUF_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_BUF_ERROR");
		case Z_DATA_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_DATA_ERROR");
		case Z_STREAM_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_STREAM_ERROR");
		default:
			PIX_ERR_RETURN(err, "zlib err");
	}
}

PixErr carkStageEnd(CarkCtx *pCtx, CarkStage *pStage, bool compress) {
	PixErr err = PIX_ERR_SUCCESS;
	PixioByteArr stageData = {0};
	for (I32 i = 0 ; i < pStage->structCount; ++i) {
		I32 structCount = structCountGet(pCtx, pStage, i);
		if (!structCount) {
			continue;
		}
		err = compileStructLogs(pCtx, pStage, i, structCount, &stageData);
		PIX_ERR_THROW_IFNOT(err, "", 1);
	}
	PIX_ERR_CATCH(1, err, ;);
	for (I32 i = 0 ; i < pStage->structCount; ++i) {
		for (I32 j = 0; j < pCtx->threadCount; ++j) {
			CarkStageLog *pStageLog = pCtx->pThreadArr[j].stageArr.pArr + pStage->idx;
			byteArrDestroy(pCtx, &pStageLog->pStructArr[i].data);
			pStageLog->pStructArr[i].count = 0;
		}
	}
	PIX_ERR_THROW_IFNOT(err, "", 0);

	//TODO compress all stages as one rolling block?
	//compress data
	pStage->bufStart = pCtx->outBuf.count;
	pStage->bufSize = stageData.byteIdx + (stageData.nextBitIdx > 0);
	z_stream zStream = {
		.zalloc = mallocZlibWrap,
		.zfree = freeZlibWrap,
		.opaque = (void *)&pCtx->alloc 
	};
	//using gzip with crc32
	err = checkZlibErr(
		Z_OK,
		deflateInit2(
			&zStream,
			compress ? Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION,
			Z_DEFLATED,
			STUC_WINDOW_BITS,
			8,
			Z_DEFAULT_STRATEGY
		)
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	zStream.avail_out = deflateBound(&zStream, (uLong)pStage->bufSize);
	PIXALC_DYN_ARR_RESIZE(
		U8,
		&pCtx->alloc,
		&pCtx->outBuf,
		pCtx->outBuf.count + zStream.avail_out
	);
	zStream.next_out = pCtx->outBuf.pArr + pCtx->outBuf.count;
	zStream.avail_in = (uInt)pStage->bufSize;
	zStream.next_in = stageData.pArr;
	err = checkZlibErr(Z_STREAM_END, deflate(&zStream, Z_FINISH));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = checkZlibErr(Z_OK, deflateEnd(&zStream));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pCtx->outBuf.count += zStream.total_out;
	pStage->bufCompressedSize = zStream.total_out;

	PIX_ERR_CATCH(0, err, ;);
	byteArrDestroy(pCtx, &stageData);
	return err;
}

PixErr carkSaveToFile(CarkCtx *pCtx, const char *pPath) {
	PixErr err = PIX_ERR_SUCCESS;
	PixioFile file = {0};
	err = pCtx->io.fpOpen(&file, pPath, 0);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	/*
	err = pCtx->io.fpWrite(pFile, &header.size, 4);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpWrite(pFile, header.pArr, (I32)header.size);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	*/
	err = pCtx->io.fpWrite(&file, pCtx->outBuf.pArr, pCtx->outBuf.count);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, ;);
	if (file.pFile) {
		err = pCtx->io.fpClose(&file);
	}
	return err;
}

void carkStageDestroy(CarkCtx *pCtx, CarkStage *pStage) {
	if (pStage->pStructMem) {
		pCtx->alloc.fpFree(pStage->pStructMem);
	}
	*pStage = (CarkStage){0};
}

void carkDestroy(CarkCtx *pCtx) {
	if (pCtx->outBuf.pArr) {
		pCtx->alloc.fpFree(pCtx->outBuf.pArr);
	}
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		for (I32 j = 0; j < pCtx->stageCount; ++j) {
			CarkStageLog *pStageLog = pCtx->pThreadArr[i].stageArr.pArr + j;
			for (I32 k = 0; k < pStageLog->structCount; k) {
				CarkStructLog *pStructLog = pStageLog->pStructArr + k;
				if (pStructLog->count) {
					byteArrDestroy(pCtx, &pStructLog->data);
				}
			}

		}
		if (pCtx->pThreadArr[i].stageArr.pArr) {
			pCtx->alloc.fpFree(pCtx->pThreadArr[i].stageArr.pArr);
		}
	}
	pCtx->alloc.fpFree(pCtx->pThreadArr);
	pixalcLinAllocDestroy(&pCtx->compAlloc);
	pixalcLinAllocDestroy(&pCtx->structAlloc);
	*pCtx = (CarkCtx){0};
}