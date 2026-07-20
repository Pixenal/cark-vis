#pragma once

#include <cark_vis_io.h>

typedef struct LogArr {
	CarkInStageLog *pArr;
	I32 size;
} LogArr;

typedef struct Session {
	LogArr logArr;
	CarkInFile file;
	CarkInFileInfo info;
} Session;