@echo off
rem SETUP's archive libraries on Windows (docs/deps.md, docs/setup-sources.md):
rem zlib 1.3.1, liblzma (xz 5.8.1) and libarchive 3.7.9 from the same pinned
rem sources as tools/deps/build_deps.sh, each checked by its sha256, built
rem static with clang-cl, CMake and Ninja into deps\archive (include, lib),
rem where CMakeLists.txt finds them. No prebuilt binaries. Usage:
rem   tools\windows\build_deps_archive.cmd
setlocal
cd /d "%~dp0\..\.."
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
if not defined VS (echo No Visual Studio with the C++ tools found. & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set DL=%CD%\build-deps\downloads
set WORK=%CD%\build-deps\win\work
set PREFIX=%CD%\deps\archive
rem the same with forward slashes: CMake reads a backslash in a path as an escape
set PREFIXF=%PREFIX:\=/%
if not exist "%DL%" mkdir "%DL%"

call :fetch zlib-1.3.1.tar.gz https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23 || exit /b 1
call :fetch xz-5.8.1.tar.xz https://github.com/tukaani-project/xz/releases/download/v5.8.1/xz-5.8.1.tar.xz 0b54f79df85912504de0b14aec7971e3f964491af1812d83447005807513cd9e || exit /b 1
call :fetch libarchive-3.7.9.tar.xz https://www.libarchive.org/downloads/libarchive-3.7.9.tar.xz ed8b5732e4cd6e30fae909fb945cad8ff9cb7be5c6cdaa3944ec96e4a200c04c || exit /b 1
echo sources: all three match their pins

if exist "%WORK%" rmdir /s /q "%WORK%"
if exist "%PREFIX%" rmdir /s /q "%PREFIX%"
mkdir "%WORK%" "%PREFIX%"
rem Windows 10's tar (bsdtar) unpacks .tar.gz and .tar.xz.
for %%f in (zlib-1.3.1.tar.gz xz-5.8.1.tar.xz libarchive-3.7.9.tar.xz) do tar -xf "%DL%\%%f" -C "%WORK%" || exit /b 1

set COMMON=-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl "-DCMAKE_INSTALL_PREFIX=%PREFIXF%" "-DCMAKE_PREFIX_PATH=%PREFIXF%" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_POLICY_VERSION_MINIMUM=3.5

set OPTS=-DZLIB_BUILD_EXAMPLES=OFF
call :build "%WORK%\zlib-1.3.1" || exit /b 1
rem the static zlib only: the import library and DLL would win over it
del /q "%PREFIX%\lib\zlib.lib" "%PREFIX%\bin\zlib*.dll" 2>nul
echo built zlib

set OPTS=-DBUILD_SHARED_LIBS=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_DOC=OFF -DXZ_NLS=OFF
call :build "%WORK%\xz-5.8.1" || exit /b 1
echo built liblzma

set OPTS=-DBUILD_SHARED_LIBS=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=ON -DENABLE_ZLIB=ON -DENABLE_LZMA=ON -DENABLE_BZip2=OFF -DENABLE_ZSTD=OFF -DENABLE_LZ4=OFF -DENABLE_LIBB2=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_ICONV=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_LZO=OFF "-DZLIB_LIBRARY=%PREFIXF%/lib/zlibstatic.lib" "-DZLIB_INCLUDE_DIR=%PREFIXF%/include"
call :build "%WORK%\libarchive-3.7.9" || exit /b 1
echo built libarchive
rem Their licences, for the package (linked into mw3.exe, so no DLL carries them).
for %%l in ("libarchive-3.7.9 COPYING" "zlib-1.3.1 LICENSE" "xz-5.8.1 COPYING COPYING.0BSD") do call :licences %%~l || exit /b 1
echo prefix: %PREFIX%
exit /b 0

:fetch
if not exist "%DL%\%1" curl -sSfL -o "%DL%\%1" %2 || exit /b 1
for /f "usebackq delims=" %%h in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%DL%\%1').Hash.ToLower()"`) do set GOT=%%h
if not "%GOT%"=="%3" (echo %1: sha256 %GOT%, pinned %3: refused & exit /b 1)
exit /b 0

:licences
rem %1 a source folder, then its licence files
set LIC=%PREFIX%\share\licenses\%1
mkdir "%LIC%" 2>nul
set SRCDIR=%WORK%\%1
:licences_next
shift
if "%~1"=="" exit /b 0
copy /y "%SRCDIR%\%~1" "%LIC%\" >nul || exit /b 1
goto licences_next

:build
rem one library: %1 its source folder, OPTS its options
cmake -S "%~1" -B "%~1\_build" %COMMON% %OPTS% >nul || exit /b 1
cmake --build "%~1\_build" >nul || exit /b 1
cmake --install "%~1\_build" >nul || exit /b 1
exit /b 0
