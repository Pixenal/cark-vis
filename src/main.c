#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <pixenals_error_utils.h>
#include <pixenals_types.h>
#include <pixenals_math_utils.h>

#include <cark_vis_gui.hpp>

#define CARK_PATH_LEN_MAX 4096
#define PI 3.1415926536f

typedef int16_t I16;
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
	GLuint ubo;
	GLuint uboIdx;
	GLuint uboBind;
} GlCtx;

typedef struct View {
	PixtyV2_F32 pan;
	float yaw;
	float pitch;
	float camDist;
} View;

typedef struct Keys {
	bool pan;
	bool orbit;
} Keys;

static PixalcFPtrs alloc = {
	.fpMalloc = malloc,
	.fpCalloc = calloc,
	.fpFree = free,
	.fpRealloc = realloc
};

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

typedef struct DrawArgs {
	PixtyM4x4 persp;
	PixtyM4x4 view;
	PixtyV2_F32 pan;
	float yaw;
	float pitch;
	float camDist;
} DrawArgs;

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
		layout (location = 0) in vec3 vertPos;\
		layout (std140) uniform drawArgs {\
			mat4 persp;\
			mat4 view;\
			vec2 pan;\
			float yaw;\
			float pitch;\
			float camDist;\
		};\
		void main() {\
			float sinYaw = sin(yaw);\
			float cosYaw = cos(yaw);\
			float sinPitch = sin(pitch);\
			float cosPitch = cos(pitch);\
			mat4 yawMat = mat4(\
				cosYaw, .0f, -sinYaw, .0f,\
				.0f, 1.0f, .0f, .0f,\
				sinYaw, .0f, cosYaw, .0f,\
				.0f, .0f, .0f, 1.0f\
			);\
			mat4 pitchMat = mat4(\
				1.0, .0, .0, .0f,\
				.0, cosPitch, sinPitch, .0f,\
				.0, -sinPitch, cosPitch, .0f,\
				.0f, .0f, .0f, 1.0f\
			);\
			mat4 rot = pitchMat * yawMat;\
			vec4 pos = rot * vec4(vertPos, 1.0f);\
			pos.xyz += vec3(.0f, .0f, -1.0f) * camDist;\
			pos.xy += pan;\
			gl_Position = persp * pos;\
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
		-.5f, -.5f, -.5f,
		.5f, -.5f, -.5f,
		.5f, -.5f, .5f,
		-.5f, -.5f, .5f,
		-.5f, .5f, -.5f,
		.5f, .5f, -.5f,
		.5f, .5f, .5f, 
		-.5f, .5f, .5f
	};
	GLuint cornerArr[] = {
		0, 1, 2, 2, 3, 0,
		0, 1, 5, 5, 4, 0,
		1, 2, 6, 6, 5, 1,
		2, 3, 7, 7, 6, 2,
		3, 0, 4, 4, 7, 3,
		4, 5, 6, 6, 7, 2
	};

	glGenVertexArrays(1, &pGlCtx->vao);
	glBindVertexArray(pGlCtx->vao);
	glGenBuffers(1, &pGlCtx->ebo);
	glGenBuffers(1, &pGlCtx->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pGlCtx->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(posArr), posArr, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pGlCtx->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cornerArr), cornerArr, GL_STATIC_DRAW);
	I32 vec3Size = 3 * sizeof(GL_FLOAT);
	glVertexAttribPointer(pGlCtx->vertPosLocation, 3, GL_FLOAT, false, vec3Size, NULL);
	glEnableVertexAttribArray(pGlCtx->vertPosLocation);

	glGenBuffers(1, &pGlCtx->ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, pGlCtx->ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(DrawArgs), NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	pGlCtx->uboBind = 0;
	glBindBufferBase(GL_UNIFORM_BUFFER, pGlCtx->uboBind, pGlCtx->ubo);
	pGlCtx->uboIdx = glGetUniformBlockIndex(pGlCtx->prog, "drawArgs");
	glUniformBlockBinding(pGlCtx->prog, pGlCtx->uboIdx, pGlCtx->uboBind);

	return err;
}

static
PixtyM4x4 frustum(
	float left,
	float right,
	float bottom,
	float top,
	float zNear,
	float zFar
) {
	float a = (right + left) / (right - left);
	float b = (top + bottom) / (top - bottom);
	float c = -(zFar + zNear) / (zFar - zNear);
	float d = -(2.0f * zFar * zNear) / (zFar - zNear);
	return (PixtyM4x4) {
		2.0f * zNear / (right - left), .0f, .0f, .0f,
		.0f, 2.0f * zNear / (top - bottom), .0f, .0f,
		a, b, c, -1.0f,
		.0f, .0f, d, .0f
	};
}

static
PixtyM4x4 perspective(double yFov, double aspect, double zNear, double zFar) {
	float f = 1.0f / tanf(yFov / 2.0f);
	return (PixtyM4x4) {
		f / aspect, .0f, .0f, .0f,
		.0f, f, .0f, .0f,
		.0f, .0f, (zNear + zFar) / (zNear - zFar), -1.0f,
		.0f, .0f, (2.0f * zNear * zFar) / (zNear - zFar), .0f
	};
}

static
PixErr draw(
	SDL_Window *pWindow,
	GlCtx *pGlCtx,
	const View *pView,
	PixtyV2_I32 windowSize
) {
	PixErr err = PIX_ERR_SUCCESS;
	glClearColor(.2f, .2f, .2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(pGlCtx->prog);

	float aspect = (F32)windowSize.d[0] / (F32)windowSize.d[1];
	float zNear = .001f;
	float zFar = 100.0f;
	DrawArgs drawArgs = {
		.persp = perspective(PI / 4.0f, aspect, zNear, zFar),
		.view = {
			1.0f, .0f, .0f, .0f,
			.0f, 1.0f, .0f, 0.0f,
			.0f, .0f, 1.0f, 0.0f,
			.0f, .0f, 0.0f, 1.0f
		},
		.pan = pView->pan,
		.yaw = pView->yaw,
		.pitch = pView->pitch,
		.camDist = pView->camDist
	};
	glBindBuffer(GL_UNIFORM_BUFFER, pGlCtx->ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(DrawArgs), &drawArgs);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	glBindVertexArray(pGlCtx->vao);
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, NULL);
	err = carkGuiDraw();
	PIX_ERR_RETURN_IFNOT(err, "");

	SDL_ERR_RET(SDL_GL_SwapWindow(pWindow));
	return err;
}

static
PixErr eventHandle(
	PixtyV2_I32 windowSize,
	SDL_Event *pEvent,
	bool *pExit,
	View *pView,
	Keys *pKeys
) {
	PixErr err = PIX_ERR_SUCCESS;
	PixtyV2_F32 fWindowSize = {(F32)windowSize.d[0], (F32)windowSize.d[1]};
	switch (pEvent->type) {
		case SDL_EVENT_QUIT:
			//v fallthrough v
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			*pExit = true;
			return err;
		case SDL_EVENT_MOUSE_MOTION:
			if (pKeys->orbit) {
				PixtyV2_F32 motion = {pEvent->motion.xrel, pEvent->motion.yrel};
				if (pKeys->pan) {
					motion = _(motion V2MULS .05f);
					motion.d[1] *= -1.0f;
					_(&pView->pan V2ADDEQL motion);
				}
				else {
					motion = _(_(motion V2DIV fWindowSize) V2MULS 2.0f * PI);
					float wrap = 2.0f * PI;
					pView->yaw = fmod(pView->yaw + motion.d[0], wrap);
					pView->pitch = fmod(pView->pitch + motion.d[1], wrap);
				}
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			pKeys->orbit = pEvent->button.button == SDL_BUTTON_MIDDLE;
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (pEvent->button.button == SDL_BUTTON_MIDDLE) {
				pKeys->orbit = false;
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			switch (pEvent->key.key) {
				case SDLK_LSHIFT:
				//v fallthrough v
				case SDLK_RSHIFT:
					pKeys->pan = true;
					break;
				default:
					;
			}
			break;
		case SDL_EVENT_KEY_UP:
			switch (pEvent->key.key) {
				case SDLK_LSHIFT:
				//v fallthrough v
				case SDLK_RSHIFT:
					pKeys->pan = false;
					break;
				default:
					;
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			pView->camDist -= pEvent->wheel.y;
			pView->camDist = pView->camDist >= .0f ? pView->camDist : .0f;
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
PixErr guiEventHandle(SDL_Window *pWindow, CarkGuiState *pGui, CarkGuiEvent event) {
	PixErr err = PIX_ERR_SUCCESS;
	switch (event) {
		case CARK_GUI_EVENT_FILE_OPEN:
			if (!pGui->fileDialogActive && !pGui->pFileDialogPath) {
				pGui->fileDialogActive = true;
				SDL_ShowOpenFileDialog(
					fileOpenCallback,
					pGui,
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
void sessionClear(Session *pSession) {
	for (I32 i = 0; i < pSession->logArr.size; ++i) {
		carkInStageLogDestroy(&alloc, pSession->logArr.pArr + i);
	}
	carkInFileDestroy(&alloc, &pSession->file);
	pSession->info = (CarkInFileInfo){0};
}

static
void sessionDestroy(Session *pSession) {
	sessionClear(pSession);
	if (pSession->logArr.pArr) {
		free(pSession->logArr.pArr);
	}
	*pSession = (Session){0};
}

static
PixErr openNewSession(CarkGuiState *pGui, Session *pSession) {
	PixErr err = PIX_ERR_SUCCESS;
	printf("opening log file %s\n", pGui->pFileDialogPath);
	
	sessionClear(pSession);
	CarkInCtx carkCtx = {0};
	err = carkInInit(NULL, NULL, &carkCtx);
	PIX_ERR_THROW_IFNOT(err, "", 1);
	err = carkInFileInit(&carkCtx, &pSession->file);
	PIX_ERR_THROW_IFNOT(err, "", 1);
	err = carkInFileOpen(&carkCtx, pGui->pFileDialogPath, &pSession->file);
	PIX_ERR_THROW_IFNOT(err, "", 1);
	err = carkInFileLoadInfo(&carkCtx, &pSession->file, &pSession->info);
	PIX_ERR_THROW_IFNOT(err, "", 1);
	PIXALC_DYN_ARR_RESIZE(
		CarkInStageLog,
		&alloc,
		&pSession->logArr,
		pSession->info.pStageArr->count
	);
	for (I32 i = 0; i < pSession->info.pStageArr->count; ++i) {
		err = carkInFileLoadLog(
			&carkCtx,
			&pSession->file,
			i,
			pSession->logArr.pArr + i
		);
		PIX_ERR_THROW_IFNOT(err, "", 1);
	}
	PIX_ERR_CATCH(1, err, ;);
	if (carkInFileIsOpen(&pSession->file)) {
		PixErr closeErr = carkInFileClose(&carkCtx, &pSession->file);
		err = err == PIX_ERR_SUCCESS ? closeErr : err;
	}
	carkInCtxDestroy(&carkCtx);
	free(pGui->pFileDialogPath);
	pGui->pFileDialogPath = NULL;
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err,
		sessionClear(pSession);
	);
	return err;
}

static
PixErr update(CarkGuiState *pGui, Session *pSession, View *pView) {
	PixErr err = PIX_ERR_SUCCESS;
	//TODO..

	if (!pGui->fileDialogActive && pGui->pFileDialogPath) {
		err = openNewSession(pGui, pSession);
		if (err != PIX_ERR_SUCCESS) {
			printf("failed to open log file\n");
			err = PIX_ERR_SUCCESS;
		}
	}
	return err;
}

static
PixErr mainLoop(SDL_Window *pWindow, GlCtx *pGlCtx) {
	PixErr err = PIX_ERR_SUCCESS;

	Session session = {0};
	CarkGuiState gui = {0};
	View view = {.yaw = PI * .25f, .pitch = PI * .125f, .camDist = 6.0f};
	Keys keys = {0};
	do {
		PixtyV2_I32 windowSize = {0};
		SDL_GetWindowSize(pWindow, windowSize.d, windowSize.d + 1);
		SDL_Event event = {0};
		while (SDL_PollEvent(&event)) {
			bool exit = false;
			err = eventHandle(windowSize, &event, &exit, &view, &keys);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (exit) {
				return err;
			}
		}

		CarkGuiEventQueue guiQueue = {0};
		err = carkGuiLayout(&session, windowSize, &guiQueue);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		for (I32 i = 0; i < guiQueue.count; ++i) {
			err = guiEventHandle(pWindow, &gui, guiQueue.queue[i]);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}

		err = update(&gui, &session, &view);
		PIX_ERR_THROW_IFNOT(err, "", 0);

		err = draw(pWindow, pGlCtx, &view, windowSize);
		PIX_ERR_THROW_IFNOT(err, "draw failed", 0);
		SDL_Delay(1);
	} while(true);

	PIX_ERR_CATCH(0, err, ;);
	while (gui.fileDialogActive) {
		SDL_Delay(1);
	}
	if (gui.pFileDialogPath) {
		free(gui.pFileDialogPath);
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