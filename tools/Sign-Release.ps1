param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$SignToolPath,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $Executable).Path
$signTool = (Resolve-Path -LiteralPath $SignToolPath).Path
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if (-not $certificate.HasPrivateKey) { throw 'O certificado precisa ter uma chave privada acessivel.' }
if ($certificate.NotAfter -le (Get-Date)) { throw 'Certificado expirado.' }
if ($certificate.Subject -eq $certificate.Issuer) { throw 'Certificado autoassinado nao serve para esta distribuicao publica.' }
if (-not ($certificate.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3')) { throw 'O certificado precisa permitir assinatura de codigo.' }
& $signTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $binary
if ($LASTEXITCODE -ne 0) { throw 'A assinatura falhou; nao publique como assinado.' }
& $signTool verify /pa /all /v $binary
if ($LASTEXITCODE -ne 0) { throw 'A verificacao Authenticode falhou.' }
$signature = Get-AuthenticodeSignature -LiteralPath $binary
if ($signature.Status -ne 'Valid') { throw "Assinatura nao valida: $($signature.Status)" }
Get-FileHash -LiteralPath $binary -Algorithm SHA256
