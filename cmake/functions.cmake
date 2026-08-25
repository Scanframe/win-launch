set(SF_DEFAULT_COMPONENT_NAME "runtime")

##!
# Fix for an optional argument in a nested function where a variable ARGV4 when not passed
# as an argument has the value of the parent function.
#  @param _OutVar Name of the output variable returning the value which is not defined in the parent scope when
#     the index is out of range. Use 'if (DEFINED _MyArg)' to check it.
#  @param _Index Index value into the list of additional arguments.
#  @param _Argn List of additional arguments of set with "${ARGN}" by the calling function.
#
function(sf_get_optional_argument _VarOut _Index _Argn)
	list(LENGTH _Argn _Length)
	unset(${_VarOut} PARENT_SCOPE)
	if (_Index LESS _Length)
		list(GET _Argn ${_Index} _Value)
		set(${_VarOut} "${_Value}" PARENT_SCOPE)
	endif ()
endfunction()

##!
# Checks if the required passed file exists.
# When not a useful fatal message is produced.
#
function(sf_check_file_exists _File)
	if (NOT EXISTS "${_File}")
		message(SEND_ERROR "The file \"${_File}\" does not exist. Check order of dependent add_subdirectory(...).")
	endif ()
endfunction()

##!
# Gets the version from the Git repository using 'PROJECT_SOURCE_DIR' variable.
# Always returns a versions list where per index:
# 1: Actual version
# 2: Release-candidate number
# 3: Diverted commits since the tag was created.
# 3: A hash ???
# When no tag is set it simulates finding 'v0.0.0-rc.0' as the version tag.
#
function(sf_get_git_tag_version _VarOut _SrcDir)
	# Initialize return value.
	set(${_VarOut} "" PARENT_SCOPE)
	# Get git binary location for execution.
	find_program(_GitExe "git" PATHS "$ENV{SYSTEMDRIVE}/cygwin64/bin")
	if (NOT _GitExe)
		message(SEND_ERROR "Git program not found!")
	endif ()
	if ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
		# Get the toplevel directory of this repository or submodule.
		# This is faster then the other call and cache the version result to speed configuration up.
		execute_process(COMMAND
			"${_GitExe}" rev-parse --show-toplevel
			# Use the current project directory to find.
			WORKING_DIRECTORY "${_SrcDir}"
			OUTPUT_VARIABLE _FilePath
			RESULT_VARIABLE _ExitCode
			ERROR_VARIABLE _ErrorText
			ECHO_ERROR_VARIABLE
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		# Replace the directory separators from the filepath.
		string(REPLACE "/" "-" _FilePath "${_FilePath}")
		# Prefix the file with the path.
		set(_FilePath "${CMAKE_BINARY_DIR}/git-cache/ver${_FilePath}")
		# Check if the cache file exists.
		if (NOT EXISTS "${_FilePath}")
			# Only annotated tags so no '--tags' option.
			execute_process(COMMAND
				#"${_GitExe}" rev-parse --show-toplevel
				"${_GitExe}" describe --dirty --match "v*.*.*"
				# Use the current project directory to find.
				WORKING_DIRECTORY "${_SrcDir}"
				OUTPUT_VARIABLE _Version
				RESULT_VARIABLE _ExitCode
				ERROR_VARIABLE _ErrorText
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_STRIP_TRAILING_WHITESPACE
			)
			# Do not cache an empty version to file.
			if (NOT _Version STREQUAL "")
				# Write the cache file.
				file(WRITE "${_FilePath}" "${_Version}")
				# Notify the that a cache version is used.
				message(STATUS "${CMAKE_CURRENT_FUNCTION}(): Creating cache version (${_Version})")
			endif ()
		else ()
			# Read the cache file.
			file(READ "${_FilePath}" _Version)
		endif ()
	else ()
		# Only annotated tags so no '--tags' option.
		execute_process(COMMAND "${_GitExe}" -C "${_SrcDir}" describe --dirty --match "v*.*.*"
			# Use the current project directory to find.
			WORKING_DIRECTORY "${_SrcDir}"
			OUTPUT_VARIABLE _Version
			RESULT_VARIABLE _ExitCode
			ERROR_VARIABLE _ErrorText
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_STRIP_TRAILING_WHITESPACE
		)
	endif ()
	# Check the exist code for an error.
	if (_ExitCode GREATER 0)
		message(VERBOSE "Repository '${_SrcDir}' not having a version tag like 'v1.2.3' or 'v1.2.3-rc.4 ?!")
		message(VERBOSE "${_GitExe} describe --dirty --match v* ... Exited with (${_ExitCode}). '${_ErrorText}'")
		# Set an initial version to allow continuing.
		set(_Version "v0.0.0-rc.0-dirty")
	endif ()
	# Regular expression getting all elements.
	set(_RegEx "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-rc\\.?([0-9]+))?(-([0-9]+)?(-([a-z0-9]+))?)?(-dirty)?$")
	#[[
	Matching possible different results to match.
	v1.2.3-rc.4-56-78abcdef-dirty
	v0.0.1-42-g914edbb-dirty
	v0.1.1-rc.9-dirty
	v0.1.1-rc.9-12
	v0.1.2-dirty
	v0.1.1
	Group 1 > Version          : 1.2.3
	Group 3 > Release Candidate: 4f4d0976ac5eb0a07889f1913f38d66127f3b9abe
	Group 5 > Commits since tag: 56
	Group 7 > Hash of some sort: 78abcdef
	]]
	string(REGEX MATCH "${_RegEx}" _Dummy_ "${_Version}")
	if ("${CMAKE_MATCH_1}" STREQUAL "")
		message(WARNING "Git returned tag '${_Version}' from '${_SrcDir}' does not match regex '${_RegEx}' !")
		set(${_VarOut} "0;0;0;0" PARENT_SCOPE)
	else ()
		# Make a list of the versions.
		set(${_VarOut} "${CMAKE_MATCH_1}" "${CMAKE_MATCH_3}" "${CMAKE_MATCH_5}" "${CMAKE_MATCH_7}" PARENT_SCOPE)
	endif ()
endfunction()

##!
# Reports the version retrieved with Sf_GetGitTagVersion().
#
function(sf_report_git_tag_version _Versions)
	# Split the list into separate values.
	list(GET _Versions 0 _Version)
	list(GET _Versions 1 _ReleaseCandidate)
	list(GET _Versions 2 _CommitOffset)
	set(_List "Git Tag Version: ${_Version}")
	if (NOT _ReleaseCandidate STREQUAL "")
		list(APPEND _List "Release-Candidate: ${_ReleaseCandidate}")
	endif ()
	if (NOT _CommitOffset STREQUAL "")
		list(APPEND _List "Commit-Offset: ${_CommitOffset}")
	endif ()
	list(JOIN _List " > " _List)
	message(STATUS "${_List}")
endfunction()

##!
# Sets the extension of the created shared library or executable.
#
function(sf_set_target_output_name _Target)
	# When the first optional argument is given use it to set labels.
	sf_get_optional_argument(_OutputName 0 "${ARGN}")
	if (NOT DEFINED _OutputName)
		set(_OutputName "${_Target}")
	endif ()
	get_target_property(_type "${_Target}" TYPE)
	if (_type STREQUAL "EXECUTABLE")
		if (WIN32)
			set_target_properties(${_Target} PROPERTIES OUTPUT_NAME "${_OutputName}" SUFFIX ".exe")
		else ()
			set_target_properties(${_Target} PROPERTIES OUTPUT_NAME "${_OutputName}" SUFFIX ".bin")
		endif ()
	elseif (_type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
		if (WIN32)
			set_target_properties(${_Target} PROPERTIES LIBRARY_OUTPUT_NAME "${_OutputName}" SUFFIX ".dll")
		else ()
			set_target_properties(${_Target} PROPERTIES LIBRARY_OUTPUT_NAME "${_OutputName}" SUFFIX ".so")
		endif ()
	endif ()
endfunction()

##!
# Gets the output path of the given target at configure time.
#
function(sf_get_target_output_path _target _result)
	get_target_property(_out_name ${_target} OUTPUT_NAME)
	get_target_property(_out_suffix "${_target}" SUFFIX)
	get_target_property(_out_dir ${_target} RUNTIME_OUTPUT_DIRECTORY)
	if (NOT _out_dir)
		set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}")
	endif ()
	if (NOT _out_name)
		set(_out_name "${_target}")
	endif ()
	set(${_result} "${_out_dir}/${_out_name}${_out_suffix}" PARENT_SCOPE)
endfunction()

##!
# Sets the passed target version property when not set already.
# The order in which the version is retrieved:
#
function(sf_set_target_version _Target)
	set(_Version "${PROJECT_VERSION}")
	# Get the type of the target.
	get_target_property(_Type ${_Target} TYPE)
	# When the version string was resolved apply the properties.
	if (NOT "${_Version}" STREQUAL "")
		# Only in Linux SOVERSION makes sense.
		if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
			# Do not want symlink like SO-file.
			if (_Type STREQUAL "EXECUTABLE")
				set_target_properties("${_Target}" PROPERTIES SOVERSION "${_Version}")
			else ()
				# Get the major version
				string(REGEX REPLACE "^([0-9]+)\\..*" "\\1" _MajorVersion "${_Version}")
				# Set the target version properties for Linux.
				set_target_properties("${_Target}" PROPERTIES VERSION "${_Version}" SOVERSION "${_MajorVersion}")
			endif ()
		else ()
			# Set the target version properties for Windows.
			set_target_properties("${_Target}" PROPERTIES SOVERSION "${_Version}")
		endif ()
	endif ()
endfunction()

##!
# Add version resource 'resource.rc' to be compiled by passed target.
#
function(sf_add_version_resource _Target)
	get_target_property(_Version "${_Target}" SOVERSION)
	get_target_property(_Type "${_Target}" TYPE)
	if (_Type STREQUAL "EXECUTABLE")
		get_target_property(_OutputName "${_Target}" OUTPUT_NAME)
	elseif (_Type STREQUAL "SHARED_LIBRARY")
		get_target_property(_OutputName "${_Target}" LIBRARY_OUTPUT_NAME)
	endif ()
	# Check if _OutputName was set.
	if (NOT _OutputName)
		message(SEND_ERROR "For target '${_Target}', a call to Sf_SetTargetSuffix() must preceded ${CMAKE_CURRENT_FUNCTION}()!")
	endif ()
	get_target_property(_OutputSuffix "${_Target}" SUFFIX)
	string(REPLACE "." "," RC_WindowsFileVersion "${_Version},0")
	set(RC_WindowsProductVersion "${RC_WindowsFileVersion}")
	set(RC_FileVersion "${_Version}")
	set(RC_ProductVersion "${RC_FileVersion}")
	set(RC_FileDescription "${CMAKE_PROJECT_DESCRIPTION}")
	set(RC_ProductName "${CMAKE_PROJECT_DESCRIPTION}")
	set(RC_OriginalFilename "${_OutputName}${_OutputSuffix}")
	set(RC_InternalName "${_OutputName}${_OutputSuffix}")
	set(RC_Compiler "${CMAKE_HOST_SYSTEM} ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
	set(_icon_path "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/res/logo.ico")
	set(RC_ApplicationIcon "101 ICON \"${_icon_path}\"")
	string(TIMESTAMP RC_BuildDateTime "%Y-%m-%dT%H:%M:%SZ" UTC)
	if (NOT DEFINED SF_COMPANY_NAME)
		set(RC_CompanyName "Unknown")
	else ()
		set(RC_CompanyName "${SF_COMPANY_NAME}")
	endif ()
	set(_HomepageUrl "${HOMEPAGE_URL}")
	set(RC_Comments "Build on '${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR} ${CMAKE_HOST_SYSTEM_VERSION}' with '${CMAKE_C_COMPILER_ID}'.")
	# Set input and output files for the generation of the actual config file.
	set(_FileIn "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tpl/version.rc")
	# Make sure the file exists.
	sf_check_file_exists("${_FileIn}")
	# Assemble the file out.
	set(_FileOut "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
	# Generate the configure the file for the resource.
	configure_file("${_FileIn}" "${_FileOut}" @ONLY NEWLINE_STYLE LF)
	#
	target_sources("${_Target}" PRIVATE "${_FileOut}")
endfunction()

##!
# Get all added targets in all subdirectories.
#  @param _result The list containing all found targets
#  @param _dir Root directory to start looking from
#  @param _inc_deps Include dependencies TRUE or FALSE.
#
function(sf_get_all_targets _result _dir _inc_deps)
	# Get the length of the name to skip.
	string(LENGTH "${FETCHCONTENT_BASE_DIR}" _length)
	get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
	foreach (_subdir IN LISTS _subdirs)
		string(SUBSTRING "${_subdir}" 0 ${_length} _tmp)
		if (NOT _inc_deps AND _tmp STREQUAL FETCHCONTENT_BASE_DIR)
			#message(NOTICE "Skipping: ${_subdir}")
			continue()
		endif ()
		sf_get_all_targets(${_result} "${_subdir}" ${_inc_deps})
	endforeach ()
	get_directory_property(_sub_targets DIRECTORY "${_dir}" BUILDSYSTEM_TARGETS)
	set(${_result} ${${_result}} ${_sub_targets} PARENT_SCOPE)
endfunction()

##!
# Gets the safe filename version of the current or passed architecture.
# @param _OutVar Resulting architecture name.
# @param _Arch  Optional architecture string.
#
function(sf_get_safe_architecture_name _OutVar)
	# Default to the host system processor if no argument is passed
	sf_get_optional_argument(_Arch 0 "${ARGN}")
	if (NOT DEFINED _Arch)
		set(_Arch "${CMAKE_SYSTEM_PROCESSOR}")
	endif ()
	# Normalize to lowercase.
	string(TOLOWER "${_Arch}" _arch_lower)
	# Map the architecture to a file-safe naming convention.
	if (_arch_lower STREQUAL "x86_64" OR _arch_lower STREQUAL "amd64")
		set(_arch_safe "amd64")
	elseif (_arch_lower STREQUAL "aarch64" OR _arch_lower STREQUAL "arm64")
		set(_arch_safe "arm64")
	elseif (_arch_lower STREQUAL "armv7l" OR _arch_lower STREQUAL "armv8l")
		set(_arch_safe "armv7")
	elseif (_arch_lower STREQUAL "armv6l")
		set(_arch_safe "armv6")
	elseif (_arch_lower MATCHES "i.86" OR _arch_lower STREQUAL "x86" OR _arch_lower STREQUAL "386")
		set(_arch_safe "i386")
	elseif (_arch_lower STREQUAL "ppc64le" OR _arch_lower STREQUAL "ppc64el")
		set(_arch_safe "ppc64le")
	elseif (_arch_lower STREQUAL "s390x")
		set(_arch_safe "s390x")
	elseif (_arch_lower STREQUAL "riscv64")
		set(_arch_safe "riscv64")
	else ()
		# Fallback safety: Replace any underscores or slashes with hyphens.
		string(REPLACE "_" "-" _arch_lower "${_arch_lower}")
		string(REPLACE "/" "-" _arch_safe "${_arch_lower}")
	endif ()
	# Pass the value back up to the parent scope
	set(${_OutVar} "${_arch_safe}" PARENT_SCOPE)
endfunction()

##!
# Call message() on each item in the given variable prefixed with an index number.
# Use 'CMAKE_MESSAGE_INDENT' to prefix each message.
#
function(sf_list_path _Path)
	sf_get_optional_argument(_Prefix 0 "${ARGN}")
	if (NOT _Mode)
	endif ()
	if (WIN32 AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
	endif ()
	# Check if the variable is a Linux path one.
	string(FIND "${_Path}" ";" _idx)
	# Check if this is a Linux path.
	if (_idx EQUAL -1)
		string(REPLACE ":" ";" _Path "${_Path}")
	endif ()
	set(_Counter 0)
	foreach (_Dir IN LISTS _Path)
		message(STATUS "${_Prefix}[${_Counter}]: ${_Dir}")
		math(EXPR _Counter "${_Counter} + 1")
	endforeach ()
endfunction()

##!
# Reports information about the CMake and sets compiler general options depending on the selected compiler.
#
function(sf_compiler_info)
	if ("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
		list(APPEND CMAKE_MESSAGE_INDENT "CMake ")
		# Report when the global C or C++ standard has not been set.
		if (CMAKE_C_STANDARD_REQUIRED AND "${CMAKE_C_STANDARD}" STREQUAL "")
			message(SEND_ERROR "Global C++ standard using 'CMAKE_C_STANDARD' has not been set!")
		endif ()
		if (CMAKE_CXX_STANDARD_REQUIRED AND "${CMAKE_CXX_STANDARD}" STREQUAL "")
			message(SEND_ERROR "Global C++ standard using 'CMAKE_CXX_STANDARD' has not been set!")
		endif ()
		message(STATUS "Version           : ${CMAKE_VERSION}")
		message(STATUS "Message Log Level : ${CMAKE_MESSAGE_LOG_LEVEL}")
		message(STATUS "Verbose Makefile  : ${CMAKE_VERBOSE_MAKEFILE}")
		message(STATUS "Build Type        : ${CMAKE_BUILD_TYPE}")
		message(STATUS "Generator         : ${CMAKE_MAKE_PROGRAM}")
		message(STATUS "System            : ${CMAKE_SYSTEM}")
		message(STATUS "Host System       : ${CMAKE_HOST_SYSTEM}")
		message(STATUS "Cross Compiling   : ${CMAKE_CROSSCOMPILING}")
		message(STATUS "System Info File  : ${CMAKE_SYSTEM_INFO_FILE}")
		message(STATUS "System Processor  : ${CMAKE_SYSTEM_PROCESSOR}")
		message(STATUS "Host Sys.Processor: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
		message(STATUS "Runtime Output Dir: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
		message(STATUS "Library Output Dir: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
		message(STATUS "C   Launcher      : ${CMAKE_C_COMPILER_LAUNCHER}")
		message(STATUS "C++ Launcher      : ${CMAKE_CXX_COMPILER_LAUNCHER}")
		message(STATUS "C   Compiler      : ${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION} > ${CMAKE_C_COMPILER}")
		message(STATUS "C++ Compiler      : ${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION} > ${CMAKE_CXX_COMPILER}")
		message(STATUS "RC  Compiler      : ${CMAKE_RC_COMPILER}")
		message(STATUS "RanLib            : ${CMAKE_RANLIB}")
		message(STATUS "Nm                : ${CMAKE_NM}")
		message(STATUS "Ar                : ${CMAKE_AR}")
		message(STATUS "Linker            : ${CMAKE_LINKER}")
		message(STATUS "Strip             : ${CMAKE_STRIP}")
		# Remove the indentation of the message() function.
		list(POP_BACK CMAKE_MESSAGE_INDENT)
		# Add Scanframe indents.
		list(APPEND CMAKE_MESSAGE_INDENT "Sf ")
		message(STATUS "Running in Docker: ${SF_DOCKER}")
		message(STATUS "Host Architecture: ${SF_HOST_ARCHITECTURE}")
		message(STATUS "Architecture     : ${SF_ARCHITECTURE}")
		message(STATUS "Compiler         : ${SF_COMPILER}")
		message(STATUS "Cross Compiling  : ${SF_CROSSCOMPILING}")
		message(STATUS "Coverage Targets : ${SF_COVERAGE_ONLY_TARGETS}")
		# Remove the indentation of the message() function.
		list(POP_BACK CMAKE_MESSAGE_INDENT)
		list(APPEND CMAKE_MESSAGE_INDENT "Env ")
		set(_Vars "SF_EXECUTABLE_DIR;SF_LIBRARY_DIR")
		if (NOT WIN32)
			list(APPEND _Vars "LD_LIBRARY_PATH")
		else ()
			list(APPEND _Vars "PATH")
		endif ()
		foreach (_Var IN LISTS _Vars)
			if (_Var MATCHES "PATH$")
				sf_list_path("$ENV{${_Var}}" "${_Var}")
			else ()
				message(STATUS "${_Var}: $ENV{${_Var}}")
			endif ()
		endforeach ()
		list(POP_BACK CMAKE_MESSAGE_INDENT)
	endif ()
endfunction()

