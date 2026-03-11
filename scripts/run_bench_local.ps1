param(
  [string]$ResultDir = "",
  [string]$BuildDir = "",
  [string]$Config = "Release",
  [string]$BinaryPath = "",
  [switch]$NoBuild,
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$runArgs = @("$scriptDir/run_bench.py", "--mode", "local")
if (-not [string]::IsNullOrWhiteSpace($ResultDir)) { $runArgs += $ResultDir }
if (-not [string]::IsNullOrWhiteSpace($BuildDir)) { $runArgs += @("--build-dir", $BuildDir) }
if (-not [string]::IsNullOrWhiteSpace($Config)) { $runArgs += @("--config", $Config) }
if (-not [string]::IsNullOrWhiteSpace($BinaryPath)) { $runArgs += @("--binary-path", $BinaryPath) }
if ($NoBuild) { $runArgs += "--no-build" }
if ($ExtraArgs) { $runArgs += $ExtraArgs }

if (Get-Command py -ErrorAction SilentlyContinue) {
  & py -3 @runArgs
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
  & python @runArgs
} else {
  throw "Python is required. Install Python 3 and ensure 'py' or 'python' is available in PATH."
}
