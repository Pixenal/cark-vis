#include <imgui.h>
#include <imgui_internal.h>
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

PixErr carkGuiLayout(
	Session *pSession,
	PixtyV2_I32 windowSize,
	CarkGuiEventQueue *pQueue,
	uint32_t viewportTex
) {
	PixErr err = PIX_ERR_SUCCESS;

	float topBarHeight = .0f;//set during menu creation
	float infoWidth = 144.0f;
	float timelineHeight = 160.0f;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	
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

	if (ImGui::Begin("Log", NULL, ImGuiWindowFlags_None)) {
	}
	ImGui::End();

	if (ImGui::Begin("Timeline", NULL, ImGuiWindowFlags_None)) {
		ImGui::Text("test a");

		ImVec2 size = ImGui::GetContentRegionAvail();
		if (ImGui::BeginChild(
			"stages",
			ImVec2{size.x, size.y * .8f},
			ImGuiChildFlags_Borders,
			ImGuiWindowFlags_None
		)) {
			ImGui::Text("test b");
		}
		ImGui::EndChild();
	}
	ImGui::End();

	windowFlags = ImGuiWindowFlags{
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	};
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{.0f, .0f});
	if(ImGui::Begin("Viewport", NULL, windowFlags)) {
		ImVec2 size = ImGui::GetWindowSize();
		//size.x -= 20.0f;
		//size.y -= 35.0f;
		ImGui::Image(
			(void *)(intptr_t)viewportTex,
			size,
			ImVec2{.0f, 1.0f},
			ImVec2{1.0f, .0f}
		);
		pSession->viewportSize = PixtyV2_I32{(I32)size.x, (I32)size.y};
	}
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
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
