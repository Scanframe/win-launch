#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


BOOL resolveExecutablePath(const char* inPath, char* outPath, DWORD outPathSize);
BOOL getExeDirectory(const char* inPath, char* outDir, DWORD outDirSize);
BOOL isAbsolutePath(const char* path);
BOOL resolvePath(const char* exeDir, const char* inPath, char* outPath, DWORD outPathSize);
void resolvePathList(const char* exeDir, const char* pathList, char* outList, DWORD outListSize);
BOOL getSymlinkName(const char* invokedPath, const char* resolvedPath, char* outSymlinkName, DWORD outSize);
void resolveSectionName(const char* iniPath, const char* basePrefix, const char* symlinkName, char* outSection, DWORD outSize);
void applyPathSettings(const char* iniPath, const char* exeDir, const char* sectionName);
void applyEnvSettings(const char* iniPath, const char* sectionName);
int executeProcess(char* cmdLine, const char* workDir, int timeoutSeconds);
int resolveDebugLevel(const char* iniPath);
int runIniProfile(
	const char* iniPath, const char* exeDir, int argc, char* argv[],//
	const char* pathSection, const char* envSection, const char* runSection, int timeoutSeconds,
	int debugLevel
);
int main_entry(int argc, char* argv[]);
