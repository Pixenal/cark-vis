# SPDX-FileCopyrightText: 2025 Caleb Dawson
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0077 NEW)
set(PROJECT_IO CarkVisIo)
set(PROJECT CarkVis)
set(PROJECT_GUI CarkVisGui)
PROJECT(${PROJECT_IO})
PROJECT(${PROJECT})
PROJECT(${PROJECT_GUI})
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG)
	message("Building for single build type")
	if (NOT CMAKE_BUILD_TYPE)
		message("No build type specified, building Release")
		set(CMAKE_BUILD_TYPE RELEASE "" FORCE)
	endif()
endif ()
if (UNIX)
	message("Building for Unix")
	set(CMAKE_C_FLAGS "")
	if (CMAKE_BUILD_TYPE MATCHES DEBUG)
		message("Debug build")
		set(CMAKE_C_FLAGS  "-g -O0 -DEBUG -pthread -Wshadow -Wunused -Wno-unused-label -fPIC")
		set(CMAKE_CXX_FLAGS  "-g -O0 -DEBUG -pthread -Wshadow -Wunused -Wno-unused-label -fPIC")
	endif()
	if (CMAKE_BUILD_TYPE MATCHES RELEASE)
		message("Release build")
		set(CMAKE_C_FLAGS  "-ftree-vectorize -mavx2 -finline-functions -O2 -pthread -fPIC")
		set(CMAKE_CXX_FLAGS  "-ftree-vectorize -mavx2 -finline-functions -O2 -pthread -fPIC")
	endif()
	set(CMAKE_C_STANDARD 99)
	set(CMAKE_CXX_STANDARD 11)
endif()
if (WIN32)
	message("Building for Windows")
	message("CMAKE_MSVC_RUNTIME_LIB is " ${CMAKE_MSVC_RUNTIME_LIBRARY})
	set(CMAKE_C_FLAGS "/D WIN32 /TC")
	set(CMAKE_CXX_FLAGS "/D WIN32 /TC")
	set(CMAKE_C_FLAGS_RELEASE "/O2")
	set(CMAKE_CXX_FLAGS_RELEASE "/O2")
	set(CMAKE_C_STANDARD 11)
	set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(${PROJECT} src/main.c)
add_library(${PROJECT_IO} STATIC src/cark_vis_io.c)
set(IMGUI extern/imgui)
add_library(${PROJECT_GUI} STATIC
	src/cark_vis_gui.cpp ${IMGUI}/imgui.cpp ${IMGUI}/imgui_demo.cpp ${IMGUI}/imgui_draw.cpp
	${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp ${IMGUI}/backends/imgui_impl_sdl3.cpp
	${IMGUI}/backends/imgui_impl_opengl3.cpp
)
option(
	CARK_PIX_PATH
	"Override the path to the folder containing pixenals-* libraries (set to extern by default)"
	"extern"
)

message("FIND_ZLIB is " ${FIND_ZLIB})
if (FIND_ZLIB)
	message("Attempting to find zlib")
	find_package(ZLIB REQUIRED)
	message("--zlib include is " ${ZLIB_INCLUDE_DIRS})
	message("Note that if the above are mismatched, there may be errors at compile time.")
	message ("Consider disabling FIND_ZLIB and specifying manually using ZLIB_LIB and ZLIB_INCLUDE. Or using Conan.")
	message("See github docs for more info.")
	target_include_directories(${PROJECT_IO} PRIVATE ${ZLIB_INCLUDE_DIRS})
	target_include_directories(${PROJECT} PRIVATE ${ZLIB_INCLUDE_DIRS})
	target_link_libraries(${PROJECT_IO} PRIVATE ${ZLIB_LIBRARIES} ${ZLIB_DEPENDENCIES})
	target_link_libraries(${PROJECT} PRIVATE ${ZLIB_LIBRARIES} ${ZLIB_DEPENDENCIES})
else()
	target_include_directories(${PROJECT_IO} PRIVATE ${ZLIB_INCLUDE})
	target_include_directories(${PROJECT} PRIVATE ${ZLIB_INCLUDE})
	target_link_libraries(${PROJECT_IO} PRIVATE ${ZLIB_LIB})
	target_link_libraries(${PROJECT} PRIVATE ${ZLIB_LIB})
endif()

message("CARK_PIX_PATH is " ${CARK_PIX_PATH})
set(CARK_PIX_ALLOC "" ${CARK_PIX_PATH}/pixenals-alloc-utils)
set(CARK_PIX_IO "" ${CARK_PIX_PATH}/pixenals-io-utils)
if (NOT TARGET PixenalsAllocUtils)
	add_subdirectory(${CARK_PIX_ALLOC} ${CARK_PIX_ALLOC})
endif()
if (NOT TARGET PixenalsIoUtils)
	add_subdirectory(${CARK_PIX_IO} ${CARK_PIX_IO})
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
add_subdirectory(extern/glew-cmake)
find_package(OpenGL REQUIRED)
target_link_libraries(${PROJECT_IO} PRIVATE PixenalsAllocUtils PixenalsIoUtils)
target_link_libraries(${PROJECT} PRIVATE
	PixenalsAllocUtils PixenalsIoUtils SDL3::SDL3 libglew_static OpenGL::GL
	${PROJECT_IO} ${PROJECT_GUI}
)
target_link_libraries(${PROJECT_GUI} PRIVATE PixenalsAllocUtils PixenalsIoUtils
	SDL3::SDL3
)
target_include_directories(${PROJECT_IO} PRIVATE include src)
target_include_directories(${PROJECT} PRIVATE include src)
target_include_directories(${PROJECT_GUI} PRIVATE include src ${IMGUI})
target_include_directories(${PROJECT_IO} PUBLIC
	${CARK_PIX_PATH}/pixenals-types/include ${CARK_PIX_PATH}/pixenals-error-utils/include
	${CARK_PIX_ALLOC}/include ${CARK_PIX_IO}/include
)
target_include_directories(${PROJECT} PUBLIC
	${CARK_PIX_PATH}/pixenals-types/include ${CARK_PIX_PATH}/pixenals-error-utils/include
	${CARK_PIX_ALLOC}/include ${CARK_PIX_IO}/include extern/glew-cmake/include
	${OPENGL_INCLUDE_DIR}
)
target_include_directories(${PROJECT_GUI} PUBLIC
	${CARK_PIX_PATH}/pixenals-types/include ${CARK_PIX_PATH}/pixenals-error-utils/include
	${CARK_PIX_ALLOC}/include ${CARK_PIX_IO}/include
)
