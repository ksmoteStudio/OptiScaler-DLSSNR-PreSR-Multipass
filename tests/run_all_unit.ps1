# Runs the self-contained C++ unit tests.
#
# Why this exists (2026-10-07): tests/ held 61 test files and NOT ONE WORKFLOW
# compiled or ran any of them. `test.yml` is a release pipeline that only does
# version extraction and nightly-release bookkeeping -- searching every workflow
# for `cl.exe`, `tests/` or `run_*.ps1` returns zero hits. Without a runner there
# is no red, and TDD is decoration.
#
# Usage (from a Visual Studio x64 developer shell):
#   .\tests\run_all_unit.ps1
#   .\tests\run_all_unit.ps1 -Required nr_runtime_status_unit
#
# Exit code is 0 when every REQUIRED test passed, 1 otherwise. Non-required
# tests are still compiled and run, and reported, but do not fail the build --
# most of them were never executed, so some are expected to be broken and
# fixing them is separate work.

[CmdletBinding()]
param(
    [string[]] $Required = @('nr_runtime_status_unit'),
    [string]   $OutputDirectory = (Join-Path $env:TEMP 'OptiScaler-unit-tests'),
    [int]      $TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe not on PATH -- run this from a Visual Studio x64 developer shell.'
}

# Include paths shared by the standalone tests. Individual tests that need extra
# libs (vulkan-1.lib, ole32.lib) declare those in their own run_*.ps1; those
# wrappers remain the supported path for them.
$includes = @(
    "/I$repo\OptiScaler",
    "/I$repo\external\vulkan\include",
    "/I$repo\external\nvngx_dlss_sdk",
    "/I$repo\external\spdlog\include"
)

$results = [System.Collections.Generic.List[object]]::new()

function Invoke-UnitTest([string] $path) {
    $name = [IO.Path]::GetFileNameWithoutExtension($path)
    $exe = Join-Path $OutputDirectory "$name.exe"
    $obj = Join-Path $OutputDirectory "$name.obj"
    $stdout = Join-Path $OutputDirectory "$name.out.txt"

    # NOTE: no leading 'cl.exe' in this array. Splatting (@name) is only valid as a
    # bare argument to a command -- '@compile[1..($compile.Count-1)]' is a parse
    # error, which is what the first CI run died on.
    $compileArgs = @('/nologo', '/std:c++20', '/EHsc') + $includes +
                   @("/Fo$obj", "/Fe$exe", $path)
    $compileOutput = & cl.exe @compileArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        return [pscustomobject]@{ Name = $name; Outcome = 'compile-failed'; Detail = ($compileOutput -join "`n") }
    }

    $runOutput = & $exe 2>&1
    $code = $LASTEXITCODE
    $runOutput | Out-File -LiteralPath $stdout -Encoding utf8
    if ($code -ne 0) {
        return [pscustomobject]@{ Name = $name; Outcome = 'failed'; Detail = "exit $code -- see $stdout" }
    }
    return [pscustomobject]@{ Name = $name; Outcome = 'passed'; Detail = '' }
}

Push-Location -LiteralPath $repo
try {
    $candidates = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cpp' -File |
                    Where-Object { $_.Name -notmatch '_smoke\.cpp$' } |
                    Sort-Object Name)

    Write-Output "Discovered $($candidates.Count) standalone unit test(s)."
    Write-Output "Required: $($Required -join ', ')"
    Write-Output ''

    foreach ($file in $candidates) {
        $name = [IO.Path]::GetFileNameWithoutExtension($file.Name)
        Write-Output "==> $name"
        $result = Invoke-UnitTest $file.FullName
        $results.Add($result)
        $marker = switch ($result.Outcome) {
            'passed' { 'PASS' } 'failed' { 'FAIL' } 'compile-failed' { 'BUILDFAIL' }
        }
        Write-Output "    [$marker]"
        if ($result.Detail) {
            $firstLine = ($result.Detail -split "`r?`n" | Where-Object { $_ -match '\S' } | Select-Object -First 1)
            if ($firstLine) { Write-Output "    $firstLine" }
        }
    }

    Write-Output ''
    Write-Output '================ SUMMARY ================'
    foreach ($group in ($results | Group-Object Outcome | Sort-Object Name)) {
        Write-Output ("{0,-16} {1}" -f $group.Name, $group.Count)
    }
    Write-Output ''

    $requiredResults = @($results | Where-Object { $Required -contains $_.Name })
    $missing = @($Required | Where-Object { $_ -notin $results.Name })
    if ($missing.Count -gt 0) {
        Write-Output "[FAIL] Required test(s) never ran: $($missing -join ', ')"
        return 1
    }

    $requiredFailed = @($requiredResults | Where-Object { $_.Outcome -ne 'passed' })
    if ($requiredFailed.Count -gt 0) {
        Write-Output "[FAIL] Required test(s) failed: $(($requiredFailed | ForEach-Object { $_.Name }) -join ', ')"
        return 1
    }

    Write-Output "[ok] All required test(s) passed."
    return 0
}
finally {
    Pop-Location
}