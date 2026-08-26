/**
 * @file cmd-pass.c
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

/* Resolves the real, final path of an executable, following symlinks and other reparse points.
 * GetModuleFileNameA alone reports the path as it was invoked, which for a symlinked executable
 * is the symlink's own path rather than the file it points to. Opening the file and asking
 * Windows for its final path (as GetFinalPathNameByHandleA does) resolves any such indirection
 * to the actual underlying file on disk.
 * On success, writes the resolved path into outPath (of size outPathSize) and returns TRUE.
 * On failure, leaves outPath untouched and returns FALSE, so the caller can fall back to the
 * original, unresolved path. */
BOOL resolveExecutablePath(const char* inPath, char* outPath, DWORD outPathSize)
{
	/* Open the executable file itself, purely to query its identity, not to read/write it. */
	HANDLE hFile = CreateFileA(inPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	/* Ask Windows for the final, symlink-resolved path. This comes back in the extended-length
	 * "\\?\" form (e.g. "\\?\C:\real\path\app.exe", or "\\?\UNC\server\share\..." for network
	 * shares), which we normalize below into an ordinary-looking path. */
	char rawPath[MAX_PATH + 8];
	DWORD rawLen = GetFinalPathNameByHandleA(hFile, rawPath, sizeof(rawPath), FILE_NAME_NORMALIZED);
	CloseHandle(hFile);

	if (rawLen == 0 || rawLen >= sizeof(rawPath))
	{
		return FALSE;
	}

	/* Strip the "\\?\" extended-path prefix. Network shares come back as "\\?\UNC\server\share",
	 * which needs to become "\\server\share" rather than the literal "UNC\server\share" you'd get
	 * from just chopping off "\\?\" - so that case is handled separately. */
	const char* resolved = rawPath;
	char unc[MAX_PATH + 8];

	if (strncmp(resolved, "\\\\?\\UNC\\", 8) == 0)
	{
		/* "\\?\UNC\server\share..." -> "\\server\share..." */
		if (_snprintf_s(unc, sizeof(unc), _TRUNCATE, "\\\\%s", resolved + 8) < 0)
		{
			return FALSE;
		}
		resolved = unc;
	}
	else if (strncmp(resolved, "\\\\?\\", 4) == 0)
	{
		resolved += 4;
	}

	if (strlen(resolved) >= outPathSize)
	{
		return FALSE;
	}

	strcpy(outPath, resolved);
	return TRUE;
}

/* Extracts the directory portion of an executable path.
 * If inPath is "C:\dir\app.exe", writes "C:\dir" to outDir.
 * If inPath is "C:\app.exe", writes "C:\" to outDir.
 * If inPath contains no slashes, writes "." to outDir. */
BOOL getExeDirectory(const char* inPath, char* outDir, DWORD outDirSize)
{
	if (inPath == NULL || outDir == NULL || outDirSize == 0)
	{
		return FALSE;
	}

	const char* lastSlash = strrchr(inPath, '\\');
	const char* lastFwdSlash = strrchr(inPath, '/');
	if (lastFwdSlash > lastSlash)
	{
		lastSlash = lastFwdSlash;
	}

	if (lastSlash != NULL)
	{
		size_t dirLen = (size_t) (lastSlash - inPath);
		/* Check for drive root like "C:\" */
		if (dirLen == 2 && inPath[1] == ':')
		{
			dirLen = 3; /* Include the trailing slash for drive root, e.g. "C:\" */
		}
		else if (dirLen == 0)
		{
			dirLen = 1; /* Root directory "\" */
		}

		if (dirLen >= outDirSize)
		{
			return FALSE;
		}

		strncpy(outDir, inPath, dirLen);
		outDir[dirLen] = '\0';
		return TRUE;
	}

	if (outDirSize < 2)
	{
		return FALSE;
	}
	strcpy(outDir, ".");
	return TRUE;
}

/* Checks whether a path string is absolute (starts with drive specifier like C:\ or UNC \\). */
BOOL isAbsolutePath(const char* path)
{
	if (path == NULL || path[0] == '\0')
	{
		return FALSE;
	}

	/* Drive letter path: e.g. C:\ or C:/ */
	if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
	{
		return TRUE;
	}

	/* UNC path: e.g. \\server\share or //server/share */
	if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
	{
		return TRUE;
	}

	return FALSE;
}

/* Resolves inPath relative to exeDir if relative, or normalizes it if absolute.
 * Writes normalized full path to outPath. */
BOOL resolvePath(const char* exeDir, const char* inPath, char* outPath, DWORD outPathSize)
{
	if (inPath == NULL || inPath[0] == '\0' || outPath == NULL || outPathSize == 0)
	{
		return FALSE;
	}

	char combined[MAX_PATH * 2];
	if (isAbsolutePath(inPath))
	{
		if (GetFullPathNameA(inPath, outPathSize, outPath, NULL) == 0)
		{
			return FALSE;
		}
	}
	else
	{
		size_t exeDirLen = strlen(exeDir);
		if (exeDirLen > 0 && (exeDir[exeDirLen - 1] == '\\' || exeDir[exeDirLen - 1] == '/'))
		{
			snprintf(combined, sizeof(combined), "%s%s", exeDir, inPath);
		}
		else
		{
			snprintf(combined, sizeof(combined), "%s\\%s", exeDir, inPath);
		}

		if (GetFullPathNameA(combined, outPathSize, outPath, NULL) == 0)
		{
			return FALSE;
		}
	}

	return TRUE;
}

/* Resolves a semicolon-delimited list of paths against exeDir, writing resolved list to outList. */
void resolvePathList(const char* exeDir, const char* pathList, char* outList, DWORD outListSize)
{
	if (outList == NULL || outListSize == 0)
	{
		return;
	}
	outList[0] = '\0';

	if (pathList == NULL || pathList[0] == '\0')
	{
		return;
	}

	/* Duplicate input string for tokenization */
	char* listCopy = strdup(pathList);
	if (listCopy == NULL)
	{
		return;
	}

	char* token = strtok(listCopy, ";");
	size_t currentLen = 0;

	while (token != NULL)
	{
		/* Trim leading and trailing whitespace */
		while (*token == ' ' || *token == '\t')
		{
			token++;
		}
		char* end = token + strlen(token) - 1;
		while (end >= token && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
		{
			*end = '\0';
			end--;
		}

		if (*token != '\0')
		{
			char resolvedItem[MAX_PATH];
			const char* toAppend = token;
			if (resolvePath(exeDir, token, resolvedItem, sizeof(resolvedItem)))
			{
				toAppend = resolvedItem;
			}

			size_t itemLen = strlen(toAppend);
			size_t needed = itemLen + (currentLen > 0 ? 1 : 0) + 1;
			if (currentLen + needed <= outListSize)
			{
				if (currentLen > 0)
				{
					outList[currentLen++] = ';';
					outList[currentLen] = '\0';
				}
				strcpy(outList + currentLen, toAppend);
				currentLen += itemLen;
			}
		}

		token = strtok(NULL, ";");
	}

	free(listCopy);
}

/* Extracts the base name from the invoked executable path (stripping directory and .exe extension)
 * and determines if the executable was invoked via a symlink.
 * Returns TRUE if invokedPath differs from resolvedPath, indicating symlink execution. */
BOOL getSymlinkName(const char* invokedPath, const char* resolvedPath, char* outSymlinkName, DWORD outSize)
{
	if (outSymlinkName == NULL || outSize == 0)
	{
		return FALSE;
	}
	outSymlinkName[0] = '\0';

	if (invokedPath == NULL || invokedPath[0] == '\0')
	{
		return FALSE;
	}

	/* Find start of base filename */
	const char* baseName = invokedPath;
	const char* lastSlash = strrchr(invokedPath, '\\');
	const char* lastFwdSlash = strrchr(invokedPath, '/');
	if (lastFwdSlash > lastSlash)
	{
		lastSlash = lastFwdSlash;
	}
	if (lastSlash != NULL)
	{
		baseName = lastSlash + 1;
	}

	size_t baseLen = strlen(baseName);
	/* Strip trailing .exe (case-insensitive) */
	if (baseLen >= 4 && _stricmp(baseName + baseLen - 4, ".exe") == 0)
	{
		baseLen -= 4;
	}

	if (baseLen >= outSize)
	{
		baseLen = outSize - 1;
	}
	strncpy(outSymlinkName, baseName, baseLen);
	outSymlinkName[baseLen] = '\0';

	/* Check if invokedPath differs from resolvedPath */
	if (resolvedPath != NULL && resolvedPath[0] != '\0')
	{
		return (_stricmp(invokedPath, resolvedPath) != 0);
	}

	return FALSE;
}

/* Resolves effective section name for a given section prefix (e.g. "PATH", "ENV", "RUN").
 * If symlinkName is non-empty, checks [MAP@<basePrefix>] for symlinkName.
 * If found and non-empty, writes "<basePrefix>@<value>".
 * Otherwise, checks if "<basePrefix>@" section exists; if not and legacy "<basePrefix>" exists,
 * uses "<basePrefix>". Defaults to "<basePrefix>@". */
void resolveSectionName(const char* iniPath, const char* basePrefix, const char* symlinkName, char* outSection, DWORD outSize)
{
	if (outSection == NULL || outSize == 0 || basePrefix == NULL)
	{
		return;
	}

	char mappedVal[256];
	mappedVal[0] = '\0';

	if (symlinkName != NULL && symlinkName[0] != '\0')
	{
		char mapSection[64];
		snprintf(mapSection, sizeof(mapSection), "MAP@%s", basePrefix);
		GetPrivateProfileStringA(mapSection, symlinkName, "", mappedVal, sizeof(mappedVal), iniPath);
	}

	if (mappedVal[0] != '\0')
	{
		snprintf(outSection, outSize, "%s@%s", basePrefix, mappedVal);
		return;
	}

	/* Fallback logic: check <basePrefix>@ first, then legacy <basePrefix> */
	char defaultSection[64];
	snprintf(defaultSection, sizeof(defaultSection), "%s@", basePrefix);

	char checkBuf[8];
	if (GetPrivateProfileSectionA(defaultSection, checkBuf, sizeof(checkBuf), iniPath) > 0)
	{
		snprintf(outSection, outSize, "%s@", basePrefix);
	}
	else if (GetPrivateProfileSectionA(basePrefix, checkBuf, sizeof(checkBuf), iniPath) > 0)
	{
		snprintf(outSection, outSize, "%s", basePrefix);
	}
	else
	{
		snprintf(outSection, outSize, "%s@", basePrefix);
	}
}

/* Reads the path section of the ini file and updates the PATH environment variable. */
void applyPathSettings(const char* iniPath, const char* exeDir, const char* sectionName)
{
	char buffer[32768];
	char replaceVal[32768];
	char prependVal[32768];
	char appendVal[32768];
	char currentPath[32768];
	char newPath[65536];

	replaceVal[0] = '\0';
	prependVal[0] = '\0';
	appendVal[0] = '\0';
	currentPath[0] = '\0';
	newPath[0] = '\0';

	/* Read replace key */
	if (GetPrivateProfileStringA(sectionName, "replace", "", buffer, sizeof(buffer), iniPath) > 0)
	{
		resolvePathList(exeDir, buffer, replaceVal, sizeof(replaceVal));
	}

	/* Read prepend key */
	if (GetPrivateProfileStringA(sectionName, "prepend", "", buffer, sizeof(buffer), iniPath) > 0)
	{
		resolvePathList(exeDir, buffer, prependVal, sizeof(prependVal));
	}

	/* Read append key */
	if (GetPrivateProfileStringA(sectionName, "append", "", buffer, sizeof(buffer), iniPath) > 0)
	{
		resolvePathList(exeDir, buffer, appendVal, sizeof(appendVal));
	}

	/* If replace was specified, base is replaceVal; otherwise base is current PATH */
	if (replaceVal[0] != '\0')
	{
		snprintf(currentPath, sizeof(currentPath), "%s", replaceVal);
	}
	else
	{
		GetEnvironmentVariableA("PATH", currentPath, sizeof(currentPath));
	}

	/* Assemble newPath with prependVal + base + appendVal */
	int written = 0;
	if (prependVal[0] != '\0')
	{
		written += snprintf(newPath + written, sizeof(newPath) - written, "%s", prependVal);
	}

	if (currentPath[0] != '\0')
	{
		if (written > 0)
		{
			written += snprintf(newPath + written, sizeof(newPath) - written, ";%s", currentPath);
		}
		else
		{
			written += snprintf(newPath + written, sizeof(newPath) - written, "%s", currentPath);
		}
	}

	if (appendVal[0] != '\0')
	{
		if (written > 0)
		{
			written += snprintf(newPath + written, sizeof(newPath) - written, ";%s", appendVal);
		}
		else
		{
			written += snprintf(newPath + written, sizeof(newPath) - written, "%s", appendVal);
		}
	}

	if (replaceVal[0] != '\0' || prependVal[0] != '\0' || appendVal[0] != '\0')
	{
		SetEnvironmentVariableA("PATH", newPath);
	}
}

/* Reads the environment section of the ini file and sets environment variables. */
void applyEnvSettings(const char* iniPath, const char* sectionName)
{
	char buffer[32768];
	DWORD len = GetPrivateProfileSectionA(sectionName, buffer, sizeof(buffer), iniPath);
	if (len == 0)
	{
		return;
	}

	/* Ensure double-null termination in case of truncation */
	buffer[sizeof(buffer) - 1] = '\0';
	buffer[sizeof(buffer) - 2] = '\0';

	/* GetPrivateProfileSectionA returns entries formatted as "KEY=VALUE\0KEY2=VALUE2\0\0" */
	const char* entry = buffer;
	while (*entry != '\0')
	{
		const char* eq = strchr(entry, '=');
		if (eq != NULL && eq != entry)
		{
			size_t keyLen = (size_t) (eq - entry);
			char key[4096];
			if (keyLen < sizeof(key))
			{
				memcpy(key, entry, keyLen);
				key[keyLen] = '\0';
				const char* val = eq + 1;
				SetEnvironmentVariableA(key, val);
			}
		}
		entry += strlen(entry) + 1;
	}
}

/* Executes a command line with the specified working directory, inheriting console handles
 * and enforcing timeout if configured. Returns exit code on success, or -1 on failure to launch. */
int executeProcess(char* cmdLine, const char* workDir, int timeoutSeconds)
{
	/* Setup process startup info */
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);

	/* Pass the genuine console handles through so the child attaches to the real console. */
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));

	/* Create the child process */
	if (!CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, workDir, &si, &pi))
	{
		return -1;
	}

	/* Wait for process with timeout */
	DWORD waitResult;
	if (timeoutSeconds > 0)
	{
		/* Convert the configured timeout from seconds to milliseconds for WaitForSingleObject. */
		DWORD timeoutMs = (DWORD) timeoutSeconds * 1000;
		waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);

		if (waitResult == WAIT_TIMEOUT)
		{
			/* The process took too long; terminate it forcibly and wait for it to actually exit. */
			fprintf(stderr, "\nProcess terminated after %d seconds timeout\n", timeoutSeconds);
			TerminateProcess(pi.hProcess, 1);
			WaitForSingleObject(pi.hProcess, INFINITE);
		}
	}
	else
	{
		/* No timeout configured; wait indefinitely for the child to finish. */
		waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
	}

	/* Get exit code */
	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);

	/* Cleanup */
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	/* Return the child's exit code */
	return (int) exitCode;
}

/* Reads the debug level from [OPTIONS] (or alias [OPTION]) section. */
int resolveDebugLevel(const char* iniPath)
{
	int debugLevel = GetPrivateProfileIntA("OPTIONS", "debug-level", -1, iniPath);
	if (debugLevel < 0)
	{
		debugLevel = GetPrivateProfileIntA("OPTION", "debug-level", 0, iniPath);
	}
	if (debugLevel < 0)
	{
		debugLevel = 0;
	}
	return debugLevel;
}

/* Reads timeout configuration from [MAX-TIME] section.
 * Default timeout is read from key '@' (defaults to 0).
 * If symlinkName is non-empty, checks [MAX-TIME] for symlinkName override. */
int resolveTimeout(const char* iniPath, const char* symlinkName)
{
	int timeoutSeconds = GetPrivateProfileIntA("MAX-TIME", "@", 0, iniPath);
	if (timeoutSeconds < 0)
	{
		timeoutSeconds = 0;
	}

	if (symlinkName != NULL && symlinkName[0] != '\0')
	{
		char buf[64];
		if (GetPrivateProfileStringA("MAX-TIME", symlinkName, "", buf, sizeof(buf), iniPath) > 0 && buf[0] != '\0')
		{
			int symTimeout = atoi(buf);
			if (symTimeout >= 0)
			{
				timeoutSeconds = symTimeout;
			}
		}
	}

	return timeoutSeconds;
}

/* Parses and executes an .ini profile configuration. */
int runIniProfile(
	const char* iniPath, const char* exeDir, int argc, char* argv[],//
	const char* pathSection, const char* envSection, const char* runSection, int timeoutSeconds,
	int debugLevel
)
{
	if (debugLevel >= 1)
	{
		fprintf(stderr, "Active INI sections: [%s], [%s], [%s]\n", pathSection, envSection, runSection);
	}

	/* 1. Apply path settings */
	applyPathSettings(iniPath, exeDir, pathSection);

	/* 2. Apply environment settings */
	applyEnvSettings(iniPath, envSection);

	/* 3. Read RUN keys from resolved runSection */
	char targetExeRaw[MAX_PATH];
	if (GetPrivateProfileStringA(runSection, "executable", "", targetExeRaw, sizeof(targetExeRaw), iniPath) == 0 || targetExeRaw[0] == '\0')
	{
		fprintf(stderr, "Error: No executable specified in [%s] section of %s\n", runSection, iniPath);
		return 1;
	}

	char targetExeResolved[MAX_PATH];
	if (!resolvePath(exeDir, targetExeRaw, targetExeResolved, sizeof(targetExeResolved)))
	{
		strncpy(targetExeResolved, targetExeRaw, sizeof(targetExeResolved) - 1);
		targetExeResolved[sizeof(targetExeResolved) - 1] = '\0';
	}

	char workdirRaw[MAX_PATH];
	char workdirResolved[MAX_PATH];
	const char* effectiveWorkDir = NULL;
	if (GetPrivateProfileStringA(runSection, "workdir", "", workdirRaw, sizeof(workdirRaw), iniPath) > 0 && workdirRaw[0] != '\0')
	{
		if (resolvePath(exeDir, workdirRaw, workdirResolved, sizeof(workdirResolved)))
		{
			effectiveWorkDir = workdirResolved;
		}
		else
		{
			effectiveWorkDir = workdirRaw;
		}
	}

	char prependArgs[32768];
	prependArgs[0] = '\0';
	GetPrivateProfileStringA(runSection, "prepend", "", prependArgs, sizeof(prependArgs), iniPath);

	char appendArgs[32768];
	appendArgs[0] = '\0';
	GetPrivateProfileStringA(runSection, "append", "", appendArgs, sizeof(appendArgs), iniPath);

	int consoleVal = GetPrivateProfileIntA(runSection, "console", 1, iniPath);
	if (consoleVal == 0)
	{
		HWND hConsole = GetConsoleWindow();
		if (hConsole != NULL)
		{
			ShowWindow(hConsole, SW_HIDE);
		}
	}

	/* Build command line: "<targetExe>" [prependArgs] [argv[1..]] [appendArgs] */
	char cmdLine[32768];
	int written = snprintf(cmdLine, sizeof(cmdLine), "\"%s\"", targetExeResolved);

	if (prependArgs[0] != '\0' && written > 0 && written < (int) sizeof(cmdLine))
	{
		int appended = snprintf(cmdLine + written, sizeof(cmdLine) - written, " %s", prependArgs);
		if (appended > 0)
		{
			written += appended;
		}
	}

	for (int i = 1; i < argc && written > 0 && written < (int) sizeof(cmdLine); ++i)
	{
		int appended = snprintf(cmdLine + written, sizeof(cmdLine) - written, " %s", argv[i]);
		if (appended > 0)
		{
			written += appended;
		}
	}

	if (appendArgs[0] != '\0' && written > 0 && written < (int) sizeof(cmdLine))
	{
		int appended = snprintf(cmdLine + written, sizeof(cmdLine) - written, " %s", appendArgs);
		if (appended > 0)
		{
			written += appended;
		}
	}

	int ret = executeProcess(cmdLine, effectiveWorkDir, timeoutSeconds);
	if (ret == -1)
	{
		fprintf(stderr, "Failed to create process: %s\n", targetExeResolved);
		return 1;
	}
	return ret;
}

int main_entry(int argc, char* argv[])
{
	/* Get executable path as it was invoked (may be a symlink) */
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);

	/* If the executable was launched through a symlink, resolve it to the real underlying file
	 * so the companion file is looked up next to the actual executable, not the symlink. If
	 * resolution fails for any reason, fall back to the path as invoked. */
	char resolvedExePath[MAX_PATH];
	const char* effectiveExePath = exePath;
	if (resolveExecutablePath(exePath, resolvedExePath, sizeof(resolvedExePath)))
	{
		effectiveExePath = resolvedExePath;
	}

	/* Detect symlink and extract symlink base name if invoked via symlink */
	char symlinkName[MAX_PATH];
	BOOL isSymlink = getSymlinkName(exePath, resolvedExePath, symlinkName, sizeof(symlinkName));

	/* Extract the directory portion of the real executable */
	char exeDir[MAX_PATH];
	getExeDirectory(effectiveExePath, exeDir, sizeof(exeDir));

	/* Check for companion INI profile first: <effectiveExePath>.ini */
	char iniPath[MAX_PATH + 4];
	snprintf(iniPath, sizeof(iniPath), "%s.ini", effectiveExePath);
	DWORD iniAttrs = GetFileAttributesA(iniPath);
	if (iniAttrs != INVALID_FILE_ATTRIBUTES && !(iniAttrs & FILE_ATTRIBUTE_DIRECTORY))
	{
		const char* activeSymlink = isSymlink ? symlinkName : NULL;

		/* Resolve dynamic section names for PATH, ENV, RUN */
		char pathSection[64];
		char envSection[64];
		char runSection[64];
		resolveSectionName(iniPath, "PATH", activeSymlink, pathSection, sizeof(pathSection));
		resolveSectionName(iniPath, "ENV", activeSymlink, envSection, sizeof(envSection));
		resolveSectionName(iniPath, "RUN", activeSymlink, runSection, sizeof(runSection));

		/* Read debug-level and timeout configuration from INI */
		int debugLevel = resolveDebugLevel(iniPath);
		int timeoutSeconds = resolveTimeout(iniPath, activeSymlink);

		/* INI profile exists: execute target program according to INI settings (cmd file ignored) */
		return runIniProfile(iniPath, exeDir, argc, argv, pathSection, envSection, runSection, timeoutSeconds, debugLevel);
	}

	/* Fall back to companion command script: <effectiveExePath>.cmd */
	char scriptPath[MAX_PATH + 4];
	snprintf(scriptPath, sizeof(scriptPath), "%s.cmd", effectiveExePath);
	DWORD scriptAttrs = GetFileAttributesA(scriptPath);
	if (scriptAttrs != INVALID_FILE_ATTRIBUTES && !(scriptAttrs & FILE_ATTRIBUTE_DIRECTORY))
	{
		/* Build command line: cmd.exe /c "<script.cmd>" [args...] */
		/* The /c switch tells cmd.exe to run the script and then terminate. */
		char cmdLine[32768];
		int written = snprintf(cmdLine, sizeof(cmdLine), "cmd.exe /c \"%s\"", scriptPath);

		for (int i = 1; i < argc && written > 0 && written < (int) sizeof(cmdLine); ++i)
		{
			/* Append any extra arguments passed to this wrapper, forwarding them to the script. */
			int appended = snprintf(cmdLine + written, sizeof(cmdLine) - written, " %s", argv[i]);
			if (appended < 0)
			{
				break;
			}
			written += appended;
		}

		int ret = executeProcess(cmdLine, NULL, 0);
		if (ret == -1)
		{
			fprintf(stderr, "Failed to create process. Make sure cmd.exe is in PATH.\n");
			return 1;
		}
		return ret;
	}

	/* Neither .ini nor .cmd companion file was found */
	fprintf(stderr, "Error: Companion file not found: %s or %s\n", iniPath, scriptPath);
	return 1;
}

#if !defined SF_NO_MAIN
int main(int argc, char* argv[])
{
	return main_entry(argc, argv);
}
#endif
