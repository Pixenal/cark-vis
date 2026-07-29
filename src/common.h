#pragma once

#include <cark_vis_io.h>

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
	LogArr logArr;
	CarkInFile file;
	CarkInFileInfo info;
	GpuMesh renderMesh;
	PixtyV2_I32 viewportSize;//TODO should this be in session?
} Session;