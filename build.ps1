param([string]$Zig = 'zig')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $env:ZIG_LOCAL_CACHE_DIR=Join-Path $PSScriptRoot '.build\local'
    $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $PSScriptRoot '.build\global'
    $mh=Join-Path $PSScriptRoot 'third_party\minhook'
    & $Zig cc -target x86_64-windows-gnu -std=c11 -O2 -shared native.c "$mh\src\buffer.c" "$mh\src\hook.c" "$mh\src\trampoline.c" "$mh\src\hde\hde64.c" -I "$mh\include" -o PVZQoL2.dll -lkernel32 -luser32
    if($LASTEXITCODE -ne 0){throw 'Native compilation failed'}
    & "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /codepage:65001 /target:winexe /platform:x64 /optimize+ /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /out:PVZQoL2.exe Launcher.cs
    if($LASTEXITCODE -ne 0){throw 'Launcher compilation failed'}
} finally {Pop-Location}
