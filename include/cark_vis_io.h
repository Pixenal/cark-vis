#pragma once

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#include <pixenals_alloc_utils.h>
#include <pixenals_io_utils.h>
#include <pixenals_structs.h>

#ifndef CARK_PRECURSOR_COUNT_MAX
#define CARK_PRECURSOR_COUNT_MAX 4
#endif
#ifndef CARK_REF_COUNT_MAX
#define CARK_REF_COUNT_MAX 4
#endif
#ifndef CARK_CACHELINE_SIZE
#define CARK_CACHELINE_SIZE 64//using a static estimate for now
#endif
#ifndef CARK_NAME_LEN_MAX
#define CARK_NAME_LEN_MAX 64
#endif
//TODO these timestamp macros probably arn't needed anymore
#define CARK_TIMESTAMP_TYPE I64
#define CARK_TIMESTAMP_SIZE sizeof(CARK_TIMESTAMP_TYPE)

typedef struct CarkU8Arr {
	uint8_t *pArr;
	int64_t size;
	int64_t count;
} CarkU8Arr;

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
	CARK_COMP_DESC_SIZE,
	CARK_COMP_DESC_VEC_X,
	CARK_COMP_DESC_VEC_Y,
	CARK_COMP_DESC_VEC_Z,
	CARK_COMP_DESC_VEC_W,
	CARK_COMP_DESC_STR,
	CARK_COMP_DESC_ID,
	CARK_COMP_DESC_ENUM_COUNT
} CarkCompDesc;

typedef enum CarkGraphType {
	CARK_GRAPH_NONE,
	CARK_GRAPH_REF,
	CARK_GRAPH_ENUM_COUNT
} CarkGraphType;

//a stage-idx of -1 indicates the current stage 
//a comp-idx of -1 indicates the whole struct (all components)
typedef struct CarkRef {
	int32_t stageIdx;
	int32_t structIdx;
	int32_t compIdx;
} CarkRef;

typedef struct CarkOverride {
	int32_t val : 31;
	int32_t override : 1;
} CarkOverride;

typedef struct CarkRefOverride {
	CarkOverride stageIdx;
	CarkOverride structIdx;
	CarkOverride compIdx;
	CarkOverride inst;
} CarkRefOverride;

typedef struct CarkRefOverrideArr {
	CarkRefOverride *pArr;
	int32_t count;
} CarkRefOverrideArr;

typedef struct CarkCompInfo {
	char name[CARK_NAME_LEN_MAX + 1];
	CarkRef precursorArr[CARK_PRECURSOR_COUNT_MAX];
	int32_t precursorCount;
	CarkRef refArr[CARK_PRECURSOR_COUNT_MAX];
	int32_t refCount;
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
	int64_t bufCompressSize;
	char name[CARK_NAME_LEN_MAX + 1];
} CarkStage;

typedef struct CarkStageArr {
	CarkStage *pArr;
	int32_t size;
	int32_t count;
} CarkStageArr;

typedef struct CarkInstLog {
	PixioByteArr data;
	PixioByteArr overrides;
	int32_t count;
} CarkInstLog;

typedef struct CarkInstLogArr {
	CarkInstLog *pArr;
	int32_t size;
	int32_t count;
} CarkInstLogArr;

typedef struct CarkStructLog {
	CarkInstLogArr instArr;
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
	int64_t timeFirst;//used to determine overall timeframe of log
	int64_t timeLast;
	CarkStageLogArr stageArr;
	int32_t activeLogStage;
	bool activeLog;
	char padFooter[CARK_CACHELINE_SIZE / 2];
} CarkThread;

typedef struct CarkOutCtx {
	CarkThread *pThreadArr;
	PixalcFPtrs alloc;
	PixioFPtrs io;
	CarkStageArr stageArr;
	PixalcLinAlloc compAlloc;
	PixalcLinAlloc structAlloc;
	CarkU8Arr outBuf;
	int32_t threadCount;
	bool enabled;
} CarkOutCtx;

typedef struct CarkGraphInitCtx {
	CarkOutCtx *pCtx;
} CarkGraphInitCtx;

typedef struct CarkLog {
	CarkOutCtx *pCtx;
	const CarkStage *pStage;
	int32_t structIdx;
	int32_t inst;
	int32_t thread;
	int32_t compCount;
	int64_t overrideStart;
	bool enabled;
} CarkLog;

#ifdef __cplusplus
extern "C" {
#endif
PixErr carkOutInit(
	const PixalcFPtrs *pAlloc,
	const PixioFPtrs *pIo,
	int32_t threadCount,
	CarkOutCtx *pCtx
);
PixErr carkOutStageInit(
	CarkOutCtx *pCtx,
	const char *pName,
	const CarkStructInfoArr *pStructArr,
	int32_t *pHandle
);
//TODO add single function wrapper of start-comp-end for single-comp structs
PixErr carkOutLogStart(
	CarkOutCtx *pCtx,
	int32_t thread,
	int32_t stageIdx,
	int32_t structIdx,
	int32_t inst,
	int32_t idx,
	CarkLog *pLog
);
PixErr carkOutLogComp(
	CarkLog *pLog,
	int32_t compIdx,
	const CarkRefOverrideArr *pRefOverrideArr,
	void *pVal
);
PixErr carkOutLogEnd(CarkLog *pLog);
PixErr carkOutStageEnd(CarkOutCtx *pCtx, int32_t stageIdx, bool compress);
PixErr carkOutFileSave(CarkOutCtx *pCtx, const char *pPath, bool compressHeader);
void carkOutClear(CarkOutCtx *pCtx);
void carkOutDestroy(CarkOutCtx *pCtx);
static inline
void carkOutEnableSet(CarkOutCtx *pCtx, bool value) {
	pCtx->enabled = value;
}
static inline
bool carkOutEnableGet(CarkOutCtx *pCtx) {
	return pCtx->enabled;
}


typedef struct CarkInLoadMem {
	CarkU8Arr bufRaw;
	PixioByteArr buf;
} CarkInLoadMem;

typedef struct CarkInCtx {
	PixalcFPtrs alloc;
	PixioFPtrs io;
	CarkInLoadMem mem;
} CarkInCtx;

typedef struct CarkCompInfoArr {
	CarkCompInfo *pArr;
	int32_t size;
} CarkCompInfoArr;

typedef struct CarkInInstLog {
	int64_t dataIdx;
	PixuctAvl rangeTree;
	int32_t count;
	int32_t overrideIdx;
	int32_t byteSize;
} CarkInInstLog;

typedef struct CarkInInstLogArr {
	CarkInInstLog *pArr;
	int32_t size;
	int32_t count;
} CarkInInstLogArr;

typedef struct CarkInStructLog {
	CarkInInstLogArr instArr;
	int32_t idx;
} CarkInStructLog;

typedef struct CarkInStructLogArr {
	CarkInStructLog *pArr;
	int32_t size;
	int32_t count;
} CarkInStructLogArr;

typedef struct CarkItemRange {
	PixuctAvlNodeCore core;
	PixtyRange idxRange;
	int32_t startItem;
} CarkItemRange;

typedef struct CarkInStageLog {
	//struct-arr is sparse, containing only structs that were logged.
	CarkInStructLogArr structs;
	PixalcLinAlloc rangeMem;
	PixtyU8Arr dataMem;//using single arr to make potential future compression easier
	PixtyValidIdx64Arr overrideTable; 
} CarkInStageLog;

typedef struct CarkInFile {
	PixioFile file;
	int64_t timeStart;
	int64_t duration;
	CarkStageArr stageArr;
	CarkStructArr structArr;
	CarkCompInfoArr compArr;
	int64_t headerSize;
	I32 stageIdx;
} CarkInFile;

typedef struct CarkInFileInfo {
	const CarkStageArr *pStageArr;
} CarkInFileInfo;

typedef struct CarkInLogItem {
	const U8 *pData;
	CARK_TIMESTAMP_TYPE timestamp;
} CarkInLogItem;

PixErr carkInInit(const PixalcFPtrs *pAlloc, const PixioFPtrs *pIo, CarkInCtx *pCtx);
PixErr carkInFileInit(const CarkInCtx *pCtx, CarkInFile *pFile);
PixErr carkInFileOpen(const CarkInCtx *pCtx, const char *pPath, CarkInFile *pFile);
PixErr carkInFileClose(const CarkInCtx *pCtx, CarkInFile *pFile);
bool carkInFileIsOpen(const CarkInFile *pFile);
PixErr carkInFileLoadInfo(CarkInCtx *pCtx, CarkInFile *pFile, CarkInFileInfo *pInfo);
PixErr carkInFileInfoGet(
	const CarkInCtx *pCtx,
	const CarkInFile *pFile,
	CarkInFileInfo *pInfo
);
void carkInFileLogClear(CarkInStageLog *pLog);
PixErr carkInRefOverrideGet(
	const CarkInStageLog *pLog,
	const CarkStage *pStage,
	int32_t structIdx,
	int32_t inst,
	int32_t itemIdx,
	CarkRefOverrideArr *pRefArr
);
int32_t carkInStructLogCount(const CarkInStageLog *pLog, int32_t structIdx, int32_t inst);
PixErr carkInLogIdx(
	const CarkInStageLog *pLog,
	const CarkStage *pStage,
	int32_t structIdx,
	int32_t inst,
	int32_t itemIdx,
	CarkInLogItem *pItem
);
PixErr carkInFileLoadLog(
	CarkInCtx *pCtx,
	CarkInFile *pFile,
	I32 stageIdx,
	CarkInStageLog *pLog
);
void carkInStageLogDestroy(const PixalcFPtrs *pAlloc, CarkInStageLog *pLog);
void carkInFileDestroy(const PixalcFPtrs *pAlloc, CarkInFile *pFile);
void carkInCtxDestroy(CarkInCtx *pCtx);
//retuns size of type in bits
int32_t carkTypeSizeGet(CarkType type);
#ifdef __cplusplus
}
#endif
