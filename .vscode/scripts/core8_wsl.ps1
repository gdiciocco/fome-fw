param(
	[ValidateSet('build', 'rebuild', 'flash', 'all', 'ini')]
	[string]$Action = 'build'
)

$ErrorActionPreference = 'Stop'
$repoDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$linuxRepoDir = (& wsl.exe -d Ubuntu -- wslpath -u $repoDir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxRepoDir) {
	throw 'Impossibile convertire il percorso del repository per WSL.'
}

& wsl.exe -d Ubuntu -u root -- bash "$linuxRepoDir/.vscode/scripts/core8_wsl.sh" $linuxRepoDir $Action
exit $LASTEXITCODE
