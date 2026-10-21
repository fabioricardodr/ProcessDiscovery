[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$PackageRoot,
	[Parameter(Mandatory = $true)]
	[ValidateSet('x86', 'x64')]
	[string]$Architecture
)

$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$bridgeRelativePath = 'ProcessDiscovery.Bridge.dll'
$bridge = Join-Path $root $bridgeRelativePath
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) {
	throw "Bridge artifact not found: $bridge"
}

$manifest = [ordered]@{
	manifest_version = 1
	artifacts = @([ordered]@{
		artifact_id = 'bridge'
		architecture = $Architecture
		relative_path = $bridgeRelativePath
		file_version = '1.0.0.0'
		hash_algorithm = 'SHA-256'
		sha256 = (Get-FileHash -LiteralPath $bridge -Algorithm SHA256).Hash.ToLowerInvariant()
	})
}

$manifestPath = Join-Path $root 'artifacts-manifest.json'
$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($manifestPath, $json, [System.Text.UTF8Encoding]::new($false))
Write-Output $manifestPath
