@echo off

set VS_DIR=E:/msvc
set MSVC_VER=14.38.33130
set WINSDK_DIR=E:/msvc/Windows Kits
set WINSDK_VER=10.0.22621.0

set CLLibs=/LIBPATH:"%WINSDK_DIR%/10/Lib/%WINSDK_VER%/um/x64" /LIBPATH:"%WINSDK_DIR%/10/Lib/%WINSDK_VER%/ucrt/x64" /LIBPATH:"%VS_DIR%/VC/Tools/MSVC/%MSVC_VER%/lib/x64"
set CLIncludes=/I "%WINSDK_DIR%/10/Include/%WINSDK_VER%/um" /I "%WINSDK_DIR%/10/Include/%WINSDK_VER%/shared" /I "%WINSDK_DIR%/10/Include/%WINSDK_VER%/ucrt" /I "%WINSDK_DIR%/10/Include/%WINSDK_VER%/winrt" /I "%WINSDK_DIR%/10/Include/%WINSDK_VER%/cppwinrt" /I "%VS_DIR%/VC/Tools/MSVC/%MSVC_VER%/include"

set LDFLAGS=kernel32.lib user32.lib gdi32.lib d3d11.lib dxgi.lib dcomp.lib winmm.lib d3dcompiler.lib /INCREMENTAL:NO /NODEFAULTLIB /DYNAMICBASE:NO /STACK:0x10000,0x10000 /SUBSYSTEM:WINDOWS,5.02

set NAME=overlay
set CFLAGS=/Fe:"%NAME%.exe" /Fo:"%NAME%.obj" /nologo /fp:fast /fp:except- /EHa- /GR- /GS- /Gs999999999 /GF /Od /Zi

cl %CFLAGS% "%CD%\win32_main.cpp" %CLIncludes% /link %CLLibs% %LDFLAGS%
if %ERRORLEVEL% == 0 (
	echo SUCCESS
)
