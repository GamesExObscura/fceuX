param(
    [string]$Build = "$PSScriptRoot/../../build-miracle-nmake",
    [string]$Qt = 'C:/Qt/6.11.0/msvc2022_64',
    [string]$Sdl = 'C:/SDL2-devel-2.30.12-VC/SDL2-2.30.12'
)
$ErrorActionPreference = 'Stop'
$source = Get-Content "$PSScriptRoot/../src/drivers/Qt/MiracleWindow.cpp" -Raw
$start = $source.IndexOf('namespace {')
$end = $source.IndexOf('static QPointF mouseEventPosition')
if ($start -lt 0 -or $end -le $start) { throw 'Audio source boundaries changed' }
$out = New-Item "$Build/audio-check" -ItemType Directory -Force
# Extract production code, rather than maintaining a separate copy of the synth.
$source.Substring($start, $end - $start) | Set-Content "$out/miracle-audio-under-test.hpp"
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$command = 'call "{0}" >nul && cl /nologo /EHsc /std:c++17 /MD /O2 /DMSVC /DQT_NO_DEBUG /I"{1}/include" /I"{1}/include/QtCore" /I"{2}/include" /I"{3}/../src" /I"{4}" "{3}/miracle-audio-check.cpp" /Fe:"{4}/miracle-audio-check.exe" /Fo:"{4}/miracle-audio-check.obj" /link /LIBPATH:"{1}/lib" Qt6Core.lib' -f $vcvars,$Qt,$Sdl,$PSScriptRoot,$out.FullName
$command = $command.Replace('/std:c++17', '/std:c++17 /Zc:__cplusplus /permissive-')
& $env:ComSpec /d /c $command
if ($LASTEXITCODE) { throw 'Audio regression compile failed' }
$oldPath = $env:PATH
try {
    $env:PATH = "$Qt/bin;$oldPath"
    & "$out/miracle-audio-check.exe"
    if ($LASTEXITCODE) { throw 'Audio regression failed' }
} finally { $env:PATH = $oldPath }
