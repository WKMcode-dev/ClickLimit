param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$SignToolPath,
    [string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
$signTool = (Resolve-Path -LiteralPath $SignToolPath).Path
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Informe uma pasta de pacote.' }
if (-not (Test-Path -LiteralPath (Join-Path $root 'ClickLimit.exe') -PathType Leaf)) { throw 'ClickLimit.exe ausente.' }
$expected = ($CertificateThumbprint -replace '\s','').ToUpperInvariant()
$reparse = @(Get-ChildItem -LiteralPath $root -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
if ($reparse.Count) { throw 'O pacote nao pode conter links ou pontos de juncao.' }
$binaries = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | Where-Object { $_.Extension -in '.exe','.dll','.msi' })
$publisher = $null
foreach ($binary in $binaries) {
    & $signTool verify /pa /all /tw $binary.FullName | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Verificacao ou carimbo de tempo falhou: $($binary.Name)" }
    $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or -not $signature.TimeStamperCertificate) {
        throw "Assinatura publica valida e carimbo de tempo obrigatorios: $($binary.Name)"
    }
    $thumbprint = $signature.SignerCertificate.Thumbprint.ToUpperInvariant()
    if ($expected -and $thumbprint -ne $expected) { throw "Certificado inesperado: $($binary.Name)" }
    if (-not $publisher) { $publisher = $thumbprint }
    if ($thumbprint -ne $publisher) { throw 'Todos os binarios do pacote devem usar a mesma identidade.' }
    [pscustomobject]@{
        File = $binary.FullName.Substring($root.Length + 1)
        Publisher = $signature.SignerCertificate.Subject
        Thumbprint = $thumbprint
        TimestampIssuer = $signature.TimeStamperCertificate.Subject
        SHA256 = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash
    }
}
