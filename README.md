# Windows Launcher (cmd-pass)

High-Performance, Zero-Dependency Native Windows Launcher & Environment Wrapper.

> **Seamless process wrapping, dynamic environment injection, and execution timeout control — packaged into a single, lightning-fast native binary.**

---

## 🚀 Overview

**`cmd-pass`** is an ultra-lightweight, zero-dependency C native launcher for Windows designed for developers, maintainers, and DevOps engineers. It acts as a transparent execution wrapper around your binaries or scripts, giving you granular control over environment variables, system `PATH` modifications, working directories, command-line argument injection, and execution timeouts—all without requiring complex runtime dependencies.

Whether you need to ship portable software packages, enforce execution limits in CI/CD runners, or map multiple environment profiles via symlinks, `cmd-pass` delivers rock-solid performance with virtually zero footprint.

---

## 🔥 Key Features & Value Proposition

* **⚡ Ultra-Minimal Footprint (Zero C++ Runtime)**
  Built entirely in standard C (C17) using native Win32 APIs (`CreateProcessA`, `GetPrivateProfileStringA`). No C++ runtime overhead, no `iostream`, no exceptions, and no external runtime DLL requirements.
* **🎯 True Native Console I/O Passthrough**
  Connects child processes directly to the parent's real standard console handles (`STD_INPUT_HANDLE`, `STD_OUTPUT_HANDLE`, `STD_ERROR_HANDLE`). Maintains full interactive console capability, including terminal colors, ANSI escapes, native input echo, and `cls`.
* **⚙️ Dynamic INI Configuration & Environment Injection**
  Inject system environment variables (`[ENV@]`) and dynamically prepending, appending, or replacing entries in the system `PATH` (`[PATH@]`) on the fly.
* **⏱️ Built-in Timeout Management**
  Set execution time limits (`[MAX-TIME]`) in seconds to prevent runaway processes or hung scripts in automated build pipelines. Automatically terminates process trees upon timeout.
* **🔗 Symlink-Aware Profile Mapping**
  Resolves execution paths behind Windows symlinks and reparse points (`GetFinalPathNameByHandleA`). Map different execution profiles, environment variables, or target binaries depending on the symlink name used to invoke the launcher.
* **📦 Fallback Script Execution**
  Automatically looks for a companion `.ini` file matching the binary name. If unavailable, seamlessly falls back to executing a matching `.cmd` script via `cmd.exe /c`.
* **🛠️ Cross-Platform Build System**
  Full CMake configuration (`CMakePresets.json`) supporting GCC/MinGW, MSVC (2022/2026), and cross-compilation from GNU/Linux via GW toolchains.

---

## 🏗️ Architecture & How It Works

`cmd-pass` executes a strict resolution workflow upon launch:

1. **Symlink & Path Resolution:** Determines the real physical path of the executable using `GetFinalPathNameByHandleA`, resolving symlinks while preserving the original invocation name for context mapping.
2. **Companion Configuration Lookup:**
   * **INI Profile Mode (`<exe_name>.ini`):** Parses profile sections using Win32 Private Profile APIs.
   * **Command Script Fallback (`<exe_name>.cmd`):** If no INI exists, forwards arguments and executes the script.
3. **Profile Mapping & Environment Setup:**
   * Evaluates symlink mapping rules (`[MAP@PATH]`, `[MAP@ENV]`, `[MAP@RUN]`).
   * Modifies the environment (`PATH` prepending/appending/replacement, key-value variable injection).
4. **Process Spawning & Control:**
   * Prepends and appends custom flags around incoming `argv` parameters.
   * Sets the specified working directory.
   * Configures console visibility.
   * Spawns child process, enforces standard I/O passthrough, and monitors maximum execution time.

---

## ⚙️ Configuration Format (`cmd-pass.ini`)

A standard companion configuration file (`app.ini` alongside `app.exe`) supports full profile customization:

```ini
; General options
[OPTIONS]
debug-level=1; 1 prints active INI sections to stderr

; Maximum execution timeout in seconds
[MAX-TIME]
@=0          ; Default: Unlimited
my-symlink=30; Timeout 30 seconds when invoked via 'my-symlink.exe'

; Dynamic section mapping based on invocation name
[MAP@PATH]
my-symlink=app2

[MAP@ENV]
my-symlink=app2

[MAP@RUN]
my-symlink=app2

; Default PATH modification section
[PATH@]
prepend=C:\MyApp1;Tools\Bin
append=C:\MyApp2\Tools
; replace=C:\CustomPathOnly

; Environment variable injection section
[ENV@]
MY_ENV_VAR1=Value1
MY_ENV_VAR2=Value2

; Main target program configuration section
[RUN@]
workdir=.
prepend=--silent
append=--quiet
console=1; Set to 0 to hide console window
executable=..\bin\my-app.exe

; Alternate mapped profile sections
[PATH@app2]
prepend=C:\MyApp2

[ENV@app2]
MY_ENV_VAR=App2Value

[RUN@app2]
executable=..\bin\my-app2.exe
```

---

## 🛠️ Building & DevOps Integration

`cmd-pass` uses modern **CMake (>= 3.29)** and multi-preset configurations for streamlined local development and CI/CD automated packaging.

### Prerequisites
* **Windows Build:** MSVC (Visual Studio 2022/2026) or MinGW-w64.
* **Linux Cross-Compilation:** GCC with GW/MinGW cross-compiler toolchain.
* **Build System:** CMake & Ninja.


### ⚙️ Configuration Parameters

When adding new targets in the top-level `CMakeLists.txt`, the following arguments can be tailored per
`ExternalProject_Add` invocation:

| Parameter              | Description                                                                |
|:-----------------------|:---------------------------------------------------------------------------|
| `LAUNCHER_TARGET_NAME` | Sets the output binary name and `ProductName` metadata.                    |
| `APP_ICON_PATH`        | Path to the `.ico` file embedded into the executable.                      |
| `APP_VERSION_STR`      | Opotional version string (e.g., `1.0.0`) attached to the binary resources. |
| `CMAKE_TOOLCHAIN_FILE` | Forwards the cross-compiler/toolchain file to sub-builds.                  |


### Quick Build Instructions

#### Using CMake Presets (Recommended)

```bash
# Debug build using MSVC on Windows
cmake --preset msvc-debug
cmake --build --preset msvc-debug

# Release build using MinGW on Windows
cmake --preset mingw-release
cmake --build --preset mingw-release

# Cross-compile Release binary from Linux (GW toolchain)
cmake --preset gw-release
cmake --build --preset gw-release
```

#### Manual Compilation Command Line

For minimal environments without CMake:

```bash
# MSVC (Native Windows)
cl /std:c17 /O1 /Gy /Gw src/main.c /link /OPT:REF /OUT:cmd-pass.exe

# MinGW (Cross-compile / Native MinGW)
x86_64-w64-mingw32-gcc -std=c17 -Os -s -static -ffunction-sections -fdata-sections -Wl,--gc-sections -o cmd-pass.exe src/main.c
```

---

## 📦 Packaging & CI/CD Pipelines

`cmd-pass` includes out-of-the-box CPack preset integration supporting multiple distribution formats:

* **ZIP / TGZ Packages** for portable binary distribution.
* **NSIS Installers (`NSIS64`)** for automated Windows environment setup.

Execute a full workflow test, build, and package pass:
```bash
cmake --workflow --preset msvc-release
```

---

## 📄 License

This project is open-source. Refer to the repository root for specific licensing details.
