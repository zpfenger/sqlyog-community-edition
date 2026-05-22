@echo off
setlocal enabledelayedexpansion

:: Clean problematic environment variables
set "PATH_NEW="
for %%P in ("%PATH:;=" "%") do (
    set "P=%%~P"
    set "P=!P: =!"
    if not "!P!"=="" (
        if "!PATH_NEW!"=="" (
            set "PATH_NEW=!P!"
        ) else (
            set "PATH_NEW=!PATH_NEW!;!P!"
        )
    )
)
set "PATH=%PATH_NEW%"

cd /d D:\AI\sqlyog-community\build
set MSBUILD="E:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
echo Building SQLyog Community x64 Release...
%MSBUILD% SQLyogCommunity.sln /p:Configuration=Release /p:Platform=x64 /t:Build /m:1 /nodeReuse:false /v:minimal > build_full_log.txt 2>&1
type build_full_log.txt
echo Exit code: %ERRORLEVEL%

:: ========== Post-build: copy DLLs and create sqlyog.ini ==========
echo.
echo ========================================
echo Copying runtime DLLs to output...
echo ========================================

set "OUT_DIR=..\bin\x64\Release"
set "DLL_SRC=..\lib\x64\release"

:: MySQL connector / SSL / crypto DLLs
copy /Y "%DLL_SRC%\libcrypto-3-x64.dll"    "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\libssl-3-x64.dll"      "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\libeay32.dll"           "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\ssleay32.dll"           "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\msvcr120.dll"           "%OUT_DIR%\" >nul

:: MySQL authentication plugins
copy /Y "%DLL_SRC%\caching_sha2_password.dll"  "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\client_ed25519.dll"        "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\sha256_password.dll"       "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\auth_gssapi_client.dll"    "%OUT_DIR%\" >nul

:: MySQL pipe/shmem virtual IO
copy /Y "%DLL_SRC%\pvio_npipe.dll"          "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\pvio_shmem.dll"          "%OUT_DIR%\" >nul

:: MySQL dialog / Scintilla / HTMLayout
copy /Y "%DLL_SRC%\dialog.dll"               "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\SciLexer.dll"             "%OUT_DIR%\" >nul
copy /Y "%DLL_SRC%\htmlayout.dll"             "%OUT_DIR%\" >nul

:: PCRE (from localization)
copy /Y "..\localization\Tools\libpcre-0.dll" "%OUT_DIR%\" >nul

:: Keywords database (SQL syntax highlighting)
copy /Y "..\lib\Keywords.db" "%OUT_DIR%\" >nul

:: L10n translation database (localization: zh-cn, ja, ko, en)
if not exist "..\localization\bin\L10n.db" (
    echo Compiling L10n.db from XML translation files...
    cd /d "..\localization"
    call compile.bat
    cd /d "..\build"
)
copy /Y "..\localization\bin\L10n.db" "%OUT_DIR%\" >nul

:: Create empty sqlyog.ini
echo. > "%OUT_DIR%\sqlyog.ini"

echo.
echo ========================================
echo DLLs and sqlyog.ini ready in:
echo   %OUT_DIR%
echo ========================================
dir /b "%OUT_DIR%\*.dll" | find /c /v ""
echo DLL files copied.
dir "%OUT_DIR%\sqlyog.ini"
