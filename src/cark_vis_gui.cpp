#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include <cark_vis_gui.hpp>

#define IMGUI_ERR_RET(a, message) PIX_ERR_RETURN_IFNOT_COND(err, !!(a), message);
#define IMGUI_ERR_THROW(a, message, handle)\
	PIX_ERR_THROW_IFNOT_COND(err, !!(a), message, handle);

typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;
typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef float F32;
typedef double F64;

PixErr carkGuiInit(void *pWindow, void *pSdlGlCtx) {
	PixErr err = PIX_ERR_SUCCESS;
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	IMGUI_ERR_RET(
		ImGui_ImplSDL3_InitForOpenGL((SDL_Window *)pWindow, pSdlGlCtx),
		"imgui sdl init failed"
	);
	IMGUI_ERR_RET(ImGui_ImplOpenGL3_Init(), "imgui opengl init failed");
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	return err;
}

PixErr carkGuiEvent(const void *pEvent) {
	PixErr err = PIX_ERR_SUCCESS;
	ImGui_ImplSDL3_ProcessEvent((SDL_Event *)pEvent);
	return err;
}

static
void eventQueuePush(CarkGuiEventQueue *pQueue, CarkGuiEvent event) {
	PIX_ERR_ASSERT("", pQueue->count <= CARK_GUI_EVENT_QUEUE_MAX);
	if (pQueue->count == CARK_GUI_EVENT_QUEUE_MAX) {
		printf("gui event queue full! size is %d\n", CARK_GUI_EVENT_QUEUE_MAX);
		return;
	}
	pQueue->queue[pQueue->count] = event;
	++pQueue->count;
}

static I8 stageIcon[] {
	ICON_ARRAY,//none
	ICON_ARRAY,//array
	ICON_MESH//mesh
};

static I8 structIcon[] {
	ICON_ARRAY,//none
	ICON_ARRAY,//misc
	ICON_ARROW,//idx
	ICON_MESH,//mesh
	ICON_TRI,//face
	ICON_ARROW,//corner
	ICON_VERT,//pos
	ICON_VERT//uv
};

static
void drawIcon(const CarkGuiState *pGui, Icon icon) {
	ImGui::Image(
		(void *)(intptr_t)pGui->iconArr[icon],
		ImVec2{(F32)ICON_SIZE, (F32)ICON_SIZE},
		ImVec2{.0f, .0f},
		ImVec2{1.0f, 1.0f}
	);
}

static
void logRow(
	I32 offset,
	I32 idxInRange,
	PixtyRange range,
	const CarkInStructLog *pStructLog,
	const CarkStruct *pStructInfo
) {
	ImGui::TableNextRow();
	I32 byteIdx = (offset + idxInRange) * (CARK_TIMESTAMP_SIZE + pStructInfo->byteSize);
	const U8 *pData = pStructLog->data.pArr + byteIdx;
	I64 timestamp = 0;
	memcpy(&timestamp, pData, CARK_TIMESTAMP_SIZE);
	pData += CARK_TIMESTAMP_SIZE;
	if (ImGui::TableSetColumnIndex(0)) {
		ImGui::Text("%p", timestamp);
	}
	if (ImGui::TableSetColumnIndex(1)) {
		ImGui::Text("%d", range.start + idxInRange);
	}
	for (I32 k = 0; k < pStructInfo->info.compCount; ++k) {
		const CarkCompInfo *pCompInfo = pStructInfo->info.pCompArr + k;
		const void *pVal = pData;
		pData += carkTypeSizeGet(pCompInfo->type);
		if (!ImGui::TableSetColumnIndex(2 + k)) {
			continue;
		}
		switch (pCompInfo->type) {
			case CARK_TYPE_I8:
				ImGui::Text("%d", *(const I8 *)pVal);
				break;
			case CARK_TYPE_I16:
				ImGui::Text("%d", *(const I16 *)pVal);
				break;
			case CARK_TYPE_I32:
				ImGui::Text("%d", *(const I32 *)pVal);
				break;
			case CARK_TYPE_I64:
#ifdef WIN32
				ImGui::Text("%ll", *(const I64 *)pVal);
#else
				ImGui::Text("%l", *(const I64 *)pVal);
#endif
				break;
			case CARK_TYPE_U8:
				ImGui::Text("%u", *(const U8 *)pVal);
				break;
			case CARK_TYPE_U16:
				ImGui::Text("%u", *(const U16 *)pVal);
				break;
			case CARK_TYPE_U32:
				ImGui::Text("%u", *(const U32 *)pVal);
				break;
			case CARK_TYPE_U64:
#ifdef WIN32
				ImGui::Text("%ull", *(const U64 *)pVal);
#else
				ImGui::Text("%ul", *(const U64 *)pVal);
#endif
				break;
			case CARK_TYPE_F32:
				ImGui::Text("%f", *(const F32 *)pVal);
				break;
			case CARK_TYPE_F64:
				ImGui::Text("%f", *(const F64 *)pVal);
				break;
			default:
				PIX_ERR_ASSERT("invalid component type", false);
		}
	}
}

static
void logTable(const CarkInStructLog *pStructLog, const CarkStruct *pStructInfo) {
	ImGuiTableColumnFlags columnFlags = ImGuiTableColumnFlags_None;
	ImGui::TableSetupColumn("Timestamp", columnFlags);
	ImGui::TableSetupColumn("Index", columnFlags);
	for (I32 i = 0; i < pStructInfo->info.compCount; ++i) {
		ImGui::TableSetupColumn(pStructInfo->info.pCompArr[i].name, columnFlags);
	}
	ImGui::TableHeadersRow();
	I32 offset = 0;
	for (I32 i = 0; i < pStructLog->rangeArr.count; ++i) {
		PixtyRange range = pStructLog->rangeArr.pArr[i];
		I32 rangeSize = range.end - range.start;
		for (I32 j = 0; j < rangeSize; ++j) {
			logRow(offset, j, range, pStructLog, pStructInfo);
		}
		offset += rangeSize;
	}
	ImGui::EndTable();
}

static
void infoList(Session *pSession, CarkGuiState *pGui) {
	for (I32 i = 0; i < pSession->info.pStageArr->count; ++i) {
		const CarkStage *pStage = pSession->info.pStageArr->pArr + i;
		ImGuiTreeNodeFlags treeFlags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			(pSession->activeStage == i ? ImGuiTreeNodeFlags_Selected : 0x0);
		bool isOpen = ImGui::TreeNodeEx(
			(void *)(intptr_t)i,
			treeFlags,
			"%s",
			pStage->name
		);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			pSession->activeStage = i;
		}
		ImGui::SameLine();
		drawIcon(pGui, (Icon)stageIcon[pSession->stageTypeArr.pArr[i]]);
		if (isOpen) {
			for (I32 j = 0; j < pStage->structCount; ++j) {
				const CarkStructInfo *pStruct = &pStage->pStructArr[j].info;
				bool active = j == pSession->activeStruct;
				if (active) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{.5f, .5f, 1.0f, 1.0f});
				}
				if (ImGui::Button(pStruct->name)) {
					pSession->activeStruct = j;
				}
				ImGui::SameLine();
				drawIcon(pGui, (Icon)structIcon[pStruct->desc]);
				if (active) {
					ImGui::PopStyleColor();
				}
			}
			ImGui::TreePop();
		}
	}
}

PixErr carkGuiLayout(
	Session *pSession,
	PixtyV2_I32 windowSize,
	CarkGuiState *pGui,
	CarkGuiEventQueue *pQueue,
	uint32_t viewportTex,
	uint32_t timelineTex
) {
	PixErr err = PIX_ERR_SUCCESS;

	float topBarHeight = .0f;//set during menu creation
	float infoWidth = 144.0f;
	float timelineHeight = 160.0f;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	
	ImFont *pFont = ImGui::GetDefaultFont();
	pFont->Scale = 1.2725f;
	ImGui::PushFont(pFont);
	
	ImGuiID dockId = ImGui::GetID("Dockspace");
	ImGuiViewport* pViewport = ImGui::GetMainViewport();
	if (ImGui::DockBuilderGetNode(dockId) == nullptr) {
		ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockId, pViewport->Size);
		ImGuiID dockIdMain = dockId;
		ImGuiID dockIdLeft = 0;
		ImGuiID dockIdRight = 0;
		ImGuiID dockIdDown = 0;
		ImGui::DockBuilderSplitNode(dockIdMain, ImGuiDir_Left, .2f, &dockIdLeft, &dockIdMain);
		ImGui::DockBuilderSplitNode(dockIdMain, ImGuiDir_Down, .3f, &dockIdDown, &dockIdMain);
		ImGui::DockBuilderSplitNode(dockIdMain, ImGuiDir_Right, .33f, &dockIdRight, &dockIdMain);

		ImGui::DockBuilderDockWindow("Info", dockIdLeft);
		ImGui::DockBuilderDockWindow("Timeline", dockIdDown);
		ImGui::DockBuilderDockWindow("Log", dockIdRight);
		ImGui::DockBuilderDockWindow("Viewport", dockIdMain);
		ImGui::DockBuilderFinish(dockId);
	}
	ImGui::DockSpaceOverViewport(dockId, pViewport, ImGuiDockNodeFlags_PassthruCentralNode);

	ImGuiWindowFlags windowFlags{
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_MenuBar
	};
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::Button("Open Log")) {
				eventQueuePush(pQueue, CARK_GUI_EVENT_FILE_OPEN);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::Button("Readme")) {
				eventQueuePush(pQueue, CARK_GUI_EVENT_README);
			}
			ImGui::EndMenu();
		}
		topBarHeight = ImGui::GetWindowHeight();
		ImGui::EndMainMenuBar();
	}
	
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{.15f, .15f, .15f, 1.0f});
	if (ImGui::Begin("Info", NULL, ImGuiWindowFlags_None)) {
		float labelWidthBase = ImGui::GetFontSize() * 12;
		float labelWidthMax = ImGui::GetContentRegionAvail().x * .4f;
		ImGui::PushItemWidth(
			-(labelWidthBase < labelWidthMax ? labelWidthBase : labelWidthMax)
		);
		if (pSession->info.pStageArr) {
			infoList(pSession, pGui);
		}
		ImGui::Spacing();
		ImGui::End();
	}

	if (ImGui::Begin("Log", NULL, ImGuiWindowFlags_None)) {
		I32 stageIdx = pSession->activeStage;
		if (stageIdx != -1 && pSession->info.pStageArr) {
			PIX_ERR_ASSERT("", stageIdx < pSession->info.pStageArr->count);
			const CarkInStageLog *pStageLog = pSession->logArr.pArr + stageIdx;
			const CarkStage *pStageInfo = pSession->info.pStageArr->pArr + stageIdx;
			PIX_ERR_ASSERT("", pSession->activeStruct < pStageInfo->structCount);
			I32 structIdx = pSession->activeStruct;
			const CarkInStructLog *pStructLog = pStageLog->structs.pArr + structIdx;
			const CarkStruct *pStructInfo = pStageInfo->pStructArr + structIdx;
			ImGuiTableFlags tableFlags =
				ImGuiTableFlags_ScrollX |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_BordersOuter |
				ImGuiTableFlags_BordersV |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_NoHostExtendX;
			I32 columnCount = 2 + pStructInfo->info.compCount;
			if (ImGui::BeginTable("Comp Log", columnCount, tableFlags, ImVec2{.0f, .0f})) {
				logTable(pStructLog, pStructInfo);
			}
		}
		ImGui::End();
	}

	if (ImGui::Begin("Timeline", NULL, ImGuiWindowFlags_None)) {
		ImGui::Text("test a");
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{.0f, .0f});
		ImVec2 size = ImGui::GetContentRegionAvail();
		if (ImGui::BeginChild(
			"stages",
			ImVec2{size.x, size.y * .8f},
			ImGuiChildFlags_Borders,
			ImGuiWindowFlags_None
		)) {
			ImVec2 size = ImGui::GetWindowSize();
			ImGui::Image(
				(void *)(intptr_t)timelineTex,
				size,
				ImVec2{.0f, 1.0f},
				ImVec2{1.0f, .0f}
			);
			pSession->timelineSize = PixtyV2_I32{(I32)size.x, (I32)size.y};
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
	}
	ImGui::End();

	windowFlags = ImGuiWindowFlags{
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	};
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{.0f, .0f});
	if(ImGui::Begin("Viewport", NULL, windowFlags)) {
		ImVec2 size = ImGui::GetWindowSize();
		ImGui::Image(
			(void *)(intptr_t)viewportTex,
			size,
			ImVec2{.0f, 1.0f},
			ImVec2{1.0f, .0f}
		);
		pSession->viewportSize = PixtyV2_I32{(I32)size.x, (I32)size.y};
		ImGui::End();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	ImGui::PopFont();
	return err;
}

PixErr carkGuiDraw() {
	PixErr err = PIX_ERR_SUCCESS;
	ImGui::Render();
	ImDrawData *pDrawData = ImGui::GetDrawData();
	PIX_ERR_RETURN_IFNOT_COND(err, pDrawData, "no draw data");
	ImGui_ImplOpenGL3_RenderDrawData(pDrawData);
	return err;
}

void carkGuiDestroy() {
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}
