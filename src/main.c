#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <pixenals_error_utils.h>
#include <pixenals_types.h>
#include <pixenals_math_utils.h>
#include <pixenals_mesh_utils.h>

#include <cark_vis_gui.hpp>

#define CARK_PATH_LEN_MAX 4096
#define PI 3.1415926536f
#define VERT_POS_LOCATION 0

typedef int16_t I16;
typedef int32_t I32;
typedef uint32_t U32;
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
	GLuint frameBuf;
	GLuint depthBuf;
	GLuint targetTex;
	PixtyV2_I32 frameSize;
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

typedef enum StageDataType {
	STAGE_DATA_NONE,
	STAGE_DATA_ARRAY,
	STAGE_DATA_MESH,
	STAGE_DATA_ENUM_COUNT
} StageDataType;

static PixalcFPtrs alloc = {
	.fpMalloc = malloc,
	.fpCalloc = calloc,
	.fpFree = free,
	.fpRealloc = realloc
};

typedef struct Required {
	CarkDesc *pArr;
	I32 size;
} Required;

//first required element not included,
//eg STAGE_DATA_MESH requires CARK_DESC_FACE as root struct
static CarkDesc requiredMeshArr[] = {CARK_DESC_POS};
static Required requiredTable[STAGE_DATA_ENUM_COUNT] = {0};

static StageDataType typeFromDesc[CARK_DESC_ENUM_COUNT] = {0};

typedef struct V3_F32Arr {
PixtyV3_F32 *pArr;
	I32 size;
	I32 count;
} V3_F32Arr;

typedef struct FaceRange {
	I32 start;
	I32 size;
} FaceRange;

typedef struct Mesh {
	PixtyI32Arr faces;
	PixtyI32Arr corners;
	V3_F32Arr pos;
	bool tris;
} Mesh;

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
		layout(location = 0) out vec3 fragColor;\
		void main() {\
			fragColor = vec3(1.0, .0, .0);\
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
	glVertexAttribPointer(VERT_POS_LOCATION, 3, GL_FLOAT, false, vec3Size, NULL);
	glEnableVertexAttribArray(VERT_POS_LOCATION);

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
PixErr frameBufInit(GlCtx *pGlCtx, PixtyV2_I32 size) {
	PixErr err = PIX_ERR_SUCCESS;
	if (!size.d[0] || !size.d[1]) {
		size = (PixtyV2_I32){256, 256};
	}
	pGlCtx->frameSize = size;
	glGenFramebuffers(1, &pGlCtx->frameBuf);
	glBindFramebuffer(GL_FRAMEBUFFER, pGlCtx->frameBuf);
	glGenTextures(1, &pGlCtx->targetTex);
	glBindTexture(GL_TEXTURE_2D, pGlCtx->targetTex);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGB,
		size.d[0],
		size.d[1],
		0,
		GL_RGB,
		GL_UNSIGNED_BYTE,
		NULL
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glGenRenderbuffers(1, &pGlCtx->depthBuf);
	glBindRenderbuffer(GL_RENDERBUFFER, pGlCtx->depthBuf);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.d[0], size.d[1]);
	glFramebufferRenderbuffer(
		GL_FRAMEBUFFER,
		GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER,
		pGlCtx->depthBuf
	);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, pGlCtx->targetTex, 0);
	glDrawBuffers(1, (GLenum[]){GL_COLOR_ATTACHMENT0});
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
		""
	);
	return err;
}

static
void frameBufDestroy(GlCtx *pGlCtx) {
	glDeleteRenderbuffers(1, &pGlCtx->depthBuf);
	glDeleteTextures(1, &pGlCtx->targetTex);
	glDeleteFramebuffers(1, &pGlCtx->frameBuf);
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
	Session *pSession,
	GlCtx *pGlCtx,
	const View *pView,
	PixtyV2_I32 windowSize
) {
	PixErr err = PIX_ERR_SUCCESS;
	glBindFramebuffer(GL_FRAMEBUFFER, pGlCtx->frameBuf);
	glViewport(0, 0, pGlCtx->frameSize.d[0], pGlCtx->frameSize.d[1]);
	glClearColor(.1f, .1f, .1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(pGlCtx->prog);

	float aspect = (F32)pGlCtx->frameSize.d[0] / (F32)pGlCtx->frameSize.d[1];
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
	I32 triCount;
	if (pSession->renderMesh.triCount) {
		glBindVertexArray(pSession->renderMesh.vao);
		triCount = pSession->renderMesh.triCount;
	}
	else {
		glBindVertexArray(pGlCtx->vao);
		triCount = 12;
	}
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glEnable(GL_DEPTH_TEST);
	glDrawElements(GL_TRIANGLES, triCount * 3, GL_UNSIGNED_INT, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(.0f, .0f, .0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
		case CARK_GUI_EVENT_README:
			SDL_OpenURL("https://www.github.com/pixenal/cark-vis");
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

#define STACK_SIZE 32

typedef struct StackEntry {
	CarkRef ref;
	I32 nextComp;
	U32 nextRef : 30;
	U32 singleComp : 1;
	U32 seen : 1;
} StackEntry;

typedef struct Stack {
	StackEntry stack[STACK_SIZE];
	I32 ptr;
} Stack;

typedef struct EntryPtrs {
	const CarkStage *pStage;
	const CarkStructInfo *pStruct;
	const CarkCompInfo *pComp;
} EntryPtrs;

static
EntryPtrs stackEntryPtrsGet(const Session *pSession, const StackEntry *pEntry) {
	const CarkStage *pStage = pSession->info.pStageArr->pArr + pEntry->ref.stageIdx;
	const CarkStructInfo *pStruct = &pStage->pStructArr[pEntry->ref.structIdx].info;
	return (EntryPtrs) {
		.pStage = pStage,
		.pStruct = pStruct,
		.pComp = pStruct->pCompArr + pEntry->ref.compIdx
	};
}

static
void stackPush(
	const Session *pSession,
	Stack *pStack,
	const CarkRef *pRef
) {
	const StackEntry *pParentEntry = pStack->stack + pStack->ptr;
	I32 stageIdx = pRef->stageIdx == -1 ? pParentEntry->ref.stageIdx : pRef->stageIdx;
	const CarkStage *pStage = pSession->info.pStageArr->pArr + stageIdx;
	I32 structIdx = pRef->structIdx == -1 ? pParentEntry->ref.structIdx : pRef->structIdx;
	const CarkStructInfo *pStruct = &pStage->pStructArr[structIdx].info;
	bool singleComp = pRef->compIdx != -1;
	I32 compIdx = singleComp ? pRef->compIdx : 0;

	++pStack->ptr;
	pStack->stack[pStack->ptr] = (StackEntry){
		.ref = {.stageIdx = stageIdx, .structIdx = structIdx, .compIdx = compIdx},
		.nextRef = 0,
		.nextComp = compIdx,
		.singleComp = singleComp
	};
}

static
void stackInit(Stack *pStack, I32 stageIdx, I32 structIdx) {
	*pStack = (Stack){
		.stack = {
			{
				.ref = {
					.stageIdx = stageIdx,
					.structIdx = structIdx
				},
				.nextRef = 0,
				.nextComp = 0
			}
		}
	};
}

static
void stackPop(Stack *pStack) {
	--pStack->ptr;
}

static
bool stackPushNext(const Session *pSession, Stack *pStack) {
	StackEntry *pEntry = pStack->stack + pStack->ptr;
	EntryPtrs ptrs = stackEntryPtrsGet(pSession, pEntry);
	pEntry->seen = true;
	if (pEntry->nextRef < ptrs.pComp->refCount) {
		const CarkRef *pRef = ptrs.pComp->refArr + pEntry->nextRef;
		stackPush(pSession, pStack, pRef);
		++pEntry->nextRef;
		return true;
	}
	//TODO fix this, pushes individual position vec components
	//shouldn't do that
	/*
	if (!pEntry->singleComp && pEntry->nextComp < ptrs.pStruct->compCount) {
		ptrs.pComp = ptrs.pStruct->pCompArr + pEntry->nextComp;
		stackPush(pSession, pStack, ptrs.pComp->refArr);
		++pEntry->nextComp;
		pEntry->nextRef = 0;
		return true;
	}
	*/
	return false;
}

typedef struct DescIdx {
	CarkRef ref;
	U32 order : 31;
	U32 valid : 1;
} DescIdx;

static
bool validAsStageDataType(const DescIdx *pContains, StageDataType type) {
	const Required *pRequired = requiredTable + type;
	for (I32 i = 0; i < pRequired->size; ++i) {
		CarkDesc requiredDesc = pRequired->pArr[i];
		DescIdx item = pContains[requiredDesc];
		if (!item.valid) {
			return false;
		}
		for (I32 j = 0; j < i; ++j) {
			DescIdx itemPrev = pContains[pRequired->pArr[j]];
			PIX_ERR_ASSERT("", itemPrev.valid && item.order != itemPrev.order);
			if (item.order < itemPrev.order) {
				return false;
			}
		}
	}
	return true;
}

//TODO allow for redirection of individual vector components,
//eg, x, y and/or z are located in different structs.
static
bool structIsValid(const CarkStructInfo *pStruct, I32 compIdx) {
	CarkCompDesc descCheck[CARK_COMP_DESC_ENUM_COUNT] = {0};
	switch (pStruct->desc) {
		case CARK_DESC_IDX:
			if (pStruct->pCompArr[compIdx].desc != CARK_COMP_DESC_IDX) {
				return false;
			}
			break;
		case CARK_DESC_FACE:
			if (pStruct->pCompArr[compIdx].desc != CARK_COMP_DESC_IDX) {
				return false;
			}
			for (I32 i = 0; i < pStruct->compCount; ++i) {
				descCheck[pStruct->pCompArr[i].desc] = true;
			}
			if (!descCheck[CARK_COMP_DESC_SIZE]) {
				return false;
			}
			break;
		case CARK_DESC_POS:
			if (pStruct->compCount < 2 || pStruct->compCount > 3) {
				return false;
			}
			for (I32 i = 0; i < pStruct->compCount; ++i) {
				if (pStruct->pCompArr[i].type != CARK_TYPE_F32) {
					return false;
				}
				descCheck[pStruct->pCompArr[i].desc] = true;
			}
			if (!descCheck[CARK_COMP_DESC_VEC_X] ||
			    !descCheck[CARK_COMP_DESC_VEC_Y] ||
			    !descCheck[CARK_COMP_DESC_VEC_Z]
			) {
				return false;
			}
			break;
		case CARK_DESC_UV:
			if (pStruct->compCount != 2) {
				return false;
			}
			for (I32 i = 0; i < pStruct->compCount; ++i) {
				if (pStruct->pCompArr[i].type != CARK_TYPE_F32) {
					return false;
				}
				descCheck[pStruct->pCompArr[i].desc] = true;
			}
			if (!descCheck[CARK_COMP_DESC_VEC_X] || !descCheck[CARK_COMP_DESC_VEC_Y]) {
				return false;
			}
			break;
		default:
			;
	}
	return true;
}

static
bool seenEntry(const StackEntry *pEntry) {
	return pEntry->seen;
}

static
PixErr parseStructInfo(
	const Session *pSession,
	I32 stageIdx,
	I32 structIdx,
	StageDataType *pType,
	DescIdx *pContains
) {
	PixErr err = PIX_ERR_SUCCESS;
	I32 descOrder = 0;
	Stack stack = {0};
	stackInit(&stack, stageIdx, structIdx);
	*pType = typeFromDesc[
		stackEntryPtrsGet(pSession, stack.stack + stack.ptr).pStruct->desc
	];
	bool pop;
	do {
		pop = true;
		StackEntry *pEntry = stack.stack + stack.ptr;
		EntryPtrs ptrs = stackEntryPtrsGet(pSession, pEntry);
		CarkDesc desc = ptrs.pStruct->desc;
		if (!pEntry->seen) {
			if (!structIsValid(ptrs.pStruct, pEntry->ref.compIdx)) {
				continue;
			}
			if (desc != CARK_DESC_IDX) {
				pContains[desc] = (DescIdx){
					.ref = pEntry->ref,
					.order = descOrder,
					.valid = true
				};
				++descOrder;
			}
		}
		if (stackPushNext(pSession, &stack)) {
			pop = false;
		}
	} while(pop ? stackPop(&stack) : 0, stack.ptr >= 0); 
	if (!validAsStageDataType(pContains, *pType)) {
		//fallback to array,
		//this will simple display structure in a spreadsheet format
		*pType = STAGE_DATA_ARRAY;
	}
	return err;
}

static
const CarkStruct *structFromRef(const Session *pSession, CarkRef ref) {
	return pSession->info.pStageArr->pArr[ref.stageIdx].pStructArr + ref.structIdx;
}

static
const CarkInStructLog *structLogFromRef(const Session *pSession, CarkRef ref) {
	return pSession->logArr.pArr[ref.stageIdx].structs.pArr + ref.structIdx;
}

static
PixErr faceCornersGet(
	const Session *pSession,
	const DescIdx *pContains,
	FaceRange faceRange,
	PixtyI32Arr *pCornerBuf
) {
	PixErr err = PIX_ERR_SUCCESS;
	pCornerBuf->count = 0;
	CarkRef cornerRef = pContains[CARK_DESC_CORNER].ref;
	const CarkStruct *pCornerInfo = structFromRef(pSession, cornerRef);
	const CarkInStructLog *pCornerLog = structLogFromRef(pSession, cornerRef);
	PixtyRange range = (PixtyRange){0};
	I32 cornerOffset = 0;
	bool found = false;
	for (I32 i = 0; i < pCornerLog->rangeArr.count; ++i) {
		range = pCornerLog->rangeArr.pArr[i];
		if (faceRange.start >= range.start &&
		    faceRange.start + faceRange.size <= range.end
		) {
			found = true;
			break;
		}
		cornerOffset += range.end - range.start;
	}
	if (!found) {
		return err;//corners weren't logged
	}
	for (I32 i = 0; i < faceRange.size; ++i) {
		I32 idx = cornerOffset + faceRange.start - range.start + i;
		I32 byteIdx = idx * (pCornerInfo->byteSize + sizeof(I32));
		I32 vertIdx = *(I32 *)(pCornerLog->data.pArr + byteIdx + sizeof(F32));
		I32 newIdx = 0;
		PIXALC_DYN_ARR_ADD(I32, &alloc, pCornerBuf, newIdx);
		pCornerBuf->pArr[newIdx] = vertIdx;
	}
	return err;
}

static
PixErr facePosGet(
	const Session *pSession,
	const DescIdx *pContains,
	FaceRange faceRange,
	PixtyI32Arr *pCornerBuf,
	V3_F32Arr *pPosBuf,
	PixtyValidIdx *pVertRedir,
	bool *pNoLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	pPosBuf->count = 0;
	CarkRef posRef = pContains[CARK_DESC_POS].ref;
	const CarkStruct *pPosInfo = structFromRef(pSession, posRef);
	const CarkInStructLog *pPosLog = structLogFromRef(pSession, posRef);
	PIX_ERR_ASSERT("", faceRange.size == pCornerBuf->count);
	for (I32 i = 0; i < faceRange.size; ++i) {
		I32 vertIdx = pCornerBuf->pArr[i];
		if (pVertRedir[vertIdx].valid) {
			continue;
		}
		I32 posOffset = 0;
		PixtyV3_F32 pos = {0};
		bool found = false;
		for (I32 j = 0; j < pPosLog->rangeArr.count; ++j) {
			PixtyRange vertRange = pPosLog->rangeArr.pArr[j];
			if (vertIdx >= vertRange.start && vertIdx < vertRange.end) {
				I32 byteIdx = (posOffset + vertIdx - vertRange.start) * (pPosInfo->byteSize + sizeof(I32));
				memcpy(
					pos.d,
					pPosLog->data.pArr + byteIdx + sizeof(F32),
					sizeof(PixtyV3_F32)
				);
				found = true;
				break;
			}
			posOffset += vertRange.end - vertRange.start;
		}
		if (!found) {
			*pNoLog = true;
			return err;//pos wasn't logged
		}
		I32 newIdx = 0;
		PIXALC_DYN_ARR_ADD(PixtyV3_F32, &alloc, pPosBuf, newIdx);
		pPosBuf->pArr[newIdx] = pos;
	}
	return err;
}

static
void meshAddFace(
	Mesh *pMesh,
	FaceRange faceRange,
	const PixtyI32Arr *pCornerBuf,
	const V3_F32Arr *pPosBuf,
	PixtyValidIdx *pVertRedir
) {
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(I32, &alloc, &pMesh->faces, newIdx);
	pMesh->faces.pArr[newIdx] = pMesh->corners.count;
	I32 posBufIdx = 0;
	for (I32 i = 0; i < faceRange.size; ++i) {
		PixtyValidIdx *pRedirEntry = pVertRedir + pCornerBuf->pArr[i];
		I32 vertIdx = 0;
		if (pRedirEntry->valid) {
			vertIdx = (I32)pRedirEntry->idx;
		}
		else {
			PIXALC_DYN_ARR_ADD(PixtyV3_F32, &alloc, &pMesh->pos, vertIdx);
			pMesh->pos.pArr[vertIdx] = pPosBuf->pArr[posBufIdx];
			++posBufIdx;
			*pRedirEntry = (PixtyValidIdx){.valid = true, .idx = vertIdx};
		}
		newIdx = 0;
		PIXALC_DYN_ARR_ADD(I32, &alloc, &pMesh->corners, newIdx);
		pMesh->corners.pArr[newIdx] = vertIdx;
	}
}

//TODO generalise (eg mesh may not use a corner list)
static
PixErr meshFromLog(
	const Session *pSession,
	const DescIdx *pContains,
	PixtyI32Arr *pCornerBuf,
	V3_F32Arr *pPosBuf,
	Mesh *pMesh
) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkRef faceRef = pContains[CARK_DESC_FACE].ref;
	const CarkStruct *pFaceInfo = structFromRef(pSession, faceRef);
	const CarkInStructLog *pFaceLog = structLogFromRef(pSession, faceRef);
	
	I32 vertCount;
	{
		CarkRef posRef = pContains[CARK_DESC_POS].ref;
		const CarkInStructLog *pPosLog = structLogFromRef(pSession, posRef);
		vertCount = carkInStructLogCount(pPosLog);
	}
	PixtyValidIdx *pVertRedir = calloc(vertCount, sizeof(PixtyValidIdx));
	I32 faceOffset = 0;

	I32 sizeCompIdx = -1;
	for (I32 i = 0; i < pFaceInfo->info.compCount; ++i) {
		if (pFaceInfo->info.pCompArr[i].desc == CARK_COMP_DESC_SIZE) {
			sizeCompIdx = i;
			break;
		}
	}
	PIX_ERR_ASSERT("existance of size comp was checked during parsing", sizeCompIdx != -1);
	for (I32 i = 0; i < pFaceLog->rangeArr.count; ++i) {
		PixtyRange range = pFaceLog->rangeArr.pArr[i];
		I32 rangeSize = range.end - range.start;
		for (I32 j = 0; j < rangeSize; ++j) {
			I32 byteIdx = (faceOffset + j) * (pFaceInfo->byteSize + sizeof(I32));
			const U8 *pData = pFaceLog->data.pArr + byteIdx;
			//TODO assuming components before size comp are i32,
			//put a func in io lib to get byte offset of a component idx
			//(accounting for byte size of other components in struct)
			FaceRange faceRange = {
				.start = *(I32 *)(pData + sizeof(F32)),
				.size = *(I32 *)(pData + sizeof(F32) + sizeCompIdx * sizeof(I32))
			};
			err = faceCornersGet(pSession, pContains, faceRange, pCornerBuf);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (!pCornerBuf->count) {
				//one or all corners weren't logged
				break;
			}
			bool noLog = false;
			err = facePosGet(
				pSession,
				pContains,
				faceRange,
				pCornerBuf,
				pPosBuf,
				pVertRedir,
				&noLog
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (noLog) {
				break;//one or all positions weren't logged
			}
			meshAddFace(pMesh, faceRange, pCornerBuf, pPosBuf, pVertRedir);
		}
		faceOffset += rangeSize;
	}
	PIXALC_DYN_ARR_RESIZE(I32, &alloc, &pMesh->faces, pMesh->faces.count + 1);
	pMesh->faces.pArr[pMesh->faces.count] = pMesh->corners.count;
	PIX_ERR_CATCH(0, err, ;);
	if (pVertRedir) {
		free(pVertRedir);
	}
	return err;
}

static
void triAdd(PixtyI32Arr *pTris, const Mesh *pMesh, I32 start, I32 a, I32 b, I32 c) {
	PIXALC_DYN_ARR_RESIZE(I32, &alloc, pTris, pTris->count + 3);
	pTris->pArr[pTris->count + 0] = pMesh->corners.pArr[start + a];
	pTris->pArr[pTris->count + 1] = pMesh->corners.pArr[start + b];
	pTris->pArr[pTris->count + 2] = pMesh->corners.pArr[start + c];
	pTris->count += 3;
}

static
PixtyV3_F32 meshPosGet(const void *pMeshRaw, PixmshFaceRange face, int32_t corner) {
	const Mesh *pMesh = pMeshRaw;
	I32 cornerAbs = face.start + corner;
	PIX_ERR_ASSERT("", cornerAbs >= 0 && cornerAbs < pMesh->corners.count);
	I32 vertIdx = pMesh->corners.pArr[cornerAbs];
	PIX_ERR_ASSERT("", vertIdx >= 0 && vertIdx < pMesh->pos.count);
	return pMesh->pos.pArr[vertIdx];
}

static
PixErr meshTriangulate(Mesh *pMesh) {
	PixErr err = PIX_ERR_SUCCESS;
	PixtyI32Arr tris = {0};
	PixtyU8Arr idxBuf = {0};
	PIX_ERR_ASSERT("", pMesh->faces.pArr && pMesh->corners.pArr && pMesh->pos.pArr);
	for (I32 i = 0; i < pMesh->faces.count; ++i) {
		FaceRange face = {.start = pMesh->faces.pArr[i]};
		face.size = pMesh->faces.pArr[i + 1] - face.start;
		if (face.size == 3) {
			triAdd(&tris, pMesh, face.start, 0, 1, 2);
			continue;
		}
		if (face.size == 4) {
			triAdd(&tris, pMesh, face.start, 0, 1, 2);
			triAdd(&tris, pMesh, face.start, 2, 3, 0);
			continue;
		}
		if (face.size > (I32)UINT8_MAX) {
			continue;//idx-buf uses U8, so skip this face
		}
		PIX_ERR_ASSERT("", face.size >= 2);
		PIXALC_DYN_ARR_RESIZE(U8, &alloc, &idxBuf, face.size - 2);
		I32 triCount = pixmshTriangulateFace(
			&alloc,
			(PixmshFaceRange){.start = face.start, .size = face.size},
			pMesh,
			meshPosGet,
			idxBuf.pArr
		);
		for (I32 j = 0; j < triCount; ++j) {
			I32 start = j * 3;
			triAdd(
				&tris,
				pMesh,
				face.start,
				idxBuf.pArr[start + 0],
				idxBuf.pArr[start + 1],
				idxBuf.pArr[start + 2]
			);
		}
	}
	free(pMesh->faces.pArr);
	pMesh->faces = (PixtyI32Arr){0};
	free(pMesh->corners.pArr);
	pMesh->corners = tris;
	pMesh->tris = true;

	if (idxBuf.pArr) {
		free(idxBuf.pArr);
	}
	return err;
}

static
void meshDestroy(Mesh *pMesh) {
	if (pMesh->faces.pArr) {
		free(pMesh->faces.pArr);
	}
	if (pMesh->corners.pArr) {
		free(pMesh->corners.pArr);
	}
	if (pMesh->pos.pArr) {
		free(pMesh->pos.pArr);
	}
	*pMesh = (Mesh){0};
}

static
PixErr meshLoadOnGpu(Session *pSession, const Mesh *pMesh) {
	PixErr err = PIX_ERR_SUCCESS;
	glGenVertexArrays(1, &pSession->renderMesh.vao);
	glBindVertexArray(pSession->renderMesh.vao);
	glGenBuffers(1, &pSession->renderMesh.ebo);
	glGenBuffers(1, &pSession->renderMesh.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pSession->renderMesh.vbo);
	glBufferData(
		GL_ARRAY_BUFFER,
		pMesh->pos.count * sizeof(PixtyV3_F32),
		pMesh->pos.pArr,
		GL_STATIC_DRAW
	);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pSession->renderMesh.ebo);
	glBufferData(
		GL_ELEMENT_ARRAY_BUFFER,
		pMesh->corners.count * sizeof(I32),
		pMesh->corners.pArr,
		GL_STATIC_DRAW
	);
	I32 vecSize = sizeof(PixtyV3_F32);
	glVertexAttribPointer(VERT_POS_LOCATION, 3, GL_FLOAT, false, vecSize, NULL);
	glEnableVertexAttribArray(VERT_POS_LOCATION);
	pSession->renderMesh.triCount = pMesh->corners.count / 3;
	return err;
}

static
PixErr openNewSession(CarkGuiState *pGui, Session *pSession) {
	PixErr err = PIX_ERR_SUCCESS;
	printf("opening log file %s\n", pGui->pFileDialogPath);
	
	sessionClear(pSession);
	CarkInCtx carkCtx = {0};
	PixtyI32Arr cornerBuf = {0};
	V3_F32Arr posBuf = {0};
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
	pSession->logArr.size = pSession->info.pStageArr->count;
	pSession->logArr.pArr = calloc(pSession->logArr.size, sizeof(CarkInStageLog));
	for (I32 i = 0; i < pSession->info.pStageArr->count; ++i) {
		err = carkInFileLoadLog(&carkCtx, &pSession->file, i, pSession->logArr.pArr + i);
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

	for (I32 i = 0; i < pSession->info.pStageArr->count; ++i) {
		const CarkStage *pStage = pSession->info.pStageArr->pArr + i;
		if (!pStage->structCount) {
			continue;
		}
		StageDataType type = STAGE_DATA_NONE;
		//TODO search through all structs in stage to find roots,
		//currently only using struct at idx 0
		DescIdx contains[CARK_DESC_ENUM_COUNT] = {0};
		err = parseStructInfo(pSession, i, 0, &type, contains);
		PIX_ERR_THROW_IFNOT(err, "", 1);
		if (type != STAGE_DATA_MESH) {
			continue;//TODO handle other types like ARRAY
		}
		Mesh mesh = {0};
		err = meshFromLog(pSession, contains, &cornerBuf, &posBuf, &mesh);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		err = meshTriangulate(&mesh);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		err = meshLoadOnGpu(pSession, &mesh);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		meshDestroy(&mesh);
	}

	PIX_ERR_CATCH(0, err,
		sessionClear(pSession);
	);
	if (cornerBuf.pArr) {
		free(cornerBuf.pArr);
	}
	if (posBuf.pArr) {
		free(posBuf.pArr);
	}
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
		err = carkGuiLayout(&session, windowSize, &guiQueue, pGlCtx->targetTex);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		for (I32 i = 0; i < guiQueue.count; ++i) {
			err = guiEventHandle(pWindow, &gui, guiQueue.queue[i]);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}

		err = update(&gui, &session, &view);
		PIX_ERR_THROW_IFNOT(err, "", 0);

		bool frameBufValid = glIsFramebuffer(pGlCtx->frameBuf);
		if (!frameBufValid ||
		    !pixmV2I32Equal(session.viewportSize, pGlCtx->frameSize)
		) {
			if (frameBufValid) {
				frameBufDestroy(pGlCtx);
			}
			frameBufInit(pGlCtx, session.viewportSize);
		}
		err = draw(pWindow, &session, pGlCtx, &view, windowSize);
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

static
void init() {
	requiredTable[STAGE_DATA_MESH] = (Required){
		.pArr = requiredMeshArr,
		.size = sizeof(requiredMeshArr) / sizeof(CarkDesc)
	};

	typeFromDesc[CARK_DESC_MESH] = STAGE_DATA_MESH;
	typeFromDesc[CARK_DESC_FACE] = STAGE_DATA_MESH;
}

int main(int argc, char **argv) {
	PixErr err = PIX_ERR_SUCCESS;

	init();

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
	if (glIsFramebuffer(glCtx.frameBuf)) {
		frameBufDestroy(&glCtx);
	}
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