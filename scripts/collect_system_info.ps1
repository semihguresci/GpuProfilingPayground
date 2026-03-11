param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Args
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (Get-Command py -ErrorAction SilentlyContinue) {
  & py -3 "$scriptDir/collect_system_info.py" @Args
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
  & python "$scriptDir/collect_system_info.py" @Args
} else {
  throw "Python is required. Install Python 3 and ensure 'py' or 'python' is available in PATH."
}
