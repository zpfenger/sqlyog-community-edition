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
