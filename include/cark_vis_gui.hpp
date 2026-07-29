#pragma once

#include <pixenals_error_utils.h>
#include <pixenals_types.h>

#include <common.h>

#ifndef CARK_GUI_EVENT_QUEUE_MAX
#define CARK_GUI_EVENT_QUEUE_MAX 64
#endif

typedef enum CarkGuiEvent {
	CARK_GUI_EVENT_NONE,
	CARK_GUI_EVENT_FILE_OPEN,
	CARK_GUI_EVENT_README,
	CARK_GUI_EVENT_ENUM_COUNT
} CarkGuiEvent;

typedef struct CarkGuiEventQueue {
	CarkGuiEvent queue[CARK_GUI_EVENT_QUEUE_MAX];
	int32_t count;
} CarkGuiEventQueue;

typedef struct CarkGuiState {
	volatile bool fileDialogActive;
	char *pFileDialogPath;
} CarkGuiState;

#ifdef __cplusplus
extern "C" {
#endif
PixErr carkGuiInit(void *pWindow, void *pSdlGlCtx);
PixErr carkGuiEvent(const void *pEvent);
PixErr carkGuiLayout(
	Session *pSession,
	PixtyV2_I32 windowSize,
	CarkGuiEventQueue *pQueue,
	uint32_t viewportTex
);
PixErr carkGuiDraw();
void carkGuiDestroy();
#ifdef __cplusplus
}
#endif

