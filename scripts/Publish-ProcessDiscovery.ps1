[CmdletBinding()]
param(
	[ValidateSet('x86', 'x64')]
	[string]$Architecture = 'x64',
	[ValidateSet('Debug', 'Release')]
	[string]$Configuration = 'Release',
	[string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$platform = if ($Architecture -eq 'x86') { 'x86' } else { 'x64' }
$rid = if ($Architecture -eq 'x86') { 'win-x86' } else { 'win-x64' }
$nativeRoot = Join-Path $root "artifacts\$Architecture"
$output = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
	Join-Path $root "dist\$Architecture"
} else {
	[System.IO.Path]::GetFullPath($OutputRoot)
}

$requiredNativeFiles = @(
	'ProcessDiscovery.Bridge.dll',
	'ProcessDiscovery.dll',
	'artifacts-manifest.json'
)
foreach ($file in $requiredNativeFiles) {
	$source = Join-Path $nativeRoot $file
	if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
		throw "Artefato nativo ausente para $Architecture`: $source"
	}
}

$project = Join-Path $root 'src\ProcessDiscovery.App\ProcessDiscovery.App.csproj'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
	throw "Projeto WPF não encontrado: $project"
}

if (Test-Path -LiteralPath $output) {
	Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output -Force | Out-Null

& dotnet publish $project `
	'--configuration' $Configuration `
	'--runtime' $rid `
	'--self-contained' 'false' `
	"-p:Platform=$platform" `
	"-p:PlatformTarget=$platform" `
	'--output' $output
if ($LASTEXITCODE -ne 0) {
	throw "Falha ao publicar a aplicação $Architecture."
}

foreach ($file in $requiredNativeFiles) {
	Copy-Item -LiteralPath (Join-Path $nativeRoot $file) -Destination (Join-Path $output $file) -Force
}

$missing = $requiredNativeFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $output $_) -PathType Leaf) }
if ($missing) {
	throw "Pacote $Architecture incompleto: $($missing -join ', ')"
}

Write-Output "Pacote publicado: $output"
