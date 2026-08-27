#pragma once

/**
 * @file main.h
 * @brief Utility to execute a companion Windows INI profile (.ini) or command (.cmd) script with I/O passthrough and timeout management.
 *
 * When a companion INI file named identically to the executable (plus a .ini extension) is found,
 * it is parsed using the Win32 Private Profile API. The wrapper configures the process PATH ([PATH] section),
 * sets custom environment variables ([ENV] section), and executes the target program ([RUN] section),
 * taking precedence over any .cmd script.
 * When the .ini file is not present, it falls back to executing a companion command script (.cmd).
 *
 * It connects the child process directly to this process's real console handles (rather than intermediary pipes)
 * so that interactive console behavior - such as cls, colors, and native input echo - works directly.
 * It also enforces execution limits based on the [MAX-TIME] configuration in the companion INI file.
 *
 * This is plain C (no C++ runtime at all): only the Win32 API and the C standard library are used,
 * so the statically-linked binary stays minimal - no iostream, no filesystem, no exceptions/RTTI,
 * no C++ runtime startup code.
 *
 * Compilation:
 * @code
 * cl /std:c17 cmd-pass.c
 * x86_64-w64-mingw32-gcc -std=c17 -O2 -static -o cmd-pass.exe cmd-pass.c
 * x86_64-w64-mingw32-gcc -std=c17 -Os -s -static -ffunction-sections -fdata-sections -Wl,--gc-sections -o cmd-pass.exe cmd-pass.c
 * @endcode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/**
 * @brief Resolves the real, final path of an executable, following symlinks and reparse points.
 * @param inPath The input path to the executable as invoked.
 * @param outPath Destination buffer to receive the resolved path.
 * @param outPathSize Size of the destination buffer in bytes.
 * @return TRUE if the path was successfully resolved; otherwise FALSE.
 */
BOOL resolveExecutablePath(const char* inPath, char* outPath, DWORD outPathSize);

/**
 * @brief Extracts the directory portion of an executable path.
 * @param inPath Path to the executable file.
 * @param outDir Destination buffer to receive the directory path.
 * @param outDirSize Size of the destination buffer in bytes.
 * @return TRUE on success; otherwise FALSE if inputs are invalid or buffer is too small.
 */
BOOL getExeDirectory(const char* inPath, char* outDir, DWORD outDirSize);

/**
 * @brief Checks whether a path string is absolute (starts with drive letter or UNC prefix).
 * @param path The path string to check.
 * @return TRUE if the path is absolute; otherwise FALSE.
 */
BOOL isAbsolutePath(const char* path);

/**
 * @brief Resolves a relative path against an executable directory or normalizes an absolute path.
 * @param exeDir Base directory used for resolving relative paths.
 * @param inPath The input path to resolve or normalize.
 * @param outPath Destination buffer to receive the resolved full path.
 * @param outPathSize Size of the destination buffer in bytes.
 * @return TRUE if the path was successfully resolved; otherwise FALSE.
 */
BOOL resolvePath(const char* exeDir, const char* inPath, char* outPath, DWORD outPathSize);

/**
 * @brief Resolves a semicolon-delimited list of paths against an executable directory.
 * @param exeDir Base directory used for resolving relative path entries.
 * @param pathList Semicolon-delimited list of paths to resolve.
 * @param outList Destination buffer to receive the resolved semicolon-delimited path list.
 * @param outListSize Size of the destination buffer in bytes.
 */
void resolvePathList(const char* exeDir, const char* pathList, char* outList, DWORD outListSize);

/**
 * @brief Extracts the base name of the invoked executable and determines if it was invoked via a symlink.
 * @param invokedPath The path used to invoke the executable.
 * @param resolvedPath The resolved target path of the executable.
 * @param outSymlinkName Destination buffer to receive the base name without directory and .exe extension.
 * @param outSize Size of the destination buffer in bytes.
 * @return TRUE if invoked via a symlink (invoked path differs from resolved path); otherwise FALSE.
 */
BOOL getSymlinkName(const char* invokedPath, const char* resolvedPath, char* outSymlinkName, DWORD outSize);

/**
 * @brief Resolves the effective INI section name for a given prefix, accounting for symlink mappings and fallbacks.
 * @param iniPath Path to the companion INI file.
 * @param basePrefix Base section prefix (e.g., "PATH", "ENV", "RUN").
 * @param symlinkName Symlink base name if invoked via symlink, or NULL/empty otherwise.
 * @param outSection Destination buffer to receive the resolved section name.
 * @param outSize Size of the destination buffer in bytes.
 */
void resolveSectionName(const char* iniPath, const char* basePrefix, const char* symlinkName, char* outSection, DWORD outSize);

/**
 * @brief Reads PATH configuration from the specified INI section and updates the process PATH environment variable.
 * @param iniPath Path to the companion INI file.
 * @param exeDir Base directory used for resolving relative path entries.
 * @param sectionName Name of the INI section containing PATH settings.
 */
void applyPathSettings(const char* iniPath, const char* exeDir, const char* sectionName);

/**
 * @brief Removes matching leading and trailing double quotes from a string in place.
 * @param str The null-terminated string to modify in place.
 */
void removeMatchingQuotes(char* str);

/**
 * @brief Reads environment variable definitions from the specified INI section and sets them in the current process.
 * @param iniPath Path to the companion INI file.
 * @param sectionName Name of the INI section containing environment variable key-value pairs.
 */
void applyEnvSettings(const char* iniPath, const char* sectionName);

/**
 * @brief Executes a command line with console handle inheritance and optional timeout enforcement.
 * @param cmdLine Command line string to execute.
 * @param workDir Working directory for the child process, or NULL to use current directory.
 * @param timeoutSeconds Maximum execution time in seconds before forcible termination, or 0 for no timeout.
 * @return Exit code of the child process, or -1 on failure to create the process.
 */
int executeProcess(char* cmdLine, const char* workDir, int timeoutSeconds);

/**
 * @brief Reads the debug level from the OPTIONS section of the INI file and updates the global debug setting.
 * @param iniPath Path to the companion INI file.
 */
void resolveDebugLevel(const char* iniPath);

/**
 * @brief Applies environment and PATH settings and executes the target process configured in an INI profile.
 * @param iniPath Path to the companion INI file.
 * @param exeDir Directory containing the launcher executable.
 * @param argc Argument count passed to the main process.
 * @param argv Argument vector passed to the main process.
 * @param pathSection Name of the INI section containing PATH settings.
 * @param envSection Name of the INI section containing environment variable settings.
 * @param runSection Name of the INI section containing execution parameters.
 * @param timeoutSeconds Execution timeout in seconds, or 0 for no timeout.
 * @return Exit code of the executed target process, or 1 on configuration/creation failure.
 */
int runIniProfile(
	const char* iniPath, const char* exeDir, int argc, char* argv[],//
	const char* pathSection, const char* envSection, const char* runSection, int timeoutSeconds
);

/**
 * @brief Main entry point implementation for the launcher utility.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return Exit code to return to the operating system.
 */
int main_entry(int argc, char* argv[]);
