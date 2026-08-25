# Required first entry checking the cmake version.
cmake_minimum_required(VERSION 3.29...4.4)

project("cmd-pass" LANGUAGES C)

include(ExternalProject)

# Define the targets you want to build
set(LAUNCHERS "AppA" "AppB")

# Common directories.
set(EXTERNAL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/launcher_src")
set(EXTERNAL_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/launchers_build")
set(LAUNCHER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")

foreach (LAUNCHER_NAME ${LAUNCHERS})
	externalproject_add(
		ext_${LAUNCHER_NAME}
		PREFIX "${EXTERNAL_BIN_DIR}/${LAUNCHER_NAME}"
		SOURCE_DIR "${EXTERNAL_SRC_DIR}"
		BINARY_DIR "${EXTERNAL_BIN_DIR}/${LAUNCHER_NAME}_build"
		# Pass the toolchain file and target-specific variables.
		CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${LAUNCHER_OUTPUT_DIR}
		-DLAUNCHER_TARGET_NAME=${LAUNCHER_NAME}
		-DAPP_ICON_PATH=${CMAKE_CURRENT_SOURCE_DIR}/resources/${LAUNCHER_NAME}.ico
		-DAPP_VERSION_STR=1.0.0.0
		# Rebuild if the launcher source changes.
		BUILD_ALWAYS 1
		# Skip installation step if not needed.
		INSTALL_COMMAND ""
	)
endforeach ()
