param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$OutputZip,
    [Parameter(Mandatory=$true)][string]$SignToolPath,
    [string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\','/')
$output = [IO.Path]::GetFullPath($OutputZip)
if ($output -notmatch '\.zip$') { throw 'A saida precisa terminar em .zip.' }
if ($output.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'O ZIP deve ficar fora da pasta do pacote.'
}
$manifest = $output + '.signatures.json'
$checksum = $output + '.sha256'
foreach ($path in @($output,$manifest,$checksum)) {
    if (Test-Path -LiteralPath $path) { throw "A saida ja existe; escolha outro nome: $path" }
}
if (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($output)) -PathType Container)) {
    throw 'A pasta de saida precisa existir.'
}
# Valida antes de criar qualquer arquivo de distribuicao.
$results = @(& (Join-Path $PSScriptRoot 'Verify-Release.ps1') -PackageDirectory $root -SignToolPath $SignToolPath -CertificateThumbprint $CertificateThumbprint)
Compress-Archive -LiteralPath $root -DestinationPath $output -CompressionLevel Optimal
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifest -Encoding UTF8
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
($hash + '  ' + [IO.Path]::GetFileName($output)) | Set-Content -LiteralPath $checksum -Encoding ASCII
Write-Host 'Pacote assinado verificado. Publique ZIP, SHA256 e manifesto juntos.'
Get-Item -LiteralPath $output,$manifest,$checksum
