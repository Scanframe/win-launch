##
## This file is included in the cmake project configuration.
## It prepares stuff for CPack to run its project.
## File '.sf/SfInstallInclude.cmake' is created by 'project.cmake' when CPack is called.
## When this repository is used as an external project the cmake install si normally called
## to copy them to the staging directory.
##
install(CODE [[
## Hack for allowing ZIP/ARCHIVE generator to avoid absolute file to be created.
set(SF_ROOT_PREFIX ".")
include("${CMAKE_CURRENT_LIST_DIR}/.sf/SfInstallInclude.cmake" OPTIONAL)
]]
	COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
)

# Set some variables required by this script and CPack as well.
# The provider name for using as an install prefix (directory like: /opt/<provider-name>/my-app).
set(SF_PROVIDER_NAME "Scanframe")
set(SF_PROVIDER_EMAIL "info@scanframe.nl")

if (WIN32)
	# FIXME: Somehow the you cannot specify a sub-folder since it mixes slashes.
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "${SF_PROVIDER_NAME}")
	set(CPACK_PACKAGING_INSTALL_PREFIX "/${SF_PROVIDER_NAME}/${CMAKE_PROJECT_NAME}")
endif ()

# Assemble the basename for the packages of this project.
set(SF_PACKAGE_BASE_NAME "${CMAKE_PROJECT_NAME}")

# Add toolchain information to package name
if (CMAKE_C_COMPILER_ID)
	string(TOLOWER "${CMAKE_C_COMPILER_ID}" SF_TOOLCHAIN_ID)
	set(SF_TOOLCHAIN_STRING "${SF_TOOLCHAIN_ID}")
	if (CMAKE_C_COMPILER_VERSION)
		string(REGEX REPLACE "([0-9]+\\.[0-9]+).*" "\\1" SF_TOOLCHAIN_VERSION "${CMAKE_C_COMPILER_VERSION}")
		string(APPEND SF_TOOLCHAIN_STRING "-${SF_TOOLCHAIN_VERSION}")
	endif ()
	if (WIN32)
		string(PREPEND SF_TOOLCHAIN_STRING "win+")
	else ()
		string(PREPEND SF_TOOLCHAIN_STRING "lnx+")
	endif ()
	sf_get_safe_architecture_name(SF_ARCHITECTURE_SAFE "${SF_ARCHITECTURE}")
	# Set custom package filename including toolchain
	set(SF_PACKAGE_NAME "${SF_PACKAGE_BASE_NAME}-${SF_TOOLCHAIN_STRING}-${SF_ARCHITECTURE_SAFE}")
endif ()

# Define the path for the include file in the binary build directory.
set(CPACK_SF_INCLUDE_VARS_FILE "${CMAKE_CURRENT_BINARY_DIR}/.sf/SfCPackProjectVars.cmake")

# Pass also the cmake binary build directory using en variable with an 'SF_' prefix.
set(SF_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

set(_ExecTargets)

sf_get_all_targets(_Targets "${CMAKE_SOURCE_DIR}" TRUE)
message("TARGETS: ${_Targets}")
sf_get_all_tests(_Tests "${CMAKE_SOURCE_DIR}" TRUE)

foreach (_Target IN LISTS _Targets)
	# Get the type of the target.
	get_target_property(_Type "${_Target}" TYPE)
	# Only add linking options for target types that are linked.
	if (_Type STREQUAL "EXECUTABLE" OR _Type STREQUAL "DYNAMIC_LIBRARY")
		if (NOT _Target IN_LIST _Tests)
			list(APPEND _ExecTargets "${_Target}")
		endif ()
	endif ()
endforeach ()

install(TARGETS ${_ExecTargets}
	RUNTIME DESTINATION . COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
	LIBRARY DESTINATION lib COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
	ARCHIVE DESTINATION arc COMPONENT "devel"
)

set(CPACK_PACKAGE_EXECUTABLES)
# Create a launcher for each executable.
foreach (_ExecTarget IN LISTS _ExecTargets)
	sf_get_target_output_path("${_ExecTarget}" _OutputPath)
	get_target_property(_OutputName "${_ExecTarget}" OUTPUT_NAME)
	get_target_property(_OutputDir ${_ExecTarget} RUNTIME_OUTPUT_DIRECTORY)
	# Skip this file when output name is not set.
	if (NOT _OutputName)
		message(STATUS "Skipping target: ${_ExecTarget}")
		continue()
	endif ()
	list(APPEND SF_OUTPUT_PATHS_${SF_DEFAULT_COMPONENT_NAME} "${_OutputPath}")
	get_target_property(_OutputSuffix "${_ExecTarget}" SUFFIX)
	# Each entry is is a combination of 2 items in the list executable first and then the shortcut name.
	list(APPEND CPACK_PACKAGE_EXECUTABLES "${CPACK_PACKAGE_INSTALL_DIRECTORY}/${CMAKE_PROJECT_NAME}/${_OutputName}${_OutputSuffix}" "${_OutputName}")
	if (SF_INCLUDE_SAMPLES)
		if (_ExecTarget STREQUAL "cmd-pass")
			foreach (_alias "np++" "ctl-panel")
				install(PROGRAMS "$<TARGET_FILE:${_ExecTarget}>"
					DESTINATION .
					RENAME "${_alias}${_OutputSuffix}"
					COMPONENT "${SF_DEFAULT_COMPONENT_NAME}"
				)
				list(APPEND CPACK_PACKAGE_EXECUTABLES "${CPACK_PACKAGE_INSTALL_DIRECTORY}/${CMAKE_PROJECT_NAME}/${_alias}${_OutputSuffix}" "${_alias}")
			endforeach ()
		endif ()
	endif ()
endforeach ()

# Get date in YYYY-MM-DD format (e.g., 2026-08-23)
string(TIMESTAMP _current_date "%Y-%m-%d")
# Create a partial manifest to merge with the Nexus WinGet service.
set(SF_ZIP_MANIFEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/.sf/winget/zip-manifest.yml")
# Duplicate quotes to escape them.
string(REPLACE "'" "''" _description "${CMAKE_PROJECT_DESCRIPTION}")
file(WRITE "${SF_ZIP_MANIFEST_FILE}" "# yaml-language-server: $schema=https://aka.ms/winget-manifest.singleton.1.12.0.schema.json
ManifestVersion: 1.12.0
Publisher: '${SF_PROVIDER_NAME}'
Author: '${SF_PROVIDER_NAME}'
PackageName: '${SF_PACKAGE_BASE_NAME}'
License: GPL
ShortDescription: '${_description}'
Description: '${_description}'
ReleaseDate: '${_current_date}'
InstallerType: zip
NestedInstallerType: portable
Moniker: cmd-pass
Tags:
  - cmd
ArchiveBinariesDependOnPath: null
NestedInstallerFiles:
")
list(LENGTH CPACK_PACKAGE_EXECUTABLES _list_len)
if (_list_len GREATER 0)
	# Subtract 2 from length to get the last valid starting index of a pair
	math(EXPR _max_index "${_list_len} - 2")
	# Loop from 0 to max_index, stepping by 2 each time
	foreach (_index RANGE 0 ${_max_index} 2)
		list(GET CPACK_PACKAGE_EXECUTABLES ${_index} _exe_file)
		# Get the next item (index + 1)
		math(EXPR _val_index "${_index} + 1")
		list(GET CPACK_PACKAGE_EXECUTABLES ${_val_index} _shortcut_name)
		string(REPLACE "'" "''" _shortcut_name "${_shortcut_name}")
		string(REPLACE "/" "\\" _exe_file "${_exe_file}")
		file(APPEND "${SF_ZIP_MANIFEST_FILE}" "  - RelativeFilePath: '${_exe_file}'
    PortableCommandAlias: '${_shortcut_name}'
")
	endforeach ()
endif ()

# Clear out any existing file from a previous configuration run with a header.
file(WRITE "${CPACK_SF_INCLUDE_VARS_FILE}" "# Generated by CMake. Do not edit.\n")
# Retrieve all variables defined in the current CMake context
get_cmake_property(_variable_names VARIABLES)
# Somehow there are names double in the list.
list(REMOVE_DUPLICATES _variable_names)
# Loop through and write any CMAKE_PROJECT_ and SF_ prefixed variables directly to the file.
foreach (_var IN LISTS _variable_names)
	if (_var MATCHES "^(SF_|CMAKE_PROJECT_)")
		# Properly escape backslashes and quotes to handle paths and strings safely
		string(REPLACE "\\" "\\\\" _escaped_val "${${_var}}")
		string(REPLACE "\"" "\\\"" _escaped_val "${_escaped_val}")
		# Append the explicit set() command into the file
		file(APPEND "${CPACK_SF_INCLUDE_VARS_FILE}" "set(${_var} \"${_escaped_val}\")\n")
	endif ()
endforeach ()

# Set the cmake script cpack is going to run.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/project.cmake")

# Force 'CPACK_COMPONENTS_ALL' before 'include(CPack)' generates 'CPackConfig.cmake' otherwise 'CPACK_COMPONENTS_ALL' is empty.
if (NOT CPACK_COMPONENTS_ALL)
	get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
endif ()
include(CPack)

# Report the available install components.
message(STATUS "CPack All Components: ${CPACK_COMPONENTS_ALL}")
message(STATUS "CPACK_PACKAGE_EXECUTABLES: ${CPACK_PACKAGE_EXECUTABLES}")
