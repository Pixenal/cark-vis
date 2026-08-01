#include <zlib.h>

#include <cark_vis_io.h>

typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef uint32_t U32;
typedef int64_t I64;
typedef float F32;

#define CARK_WINDOW_BITS 31 //15 (+16 as using gzip)
#define CARK_FILE_VERSION 100
#define CARK_FILE_VERSION_BYTE_SIZE 2

typedef enum FileSizeIdx {
	FILE_NONE,
	FILE_VERSION,
	FILE_HEADER_SIZE_DEFL,
	FILE_HEADER_SIZE_INFL,
	FILE_STAGE_COUNT,
	FILE_STRUCT_COUNT,
	FILE_COMP_COUNT,
	FILE_STAGE_BUF_START,
	FILE_STAGE_BUF_SIZE_DEFL,
	FILE_STAGE_BUF_SIZE_INFL,
	FILE_STAGE_STRUCT_COUNT,
	FILE_STRUCT_DESC,
	FILE_STRUCT_COMP_COUNT,
	FILE_COMP_DESC,
	FILE_COMP_TYPE,
	FILE_COMP_REF_COUNT,
	FILE_COMP_PRECURSOR_COUNT,
	FILE_REF_STAGE_IDX,
	FILE_REF_STRUCT_IDX,
	FILE_REF_COMP_IDX,
	FILE_LOG_STRUCT_COUNT,
	FILE_LOG_RANGE_TOTAL,
	FILE_LOG_DATA_TOTAL,
	FILE_LOG_STRUCT,
	FILE_LOG_COUNT,
	FILE_LOG_RANGE_COUNT,
	FILE_LOG_RANGE_STARTEND,
	FILE_LOG_IDX,
	FILE_LOG_TIMESTAMP,
	FILE_ENUM_COUNT
} FileSizeIdx;

static I8 fileSizeTable[FILE_ENUM_COUNT] = {0};

#define BITLEN(a) (fileSizeTable[FILE_##a])

static const char carkFormat[] = "Cark-Vis File";
#define CARK_TOP_HEADER_SIZE (sizeof(carkFormat) + BITLEN(VERSION) / 8)

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

static
void fileTypeSizeInit() {
	fileSizeTable[FILE_VERSION] = 16;
	fileSizeTable[FILE_HEADER_SIZE_DEFL] = 32;
	fileSizeTable[FILE_HEADER_SIZE_INFL] = 32;
	fileSizeTable[FILE_STAGE_COUNT] = 16;
	fileSizeTable[FILE_STRUCT_COUNT] = 32;
	fileSizeTable[FILE_COMP_COUNT] = 32;
	fileSizeTable[FILE_STAGE_BUF_START] = 64;
	fileSizeTable[FILE_STAGE_BUF_SIZE_DEFL] = 64;
	fileSizeTable[FILE_STAGE_BUF_SIZE_INFL] = 64;
	fileSizeTable[FILE_STAGE_STRUCT_COUNT] = 16;
	fileSizeTable[FILE_STRUCT_DESC] = 8;
	fileSizeTable[FILE_STRUCT_COMP_COUNT] = 16;
	fileSizeTable[FILE_COMP_DESC] = 8;
	fileSizeTable[FILE_COMP_TYPE] = 8;
	fileSizeTable[FILE_COMP_REF_COUNT] = 8;
	fileSizeTable[FILE_COMP_PRECURSOR_COUNT] = 8;
	fileSizeTable[FILE_REF_STAGE_IDX] = 16;
	fileSizeTable[FILE_REF_STRUCT_IDX] = 16;
	fileSizeTable[FILE_REF_COMP_IDX] = 16;
	fileSizeTable[FILE_LOG_STRUCT_COUNT] = 16;
	fileSizeTable[FILE_LOG_RANGE_TOTAL] = 32;
	fileSizeTable[FILE_LOG_DATA_TOTAL] = 64;
	fileSizeTable[FILE_LOG_STRUCT] = 16;
	fileSizeTable[FILE_LOG_COUNT] = 32;
	fileSizeTable[FILE_LOG_RANGE_COUNT] = 32;
	fileSizeTable[FILE_LOG_RANGE_STARTEND] = 32;
	fileSizeTable[FILE_LOG_IDX] = 32;
	fileSizeTable[FILE_LOG_TIMESTAMP] = 32;
}

PixErr carkOutInit(
	const PixalcFPtrs *pAlloc,
	const PixioFPtrs *pIo,
	I32 threadCount,
	CarkOutCtx *pCtx
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT(
		"",
		sizeof(typeSizeArr) / sizeof(typeSizeArr[0]) == CARK_TYPE_ENUM_COUNT
	);
	fileTypeSizeInit();

	*pCtx = (CarkOutCtx) {
		.alloc = *pAlloc,
		.io = *pIo,
		.threadCount = threadCount,
		.pThreadArr = pAlloc->fpCalloc(threadCount, sizeof(CarkThread))
	};
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		pCtx->pThreadArr[i].activeLogStage = -1;
	}
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

I32 carkTypeSizeGet(CarkType type) {
	return typeSizeArr[type] / 8;
}

static
I32 structSizeGet(const CarkStructInfo *pStruct) {
	I32 size = 0;
	for (I32 i = 0; i < pStruct->compCount; ++i) {
		size += typeSizeArr[pStruct->pCompArr[i].type] / 8;
	}
	return size;
}

PixErr carkOutStageInit(
	CarkOutCtx *pCtx,
	const char *pName,
	const CarkStructInfoArr *pStructArr,
	I32 *pHandle
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

	PIXALC_DYN_ARR_ADD(CarkStage, &pCtx->alloc, &pCtx->stageArr, *pHandle);
	CarkStage *pStage = pCtx->stageArr.pArr + *pHandle;
	*pStage = (CarkStage){.structCount = pStructArr->size};

	pixalcLinAlloc(&pCtx->structAlloc, &pStage->pStructArr, pStructArr->size);
	for (I32 i = 0; i < pStructArr->size; ++i) {
		CarkStruct *pStruct = pStage->pStructArr + i;
		*pStruct = (CarkStruct){pStruct->info = pStructArr->pArr[i]};
		pixalcLinAlloc(&pCtx->compAlloc, &pStruct->info.pCompArr, pStruct->info.compCount);
		pStruct->byteSize = structSizeGet(pStructArr->pArr + i);
		for (I32 j = 0; j < pStruct->info.compCount; ++j) {
			const CarkCompInfo *pComp = pStructArr->pArr[i].pCompArr + j;
			pStruct->info.pCompArr[j] = *pComp;
		}
	}
	err = nameCpy(pStage->name, pName);
	PIX_ERR_RETURN_IFNOT(err, "failed to copy name");
	I32 structArrByteSize = CARK_CACHELINE_SIZE + sizeof(CarkStructLog) * pStructArr->size;
	pStage->pStructMem = pCtx->alloc.fpCalloc(pCtx->threadCount, structArrByteSize);
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		//stage-arr is not written to by threads, so no padding, unlike struct-arr
		//TODO rename stageArr & structArr to include 'log', to avoid confusion
		PIXALC_DYN_ARR_RESIZE(
			CarkStageLog,
			&pCtx->alloc,
			&pCtx->pThreadArr[i].stageArr,
			*pHandle ? *pHandle : 4
		);
		CarkStageLog *pStageLog = pCtx->pThreadArr[i].stageArr.pArr + *pHandle;
		*pStageLog = (CarkStageLog) {
			.stage = *pHandle,
			.structCount = pStructArr->size,
			.pStructArr = (CarkStructLog *)(pStage->pStructMem + structArrByteSize * i)
		};
	}
	return err;
}

PixErr carkOutLogStart(
	CarkOutCtx *pCtx,
	I32 thread,
	I32 stageIdx,
	I32 structIdx,
	I32 idx,
	CarkLog *pLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	if (!pCtx->enabled) {
		*pLog = (CarkLog){.pCtx = pCtx, .enabled = pCtx->enabled};
		return err;
	}
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		stageIdx >= 0 && stageIdx < pCtx->stageArr.count,
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
	const CarkStage *pStage = pCtx->stageArr.pArr + stageIdx;
	pThread->activeLog = true;
	pThread->activeLogStage = pStage->idx;
	*pLog = (CarkLog){
		.pCtx = pCtx,
		.pStage = pStage,
		.structIdx = structIdx,
		.thread = thread,
		.enabled = pCtx->enabled
	};
	CarkStageLog *pStageLog = pThread->stageArr.pArr + pLog->pStage->idx;
	CarkStructLog *pStructLog = pStageLog->pStructArr + structIdx;
	I32 timestamp = 0;//TODO get actual time
	const PixalcFPtrs *pAlloc = &pLog->pCtx->alloc;
	pixioByteArrWrite(pAlloc, &pStructLog->data, &idx, BITLEN(LOG_IDX));
	pixioByteArrWrite(pAlloc, &pStructLog->data, &timestamp, BITLEN(LOG_TIMESTAMP));
	return err;
}

PixErr carkOutLogComp(CarkLog *pLog, I32 compIdx, void *pVal) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pLog->enabled == pLog->pCtx->enabled,
		"logging was enabled/disabled during log entry, or state is corrupt"
	);
	if (!pLog->enabled) {
		return err;
	}
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

PixErr carkOutLogEnd(CarkLog *pLog) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pLog->enabled == pLog->pCtx->enabled,
		"logging was enabled/disabled during log entry, or state is corrupt"
	);
	if (!pLog->enabled) {
		return err;
	}
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
	const CarkOutCtx *pCtx,
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
I32 structByteSize(const CarkStruct *pStruct, bool includeIdx) {
	return
		((includeIdx ? BITLEN(LOG_IDX) : 0) + BITLEN(LOG_TIMESTAMP)) / 8 +
		pStruct->byteSize;
}

static
const void *structDataGet(
	const CarkOutCtx *pCtx,
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
			I64 byteSize = structByteSize(pStage->pStructArr + structIdx, true);
			return pStructLog->data.pArr + (I64)idxLocal * (I64)byteSize;
		}
	}
	PIX_ERR_ASSERT("idx out of bounds", false);
	return NULL;
}

typedef struct SortCtx {
	const CarkOutCtx *pCtx;
	const CarkStage *pStage;
	I32 structIdx;
} SortCtx;

static
Compare structIdxCmp(const void *pData, I32 aIdx, I32 bIdx) {
	const SortCtx *pSortCtx = pData;
	I32 structIdx = pSortCtx->structIdx;
	I32 a = *(I32 *)structDataGet(pSortCtx->pCtx, pSortCtx->pStage, structIdx, aIdx);
	I32 b = *(I32 *)structDataGet(pSortCtx->pCtx, pSortCtx->pStage, structIdx, bIdx);
	return a >= b ? STUC_COMPARE_GREAT : STUC_COMPARE_LESS;
}

static
void byteArrDestroy(const CarkOutCtx *pCtx, PixioByteArr *pArr) {
	if (pArr->pArr) {
		pCtx->alloc.fpFree(pArr->pArr);
	}
	*pArr = (PixioByteArr){0};
}

static
PixtyRange *newRangeGet(const CarkOutCtx *pCtx, PixtyRangeArr *pRangeBuf) {
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(PixtyRange, &pCtx->alloc, pRangeBuf, newIdx);
	return pRangeBuf->pArr + newIdx;
}

static
PixErr compileStructLogs(
	CarkOutCtx *pCtx,
	const CarkStage *pStage,
	I32 structIdx,
	I32 count,
	PixioByteArr *pStageData,
	PixtyRangeArr *pRangeBuf,
	I64 *pDataTotal
) {
	PixErr err = PIX_ERR_SUCCESS;
	const PixalcFPtrs *pAlloc = &pCtx->alloc;
	PIX_ERR_ASSERT("", count > 0);
	I32 *pIdxArrMem = pCtx->alloc.fpCalloc(count + 1, sizeof(I32));
	I32 *pIdxArr = pIdxArrMem + 1;
	insertionSort(
		pIdxArr,
		count,
		&(SortCtx){.pCtx = pCtx, .pStage = pStage, .structIdx = structIdx},
		structIdxCmp
	);

	pRangeBuf->count = 0;
	PixtyRange *pRange = newRangeGet(pCtx, pRangeBuf);
	pRange->start = *(I32 *)structDataGet(pCtx, pStage, structIdx, pIdxArr[0]);
	I32 idxPrev = pRange->start;
	I32 idx = 0;
	for (I32 i = 1; i < count; idxPrev = idx, ++i) {
		idx = *(I32 *)structDataGet(pCtx, pStage, structIdx, pIdxArr[i]);
		if (idx == idxPrev + 1) {
			continue;
		}
		pRange->end = idxPrev + 1;
		PixtyRange *pRange = newRangeGet(pCtx, pRangeBuf);
		pRange->start = idx;
	}
	pRange->end = idxPrev + 1;

	I32 structIdxBytes = BITLEN(LOG_IDX) / 8;
	I32 byteSize = structByteSize(pStage->pStructArr + structIdx, false);
	pixioByteArrWrite(pAlloc, pStageData, &structIdx, BITLEN(LOG_STRUCT));
	pixioByteArrWrite(pAlloc, pStageData, &count, BITLEN(LOG_COUNT));
	pixioByteArrWrite(pAlloc, pStageData, &pRangeBuf->count, BITLEN(LOG_RANGE_COUNT));
	for (I32 i = 0; i < pRangeBuf->count; ++i) {
		PixtyRange range = pRangeBuf->pArr[i];
		pixioByteArrWrite(pAlloc, pStageData, &range.start, BITLEN(LOG_RANGE_STARTEND));
		pixioByteArrWrite(pAlloc, pStageData, &range.end, BITLEN(LOG_RANGE_STARTEND));
	}
	for (I32 i = 0; i < count; ++i) {
		void *pStart =
			(U8 *)structDataGet(pCtx, pStage, structIdx, pIdxArr[i]) + structIdxBytes;
		pixioByteArrWrite(pAlloc, pStageData, pStart, byteSize * 8);
	}
	*pDataTotal += (I64)byteSize * (I64)count;
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

static
void *outBufResize(const PixalcFPtrs *pAlloc, void *pUserData, I32 minSize) {
	CarkU8Arr *pOutBuf = pUserData;
	PIXALC_DYN_ARR_RESIZE(U8, pAlloc, pOutBuf, pOutBuf->count + minSize);
	return pOutBuf->pArr + pOutBuf->count;
}

static
void *outByteArrResize(const PixalcFPtrs *pAlloc, void *pUserData, I32 minSize) {
	PixioByteArr *pOutBuf = pUserData;
	if (pOutBuf->nextBitIdx) {
		++pOutBuf->byteIdx;
		pOutBuf->nextBitIdx = 0;
	}
	PIXALC_DYN_ARR_RESIZE(U8, pAlloc, pOutBuf, pOutBuf->byteIdx + minSize);
	return pOutBuf->pArr + pOutBuf->byteIdx;
}

static
PixErr bufDeflate(
	const CarkOutCtx *pCtx,
	void *pUserData,
	void *(*fpOutBufResize)(const PixalcFPtrs *, void *, I32),
	void *pInBuf,
	I64 inBufSize,
	bool compress,
	z_stream *pZStream
) {
	PixErr err = PIX_ERR_SUCCESS;
	*pZStream = (z_stream) {
		.zalloc = mallocZlibWrap,
		.zfree = freeZlibWrap,
		.opaque = (void *)&pCtx->alloc 
	};
	//using gzip with crc32
	err = checkZlibErr(
		Z_OK,
		deflateInit2(
			pZStream,
			compress ? Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION,
			Z_DEFLATED,
			CARK_WINDOW_BITS,
			8,
			Z_DEFAULT_STRATEGY
		)
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	pZStream->avail_out = deflateBound(pZStream, (uLong)inBufSize);
	void *pOutBuf = fpOutBufResize(&pCtx->alloc, pUserData, pZStream->avail_out);
	pZStream->next_out = pOutBuf;
	pZStream->avail_in = (uInt)inBufSize;
	pZStream->next_in = pInBuf;
	err = checkZlibErr(Z_STREAM_END, deflate(pZStream, Z_FINISH));
	PIX_ERR_RETURN_IFNOT(err, "");
	err = checkZlibErr(Z_OK, deflateEnd(pZStream));
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
PixErr bufInflate(
	const CarkInCtx *pCtx,
	void *pUserData,
	I64 outBufSize,
	void *(*fpOutBufResize)(const PixalcFPtrs *, void *, I32),
	void *pInBuf,
	I64 inBufSize,
	z_stream *pZStream
) {
	PixErr err = PIX_ERR_SUCCESS;
	*pZStream = (z_stream) {
		.zalloc = mallocZlibWrap,
		.zfree = freeZlibWrap,
		.opaque = (void *)&pCtx->alloc
	};
	pZStream->next_in = pInBuf;
	err = checkZlibErr(Z_OK, inflateInit2(pZStream, CARK_WINDOW_BITS));
	PIX_ERR_RETURN_IFNOT(err, "");
	pZStream->avail_in = (uInt)inBufSize;
	void *pOutBuf = fpOutBufResize(&pCtx->alloc, pUserData, outBufSize);
	pZStream->next_out = pOutBuf;
	pZStream->avail_out = (uInt)outBufSize;
	err = checkZlibErr(Z_STREAM_END, inflate(pZStream, Z_FINISH));
	PIX_ERR_RETURN_IFNOT(err, "");
	err = checkZlibErr(Z_OK, inflateEnd(pZStream));
	PIX_ERR_RETURN_IFNOT_COND(err, pZStream->total_out == outBufSize, "");
	return err;
}

PixErr carkOutStageEnd(CarkOutCtx *pCtx, I32 stageIdx, bool compress) {
	PixErr err = PIX_ERR_SUCCESS;
	if (!pCtx->enabled) {
		return err;
	}
	PIX_ERR_RETURN_IFNOT_COND(err, stageIdx >= 0 && stageIdx < pCtx->stageArr.count, "");
	CarkStage *pStage = pCtx->stageArr.pArr + stageIdx;
	PixioByteArr stageData = {0};
	PixtyRangeArr rangeBuf = {0};//TODO move mem into out-ctx for reuse?
	I32 rangeTotal = 0;
	I64 dataTotal = 0;
	I32 headerSize = (
		BITLEN(LOG_STRUCT_COUNT) +
		BITLEN(LOG_RANGE_TOTAL) +
		BITLEN(LOG_DATA_TOTAL)
	) / 8;
	PIXALC_DYN_ARR_RESIZE(U8, &pCtx->alloc, &stageData, headerSize);
	stageData.byteIdx += headerSize;
	I32 structsLogged = 0;
	for (I32 i = 0 ; i < pStage->structCount; ++i) {
		I32 structCount = structCountGet(pCtx, pStage, i);
		if (!structCount) {
			continue;
		}
		++structsLogged;
		err = compileStructLogs(
			pCtx,
			pStage,
			i,
			structCount,
			&stageData,
			&rangeBuf,
			&dataTotal
		);
		PIX_ERR_THROW_IFNOT(err, "", 1);
		rangeTotal += rangeBuf.count;
	}
	I32 countBytes = BITLEN(LOG_STRUCT_COUNT) / 8;
	I32 rangeTotalBytes = BITLEN(LOG_RANGE_TOTAL) / 8;
	memcpy(stageData.pArr, &structsLogged, countBytes);
	I32 headerPtr = countBytes;
	memcpy(stageData.pArr + headerPtr, &rangeTotal, rangeTotalBytes);
	headerPtr += rangeTotalBytes;
	memcpy(stageData.pArr + headerPtr, &dataTotal, BITLEN(LOG_DATA_TOTAL) / 8);
	PIX_ERR_CATCH(1, err, ;);
	if (rangeBuf.pArr) {
		pCtx->alloc.fpFree(rangeBuf.pArr);
	}
	PIX_ERR_THROW_IFNOT(err, "", 0);
	for (I32 i = 0 ; i < pStage->structCount; ++i) {
		for (I32 j = 0; j < pCtx->threadCount; ++j) {
			CarkStageLog *pStageLog = pCtx->pThreadArr[j].stageArr.pArr + pStage->idx;
			byteArrDestroy(pCtx, &pStageLog->pStructArr[i].data);
			pStageLog->pStructArr[i].count = 0;
		}
	}
	PIX_ERR_THROW_IFNOT(err, "", 0);

	pStage->bufStart = pCtx->outBuf.count;
	pStage->bufSize = stageData.byteIdx + !!stageData.nextBitIdx;
	//TODO compress all stages as one rolling block?
	z_stream zStream = {0};
	err = bufDeflate(
		pCtx,
		&pCtx->outBuf,
		outBufResize,
		stageData.pArr,
		pStage->bufSize,
		compress,
		&zStream
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pCtx->outBuf.count += zStream.total_out;
	pStage->bufCompressSize = zStream.total_out;

	PIX_ERR_CATCH(0, err, ;);
	byteArrDestroy(pCtx, &stageData);
	return err;
}

static
void encodeCompRef(const CarkOutCtx *pCtx, const CarkRef *pRef, PixioByteArr *pHeader) {
	pixioByteArrWrite(&pCtx->alloc, pHeader, &pRef->stageIdx, BITLEN(REF_STAGE_IDX));
	pixioByteArrWrite(&pCtx->alloc, pHeader, &pRef->structIdx, BITLEN(REF_STRUCT_IDX));
	pixioByteArrWrite(&pCtx->alloc, pHeader, &pRef->compIdx, BITLEN(REF_COMP_IDX));
}

static
PixErr encodeCompArr(
	const CarkOutCtx *pCtx,
	const CarkStructInfo *pStructInfo,
	PixioByteArr *pHeader
) {
	PixErr err = PIX_ERR_SUCCESS;
	const PixalcFPtrs *pAlloc = &pCtx->alloc;
	pixioByteArrWrite(pAlloc, pHeader, &pStructInfo->compCount, BITLEN(STRUCT_COMP_COUNT));
	for (I32 i = 0; i < pStructInfo->compCount; ++i) {
		const CarkCompInfo *pCompInfo = pStructInfo->pCompArr + i;
		pixioByteArrWriteStr(pAlloc, pHeader, pCompInfo->name);
		pixioByteArrWrite(pAlloc, pHeader, &pCompInfo->desc, BITLEN(COMP_DESC));
		pixioByteArrWrite(pAlloc, pHeader, &pCompInfo->type, BITLEN(COMP_TYPE));
		pixioByteArrWrite(pAlloc, pHeader, &pCompInfo->refCount, BITLEN(COMP_REF_COUNT));
		for (I32 j = 0; j < pCompInfo->refCount; ++j) {
			encodeCompRef(pCtx, pCompInfo->refArr + j, pHeader);
		}
		I32 precursorCount = pCompInfo->precursorCount;
		pixioByteArrWrite(pAlloc, pHeader, &precursorCount, BITLEN(COMP_PRECURSOR_COUNT));
		for (I32 j = 0; j < precursorCount; ++j) {
			encodeCompRef(pCtx, pCompInfo->precursorArr + j, pHeader);
		}
	}
	return err;
}

static
PixErr encodeHeader(
	CarkOutCtx *pCtx,
	PixioByteArr *pHeader,
	CarkU8Arr *pOutBuf,
	bool compress
) {
	PixErr err = PIX_ERR_SUCCESS;
	const PixalcFPtrs *pAlloc = &pCtx->alloc;

	pixioByteArrWrite(pAlloc, pHeader, &pCtx->stageArr.count, BITLEN(STAGE_COUNT));
	I32 structTotal = pixalcLinAllocGetCount(&pCtx->structAlloc);
	I32 compTotal = pixalcLinAllocGetCount(&pCtx->compAlloc);
	pixioByteArrWrite(pAlloc, pHeader, &pCtx->structAlloc.linIdx, BITLEN(STRUCT_COUNT));
	pixioByteArrWrite(pAlloc, pHeader, &pCtx->compAlloc.linIdx, BITLEN(COMP_COUNT));
	for (I32 i = 0; i < pCtx->stageArr.count; ++i) {
		const CarkStage *pStage = pCtx->stageArr.pArr + i;
		pixioByteArrWriteStr(pAlloc, pHeader, pStage->name);
		pixioByteArrWrite(pAlloc, pHeader, &pStage->bufStart, BITLEN(STAGE_BUF_START));
		I64 compressSize = pStage->bufCompressSize;
		pixioByteArrWrite(pAlloc, pHeader, &compressSize, BITLEN(STAGE_BUF_SIZE_DEFL));
		pixioByteArrWrite(pAlloc, pHeader, &pStage->bufSize, BITLEN(STAGE_BUF_SIZE_INFL));
		pixioByteArrWrite(pAlloc, pHeader, &pStage->structCount, BITLEN(STAGE_STRUCT_COUNT));
		for (I32 j = 0; j < pStage->structCount; ++j) {
			const CarkStructInfo *pStructInfo = &pStage->pStructArr[j].info;
			pixioByteArrWriteStr(pAlloc, pHeader, pStructInfo->name);
			pixioByteArrWrite(pAlloc, pHeader, &pStructInfo->desc, BITLEN(STRUCT_DESC));
			err = encodeCompArr(pCtx, pStructInfo, pHeader);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
	}
	if (pHeader->nextBitIdx) {
		++pHeader->byteIdx;
		pHeader->nextBitIdx = 0;
	}
	z_stream zStream = {0};
	err = bufDeflate(
		pCtx,
		pOutBuf,
		outBufResize,
		pHeader->pArr,
		pHeader->byteIdx,
		compress,
		&zStream
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pOutBuf->count = zStream.total_out;

	PIX_ERR_CATCH(0, err, ;);
	return err;
}

PixErr carkOutFileSave(CarkOutCtx *pCtx, const char *pPath, bool compressHeader) {
	PixErr err = PIX_ERR_SUCCESS;
	//TODO handle case where a stage didn't end, but had entries logged.
	//(due to logging enable/disable toggle)
	if (!pCtx->enabled) {
		return err;
	}
	PixioByteArr topHeader = {0};
	PixioByteArr header = {0};
	CarkU8Arr headerCompress = {0};
	PixioFile file = {0};
	err = pCtx->io.fpOpen(&file, pPath, 0);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	pixioByteArrWriteStr(&pCtx->alloc, &topHeader, carkFormat);
	I32 version = CARK_FILE_VERSION;
	pixioByteArrWrite(&pCtx->alloc, &topHeader, &version, BITLEN(VERSION));
	PIX_ERR_ASSERT("", topHeader.byteIdx == CARK_TOP_HEADER_SIZE);
	err = pCtx->io.fpWrite(&file, topHeader.pArr, topHeader.byteIdx);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	err = encodeHeader(pCtx, &header, &headerCompress, compressHeader);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpWrite(&file, &header.byteIdx, BITLEN(HEADER_SIZE_INFL) / 8);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpWrite(&file, &headerCompress.count, BITLEN(HEADER_SIZE_DEFL) / 8);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpWrite(&file, headerCompress.pArr, headerCompress.count);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	err = pCtx->io.fpWrite(&file, pCtx->outBuf.pArr, pCtx->outBuf.count);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, ;);
	if (file.pFile) {
		err = pCtx->io.fpClose(&file);
	}
	if (headerCompress.pArr) {
		pCtx->alloc.fpFree(headerCompress.pArr);
	}
	if (header.pArr) {
		pCtx->alloc.fpFree(header.pArr);
	}
	if (topHeader.pArr) {
		pCtx->alloc.fpFree(topHeader.pArr);
	}
	return err;
}

static
void stageLogDestroy(CarkOutCtx *pCtx, CarkStageLog *pStageLog) {
	for (I32 i = 0; i < pStageLog->structCount; ++i) {
		CarkStructLog *pStructLog = pStageLog->pStructArr + i;
		if (pStructLog->count) {
			byteArrDestroy(pCtx, &pStructLog->data);
		}
	}
}

void carkOutClear(CarkOutCtx *pCtx) {
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		for (I32 j = 0; j < pCtx->stageArr.count; ++j) {
			stageLogDestroy(pCtx, pCtx->pThreadArr[i].stageArr.pArr + j);
		}
	}
	pCtx->outBuf.count = 0;
}

void carkOutDestroy(CarkOutCtx *pCtx) {
	if (pCtx->outBuf.pArr) {
		pCtx->alloc.fpFree(pCtx->outBuf.pArr);
	}
	for (I32 i = 0; i < pCtx->threadCount; ++i) {
		for (I32 j = 0; j < pCtx->stageArr.count; ++j) {
			stageLogDestroy(pCtx, pCtx->pThreadArr[i].stageArr.pArr + j);
		}
		if (pCtx->pThreadArr[i].stageArr.pArr) {
			pCtx->alloc.fpFree(pCtx->pThreadArr[i].stageArr.pArr);
		}
	}
	if (pCtx->pThreadArr) {
		pCtx->alloc.fpFree(pCtx->pThreadArr);
	}
	pixalcLinAllocDestroy(&pCtx->compAlloc);
	pixalcLinAllocDestroy(&pCtx->structAlloc);
	for (I32 i = 0; i < pCtx->stageArr.count; ++i) {
		if (pCtx->stageArr.pArr[i].pStructMem) {
			pCtx->alloc.fpFree(pCtx->stageArr.pArr[i].pStructMem);
		}
	}
	if (pCtx->stageArr.pArr) {
		pCtx->alloc.fpFree(pCtx->stageArr.pArr);
	}
	*pCtx = (CarkOutCtx){0};
}

PixErr carkInInit(const PixalcFPtrs *pAlloc, const PixioFPtrs *pIo, CarkInCtx *pCtx) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		!pAlloc ||
		(pAlloc->fpMalloc && pAlloc->fpCalloc && pAlloc->fpFree && pAlloc->fpRealloc),
		"one or more alloc func ptrs are null"
	);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		!pIo ||
		(pIo->fpOpen && pIo->fpRead && pIo->fpWrite && pIo->fpPosSet && pIo->fpClose),
		"one or more io func ptrs are null"
	);
	fileTypeSizeInit();
	PixalcFPtrs allocDefault = {
		.fpMalloc = malloc,
		.fpCalloc = calloc,
		.fpFree = free,
		.fpRealloc = realloc
	};
	PixioFPtrs ioDefault = {
		.fpOpen = pixioFileOpen,
		.fpWrite = pixioFileWrite,
		.fpRead = pixioFileRead,
		.fpPosSet = pixioFilePosSet,
		.fpClose = pixioFileClose
	};
	*pCtx = (CarkInCtx){
		.alloc = pAlloc ? *pAlloc : allocDefault,
		.io = pIo ? *pIo : ioDefault
	};
	return err;
}

static
void decodeCompRef(PixioByteArr *pBuf, CarkRef *pRef) {
	I16 buf = 0;
	pixioByteArrRead(pBuf, &buf, BITLEN(REF_STAGE_IDX));
	pRef->stageIdx = (I32)buf;
	pixioByteArrRead(pBuf, &buf, BITLEN(REF_STRUCT_IDX));
	pRef->structIdx = (I32)buf;
	pixioByteArrRead(pBuf, &buf, BITLEN(REF_COMP_IDX));
	pRef->compIdx = (I32)buf;
}

static
void decodeComp(PixioByteArr *pBuf, CarkCompInfo *pComp) {
	pixioByteArrReadStr(pBuf, pComp->name, CARK_NAME_LEN_MAX);
	pixioByteArrRead(pBuf, &pComp->desc, BITLEN(COMP_DESC));
	pixioByteArrRead(pBuf, &pComp->type, BITLEN(COMP_TYPE));
	pixioByteArrRead(pBuf, &pComp->refCount, BITLEN(COMP_REF_COUNT));
	for (I32 i = 0; i < pComp->refCount; ++i) {
		decodeCompRef(pBuf, pComp->refArr + i);
	}
	pixioByteArrRead(pBuf, &pComp->precursorCount, BITLEN(COMP_PRECURSOR_COUNT));
	for (I32 i = 0; i < pComp->precursorCount; ++i) {
		decodeCompRef(pBuf, pComp->precursorArr + i);
	}
}

static
PixErr decodeStages(CarkInCtx *pCtx, CarkInFile *pFile) {
	PixErr err = PIX_ERR_SUCCESS;
	PixioByteArr *pBuf = &pCtx->mem.buf;
	pixioByteArrRead(pBuf, &pFile->stageArr.size, BITLEN(STAGE_COUNT));
	pFile->stageArr.count = pFile->stageArr.size;
	pixioByteArrRead(pBuf, &pFile->structArr.size, BITLEN(STRUCT_COUNT));
	pixioByteArrRead(pBuf, &pFile->compArr.size, BITLEN(COMP_COUNT));
	if (!pFile->stageArr.size || !pFile->structArr.size || !pFile->compArr.size) {
		return err;
	}
	const PixalcFPtrs *pAlloc = &pCtx->alloc;
	pFile->stageArr.pArr = pAlloc->fpCalloc(pFile->stageArr.size, sizeof(CarkStage));
	pFile->structArr.pArr =
		pAlloc->fpCalloc(pFile->structArr.size, sizeof(CarkStruct));
	pFile->compArr.pArr = pAlloc->fpCalloc(pFile->compArr.size, sizeof(CarkCompInfo));
	I32 structTotal = 0;
	I32 compTotal = 0;
	for (I32 i = 0; i < pFile->stageArr.count; ++i) {
		CarkStage *pStage = pFile->stageArr.pArr + i;
		pixioByteArrReadStr(pBuf, pStage->name, CARK_NAME_LEN_MAX);
		pixioByteArrRead(pBuf, &pStage->bufStart, BITLEN(STAGE_BUF_START));
		pixioByteArrRead(pBuf, &pStage->bufCompressSize, BITLEN(STAGE_BUF_SIZE_DEFL));
		pixioByteArrRead(pBuf, &pStage->bufSize, BITLEN(STAGE_BUF_SIZE_INFL));
		pixioByteArrRead(pBuf, &pStage->structCount, BITLEN(STAGE_STRUCT_COUNT));
		pStage->pStructArr = pFile->structArr.pArr + structTotal;
		structTotal += pStage->structCount;
		PIX_ERR_RETURN_IFNOT_COND(err, structTotal <= pFile->structArr.size, "");
		for (I32 j = 0; j < pStage->structCount; ++j) {
			CarkStructInfo *pStruct = &pStage->pStructArr[j].info;
			pixioByteArrReadStr(pBuf, pStruct->name, CARK_NAME_LEN_MAX);
			pixioByteArrRead(pBuf, &pStruct->desc, BITLEN(STRUCT_DESC));
			pixioByteArrRead(pBuf, &pStruct->compCount, BITLEN(STRUCT_COMP_COUNT));
			pStruct->pCompArr = pFile->compArr.pArr + compTotal;
			compTotal += pStruct->compCount;
			PIX_ERR_RETURN_IFNOT_COND(err, compTotal <= pFile->compArr.size, "");
			for (I32 k = 0; k < pStruct->compCount; ++k) {
				decodeComp(pBuf, pStruct->pCompArr + k);
			}
			pStage->pStructArr[j].byteSize = structSizeGet(pStruct);
		}
	}
	return err;
}

static
PixErr decodeHeader(CarkInCtx *pCtx, I64 size, CarkInFile *pFile) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkInLoadMem *pMem = &pCtx->mem;
	
	z_stream zStream = {0};
	bufInflate(
		pCtx,
		&pMem->buf,
		size,
		outByteArrResize,
		pMem->bufRaw.pArr,
		pMem->bufRaw.count,
		&zStream
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	err = decodeStages(pCtx, pFile);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
void memReset(CarkInLoadMem *pMem) {
	pMem->buf = (PixioByteArr){.pArr = pMem->buf.pArr, .size = pMem->buf.size};
	pMem->bufRaw.count = 0;
}

PixErr carkInFileInit(const CarkInCtx *pCtx, CarkInFile *pFile) {
	*pFile = (CarkInFile){0};
	return PIX_ERR_SUCCESS;
}

PixErr carkInFileOpen(const CarkInCtx *pCtx, const char *pPath, CarkInFile *pFile) {
	PixErr err = PIX_ERR_SUCCESS;
	err = pCtx->io.fpOpen(&pFile->file, pPath, PIX_IO_FILE_OPEN_READ);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

PixErr carkInFileClose(const CarkInCtx *pCtx, CarkInFile *pFile) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pFile->file.pFile, "");
	err = pCtx->io.fpClose(&pFile->file);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

bool carkInFileIsOpen(const CarkInFile *pFile) {
	return pFile->file.pFile;
}

PixErr carkInFileLoadInfo(CarkInCtx *pCtx, CarkInFile *pFile, CarkInFileInfo *pInfo) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pFile->file.pFile, "");
	CarkInLoadMem *pMem = &pCtx->mem;
	memReset(pMem);

	PIXALC_DYN_ARR_RESIZE(U8, &pCtx->alloc, &pMem->bufRaw, CARK_TOP_HEADER_SIZE);
	err = pCtx->io.fpRead(&pFile->file, pMem->bufRaw.pArr, pMem->bufRaw.size);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	bool validVersion =
		*(I16 *)(pMem->bufRaw.pArr + sizeof(carkFormat)) == CARK_FILE_VERSION;
	bool validFormat = !strncmp((char *)pMem->bufRaw.pArr, carkFormat, sizeof(carkFormat));
	PIX_ERR_THROW_IFNOT_COND(err, validFormat && validVersion, "", 0);
	I64 headerSize = 0;
	I64 headerSizeDefl = 0;
	err = pCtx->io.fpRead(&pFile->file, &headerSize, BITLEN(HEADER_SIZE_INFL) / 8);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpRead(&pFile->file, &headerSizeDefl, BITLEN(HEADER_SIZE_DEFL) / 8);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		headerSizeDefl >= BITLEN(STAGE_COUNT),
		"read failed or header size is invalid",
		0
	);
	PIXALC_DYN_ARR_RESIZE(U8, &pCtx->alloc, &pMem->bufRaw, headerSizeDefl);
	pCtx->io.fpRead(&pFile->file, pMem->bufRaw.pArr, headerSizeDefl);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pMem->bufRaw.count = headerSizeDefl;
	err = decodeHeader(pCtx, headerSize, pFile);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		pFile->stageArr.size && pFile->structArr.size && pFile->compArr.size,
		"log is empty or corrupt",
		0
	);
	pFile->headerSize = headerSizeDefl;
	if (pInfo) {
		*pInfo = (CarkInFileInfo){.pStageArr = &pFile->stageArr};
	}
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

PixErr carkInFileInfoGet(
	const CarkInCtx *pCtx,
	const CarkInFile *pFile,
	CarkInFileInfo *pInfo
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pFile && pFile->headerSize,
		"invalid log file. was fileLoadInfo called before this?"
	);
	*pInfo = (CarkInFileInfo){.pStageArr = &pFile->stageArr};
	return err;
}

static
PixErr decodeLog(
	CarkInCtx *pCtx,
	const CarkStage *pStage,
	CarkInFile *pFile,
	CarkInStageLog *pLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkInLoadMem *pMem = &pCtx->mem;
	pMem->buf = (PixioByteArr){.pArr = pMem->buf.pArr, .size = pMem->buf.size};
	z_stream zStream = {0};
	err = bufInflate(
		pCtx,
		&pMem->buf,
		pStage->bufSize,
		outByteArrResize,
		pMem->bufRaw.pArr,
		pMem->bufRaw.count,
		&zStream
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	I32 structsLogged = 0;
	I32 rangeTotal = 0;
	I64 dataTotal = 0;
	pixioByteArrRead(&pMem->buf, &structsLogged, BITLEN(LOG_STRUCT_COUNT));
	pixioByteArrRead(&pMem->buf, &rangeTotal, BITLEN(LOG_RANGE_TOTAL));
	pixioByteArrRead(&pMem->buf, &dataTotal, BITLEN(LOG_DATA_TOTAL));
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		structsLogged > 0 && rangeTotal > 0 && dataTotal > 0,
		"log empty or corrupt"
	);
	PIXALC_DYN_ARR_RESIZE(CarkInStructLog, &pCtx->alloc, &pLog->structs, structsLogged);
	PIXALC_DYN_ARR_RESIZE(PixtyRange, &pCtx->alloc, &pLog->rangeMem, rangeTotal);
	PIXALC_DYN_ARR_RESIZE(U8, &pCtx->alloc, &pLog->dataMem, dataTotal);
	pLog->structs.count = structsLogged;
	for (I32 i = 0; i < structsLogged; ++i) {
		CarkInStructLog *pStructLog = pLog->structs.pArr + i;
		*pStructLog = (CarkInStructLog){0};
		pixioByteArrRead(&pMem->buf, &pStructLog->idx, BITLEN(LOG_STRUCT));
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			pStructLog->idx >= 0 && pStructLog->idx < pStage->structCount,
			"struct idx is out of bounds"
		);
		I32 count = 0;
		pixioByteArrRead(&pMem->buf, &count, BITLEN(LOG_COUNT));
		I32 byteSize = structByteSize(pStage->pStructArr + pStructLog->idx, false);
		pStructLog->data.count = count * byteSize;
		pStructLog->data.pArr = pLog->dataMem.pArr + pLog->dataMem.count;
		pLog->dataMem.count += pStructLog->data.count;
		pixioByteArrRead(&pMem->buf, &pStructLog->rangeArr.size, BITLEN(LOG_RANGE_COUNT));
		pStructLog->rangeArr.count = pStructLog->rangeArr.size;
		pStructLog->rangeArr.pArr = pLog->rangeMem.pArr + pLog->rangeMem.count;
		pLog->rangeMem.count += pStructLog->rangeArr.count;
		for (I32 j = 0; j < pStructLog->rangeArr.count; ++j) {
			PixtyRange *pRange = pStructLog->rangeArr.pArr + j;
			pixioByteArrRead(&pMem->buf, &pRange->start, BITLEN(LOG_RANGE_STARTEND));
			pixioByteArrRead(&pMem->buf, &pRange->end, BITLEN(LOG_RANGE_STARTEND));
		}
		pixioByteArrRead(&pMem->buf, pStructLog->data.pArr, pStructLog->data.count * 8);
	}
	return err;
}

static
PixErr loadLog(
	CarkInCtx *pCtx,
	CarkInFile *pFile,
	const CarkStage *pStage,
	CarkInStageLog *pLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkInLoadMem *pMem = &pCtx->mem;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pFile->stageArr.size && pFile->structArr.size && pFile->compArr.size,
		"log is empty or corrupt"
	);
	I64 logStart =
		CARK_TOP_HEADER_SIZE +
		(BITLEN(HEADER_SIZE_INFL) + BITLEN(HEADER_SIZE_DEFL)) / 8 +
		pFile->headerSize;
	pMem->bufRaw.count = pStage->bufCompressSize;
	PIXALC_DYN_ARR_RESIZE(U8, &pCtx->alloc, &pMem->bufRaw, pMem->bufRaw.count);
	err = pCtx->io.fpPosSet(&pFile->file, logStart + pStage->bufStart);
	err = pCtx->io.fpRead(&pFile->file, pMem->bufRaw.pArr, pMem->bufRaw.count);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = decodeLog(pCtx, pStage, pFile, pLog);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

void carkInFileLogClear(CarkInStageLog *pLog) {
	pLog->dataMem.count = pLog->rangeMem.count = pLog->structs.count = 0;
}

I32 carkInStructLogCount(const CarkInStructLog *pLog) {
	I32 count = 0;
	for (I32 i = 0; i < pLog->rangeArr.count; ++i) {
		PixtyRange range = pLog->rangeArr.pArr[i];
		count += range.end - range.start;
	}
	return count;
}

PixErr carkInLogIdx(
	const CarkInStructLog *pLog,
	const CarkStage *pStage,
	I32 itemIdx,
	CarkInLogItem *pItem
) {
	PixErr err = PIX_ERR_SUCCESS;
	I32 offset = 0;
	//TODO use bst
	for (I32 i = 0; i < pLog->rangeArr.count; ++i) {
		PixtyRange range = pLog->rangeArr.pArr[i];
		if (itemIdx >= range.start && itemIdx < range.end) {
			itemIdx += offset - range.start;
			PIX_ERR_ASSERT("", itemIdx >= 0 && itemIdx < pLog->data.count);
			I32 byteSize = pStage->pStructArr[pLog->idx].byteSize;
			const U8 *pData = pLog->data.pArr + itemIdx * byteSize;
			*pItem = (CarkInLogItem){
				.timestamp = *(F32 *)pData,
				.pData = pData + sizeof(F32)
			};
			return err;
		}
		offset += range.end - range.start;
	}
	*pItem = (CarkInLogItem){0};
	PIX_ERR_RETURN(err, "no item at specified index");
}

PixErr carkInFileLoadLog(
	CarkInCtx *pCtx,
	CarkInFile *pFile,
	I32 stageIdx,
	CarkInStageLog *pLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pFile->file.pFile, "");
	*pLog = (CarkInStageLog){0};
	const CarkStage *pStage = pFile->stageArr.pArr + stageIdx;
	err = loadLog(pCtx, pFile, pStage, pLog);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err,
		carkInStageLogDestroy(&pCtx->alloc, pLog);
	);
	return err;
}

void carkInStageLogDestroy(const PixalcFPtrs *pAlloc, CarkInStageLog *pLog) {
	if (pLog->dataMem.pArr) {
		pAlloc->fpFree(pLog->dataMem.pArr);
	}
	if (pLog->rangeMem.pArr) {
		pAlloc->fpFree(pLog->rangeMem.pArr);
	}
	if (pLog->structs.pArr) {
		pAlloc->fpFree(pLog->structs.pArr);
	}
	*pLog = (CarkInStageLog){0};
}

void carkInFileDestroy(const PixalcFPtrs *pAlloc, CarkInFile *pFile) {
	if (pFile->stageArr.pArr) {
		pAlloc->fpFree(pFile->stageArr.pArr);
	}
	if (pFile->structArr.pArr) {
		pAlloc->fpFree(pFile->structArr.pArr);
	}
	if (pFile->compArr.pArr) {
		pAlloc->fpFree(pFile->compArr.pArr);
	}
	*pFile = (CarkInFile){0};
}

void carkInCtxDestroy(CarkInCtx *pCtx) {
	if (pCtx->mem.bufRaw.pArr) {
		pCtx->alloc.fpFree(pCtx->mem.bufRaw.pArr);
	}
	if (pCtx->mem.buf.pArr) {
		pCtx->alloc.fpFree(pCtx->mem.buf.pArr);
	}
	*pCtx = (CarkInCtx){0};
}
