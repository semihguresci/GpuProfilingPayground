param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Args
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (Get-Command py -ErrorAction SilentlyContinue) {
  & py -3 "$scriptDir/run_bench.py" --mode docker @Args
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
  & python "$scriptDir/run_bench.py" --mode docker @Args
} else {
  throw "Python is required. Install Python 3 and ensure 'py' or 'python' is available in PATH."
}
