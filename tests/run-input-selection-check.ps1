param([string]$Build = "$PSScriptRoot/../../build-miracle-nmake")
$ErrorActionPreference = 'Stop'
$source = Get-Content "$PSScriptRoot/../src/drivers/Qt/input.cpp" -Raw
$start = $source.IndexOf('void InputUserActiveFix()')
$end = $source.IndexOf('int getInputSelection(')
$decodeStart = $source.IndexOf('if (!device.empty()')
$decodeEnd = $source.IndexOf('// update each of the devices', $decodeStart)
if ($start -lt 0 -or $end -le $start -or $decodeStart -lt 0) { throw 'Source boundaries changed' }
$decode = $source.Substring($decodeStart, $decodeEnd - $decodeStart).TrimEnd()
# Strip the enclosing UpdateInput for-loop's closing brace.
$decode = $decode.Substring(0, $decode.LastIndexOf('}'))
$out = New-Item "$Build/input-check" -ItemType Directory -Force
($source.Substring($start, $end - $start) + "`nint decodeStoredInput(unsigned i, const std::string& device) { int UsrInputType[3] = {};`n" + $decode + "`nreturn UsrInputType[i]; }") | Set-Content "$out/input-selection-under-test.hpp"
$command = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /std:c++17 /MD /O2 /DMSVC /I"{0}/../src" /I"{1}" "{0}/input-selection-check.cpp" /Fe:"{1}/input-selection-check.exe" /Fo:"{1}/input-selection-check.obj"' -f $PSScriptRoot,$out.FullName
& $env:ComSpec /d /c $command
if ($LASTEXITCODE) { throw 'Input regression compile failed' }
& "$out/input-selection-check.exe"
if ($LASTEXITCODE) { throw 'Input regression failed' }
