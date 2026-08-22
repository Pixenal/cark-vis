#include <float.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <plutosvg.h>
#include <pixenals_error_utils.h>
#include <pixenals_types.h>
#include <pixenals_math_utils.h>
#include <pixenals_mesh_utils.h>

#include <cark_vis_gui.hpp>

#define CARK_PATH_LEN_MAX 4096
#define PI 3.1415926536f
#define VERT_POS_LOCATION 0
#define BIN_PATH_LEN_MAX 16384
#define SENSITIVITY .5f

typedef int16_t I16;
typedef int32_t I32;
typedef uint32_t U32;
typedef float F32;
typedef double F64;

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

typedef struct GpuUbo {
	GLuint ubo;
	GLuint uboIdx;
	GLuint uboBind;
	I32 size;
} GpuUbo;

typedef struct GpuFrame {
	GLuint frameBuf;
	GLuint depthBuf;
	GLuint targetTex;
	PixtyV2_I32 frameSize;
} GpuFrame;

typedef struct KeyPress {
	bool pressed : 1;
	bool done : 1;
} KeyPress;

typedef struct Keys {
	bool pan;
	bool orbit;
	bool scroll;
} Keys;

typedef struct View {
	PixtyV4_F32 offset;
	PixtyV2_F32 pan;
	CarkGuiWindow window;
	F32 yaw;
	F32 pitch;
	F32 camDist;
	KeyPress reset;
	Keys keys;
	bool down;
} View;

typedef struct Viewport {
	GLuint prog;
	GpuMesh geo;
	GpuUbo ubo;
	GpuFrame frame;
	View view;
} Viewport;

typedef struct Timeline {
	GLuint prog;
	GpuMesh geo;
	GpuUbo ubo;
	GpuFrame frame;
	View view;
} Timeline;

typedef struct GlCtx {
	SDL_GLContext pSdlCtx;
} GlCtx;

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

char *pAssetDir = NULL;
I32 assetDirLen = 0;

//returnes if pending & sets done to true
static
bool keyPressCheck(KeyPress *pKeyPress) {
	if (pKeyPress->pressed && !pKeyPress->done) {
		pKeyPress->done = true;
		return true;
	}
	return false;
}

static
void keyPressDown(KeyPress *pKeyPress) {
	pKeyPress->pressed = !pKeyPress->done;
}

static
void keyPressUp(KeyPress *pKeyPress) {
	*pKeyPress = (KeyPress){0};
}

static
void viewResetIfPending(View *pView, bool resetOrbit, F32 camDist, PixtyV3_F32 offset) {
	if (keyPressCheck(&pView->reset)) {
		pView->offset.d[0] = offset.d[0];
		pView->offset.d[1] = offset.d[1];
		pView->offset.d[2] = offset.d[2];
		pView->camDist = camDist;
		pView->pan = (PixtyV2_F32){0};
		if (resetOrbit) {
			pView->yaw = .0f;
			pView->pitch = .0f;
		}
	}
}

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

typedef struct ViewportDrawArgs {
	PixtyM4x4 persp;
	PixtyM4x4 view;
	PixtyV4_F32 offset;
	PixtyV2_F32 pan;
	F32 yaw;
	F32 pitch;
	F32 camDist;
} ViewportDrawArgs;

typedef struct TimelineDrawArgs {
	PixtyM4x4 ortho;
	PixtyV2_F32 offset;
	PixtyV2_F32 size;
	PixtyV4_F32 colour;
	F32 sort;
	F32 intervals;
	F32 camDist;
	F32 pan;
} TimelineDrawArgs;

static
PixErr shaderCompile(GLuint shader, const GLchar *pSrc) {
	PixErr err = PIX_ERR_SUCCESS;
	GLint glErr = false;
	glShaderSource(shader, 1, &pSrc, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &glErr);
	shaderLogPrint(shader);
	if (!glErr) {
		PIX_ERR_RETURN(err, "vert shader compilation failed");
	}
	return err;
}

static
PixErr gpuProgInit(const GLchar *pVertSrc, const GLchar *pFragSrc, GLuint *pProg) {
	PixErr err = PIX_ERR_SUCCESS;
	GLint glErr = false;

	GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	err = shaderCompile(vertShader, pVertSrc);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = shaderCompile(fragShader, pFragSrc);
	PIX_ERR_RETURN_IFNOT(err, "");
	GLuint prog = *pProg = glCreateProgram();
	glAttachShader(prog, vertShader);
	glAttachShader(prog, fragShader);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &glErr);
	{
		I32 bufSize = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &bufSize);
		if (bufSize > 0) {
			char *pLog = malloc(bufSize);
			I32 len = 0;
			glGetProgramInfoLog(prog, bufSize, &len, pLog);
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
	return err;
}

static
void gpuUboInit(GLuint prog, const GLchar *pName, I32 size, I32 location, GpuUbo *pUbo) {
	pUbo->size = size;
	glGenBuffers(1, &pUbo->ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, pUbo->ubo);
	glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	pUbo->uboBind = location;
	glBindBufferBase(GL_UNIFORM_BUFFER, pUbo->uboBind, pUbo->ubo);
	pUbo->uboIdx = glGetUniformBlockIndex(prog, pName);
	glUniformBlockBinding(prog, pUbo->uboIdx, pUbo->uboBind);
}

typedef struct Bb {
	PixtyV3_F32 min;
	PixtyV3_F32 max;
} Bb;

static
void bbInit(Bb *pBb) {
	*pBb = (Bb) {
		.min = {FLT_MAX, FLT_MAX, FLT_MAX},
		.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX}
	};
}

static
void bbContrib(Bb *pBb, const PixtyV3_F32 *pPos, I32 vecSize) {
	for (I32 i = 0; i < vecSize; ++i) {
		pBb->min.d[i] = pPos->d[i] < pBb->min.d[i] ? pPos->d[i] : pBb->min.d[i];
		pBb->max.d[i] = pPos->d[i] > pBb->max.d[i] ? pPos->d[i] : pBb->max.d[i];
	}
}

static
void bbGetCentreAndSize(const Bb *pBb, I32 vecSize, PixtyV3_F32 *pCentre, F32 *pSize) {
	PixtyV3_F32 halfVec = _(_(pBb->max V3SUB pBb->min) V3DIVS 2.0f);
	F32 squareDist = 0;
	for (I32 i = 0; i < vecSize; ++i) {
		squareDist += halfVec.d[i] * halfVec.d[i];
	}
	*pCentre = _(halfVec V3ADD pBb->min);
	*pSize = sqrtf(squareDist);
}

static
void gpuMeshInit(
	I32 vecSize,
	I32 posArrSize,
	const GLfloat *pPosArr,
	I32 cornerArrSize,
	const GLuint *pCornerArr,
	I32 triCount,
	GpuMesh *pMesh,
	Bb *pBb
) {
	I32 vertCount = posArrSize / sizeof(GLfloat) / vecSize;
	PIX_ERR_ASSERT("", triCount > 0 && vertCount > 0);
	Bb bb = {0};
	bbInit(&bb);
	for (I32 i = 0; i < vertCount; ++i) {
		const PixtyV3_F32 *pPos = (void *)&pPosArr[i * vecSize];
		bbContrib(&bb, pPos, vecSize);
		if (pBb) {
			bbContrib(pBb, pPos, vecSize);
		}
	}
	*pMesh = (GpuMesh){.triCount = triCount, .vecSize = vecSize};
	bbGetCentreAndSize(&bb, vecSize, &pMesh->centre, &pMesh->size);

	glGenVertexArrays(1, &pMesh->vao);
	glBindVertexArray(pMesh->vao);
	glGenBuffers(1, &pMesh->ebo);
	glGenBuffers(1, &pMesh->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pMesh->vbo);
	glBufferData(GL_ARRAY_BUFFER, posArrSize, pPosArr, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pMesh->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, cornerArrSize, pCornerArr, GL_STATIC_DRAW);
	I32 vecByteSize = vecSize * sizeof(GL_FLOAT);
	glVertexAttribPointer(VERT_POS_LOCATION, vecSize, GL_FLOAT, false, vecByteSize, NULL);
	glEnableVertexAttribArray(VERT_POS_LOCATION);
}

static
PixErr timelineInit(Timeline *pTimeline) {
	PixErr err = PIX_ERR_SUCCESS;
	GLint glErr = false;

	pTimeline->view = (View){
		.window = CARK_GUI_WINDOW_TIMELINE,
		.camDist = 1.0f
	};

	const GLchar vertShaderSrc[] = "\
		#version 410 core\n\
		layout (location = 0) in vec2 vertPos;\
		out vec3 outPos;\
		layout (std140) uniform timelineDrawArgs {\
			mat4 ortho;\
			vec2 offset;\
			vec2 size;\
			vec4 colour;\
			float sort;\
			float intervals;\
			float camDist;\
			float pan;\
		};\
		void main() {\
			vec2 pos = vertPos * size + offset;\
			pos = pos * 2.0f - 1.0f;\
			outPos = vec3(pos, 1.0f);\
			gl_Position = ortho * vec4(pos, sort, 1.0);\
		}\
	";
	const GLchar fragShaderSrc[] = "\
		#version 410 core\n\
		in vec3 outPos;\
		layout (location = 0) out vec3 fragColor;\
		layout (std140) uniform timelineDrawArgs {\
			mat4 ortho;\
			vec2 offset;\
			vec2 size;\
			vec4 colour;\
			float sort;\
			float intervals;\
			float camDist;\
			float pan;\
		};\
		void main() {\
			if (intervals == 1.0f) {\
				float timeInterval = mod(((outPos.x * camDist) - pan * 2.0f) * 10.0f, 1.0f);\
				if (timeInterval > .02f * camDist) {\
					 discard;\
				}\
			}\
			fragColor = colour.xyz;\
		}\
	";
	err = gpuProgInit(vertShaderSrc, fragShaderSrc, &pTimeline->prog);
	PIX_ERR_RETURN_IFNOT(err, "");

	GLfloat posArr[] = {
		.0f, .0f,
		1.0f, .0f,
		1.0f, 1.0f,
		.0f, 1.0f,
	};

	GLuint cornerArr[] = {
		0, 1, 2,
		2, 3, 0
	};
	gpuMeshInit(
		2,
		sizeof(posArr),
		posArr,
		sizeof(cornerArr),
		cornerArr,
		2,
		&pTimeline->geo,
		NULL
	);
	gpuUboInit(
		pTimeline->prog,
		"timelineDrawArgs",
		sizeof(TimelineDrawArgs),
		1,
		&pTimeline->ubo
	);
	return err;
}

static
PixErr viewportInit(Viewport *pViewport) {
	PixErr err = PIX_ERR_SUCCESS;
	GLint glErr = false;

	pViewport->view = (View){
		.window = CARK_GUI_WINDOW_VIEWPORT,
		.yaw = PI * .25f,
		.pitch = PI * .125f,
		.camDist = 3.0f
	};

	const GLchar vertShaderSrc[] = "\
		#version 410 core\n\
		layout (location = 0) in vec3 vertPos;\
		layout (std140) uniform viewportDrawArgs {\
			mat4 persp;\
			mat4 view;\
			vec4 offset;\
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
			vec4 pos = rot * vec4(vertPos - offset.xyz, 1.0f);\
			pos.xyz += vec3(.0f, .0f, -1.0f) * camDist;\
			pos.xy += pan;\
			gl_Position = persp * pos;\
		}\
	";
	const GLchar fragShaderSrc[] = "\
		#version 410 core\n\
		layout(location = 0) out vec3 fragColor;\
		void main() {\
			fragColor = vec3(1.0, .0, .0);\
		}\
	";
	err = gpuProgInit(vertShaderSrc, fragShaderSrc, &pViewport->prog);
	PIX_ERR_RETURN_IFNOT(err, "");

	//default cube
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
	gpuMeshInit(
		3,
		sizeof(posArr),
		posArr,
		sizeof(cornerArr),
		cornerArr,
		12,
		&pViewport->geo,
		NULL
	);
	gpuUboInit(
		pViewport->prog,
		"viewportDrawArgs",
		sizeof(ViewportDrawArgs),
		0,
		&pViewport->ubo
	);
	return err;
}

static
void gpuMeshDestroy(GpuMesh *pMesh) {
	if (glIsBuffer(pMesh->vbo)) {
		glDeleteBuffers(1, &pMesh->vbo);
	}
	if (glIsBuffer(pMesh->ebo)) {
		glDeleteBuffers(1, &pMesh->ebo);
	}
	if (glIsVertexArray(pMesh->vao)) {
		glDeleteVertexArrays(1, &pMesh->vao);
	}
	*pMesh = (GpuMesh){0};
}

static
void gpuUboDestroy(GpuUbo *pUbo) {
	if (glIsBuffer(pUbo->ubo)) {
		glDeleteBuffers(1, &pUbo->ubo);
	}
	*pUbo = (GpuUbo){0};
}

static
void gpuFrameDestroy(GpuFrame *pFrame) {
	if (glIsFramebuffer(pFrame->frameBuf)) {
		glDeleteRenderbuffers(1, &pFrame->depthBuf);
		glDeleteTextures(1, &pFrame->targetTex);
		glDeleteFramebuffers(1, &pFrame->frameBuf);
	}
	*pFrame = (GpuFrame){0};
}

static
void timelineDestroy(Timeline *pTimeline) {
	gpuFrameDestroy(&pTimeline->frame);
	gpuUboDestroy(&pTimeline->ubo);
	gpuMeshDestroy(&pTimeline->geo);
	if (glIsProgram(pTimeline->prog)) {
		glDeleteProgram(pTimeline->prog);
	}
	*pTimeline = (Timeline){0};
}

static
void viewportDestroy(Viewport *pViewport) {
	gpuFrameDestroy(&pViewport->frame);
	gpuUboDestroy(&pViewport->ubo);
	gpuMeshDestroy(&pViewport->geo);
	if (glIsProgram(pViewport->prog)) {
		glDeleteProgram(pViewport->prog);
	}
	*pViewport = (Viewport){0};
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
	return err;
}

static
PixtyM4x4 frustum(
	F32 left,
	F32 right,
	F32 bottom,
	F32 top,
	F32 zNear,
	F32 zFar
) {
	F32 a = (right + left) / (right - left);
	F32 b = (top + bottom) / (top - bottom);
	F32 c = -(zFar + zNear) / (zFar - zNear);
	F32 d = -(2.0f * zFar * zNear) / (zFar - zNear);
	return (PixtyM4x4) {
		2.0f * zNear / (right - left), .0f, .0f, .0f,
		.0f, 2.0f * zNear / (top - bottom), .0f, .0f,
		a, b, c, -1.0f,
		.0f, .0f, d, .0f
	};
}

static
PixErr frameBufInit(GpuFrame *pFrame, PixtyV2_I32 size) {
	PixErr err = PIX_ERR_SUCCESS;
	if (!size.d[0] || !size.d[1]) {
		size = (PixtyV2_I32){256, 256};
	}
	pFrame->frameSize = size;
	glGenFramebuffers(1, &pFrame->frameBuf);
	glBindFramebuffer(GL_FRAMEBUFFER, pFrame->frameBuf);
	glGenTextures(1, &pFrame->targetTex);
	glBindTexture(GL_TEXTURE_2D, pFrame->targetTex);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGB,
		size.d[0], size.d[1],
		0,
		GL_RGB,
		GL_UNSIGNED_BYTE,
		NULL
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glGenRenderbuffers(1, &pFrame->depthBuf);
	glBindRenderbuffer(GL_RENDERBUFFER, pFrame->depthBuf);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.d[0], size.d[1]);
	glFramebufferRenderbuffer(
		GL_FRAMEBUFFER,
		GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER,
		pFrame->depthBuf
	);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, pFrame->targetTex, 0);
	glDrawBuffers(1, (GLenum[]){GL_COLOR_ATTACHMENT0});
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
		""
	);
	return err;
}

static
PixtyM4x4 ortho(F64 left, F64 right, F64 bottom, F64 top, F64 zNear, F64 zFar) {
	PixtyV3_F32 t = {
		(F32)((right + left) / (right - left)), 
		(F32)((top + bottom) / (top - bottom)),
		(F32)((zFar + zNear) / (zFar - zNear))
	};
	return (PixtyM4x4) {
		(F32)(2.0 / (right - left)), .0f, .0f, .0f,
		.0f, (F32)(2.0 / (top - bottom)), .0f, .0f,
		.0f, .0f, (F32)(-2.0 / (zFar - zNear)), .0f,
		t.d[0], t.d[1], t.d[2], 1.0f
	};
}

static
PixtyM4x4 perspective(F64 yFov, F64 aspect, F64 zNear, F64 zFar) {
	F64 f = 1.0f / tanf(yFov / 2.0f);
	return (PixtyM4x4) {
		(F32)(f / aspect), .0f, .0f, .0f,
		.0f, (F32)f, .0f, .0f,
		.0f, .0f, (F32)((zNear + zFar) / (zNear - zFar)), -1.0f,
		.0f, .0f, (F32)((2.0 * zNear * zFar) / (zNear - zFar)), .0f
	};
}

static
void gpuFrameClear(const GpuFrame *pFrame, PixtyV3_F32 col) {
	glBindFramebuffer(GL_FRAMEBUFFER, pFrame->frameBuf);
	glViewport(0, 0, pFrame->frameSize.d[0], pFrame->frameSize.d[1]);
	glClearColor(col.d[0], col.d[1], col.d[2], 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static
void gpuUboBind(const GpuUbo *pUbo, void *pData) {
	glBindBuffer(GL_UNIFORM_BUFFER, pUbo->ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, pUbo->size, pData);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static
bool stageMeshIsValid(const Session *pSession) {
	if (pSession->activeStage < 0 ||
	    pSession->activeStage >= pSession->info.pStageArr->count
	) {
		return false;
	}
	PixtyValidIdx idx = pSession->stageMeshArr.pTable[pSession->activeStage];
	return
		idx.valid &&
		pSession->activeInst >= 0 &&
		pSession->activeInst < pSession->stageMeshArr.pArr[idx.idx].instMeshArr.count;
}

static
void drawViewport(const Session *pSession, Viewport *pViewport) {
	//TODO wrap viewport->geo in mesh arr and remove cast to non-const
	GpuMeshArr fallback = {.pArr = {(GpuMesh *)&pViewport->geo}, .count = 1};
	const GpuMeshArr *pMeshArr = &fallback;
	F32 meshSize = 1.0f;
	PixtyV3_F32 offset = {0};
	if (stageMeshIsValid(pSession)) {
		I32 stageIdx = pSession->stageMeshArr.pTable[pSession->activeStage].idx;
		const StageMesh *pStageMesh = pSession->stageMeshArr.pArr + stageIdx;
		pMeshArr = &pStageMesh->instMeshArr;
		meshSize = pStageMesh->size;
		offset = pStageMesh->centre;
	}
	viewResetIfPending(&pViewport->view, false, meshSize * 3.0f, offset);

	F32 aspect =
		(F32)pViewport->frame.frameSize.d[0] /
		(F32)pViewport->frame.frameSize.d[1];
	F32 zNear = .001f;
	F32 zFar = 100.0f;
	ViewportDrawArgs drawArgs = {
		.persp = perspective(PI / 4.0f, aspect, zNear, zFar),
		.view = {
			1.0f, .0f, .0f, .0f,
			.0f, 1.0f, .0f, 0.0f,
			.0f, .0f, 1.0f, 0.0f,
			.0f, .0f, 0.0f, 1.0f
		},
		.offset = pViewport->view.offset,
		.pan = pViewport->view.pan,
		.yaw = pViewport->view.yaw,
		.pitch = pViewport->view.pitch,
		.camDist = pViewport->view.camDist
	};
	
	gpuFrameClear(&pViewport->frame, (PixtyV3_F32){.1f, .1f, .1f});
	glUseProgram(pViewport->prog);
	gpuUboBind(&pViewport->ubo, &drawArgs);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glEnable(GL_DEPTH_TEST);
	for (I32 i = 0; i < pMeshArr->count; ++i) {
		const GpuMesh *pMesh = pMeshArr->pArr + i;
		if (!pMesh->triCount) {
			continue;
		}
		glBindVertexArray(pMesh->vao);
		glDrawElements(GL_TRIANGLES, pMesh->triCount * 3, GL_UNSIGNED_INT, NULL);
	}
}

typedef struct FTimeframe {
	F64 start;
	F64 duration;
} FTimeframe;

static
FTimeframe fTimeFromCarkTimeframe(CarkTimeframe timeframe) {
	return
		(FTimeframe){.start = (I64)timeframe.start, .duration = (I64)timeframe.duration};
}

static
void drawTimeline(const Session *pSession, Timeline *pTimeline) {
	if (!pSession->info.pStageArr) {
		return;
	}
	viewResetIfPending(&pTimeline->view, true, 1.0f, (PixtyV3_F32){0});

	F32 aspect =
		(F32)pTimeline->frame.frameSize.d[0] /
		(F32)pTimeline->frame.frameSize.d[1];
	F32 zNear = .001f;
	F32 zFar = 100.0f;
	F32 size = 1.0f;
	F32 bottom = size;
	F32 right = size;
	glBindVertexArray(pTimeline->geo.vao);
	gpuFrameClear(&pTimeline->frame, (PixtyV3_F32){.0f, .0f, .0f});
	glUseProgram(pTimeline->prog);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);

	FTimeframe timeGlobal = fTimeFromCarkTimeframe(pSession->file.timeframe);
	F32 stageHeight = 1.0f / pSession->logArr.size;
	F32 pixelSizeX = 1.0f / (F32)pTimeline->frame.frameSize.d[0];
	F32 trackPadding = .00125f * aspect;
	F32 rightScaled = right * pTimeline->view.camDist;
	PixtyM4x4 orthoMat = ortho(-right, right, bottom, -bottom, zNear, zFar);
	PixtyM4x4 orthoMatZoom = ortho(-rightScaled, rightScaled, bottom, -bottom, zNear, zFar);
	for (I32 i = 0; i < pSession->logArr.size; ++i) {
		const CarkInStageLog *pStageLog = pSession->logArr.pArr + i;
		TimelineDrawArgs drawArgs = {
			.ortho = orthoMat,
			.offset = {.0f, stageHeight * i + trackPadding},
			.size = {1.0f, stageHeight - trackPadding * 2.0f},
			.colour = {.0f, .0f, .0f},
			.sort = .05f
		};
		gpuUboBind(&pTimeline->ubo, &drawArgs);
		glDrawElements(GL_TRIANGLES, 2 * 3, GL_UNSIGNED_INT, NULL);

		drawArgs.ortho = orthoMatZoom;
		drawArgs.colour = (PixtyV4_F32){.1f, .1f, .1f, 1.0f};
		drawArgs.sort = .75f;
		drawArgs.offset.d[0] += pTimeline->view.yaw;
		gpuUboBind(&pTimeline->ubo, &drawArgs);
		glDrawElements(GL_TRIANGLES, 2 * 3, GL_UNSIGNED_INT, NULL);

		FTimeframe timeStage = fTimeFromCarkTimeframe(pStageLog->timeframe);
		drawArgs.offset.d[0] = (F32)(
			(timeStage.start - timeGlobal.start) / timeGlobal.duration
		);
		drawArgs.offset.d[0] += pTimeline->view.yaw;
		drawArgs.size.d[0] = (F32)(timeStage.duration / timeGlobal.duration);
		if (drawArgs.size.d[0] < pixelSizeX * 10.0f) {
			drawArgs.size.d[0] = pixelSizeX * 10.0f;
		}
		drawArgs.colour = (PixtyV4_F32){1.0f, .0f, .0f, 1.0f};
		drawArgs.sort = 8.0f;
		gpuUboBind(&pTimeline->ubo, &drawArgs);
		glDrawElements(GL_TRIANGLES, 2 * 3, GL_UNSIGNED_INT, NULL);
	}
	TimelineDrawArgs drawArgs = {
		.ortho = orthoMat,
		.size = {1.0f, 1.0f},
		.colour = {.2f, .2f, .2f},
		.sort = 4.0f,
		.intervals = 1.0f,
		.camDist = pTimeline->view.camDist,
		.pan = pTimeline->view.yaw
	};
	gpuUboBind(&pTimeline->ubo, &drawArgs);
	glDrawElements(GL_TRIANGLES, 2 * 3, GL_UNSIGNED_INT, NULL);
}

static
PixErr draw(
	SDL_Window *pWindow,
	Session *pSession,
	Viewport *pViewport,
	Timeline *pTimeline
) {
	PixErr err = PIX_ERR_SUCCESS;
	drawViewport(pSession, pViewport);
	drawTimeline(pSession, pTimeline);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(.0f, .0f, .0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	err = carkGuiDraw();
	PIX_ERR_RETURN_IFNOT(err, "");
	SDL_ERR_RET(SDL_GL_SwapWindow(pWindow));
	return err;
}

static
void eventHandleForView(
	PixtyV2_F32 fWindowSize,
	SDL_Event *pEvent,
	View *pView,
	F32 sensitivity,
	F32 zoomMin,
	CarkGuiWindow activeWindow
) {
	bool isActive = pView->window == activeWindow;
	switch (pEvent->type) {
		case SDL_EVENT_MOUSE_MOTION:
			if (pView->keys.orbit) {
				PixtyV2_F32 motion = {pEvent->motion.xrel, pEvent->motion.yrel};
				_(&motion V2MULSEQL SENSITIVITY);
				if (pView->keys.pan) {
					motion = _(motion V2MULS .01f);
					motion.d[1] *= -1.0f;
					_(&pView->pan V2ADDEQL motion);
				}
				else if (pView->keys.scroll) {
					pView->camDist += motion.d[1] * .05f;
					pView->camDist = pView->camDist >= zoomMin ? pView->camDist : zoomMin;
				}
				else {
					motion = _(_(motion V2DIV fWindowSize) V2MULS 2.0f * PI);
					F32 wrap = 2.0f * PI;
					pView->yaw = fmod(pView->yaw + motion.d[0], wrap);
					pView->pitch = fmod(pView->pitch + motion.d[1], wrap);
				}
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (!isActive) {
				break;
			}
			pView->keys.orbit = pEvent->button.button == SDL_BUTTON_MIDDLE;
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (pEvent->button.button == SDL_BUTTON_MIDDLE) {
				pView->keys.orbit = false;
				pView->keys.scroll = false;
				pView->keys.pan = false;
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			if (!isActive) {
				break;
			}
			switch (pEvent->key.key) {
				case SDLK_LCTRL:
				//v fallthrough v
				case SDLK_RCTRL:
					pView->keys.scroll = pView->keys.scroll || !pView->keys.orbit;
					break;
				case SDLK_LSHIFT:
				//v fallthrough v
				case SDLK_RSHIFT:
					pView->keys.pan = pView->keys.pan || !pView->keys.orbit;
					break;
				case SDLK_F:
					keyPressDown(&pView->reset);
					break;
				default:
					;
			}
			break;
		case SDL_EVENT_KEY_UP:
			switch (pEvent->key.key) {
				case SDLK_LCTRL:
				//v fallthrough v
				case SDLK_RCTRL:
					pView->keys.scroll = pView->keys.scroll && pView->keys.orbit;
					break;
				case SDLK_LSHIFT:
				//v fallthrough v
				case SDLK_RSHIFT:
					pView->keys.pan = pView->keys.pan && pView->keys.orbit;
					break;
				case SDLK_F:
					keyPressUp(&pView->reset);
					break;
				default:
					;
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if (!isActive && !pView->keys.orbit) {
				break;
			}
			pView->camDist -= pEvent->wheel.y * SENSITIVITY;
			pView->camDist = pView->camDist >= zoomMin ? pView->camDist : zoomMin;
			break;
		default:
			;
	}
	pView->down = pView->keys.orbit || pView->reset.pressed;
}

static
PixErr eventHandle(
	PixtyV2_I32 windowSize,
	CarkGuiWindow activeWindow,
	SDL_Event *pEvent,
	bool *pExit,
	Viewport *pViewport,
	Timeline *pTimeline
) {
	PixErr err = PIX_ERR_SUCCESS;
	PixtyV2_F32 fWindowSize = {(F32)windowSize.d[0], (F32)windowSize.d[1]};
	switch (pEvent->type) {
		case SDL_EVENT_QUIT:
			//v fallthrough v
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			*pExit = true;
			return err;
		default:
			;
	}
	eventHandleForView(
		fWindowSize,
		pEvent,
		&pViewport->view,
		1.0f,
		.0f,
		activeWindow
	);
	eventHandleForView(
		fWindowSize,
		pEvent,
		&pTimeline->view,
		.5f,
		.00001f,
		activeWindow
	);
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
	I32 stageCount = pSession->info.pStageArr ? pSession->info.pStageArr->count : 0;
	if (pSession->stageMeshArr.pTable && stageCount) {
		memset(pSession->stageMeshArr.pTable, 0, stageCount * sizeof(I32));
	}
	for (I32 i = 0; i < pSession->stageMeshArr.count; ++i) {
		StageMesh *pStageMesh = pSession->stageMeshArr.pArr + i;
		for (I32 j = 0; j < pStageMesh->instMeshArr.count; ++j) {
			gpuMeshDestroy(pStageMesh->instMeshArr.pArr + j);
		}
	}
	pSession->stageMeshArr.count = 0;
	for (I32 i = 0; i < pSession->logArr.size; ++i) {
		carkInStageLogDestroy(&alloc, pSession->logArr.pArr + i);
	}
	carkInFileDestroy(&alloc, &pSession->file);
	pSession->info = (CarkInFileInfo){0};
	pSession->activeStage = -1;
}

static
void sessionDestroy(Session *pSession) {
	sessionClear(pSession);
	if (pSession->stageMeshArr.pTable) {
		free(pSession->stageMeshArr.pTable);
	}
	if (pSession->stageMeshArr.pArr) {
		for (I32 i = 0; i < pSession->stageMeshArr.count; ++i) {
			StageMesh *pStageMesh = pSession->stageMeshArr.pArr + i;
			if (pStageMesh->instMeshArr.pArr) {
				free(pStageMesh->instMeshArr.pArr);
			}
		}
		free(pSession->stageMeshArr.pArr);
	}
	if (pSession->stageTypeArr.pArr) {
		free(pSession->stageTypeArr.pArr);
	}
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
PixErr stageLogFromRef(
	const Session *pSession,
	I32 stageIdx,
	const CarkInStageLog **ppLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		stageIdx >= 0 && stageIdx < pSession->logArr.size,
		"out of bounds"
	);
	*ppLog = pSession->logArr.pArr + stageIdx;
	return err;

}

static
PixErr instLogFromRef(
	const Session *pSession,
	CarkInRef ref,
	const CarkInInstLog **ppLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		ref.ref.stageIdx >= 0 && ref.ref.stageIdx < pSession->logArr.size,
		"out of bounds"
	);
	const CarkInStageLog *pStageLog = pSession->logArr.pArr + ref.ref.stageIdx;
	err = carkInInstLogFromRef(pStageLog, ref, ppLog);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

typedef struct VertKey {
	I32 logIdx;
	CarkInRef ref;
} VertKey;

typedef struct VertRedir {
	PixuctHTableEntryCore core;
	I32 entryIdx;
	U32 idx : 31;
	U32 added : 1; 
	VertKey key;
} VertRedir;

static
PixuctKey vertKeyMake(const void *pKey) {
	return (PixuctKey){.pKey = pKey, .size = sizeof(VertKey)};
}

static
bool vertKeyCmp(
	const PixuctHTableEntryCore *pEntryRaw,
	const void *pKeyRaw,
	const void *pInitInfo
) {
	const VertRedir *pEntry = (void *)pEntryRaw;
	const VertKey *pKey = pKeyRaw;
	return !memcmp(&pEntry->key, pKey, sizeof(VertKey));
}

static
PixErr facePosGet(
	const Session *pSession,
	const CarkStage *pStage,
	const CarkInStageLog *pStageLog,
	PixtyI32Arr *pCornerBuf,
	V3_F32Arr *pPosBuf,
	I32 corner,
	CarkInRef posRef,
	I32 vertIdx,
	PixuctHTable *pVertTable,
	bool *pNoLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	const CarkStruct *pPosInfo = NULL;
	err = carkStructInfoFromRef(&pSession->info, posRef.ref, &pPosInfo);
	PIX_ERR_RETURN_IFNOT(err, "");
	const CarkInInstLog *pPosLog = NULL;
	err = instLogFromRef(pSession, posRef, &pPosLog);
	PIX_ERR_RETURN_IFNOT(err, "");

	VertRedir *pEntry = NULL;
	VertKey key = {.logIdx = vertIdx, .ref = posRef};
	I32 entryIdx = 0;
	SearchResult result = pixuctHTableBasicGet(
		pVertTable,
		0,
		&key,
		&pEntry,
		true,
		&entryIdx,
		vertKeyMake, vertKeyCmp
	);
	PIX_ERR_ASSERT("", pEntry);


	I32 newCorner = 0;
	PIXALC_DYN_ARR_ADD(I32, &alloc, pCornerBuf, newCorner);
	pCornerBuf->pArr[newCorner] = pEntry->added ? pEntry->entryIdx : entryIdx;

	if (pEntry->added) {
		PIX_ERR_ASSERT("", result == PIX_SEARCH_FOUND);
		return err;
	}
	CarkInLogItem item = {0};
	err = carkInLogIdx(
		pStageLog,
		pStage,
		posRef.ref.structIdx,
		posRef.inst,
		vertIdx,
		&item
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	*pEntry = (VertRedir){.core = pEntry->core, .key = key, .entryIdx = entryIdx};
	if (!item.pData) {
		*pNoLog = true;//pos wasn't logged
		return err;
	}
	I32 newVert = 0;
	PIXALC_DYN_ARR_ADD(PixtyV3_F32, &alloc, pPosBuf, newVert);
	pPosBuf->pArr[newVert] = *(PixtyV3_F32 *)item.pData;
	pEntry->idx = newVert;
	return err;
}

static
PixErr faceCornersGet(
	const Session *pSession,
	const CarkStage *pStage,
	const CarkInStageLog *pStageLog,
	CarkInRef cornerRef,
	FaceRange faceRange,
	PixtyI32Arr *pCornerBuf,
	V3_F32Arr *pPosBuf,
	PixuctHTable *pVertTable,
	bool *pNoLog
) {
	PixErr err = PIX_ERR_SUCCESS;
	pCornerBuf->count = 0;
	pPosBuf->count = 0;
	const CarkStruct *pCornerInfo = NULL;
	err = carkStructInfoFromRef(&pSession->info, cornerRef.ref, &pCornerInfo);
	PIX_ERR_RETURN_IFNOT(err, "");
	CarkInLogItem item = {0};
	err = carkInLogIdx(
		pStageLog,
		pStage,
		cornerRef.ref.structIdx,
		cornerRef.inst,
		faceRange.start,
		&item
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	if (!item.pData) {
		*pNoLog = true;
		return err;//corners weren't logged
	}
	for (I32 i = 0; i < faceRange.size; ++i) {
		I32 offset = i * (pCornerInfo->byteSize + CARK_TIMESTAMP_SIZE);
		I32 vertIdx = *(I32 *)(item.pData + offset);
		I32 corner =  faceRange.start + i;
		CarkInRefArr posRefArr = {0};
		err = carkInCompRefsGet(pStageLog, pStage, cornerRef, corner, &posRefArr);
		err = facePosGet(
			pSession,
			pStage,
			pStageLog,
			pCornerBuf,
			pPosBuf,
			corner,
			posRefArr.arr[0],
			vertIdx,
			pVertTable,
			pNoLog
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		if (*pNoLog) {
			return err;
		}
	}
	PIX_ERR_ASSERT("", faceRange.size == pCornerBuf->count);
	return err;
}

static
void meshAddFace(
	Mesh *pMesh,
	FaceRange faceRange,
	const PixtyI32Arr *pCornerBuf,
	const V3_F32Arr *pPosBuf,
	PixuctHTable *pVertTable
) {
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(I32, &alloc, &pMesh->faces, newIdx);
	pMesh->faces.pArr[newIdx] = pMesh->corners.count;
	I32 posBufIdx = 0;
	PixalcLinAlloc *pVertTableMem = pixuctHTableAllocGet(pVertTable, 0);
	for (I32 i = 0; i < faceRange.size; ++i) {
		VertRedir *pVertEntry = pixalcLinAllocIdx(pVertTableMem, pCornerBuf->pArr[i]);
		I32 vertIdx = 0;
		if (pVertEntry->added) {
			vertIdx = (I32)pVertEntry->idx;
		}
		else {
			PIXALC_DYN_ARR_ADD(PixtyV3_F32, &alloc, &pMesh->pos, vertIdx);
			pMesh->pos.pArr[vertIdx] = pPosBuf->pArr[posBufIdx];
			++posBufIdx;
			pVertEntry->idx = (U32)vertIdx;
			pVertEntry->added = true;
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
	const CarkStage *pStage,
	I32 inst,
	const DescIdx *pContains,
	PixtyI32Arr *pCornerBuf,
	V3_F32Arr *pPosBuf,
	Mesh *pMesh
) {
	PixErr err = PIX_ERR_SUCCESS;
	const CarkInStageLog *pStageLog = NULL;
	err = stageLogFromRef(pSession, pStage->idx, &pStageLog);
	PIX_ERR_RETURN_IFNOT(err, "");
	CarkInRef faceRef = {.ref = pContains[CARK_DESC_FACE].ref, .inst = inst};
	const CarkStruct *pFaceInfo = NULL;
	err = carkStructInfoFromRef(&pSession->info, faceRef.ref, &pFaceInfo);
	PIX_ERR_RETURN_IFNOT(err, "");
	const CarkInInstLog *pFaceLog = NULL;
	err = instLogFromRef(pSession, faceRef, &pFaceLog);
	PIX_ERR_RETURN_IFNOT(err, "");

	PixuctHTable vertTable = {0};
	pixuctHTableInit(
		&alloc,
		&vertTable,
		pFaceLog->count,
		(PixtyI32Arr){.pArr = (I32[]){sizeof(VertRedir)}, .count = 1},
		NULL,
		NULL,
		false
	);

	I32 faceOffset = 0;
	I32 sizeCompIdx = -1;
	for (I32 i = 0; i < pFaceInfo->info.compCount; ++i) {
		if (pFaceInfo->info.pCompArr[i].desc == CARK_COMP_DESC_SIZE) {
			sizeCompIdx = i;
			break;
		}
	}
	PIX_ERR_ASSERT("existance of size comp was checked during parsing", sizeCompIdx != -1);
	PixuctAvlIter iter = {0};
	err = pixuctAvlIterInitConst(&pFaceLog->rangeTree, &iter);
	for (; !pixuctAvlIterAtEnd(&iter); pixuctAvlIterInc(&iter)) {
		const CarkItemRange *pRange = (void *)pixuctAvlIterGetItemConst(&iter);
		I32 rangeSize = pRange->idxRange.end - pRange->idxRange.start;
		for (I32 j = 0; j < rangeSize; ++j) {
			I32 itemIdx = pRange->idxRange.start + j;
			I32 loggedIdx = pRange->startItem + j;
			I32 byteIdx = loggedIdx * (CARK_TIMESTAMP_SIZE + pFaceInfo->byteSize);
			const U8 *pData = pStageLog->dataMem.pArr + pFaceLog->dataIdx + byteIdx;
			//TODO assuming components before size comp are i32,
			//put a func in io lib to get byte offset of a component idx
			//(accounting for byte size of other components in struct)
			FaceRange faceRange = {
				.start = *(I32 *)(pData + CARK_TIMESTAMP_SIZE),
				.size = *(I32 *)(pData + CARK_TIMESTAMP_SIZE + sizeCompIdx * sizeof(I32))
			};
			CarkInRefArr refArr = {0};
			err = carkInCompRefsGet(pStageLog, pStage, faceRef, itemIdx, &refArr);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			CarkInRef cornerRef = refArr.arr[0];//TODO don't hardcode read from idx 0
			bool noLog = false;
			err = faceCornersGet(
				pSession,
				pStage,
				pStageLog,
				cornerRef,
				faceRange,
				pCornerBuf,
				pPosBuf,
				&vertTable,
				&noLog
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (noLog) {
				continue;//one or all positions weren't logged
			}
			meshAddFace(pMesh, faceRange, pCornerBuf, pPosBuf, &vertTable);
		}
		faceOffset += rangeSize;
	}
	if (!pMesh->faces.count) {
		return err;
	}
	PIXALC_DYN_ARR_RESIZE(I32, &alloc, &pMesh->faces, pMesh->faces.count + 1);
	pMesh->faces.pArr[pMesh->faces.count] = pMesh->corners.count;
	PIX_ERR_CATCH(0, err, ;);
	pixuctHTableDestroy(&vertTable);
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
		PIXALC_DYN_ARR_RESIZE(U8, &alloc, &idxBuf, (face.size - 2) * 3);
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
void meshClear(Mesh *pMesh) {
	pMesh->faces.count = 0;
	pMesh->corners.count = 0;
	pMesh->pos.count = 0;
	pMesh->tris = false;
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
PixErr meshLoadOnGpu(
	Session *pSession,
	const Mesh *pMesh,
	I32 stage,
	I32 inst,
	Bb *pStageBb
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pSession->stageMeshArr.pTable);
	PixtyValidIdx *pIdx = pSession->stageMeshArr.pTable + stage;
	if (!pIdx->valid) {
		I32 newIdx = 0;
		PIXALC_DYN_ARR_ADD_ZERO(StageMesh, &alloc, &pSession->stageMeshArr, pIdx->idx);
		pIdx->valid = true;
	}
	StageMesh *pStageMesh = pSession->stageMeshArr.pArr + pIdx->idx;
	PIXALC_DYN_ARR_RESIZE(GpuMesh, &alloc, &pStageMesh->instMeshArr, inst + 1);
	pStageMesh->instMeshArr.count = inst + 1;
	if (!pMesh->corners.count) {
		pStageMesh->instMeshArr.pArr[0] = (GpuMesh){0};
		return err;
	}
	gpuMeshInit(
		3,
		pMesh->pos.count * sizeof(PixtyV3_F32),
		(GLfloat *)pMesh->pos.pArr,
		pMesh->corners.count * sizeof(I32),
		pMesh->corners.pArr,
		pMesh->corners.count / 3,
		pStageMesh->instMeshArr.pArr + inst,
		pStageBb
	);
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
	I32 stageCount = pSession->info.pStageArr->count;
	if (!pSession->stageMeshArr.pTable && stageCount) {
		pSession->stageMeshArr.pTable = calloc(stageCount, sizeof(PixtyValidIdx));
	}
	pSession->logArr.size = pSession->info.pStageArr->count;
	pSession->logArr.pArr = calloc(pSession->logArr.size, sizeof(CarkInStageLog));
	for (I32 i = 0; i < stageCount; ++i) {
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

	PIXALC_DYN_ARR_RESIZE(StageDataType, &alloc, &pSession->stageTypeArr, stageCount);
	Mesh mesh = {0};
	for (I32 i = 0; i < stageCount; ++i) {
		const CarkStage *pStage = pSession->info.pStageArr->pArr + i;
		if (!pStage->structCount) {
			continue;
		}
		pSession->stageTypeArr.pArr[i] = STAGE_DATA_NONE;
		//TODO search through all structs in stage to find roots,
		//currently only using struct at idx 0
		DescIdx contains[CARK_DESC_ENUM_COUNT] = {0};
		err = parseStructInfo(pSession, i, 0, pSession->stageTypeArr.pArr + i, contains);
		PIX_ERR_THROW_IFNOT(err, "", 1);
		if (pSession->stageTypeArr.pArr[i] != STAGE_DATA_MESH) {
			continue;//TODO handle other types like ARRAY
		}
		CarkRef faceRef = contains[CARK_DESC_FACE].ref;
		const CarkInStageLog *pStageLog = NULL;
		err = stageLogFromRef(pSession, faceRef.stageIdx, &pStageLog);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		const CarkInStructLog *pFaceLog = pStageLog->structs.pArr + faceRef.structIdx;
		Bb bb = {0};
		bbInit(&bb);
		for (I32 j = 0; j < pFaceLog->instArr.count; ++j) {
			err = meshFromLog(pSession, pStage, j, contains, &cornerBuf, &posBuf, &mesh);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (mesh.faces.count) {
				err = meshTriangulate(&mesh);
				PIX_ERR_THROW_IFNOT(err, "", 0);
			}
			err = meshLoadOnGpu(pSession, &mesh, i, j, &bb);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			meshClear(&mesh);
		}
		PixtyValidIdx stageMeshIdx = pSession->stageMeshArr.pTable[pStage->idx];
		if (!stageMeshIdx.valid) {
			continue;
		}
		StageMesh *pStageMesh = pSession->stageMeshArr.pArr + stageMeshIdx.idx;
		I32 vecSize = pStageMesh->instMeshArr.pArr[0].vecSize;
		bbGetCentreAndSize(&bb, vecSize, &pStageMesh->centre, &pStageMesh->size);
	}
	meshDestroy(&mesh);

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
PixErr update(CarkGuiState *pGui, Session *pSession) {
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
void pathFileAppend(char *pDir, I32 dirLen, const char *pName) {
	I32 nameLen = strnlen(pName, ASSET_NAME_LEN_MAX);
	PIX_ERR_ASSERT("", nameLen < ASSET_NAME_LEN_MAX);
	memcpy(pDir + dirLen, pName, nameLen + 1);
}

static
PixErr iconsLoad(CarkGuiState *pGui) {
	PixErr err = PIX_ERR_SUCCESS;
	const char folder[] = "icons/";
	char *pIconPath = malloc(assetDirLen + sizeof(folder) + ASSET_NAME_LEN_MAX);
	memcpy(pIconPath, pAssetDir, assetDirLen);
	memcpy(pIconPath + assetDirLen, folder, sizeof(folder) - 1);
	I32 dirLen = assetDirLen + sizeof(folder) - 1;

	glGenTextures(ICON_COUNT, pGui->iconArr);
	for (I32 i = 0; i < ICON_COUNT; ++i) {
		pathFileAppend(pIconPath, dirLen, iconNames[i]);
		plutosvg_document_t *pDoc = plutosvg_document_load_from_file(pIconPath, -1, -1);
		PIX_ERR_RETURN_IFNOT_COND(err, pDoc, "");
		plutovg_surface_t *pSurface = plutosvg_document_render_to_surface(
			pDoc,
			NULL,
			ICON_SIZE, ICON_SIZE,
			NULL,
			NULL,
			NULL
		);
		unsigned char *pData = plutovg_surface_get_data(pSurface);
		glBindTexture(GL_TEXTURE_2D, pGui->iconArr[i]);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGBA,
			ICON_SIZE, ICON_SIZE,
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			pData
		);
		plutosvg_document_destroy(pDoc);
		plutovg_surface_destroy(pSurface);
	}
	return err;
}

static
void gpuFrameValidate(GpuFrame *pFrame, PixtyV2_I32 size) {
	bool frameBufValid = glIsFramebuffer(pFrame->frameBuf);
	if (!frameBufValid ||
		!pixmV2I32Equal(size, pFrame->frameSize)
	) {
		if (frameBufValid) {
			gpuFrameDestroy(pFrame);
		}
		frameBufInit(pFrame, size);
	}
}

static
PixErr mainLoop(SDL_Window *pWindow, GlCtx *pGlCtx) {
	PixErr err = PIX_ERR_SUCCESS;

	Session session = {0};
	Viewport viewport = {0};
	viewportInit(&viewport);
	Timeline timeline = {0};
	timelineInit(&timeline);
	CarkGuiState gui = {0};
	sessionClear(&session);
	err = iconsLoad(&gui);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	do {
		PixtyV2_I32 windowSize = {0};
		SDL_GetWindowSize(pWindow, windowSize.d, windowSize.d + 1);

		CarkGuiEventQueue guiQueue = {0};
		err = carkGuiLayout(
			&session,
			windowSize,
			&gui,
			&guiQueue,
			viewport.frame.targetTex,
			timeline.frame.targetTex
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		for (I32 i = 0; i < guiQueue.count; ++i) {
			err = guiEventHandle(pWindow, &gui, guiQueue.queue[i]);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}

		SDL_Event event = {0};
		bool exit = false;
		while (SDL_PollEvent(&event)) {
			err = eventHandle(
				windowSize,
				gui.activeWindow,
				&event,
				&exit,
				&viewport,
				&timeline
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (exit) {
				goto mainLoopExit;
			}
		}

		err = update(&gui, &session);
		PIX_ERR_THROW_IFNOT(err, "", 0);

		gpuFrameValidate(&viewport.frame, session.viewportSize);
		gpuFrameValidate(&timeline.frame, session.timelineSize);
		err = draw(pWindow, &session, &viewport, &timeline);
		PIX_ERR_THROW_IFNOT(err, "draw failed", 0);
		SDL_Delay(1);
	} while(true);

mainLoopExit:
	PIX_ERR_CATCH(0, err, ;);
	while (gui.fileDialogActive) {
		SDL_Delay(1);
	}
	if (gui.pFileDialogPath) {
		free(gui.pFileDialogPath);
	}
	if (glIsTexture(gui.iconArr[0])) {
		glDeleteTextures(ICON_COUNT, gui.iconArr);
	}
	viewportDestroy(&viewport);
	timelineDestroy(&timeline);
	return err;
}

static
PixErr assetDirGet(int argc, char **argv) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, argc, "binary path not passed");
	I32 binPathLen = strnlen(argv[0], BIN_PATH_LEN_MAX);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		binPathLen < BIN_PATH_LEN_MAX,
		"binary path is too long"
	);
	I32 binDirLen = 0;
	for (I32 i = binPathLen - 1; i >= 0; --i) {
		if (argv[0][i] == '/' || argv[0][i] == '\\') {
			binDirLen = i + 1;
			break;
		}
	}
	const char redirSuffix[] = "../../../assets/";
	assetDirLen = binDirLen + sizeof(redirSuffix) - 1;
	PIX_ERR_ASSERT("", assetDirLen > 0);
	pAssetDir = malloc(assetDirLen + 1);
	memcpy(pAssetDir, argv[0], binDirLen);
	memcpy(pAssetDir + binDirLen, redirSuffix, sizeof(redirSuffix));
	printf("asset dir: %s\n", pAssetDir);
	return err;
}

static
PixErr init(int argc, char **argv) {
	PixErr err = PIX_ERR_SUCCESS;
	err = assetDirGet(argc, argv);
	PIX_ERR_RETURN_IFNOT(err, "");

	requiredTable[STAGE_DATA_MESH] = (Required){
		.pArr = requiredMeshArr,
		.size = sizeof(requiredMeshArr) / sizeof(CarkDesc)
	};

	typeFromDesc[CARK_DESC_MESH] = STAGE_DATA_MESH;
	typeFromDesc[CARK_DESC_FACE] = STAGE_DATA_MESH;
	return err;
}

int main(int argc, char **argv) {
	PixErr err = PIX_ERR_SUCCESS;
	err = init(argc, argv);
	PIX_ERR_THROW_IFNOT(err, "", 0);

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
	if (glCtx.pSdlCtx) {
		SDL_GL_DestroyContext(glCtx.pSdlCtx);
	}
	if (pWindow) {
		SDL_DestroyWindow(pWindow);
	}
	if (pAssetDir) {
		free(pAssetDir);
	}
	SDL_Quit();
	return err != PIX_ERR_SUCCESS;
}

//wrapper funcs for use in gui lib (c++)
PixErr avlIterInitConst(const PixuctAvl *pHandle, PixuctAvlIter *pIter) {
	return pixuctAvlIterInitConst(pHandle, pIter);
}

bool avlIterAtEnd(const PixuctAvlIter *pIter) {
	return pixuctAvlIterAtEnd(pIter);
}

void avlIterInc(PixuctAvlIter *pIter) {
	pixuctAvlIterInc(pIter);
}

const PixuctAvlNodeCore *avlIterGetItemConst(PixuctAvlIter *pIter) {
	return pixuctAvlIterGetItemConst(pIter);
}
