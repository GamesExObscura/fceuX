param([string]$Build = "$PSScriptRoot/../../build-miracle-nmake", [string]$Qt = 'C:/Qt/6.11.0/msvc2022_64', [switch]$Native)
$ErrorActionPreference = 'Stop'
$out = New-Item "$Build/menu-check" -ItemType Directory -Force
$command = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /std:c++17 /Zc:__cplusplus /permissive- /MD /O2 /DQT_NO_DEBUG /I"{0}/../src" /I"{2}/include" /I"{2}/include/QtCore" /I"{2}/include/QtGui" /I"{2}/include/QtWidgets" /I"{2}/include/QtOpenGLWidgets" "{0}/fullscreen-menu-check.cpp" /Fe:"{1}/fullscreen-menu-check.exe" /Fo:"{1}/fullscreen-menu-check.obj" /link /LIBPATH:"{2}/lib" Qt6Core.lib Qt6Gui.lib Qt6Widgets.lib Qt6OpenGLWidgets.lib user32.lib' -f $PSScriptRoot,$out.FullName,$Qt
& $env:ComSpec /d /c $command
if ($LASTEXITCODE) { throw 'Menu regression compile failed' }
$oldPath = $env:PATH
try {
    $env:PATH = "$Qt/bin;$oldPath"
    if ($Native) { & "$out/fullscreen-menu-check.exe" --native }
    else { & "$out/fullscreen-menu-check.exe" }
    if ($LASTEXITCODE) { throw 'Menu regression failed' }
} finally { $env:PATH = $oldPath }
