cmake_minimum_required(VERSION 3.20)
project(Bodycam CXX)

if (NOT WIN32)
	message(FATAL_ERROR "Bodycam only builds for Windows (Fallout 4 x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version for the log's load line, from the newest "## x.y.z" heading in the changelog.
file(STRINGS CHANGELOG.md BODYCAM_VERSION_LINE REGEX "^## [0-9]+\.[0-9]+\.[0-9]+" LIMIT_COUNT 1)
string(REGEX MATCH "[0-9]+\.[0-9]+\.[0-9]+" BODYCAM_VERSION "${BODYCAM_VERSION_LINE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS CHANGELOG.md)

# Three builds of the same source, one per game exe (addresses differ, see src/Offsets.h). Both
# DLLs are named Bodycam.dll; the FOMOD installs whichever matches the player's game.
#   Bodycam     -> build/dist/Release/Bodycam.dll     Fallout 4 1.11.240 (AE),       F4SE 0.7.9
#   Bodycam_NG  -> build/dist_ng/Release/Bodycam.dll  Fallout 4 1.10.984 (next-gen), F4SE 0.7.2
#   Bodycam_OG  -> build/dist_og/Release/Bodycam.dll  Fallout 4 1.10.163 (old-gen),  F4SE 0.6.23
set(BODYCAM_SOURCES
	src/PluginMain.cpp
	src/Bodycam.cpp
)

foreach(target Bodycam Bodycam_NG Bodycam_OG)
	add_library(${target} SHARED ${BODYCAM_SOURCES})

	target_include_directories(${target} PRIVATE src)

	# /EHa is required: Bodycam.cpp / RayCast.h use __try/__except (SEH) around the one
	# unverified pointer dereference (see Offsets.h) so a bad address degrades to "skip
	# this frame" instead of crashing the whole game.
	target_compile_options(${target} PRIVATE /EHa /W4 /MP)
	target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS BODYCAM_VERSION="${BODYCAM_VERSION}")
	set_target_properties(${target} PROPERTIES OUTPUT_NAME "Bodycam")
endforeach()

target_compile_definitions(Bodycam_NG PRIVATE BODYCAM_NG)
target_compile_definitions(Bodycam_OG PRIVATE BODYCAM_OG)
# Separate output dirs for everything (DLL, import lib, PDB) - the shared OUTPUT_NAME would clash.
set_target_properties(Bodycam    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set_target_properties(Bodycam_NG PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist_ng" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist_ng")
set_target_properties(Bodycam_OG PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist_og" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist_og")
