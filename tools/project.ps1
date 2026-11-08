param(
    [ValidateSet('setup', 'build', 'ports', 'upload', 'monitor', 'test')]
    [string]$Action = 'build',
    [string]$Port = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$taskPython = Join-Path $projectRoot '.tools\venv\Scripts\python.exe'

function Assert-ExitCode([string]$Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step failed (exit $LASTEXITCODE)." }
}

Push-Location -LiteralPath $projectRoot
try {
    if ($Action -eq 'test') {
        $compiler = Get-Command 'g++' -ErrorAction SilentlyContinue
        if (-not $compiler) { throw 'Host tests need g++ on PATH (e.g. MinGW-w64).' }
        New-Item -ItemType Directory -Path '.tools\tests' -Force | Out-Null
        & $compiler.Source '-std=c++17' '-Wall' '-Wextra' '-Werror' '-Iinclude' 'src/debug_core.cpp' 'tests/core_test.cpp' '-o' '.tools/tests/core_test.exe'
        Assert-ExitCode 'Compile core tests'
        & '.\.tools\tests\core_test.exe'
        Assert-ExitCode 'Core tests'
        foreach ($apiVersion in @(2, 3)) {
            $testBinary = ".tools/tests/firmware_test_v$apiVersion.exe"
            & $compiler.Source '-std=c++17' '-Wall' '-Wextra' '-Werror' "-DESP_ARDUINO_VERSION_MAJOR=$apiVersion" '-Iinclude' '-Itests/fakes' 'src/debug_core.cpp' 'src/main.cpp' 'tests/firmware_test.cpp' '-o' $testBinary
            Assert-ExitCode "Compile firmware API $apiVersion tests"
            & (Join-Path $projectRoot $testBinary)
            Assert-ExitCode "Firmware API $apiVersion tests"
        }
        return
    }

    if (-not (Test-Path -LiteralPath $taskPython)) {
        & python -m venv '.tools\venv'
        Assert-ExitCode 'Create Python environment'
    }
    & $taskPython -c "import importlib.util,sys; sys.exit(0 if importlib.util.find_spec('platformio') else 1)"
    if ($LASTEXITCODE -ne 0) {
        & $taskPython -m pip install 'platformio==6.1.18'
        Assert-ExitCode 'Install PlatformIO'
    }

    switch ($Action) {
        'setup' { & $taskPython -m platformio pkg install }
        'build' { & $taskPython -m platformio run }
        'ports' { & $taskPython -m platformio device list }
        'upload' {
            $pioArguments = @('-m', 'platformio', 'run', '-t', 'upload')
            if ($Port) { $pioArguments += @('--upload-port', $Port) }
            & $taskPython @pioArguments
        }
        'monitor' {
            $pioArguments = @('-m', 'platformio', 'device', 'monitor', '-e', 'esp32-debug', '-b', '115200', '--echo', '--eol', 'LF', '--dtr', '0', '--rts', '0')
            if ($Port) { $pioArguments += @('--port', $Port) }
            & $taskPython @pioArguments
        }
    }
    Assert-ExitCode $Action
}
finally {
    Pop-Location
}

