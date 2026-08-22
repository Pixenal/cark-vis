#pragma once

#include <cark_vis_io.h>

#define ASSET_NAME_LEN_MAX 32
static char iconNames[][ASSET_NAME_LEN_MAX] = {
	"icon_array.svg",
	"icon_mesh.svg",
	"icon_tri.svg",
	"icon_vert.svg",
	"icon_arrow.svg"
};
#define ICON_COUNT (sizeof(iconNames) / ASSET_NAME_LEN_MAX)
#define ICON_SIZE 18

typedef enum Icon {
	ICON_ARRAY,
	ICON_MESH,
	ICON_TRI,
	ICON_VERT,
	ICON_ARROW
} Icon;

typedef enum StageDataType {
	STAGE_DATA_NONE,
	STAGE_DATA_ARRAY,
	STAGE_DATA_MESH,
	STAGE_DATA_ENUM_COUNT
} StageDataType;

typedef struct StageDataTypeArr {
	StageDataType *pArr;
	I32 size;
} StageDataTypeArr;

typedef struct LogArr {
	CarkInStageLog *pArr;
	int32_t size;
} LogArr;

typedef struct GpuMesh {
	uint32_t vao;
	uint32_t vbo;
	uint32_t ebo;
	int32_t vecSize;
	int32_t triCount;
	PixtyV3_F32 centre;
	F32 size;
} GpuMesh;

typedef struct GpuMeshArr {
	GpuMesh *pArr;
	int32_t size;
	int32_t count;
} GpuMeshArr;

typedef struct StageMesh {
	GpuMeshArr instMeshArr;
	PixtyV3_F32 centre;
	F32 size;
} StageMesh;

typedef struct StageMeshArr {
	StageMesh *pArr;
	PixtyValidIdx *pTable;
	int32_t size;
	int32_t count;
} StageMeshArr;

typedef struct Session {
	StageDataTypeArr stageTypeArr;
	LogArr logArr;
	CarkInFile file;
	CarkInFileInfo info;
	StageMeshArr stageMeshArr;
	PixtyV2_I32 viewportSize;//TODO should this be in session?
	PixtyV2_I32 timelineSize;//TODO ^
	I32 activeStage;
	I32 activeStruct;
	I32 activeInst;
} Session;

#ifdef __cplusplus
extern "C" {
#endif
PixErr avlIterInitConst(const PixuctAvl *pHandle, PixuctAvlIter *pIter);
bool avlIterAtEnd(const PixuctAvlIter *pIter);
void avlIterInc(PixuctAvlIter *pIter);
const PixuctAvlNodeCore *avlIterGetItemConst(PixuctAvlIter *pIter);
#ifdef __cplusplus
}
#endif
