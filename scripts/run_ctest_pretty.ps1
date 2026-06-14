#===----------------------------------------------------------------------===#
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/run_ctest_pretty.ps1
# Purpose: Normalize CTest's live Windows console stream line-by-line.
#          Some CTest builds emit LF-only status output on Windows; relaying
#          through Console.WriteLine gives cmd, PowerShell, and Windows Terminal
#          the platform newline they expect while preserving CTest's exit code.
#
#===----------------------------------------------------------------------===#

$ErrorActionPreference = "Continue"

$exitCode = 0
try {
    & ctest @args 2>&1 | ForEach-Object {
        [Console]::Out.WriteLine($_.ToString())
    }

    if ($null -ne $LASTEXITCODE) {
        $exitCode = [int]$LASTEXITCODE
    }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $exitCode = 1
}

exit $exitCode
