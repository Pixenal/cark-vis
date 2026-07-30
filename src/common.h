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
	int32_t triCount;
} GpuMesh;

typedef struct Session {
	StageDataTypeArr stageTypeArr;
	LogArr logArr;
	CarkInFile file;
	CarkInFileInfo info;
	GpuMesh renderMesh;
	PixtyV2_I32 viewportSize;//TODO should this be in session?
	I32 activeStage;
	I32 activeStruct;
} Session;