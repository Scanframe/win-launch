cmake_minimum_required(VERSION 3.29..4.4)

# Make it so our own packages are found and also the ones in the sub-module library.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
# Include some needed functions.
include(cmake/functions.cmake)
# Package setting the correct tool chain according the host and SF_COMPILER.
find_package(SfToolChain CONFIG REQUIRED)

# Set C standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT LAUNCHER_TARGET_NAME)
	set(LAUNCHER_TARGET_NAME "cmd-pass")
endif ()

set(SF_COMPANY_NAME "Scanframe")

if (NOT APP_VERSION_STR)
	# Get versions from Git when possible.
	sf_get_git_tag_version(_Versions "${CMAKE_CURRENT_SOURCE_DIR}")
	# Report the found Git tag found version.
	sf_report_git_tag_version("${_Versions}")
	# Split the list into separate values.
	list(GET _Versions 0 SF_GIT_TAG_VERSION)
	list(GET _Versions 1 SF_GIT_TAG_RC)
	list(GET _Versions 2 SF_GIT_TAG_COMMITS)
	list(GET _Versions 0 APP_VERSION_STR)
endif ()

if (NOT APP_ICON_PATH)
	set(APP_ICON_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/res/logo.ico")
	sf_check_file_exists("${APP_ICON_PATH}")
endif ()

project(${LAUNCHER_TARGET_NAME}
	LANGUAGES C
	DESCRIPTION "Windows modified environment application launcher."
	VERSION "${APP_VERSION_STR}"
)

sf_compiler_info()

#[[
if (NOT CMAKE_RC_COMPILER)
	message(FATAL_ERROR "CMAKE_RC_COMPILER not set!")
endif ()
]]

# Build the executable using the single C file and RC file
add_executable(
	${PROJECT_NAME}
	src/main.c
)

sf_set_target_output_name("${PROJECT_NAME}")
sf_set_target_version("${PROJECT_NAME}")
sf_add_version_resource("${PROJECT_NAME}")

if (WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
	if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
		# Add compilation and linker flags.
		target_compile_options(${PROJECT_NAME} PRIVATE -Os -ffunction-sections -fdata-sections)
		target_link_options(${PROJECT_NAME} PRIVATE -s -static -Wl,--gc-sections)
	elseif (MSVC)
		# Statically link C runtime (/MT).
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		# Add compiler flags.
		target_compile_options(cmd-pass PRIVATE /O1 /Gy /Gw)
		# Add linker flag to eliminate dead code/data.
		target_link_options(cmd-pass PRIVATE /OPT:REF)
	endif ()
endif ()

install(TARGETS ${_targets}
	RUNTIME DESTINATION . COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
	LIBRARY DESTINATION . COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
)

#[[
sf_install_safe(
	SOURCE
	src/conf/cmd-pass.exe.cmd
	src/conf/cmd-pass.exe.ini
	DESTINATION .
	COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
)
]]

sf_install_safe(
	FILE
	src/conf/cmd-pass.exe.cmd ./cmd-pass.exe.cmd-disabled
	src/conf/cmd-pass.exe.ini ./cmd-pass.exe.ini
	COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
)

if (SF_BUILD_TESTING)
	# Enable the tests added with add_test.
	enable_testing()
	#
	add_subdirectory(src/tests)
	# Include CDash dashboard testing module and it sets the BUILD_TESTING to 'ON'.
	include(CTest)
endif ()

include(cmake/cpack/prepare.cmake)