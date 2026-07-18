#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <pixenals_error_utils.h>
#include <pixenals_types.h>

#include <cark_vis_gui.hpp>
#include <cark_vis_io.h>

#define CARK_PATH_LEN_MAX 4096

typedef int32_t I32;
typedef float F32;

#define SDL_ERR_RET(a)\
	if (!(a)) { \
		const char *pErrStr = SDL_GetError();\
		PIX_ERR_RETURN_IFNOT(err, pErrStr ? pErrStr : "");\
	}
#define SDL_ERR_THROW(a, handle)\
	if (!(a)) { \
		const char *pErrStr = SDL_GetError();\
		PIX_ERR_THROW_IFNOT(err, pErrStr ? pErrStr : "", handle);\
	}

#define GLEW_ERR_RET(a)\
	if ((a) != GLEW_OK) { \
		const char *pErrStr = glewGetErrorString(a);\
		PIX_ERR_RETURN_IFNOT(err, pErrStr ? pErrStr : "");\
	}
#define GLEW_ERR_THROW(a, handle)\
	if ((a) != GLEW_OK) { \
		const char *pErrStr = glewGetErrorString(a);\
		PIX_ERR_THROW_IFNOT(err, pErrStr ? pErrStr : "", handle);\
	}

#define GL_ERR_RET(a, message) PIX_ERR_RETURN_IFNOT_COND(err, !!(a), message);
#define GL_ERR_THROW(a, message, handle) \
	PIX_ERR_THROW_IFNOT_COND(err, !!(a), message, handle);

typedef struct GlCtx {
	SDL_GLContext pSdlCtx;
	GLuint prog;
	GLuint vao;
	GLuint vbo;
	GLuint ebo;
	GLint vertPosLocation;
} GlCtx;

typedef struct Session {
	int i;
} Session;

typedef struct State {
	Session session;
	CarkGuiState gui;
} State;

static
void shaderLogPrint(GLuint shader) {
	if (!glIsShader(shader)) {
		printf("invalid shader handle, unable to print log\n");
		return;
	}
	I32 bufSize = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &bufSize);
	if (bufSize <= 0) {
		printf("no shader log to print\n");
		return;
	}
	char *pLog = malloc(bufSize);
	I32 len = 0;
	glGetShaderInfoLog(shader, bufSize, &len, pLog);
	if (len > 0) {
		printf("shader log:\n\n%s\n\n", pLog);
	}
	free(pLog);
}

static
PixErr initGl(SDL_Window *pWindow, GlCtx *pGlCtx) {
	PixErr err = PIX_ERR_SUCCESS;
	GLint glErr = false;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	pGlCtx->pSdlCtx = SDL_GL_CreateContext(pWindow);
	SDL_ERR_RET(pGlCtx->pSdlCtx);
	GLEW_ERR_RET(glewInit());

	GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	const GLchar *vertShaderSrc = "\
		#version 410 core\n\
		layout (location = 0) in vec2 vertPos;\
		void main() {\
			gl_Position = vec4(vertPos.x, vertPos.y, .0, 1.0);\
		}\
	";
	const GLchar *fragShaderSrc = "\
		#version 410 core\n\
		out vec4 fragColor;\
		void main() {\
			fragColor = vec4(1.0, .0, .0, 1.0);\
		}\
	";
	glShaderSource(vertShader, 1, &vertShaderSrc, NULL);
	glCompileShader(vertShader);
	glGetShaderiv(vertShader, GL_COMPILE_STATUS, &glErr);
	shaderLogPrint(vertShader);
	if (!glErr) {
		PIX_ERR_RETURN(err, "vert shader compilation failed");
	}
	glShaderSource(fragShader, 1, &fragShaderSrc, NULL);
	glCompileShader(fragShader);
	glGetShaderiv(fragShader, GL_COMPILE_STATUS, &glErr);
	shaderLogPrint(fragShader);
	if (!glErr) {
		PIX_ERR_RETURN(err, "frag shader compilation failed");
	}
	pGlCtx->prog = glCreateProgram();
	glAttachShader(pGlCtx->prog, vertShader);
	glAttachShader(pGlCtx->prog, fragShader);
	glLinkProgram(pGlCtx->prog);
	glGetProgramiv(pGlCtx->prog, GL_LINK_STATUS, &glErr);
	{
		I32 bufSize = 0;
		glGetProgramiv(pGlCtx->prog, GL_INFO_LOG_LENGTH, &bufSize);
		if (bufSize > 0) {
			char *pLog = malloc(bufSize);
			I32 len = 0;
			glGetProgramInfoLog(pGlCtx->prog, bufSize, &len, pLog);
			if (len > 0) {
				printf("\nlink log:\n%s\n\n", pLog);
			}
		}
		else {
			printf("no link log to print\n");
		}
	}
	glDeleteShader(vertShader);
	glDeleteShader(fragShader);
	PIX_ERR_RETURN_IFNOT_COND(err, glErr, "gl link failed");

	GLfloat posArr[] = {
		-.5f, -.5f,
		.5f, -.5f,
		.5f, .5f,
		-.5f, .5f
	};
	GLuint cornerArr[] = {
		0, 1, 2, 2, 3, 0
	};

	glGenVertexArrays(1, &pGlCtx->vao);
	glBindVertexArray(pGlCtx->vao);
	glGenBuffers(1, &pGlCtx->ebo);
	glGenBuffers(1, &pGlCtx->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pGlCtx->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(posArr), posArr, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pGlCtx->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cornerArr), cornerArr, GL_STATIC_DRAW);
	I32 vec2Size = 2 * sizeof(GL_FLOAT);
	glVertexAttribPointer(pGlCtx->vertPosLocation, 2, GL_FLOAT, false, vec2Size, NULL);
	glEnableVertexAttribArray(pGlCtx->vertPosLocation);

	return err;
}

static
PixErr draw(SDL_Window *pWindow, GlCtx *pGlCtx) {
	PixErr err = PIX_ERR_SUCCESS;
	glClearColor(.2f, .2f, .2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(pGlCtx->prog);
	glBindVertexArray(pGlCtx->vao);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
	err = carkGuiDraw();
	PIX_ERR_RETURN_IFNOT(err, "");

	SDL_ERR_RET(SDL_GL_SwapWindow(pWindow));
	return err;
}

static
PixErr eventHandle(
	PixtyV2_I32 windowSize,
	SDL_Event *pEvent,
	bool *pExit
) {
	PixErr err = PIX_ERR_SUCCESS;
	switch (pEvent->type) {
		case SDL_EVENT_QUIT:
			//v fallthrough v
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			*pExit = true;
			return err;
		case SDL_EVENT_MOUSE_MOTION:
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			//..
			break;
		default:
			;
	}
	err = carkGuiEvent(pEvent);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
void fileOpenCallback(void *pUserData, const char *const *ppPathArr, I32 filter) {
	CarkGuiState *pGuiState = pUserData;
	if (!ppPathArr || !ppPathArr[0]) {
		return;
	}
	I32 pathLen = strnlen(ppPathArr[0], CARK_PATH_LEN_MAX);
	if (!pathLen || pathLen == CARK_PATH_LEN_MAX) {
		return;
	}
	PIX_ERR_ASSERT("", pGuiState->fileDialogActive && !pGuiState->pFileDialogPath);
	pGuiState->pFileDialogPath = malloc(pathLen + 1);
	memcpy(pGuiState->pFileDialogPath, ppPathArr[0], pathLen + 1);
	pGuiState->fileDialogActive = false;
}

static
PixErr guiEventHandle(SDL_Window *pWindow, State *pState, CarkGuiEvent event) {
	PixErr err = PIX_ERR_SUCCESS;
	switch (event) {
		case CARK_GUI_EVENT_FILE_OPEN:
			if (!pState->gui.fileDialogActive && !pState->gui.pFileDialogPath) {
				pState->gui.fileDialogActive = true;
				SDL_ShowOpenFileDialog(
					fileOpenCallback,
					&pState->gui,
					pWindow,
					NULL,
					0,
					NULL,
					false
				);
			}
			break;
		case CARK_GUI_EVENT_NONE:
			PIX_ERR_ASSERT("", false);
		default:
			;
	}
	return err;
}

static
PixErr update(State *pState) {
	PixErr err = PIX_ERR_SUCCESS;
	//TODO..

	if (!pState->gui.fileDialogActive && pState->gui.pFileDialogPath) {
		printf("opening log file %s\n", pState->gui.pFileDialogPath);
		free(pState->gui.pFileDialogPath);
		pState->gui.pFileDialogPath = NULL;
	}
	return err;
}

static
PixErr mainLoop(SDL_Window *pWindow, GlCtx *pGlCtx) {
	PixErr err = PIX_ERR_SUCCESS;

	State state = {0};
	do {
		PixtyV2_I32 windowSize = {0};
		SDL_GetWindowSize(pWindow, windowSize.d, windowSize.d + 1);
		SDL_Event event = {0};
		while (SDL_PollEvent(&event)) {
			bool exit = false;
			err = eventHandle(windowSize, &event, &exit);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (exit) {
				return err;
			}
		}

		CarkGuiEventQueue guiQueue = {0};
		err = carkGuiLayout(windowSize, &guiQueue);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		for (I32 i = 0; i < guiQueue.count; ++i) {
			err = guiEventHandle(pWindow, &state, guiQueue.queue[i]);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}

		err = update(&state);
		PIX_ERR_THROW_IFNOT(err, "", 0);

		err = draw(pWindow, pGlCtx);
		PIX_ERR_THROW_IFNOT(err, "draw failed", 0);
		SDL_Delay(1);
	} while(true);

	PIX_ERR_CATCH(0, err, ;);
	while (state.gui.fileDialogActive) {
		SDL_Delay(1);
	}
	if (state.gui.pFileDialogPath) {
		free(state.gui.pFileDialogPath);
	}
	return err;
}

int main(int argc, char **argv) {
	PixErr err = PIX_ERR_SUCCESS;
	SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	SDL_Window *pWindow = SDL_CreateWindow("Cark Vis", 720, 480, windowFlags);
	GlCtx glCtx = {0};
	SDL_ERR_THROW(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS), 0);
	SDL_ERR_THROW(pWindow, 0);

	err = initGl(pWindow, &glCtx);
	PIX_ERR_THROW_IFNOT(err, "failed to setup opengl", 0);

	err = carkGuiInit(pWindow, glCtx.pSdlCtx);
	PIX_ERR_THROW_IFNOT(err, "failed to init gui", 0);

	err = mainLoop(pWindow, &glCtx);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, ;);
	carkGuiDestroy();
	if (glIsBuffer(glCtx.vbo)) {
		glDeleteBuffers(1, &glCtx.vbo);
	}
	if (glIsBuffer(glCtx.ebo)) {
		glDeleteBuffers(1, &glCtx.ebo);
	}
	if (glIsVertexArray(glCtx.vao)) {
		glDeleteVertexArrays(1, &glCtx.vao);
	}
	if (glIsProgram(glCtx.prog)) {
		glDeleteProgram(glCtx.prog);
	}
	if (glCtx.pSdlCtx) {
		SDL_GL_DestroyContext(glCtx.pSdlCtx);
	}
	if (pWindow) {
		SDL_DestroyWindow(pWindow);
	}
	SDL_Quit();
	return err != PIX_ERR_SUCCESS;
}