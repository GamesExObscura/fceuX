@echo off
setlocal

set "OUTPUT_DIR=%~1"
set "OUTPUT_FILE=%OUTPUT_DIR%\fceux_git_info.cpp"
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_DIR=%%~fI"

set "GIT_URL=unknown"
set "GIT_REV=unknown"

for /f "usebackq delims=" %%I in (`git -C "%REPO_DIR%" config --get remote.origin.url 2^>nul`) do set "GIT_URL=%%I"
for /f "usebackq delims=" %%I in (`git -C "%REPO_DIR%" rev-parse HEAD 2^>nul`) do set "GIT_REV=%%I"

echo %GIT_URL%
echo %GIT_REV%

> "%OUTPUT_FILE%" echo // fceux git_info.cpp -- DO NOT EDIT: This file is auto-generated at build
>> "%OUTPUT_FILE%" echo #include "Qt/fceux_git_info.h"
>> "%OUTPUT_FILE%" echo #define FCEUX_GIT_URL "%GIT_URL%"
>> "%OUTPUT_FILE%" echo #define FCEUX_GIT_REV "%GIT_REV%"
>> "%OUTPUT_FILE%" echo const char *fceu_get_git_url(void){ return FCEUX_GIT_URL; }
>> "%OUTPUT_FILE%" echo const char *fceu_get_git_rev(void){ return FCEUX_GIT_REV; }
