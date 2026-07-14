#pragma once

#include <stdint.h>

#include <pixenals_alloc_utils.h>
#include <pixenals_io_utils.h>

#ifndef CARK_PRECURSOR_COUNT_MAX
#define CARK_PRECURSOR_COUNT_MAX 4
#endif
#ifndef CARK_CACHELINE_SIZE
#define CARK_CACHELINE_SIZE 64//using a static estimate for now
#endif
#ifndef CARK_NAME_LEN_MAX
#define CARK_NAME_LEN_MAX 64
#endif

typedef enum CarkType {
	CARK_TYPE_NONE,
	CARK_TYPE_I8,
	CARK_TYPE_U8,
	CARK_TYPE_I16,
	CARK_TYPE_U16,
	CARK_TYPE_I32,
	CARK_TYPE_U32,
	CARK_TYPE_I64,
	CARK_TYPE_U64,
	CARK_TYPE_F32,
	CARK_TYPE_F64,
	CARK_TYPE_ENUM_COUNT
} CarkType;

typedef enum CarkDesc {
	CARK_DESC_NONE,
	CARK_DESC_MISC,
	CARK_DESC_IDX,
	CARK_DESC_PTR,//TODO implement this?
	CARK_DESC_MESH,
	CARK_DESC_FACE,
	CARK_DESC_CORNER,
	CARK_DESC_POS,
	CARK_DESC_UV,
	CARK_DESC_ENUM_COUNT
} CarkDesc;

typedef enum CarkCompDesc {
	CARK_COMP_DESC_NONE,
	CARK_COMP_DESC_IDX,
	CARK_COMP_DESC_VEC_X,
	CARK_COMP_DESC_VEC_Y,
	CARK_COMP_DESC_VEC_Z,
	CARK_COMP_DESC_VEC_W,
	CARK_COMP_DESC_STR,
	CARK_COMP_DESC_ENUM_COUNT
} CarkCompDesc;

typedef enum CarkGraphType {
	CARK_GRAPH_NONE,
	CARK_GRAPH_REF,
	CARK_GRAPH_ENUM_COUNT
} CarkGraphType;

typedef struct CarkRef {
	int32_t stageIdx;
	int32_t structIdx;
	int32_t compIdx;
} CarkRef;

typedef struct CarkCompInfo {
	char name[CARK_NAME_LEN_MAX + 1];
	CarkRef precursorArr[CARK_PRECURSOR_COUNT_MAX];
	CarkRef ref;
	CarkType type;
	CarkCompDesc desc;
} CarkCompInfo;

typedef struct CarkStructInfo {
	char name[CARK_NAME_LEN_MAX + 1];
	CarkCompInfo *pCompArr;
	int32_t compCount;
	CarkDesc desc;
} CarkStructInfo;

typedef struct CarkStructInfoArr {
	CarkStructInfo *pArr;
	int32_t size;
} CarkStructInfoArr;

typedef struct CarkStruct {
	CarkStructInfo info;
	int32_t byteSize;
} CarkStruct;

typedef struct CarkStructArr {
	CarkStruct *pArr;
	int32_t size;
} CarkStructArr;

typedef struct CarkStage {
	CarkStruct *pStructArr;
	U8 *pStructMem;
	int32_t structCount;
	int32_t idx;
	int64_t bufStart;
	int64_t bufSize;
	int64_t bufCompressedSize;
	char name[CARK_NAME_LEN_MAX + 1];
} CarkStage;

typedef struct CarkStructLog {
	PixioByteArr data;
	int32_t count;
} CarkStructLog;

typedef struct CarkStageLog {
	CarkStructLog *pStructArr;
	int32_t structCount;
	int32_t stage;
} CarkStageLog;

typedef struct CarkStageLogArr {
	CarkStageLog *pArr;
	int32_t size;
} CarkStageLogArr;

typedef struct CarkThread {
	char padHeader[CARK_CACHELINE_SIZE / 2];
	CarkStageLogArr stageArr;
	int32_t activeLogStage;
	bool activeLog;
	char padFooter[CARK_CACHELINE_SIZE / 2];
} CarkThread;

typedef struct CarkCtx {
	PixalcFPtrs alloc;
	PixioFPtrs io;
	CarkThread *pThreadArr;
	PixalcLinAlloc compAlloc;
	PixalcLinAlloc structAlloc;
	PixtyU8Arr outBuf;
	int32_t stageCount;
	int32_t threadCount;
} CarkCtx;

typedef struct CarkGraphInitCtx {
	CarkCtx *pCtx;
} CarkGraphInitCtx;

typedef struct CarkLog {
	CarkCtx *pCtx;
	const CarkStage *pStage;
	int32_t structIdx;
	int32_t thread;
	int32_t compCount;
} CarkLog;

PixErr carkInit(
	const PixalcFPtrs *pAlloc,
	const PixioFPtrs *pIo,
	int32_t threadCount,
	CarkCtx *pCtx
);
PixErr carkStageInit(
	CarkCtx *pCtx,
	const char *pName,
	const CarkStructInfoArr *pStructArr,
	CarkStage *pHandle
);
PixErr carkLogStart(
	CarkCtx *pCtx,
	int32_t thread,
	const CarkStage *pStage,
	int32_t structIdx,
	int32_t idx,
	CarkLog *pLog
);
PixErr carkLogComp(CarkLog *pLog, int32_t compIdx, void *pVal);
PixErr carkLogEnd(CarkLog *pLog);
PixErr carkStageEnd(CarkCtx *pCtx, CarkStage *pStage, bool compress);
PixErr carkSaveToFile(CarkCtx *pCtx, const char *pPath);
void carkStageDestroy(CarkCtx *pCtx, CarkStage *pStage);
void carkDestroy(CarkCtx *pCtx);
