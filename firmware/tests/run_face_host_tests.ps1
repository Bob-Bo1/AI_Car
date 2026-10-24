param([string]$Compiler='D:\CodexTools\zig\zig-x86_64-windows-0.15.2\zig.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Push-Location $taskRoot
try {
    $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $taskRoot 'tmp\zig-global-cache'
    $env:ZIG_LOCAL_CACHE_DIR=Join-Path $taskRoot 'tmp\zig-local-cache'
    $env:TEMP='D:\Espressif\temp';$env:TMP=$env:TEMP
    & $Compiler cc -std=c11 -Wall -Wextra -Werror -O1 -UNDEBUG -I firmware/main firmware/main/face_engine.c firmware/tests/face_engine_test.c -o tmp/face_engine_test.exe
    if($LASTEXITCODE -ne 0){throw 'Face engine test compilation failed'}
    & tmp/face_engine_test.exe
    if($LASTEXITCODE -ne 0){throw 'Face engine assertions failed'}
    & $Compiler cc -std=c11 -Wall -Wextra -Werror -O1 -UNDEBUG -I firmware/tests/stubs -I firmware/main firmware/main/face_engine.c firmware/main/face_display.c firmware/tests/face_display_test.c -o tmp/face_display_test.exe
    if($LASTEXITCODE -ne 0){throw 'Display adapter test compilation failed'}
    & tmp/face_display_test.exe
    if($LASTEXITCODE -ne 0){throw 'Display success assertions failed'}
    & tmp/face_display_test.exe error
    if($LASTEXITCODE -ne 0){throw 'Display failure isolation assertions failed'}
} finally {Pop-Location}
