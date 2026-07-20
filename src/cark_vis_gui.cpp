#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include <cark_vis_gui.hpp>

#define IMGUI_ERR_RET(a, message) PIX_ERR_RETURN_IFNOT_COND(err, !!(a), message);
#define IMGUI_ERR_THROW(a, message, handle)\
	PIX_ERR_THROW_IFNOT_COND(err, !!(a), message, handle);

typedef int32_t I32;
typedef float F32;

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

PixErr carkGuiLayout(
	Session *pSession,
	PixtyV2_I32 windowSize,
	CarkGuiEventQueue *pQueue
) {
	PixErr err = PIX_ERR_SUCCESS;
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGuiWindowFlags windowFlags{
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_MenuBar
	};
	float topBarHeight = 0;
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::Button("Open Log")) {
				eventQueuePush(pQueue, CARK_GUI_EVENT_FILE_OPEN);
			}
			ImGui::EndMenu();
		}
		topBarHeight = ImGui::GetWindowHeight();
		ImGui::EndMainMenuBar();
	}
	
	ImGui::SetNextWindowPos(ImVec2{0, topBarHeight});
	ImGui::SetNextWindowSize(ImVec2{144.0f, (F32)windowSize.d[1] - topBarHeight});
	windowFlags = ImGuiWindowFlags{
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove
	};
	if (ImGui::Begin("Stage List", NULL, windowFlags)) {
		float labelWidthBase = ImGui::GetFontSize() * 12;
		float labelWidthMax = ImGui::GetContentRegionAvail().x * .4f;
		ImGui::PushItemWidth(
			-(labelWidthBase < labelWidthMax ? labelWidthBase : labelWidthMax)
		);
		if (pSession->info.pStageArr) {
			for (I32 i = 0; i < pSession->info.pStageArr->count; ++i) {
				const CarkStage *pStage = pSession->info.pStageArr->pArr + i;
				ImGui::Text("%s", pStage->name);
				for (I32 j = 0; j < pStage->structCount; ++j) {
					const CarkStructInfo *pStruct = &pStage->pStructArr[j].info;
					ImGui::Text("  %s", pStruct->name);
					for (I32 k = 0; k < pStruct->compCount; ++k) {
						const CarkCompInfo *pComp = pStruct->pCompArr + k;
						ImGui::Text("    %s", pComp->name);
					}
				}
			}
		}
		ImGui::Spacing();
	}
	ImGui::End();
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
