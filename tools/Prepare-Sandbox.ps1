$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$resultsPath = Join-Path $projectRoot 'sandbox-results'
New-Item -ItemType Directory -Path $resultsPath -Force | Out-Null
$sourceXml = [Security.SecurityElement]::Escape($projectRoot)
$resultsXml = [Security.SecurityElement]::Escape($resultsPath)
$configuration = @"
<Configuration>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <PrinterRedirection>Disable</PrinterRedirection>
  <AudioInput>Disable</AudioInput>
  <MappedFolders>
    <MappedFolder><HostFolder>$sourceXml</HostFolder><SandboxFolder>C:\ClickLimit</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$resultsXml</HostFolder><SandboxFolder>C:\Results</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\ClickLimit\tools\Run-In-Sandbox.ps1</Command></LogonCommand>
</Configuration>
"@
$file = Join-Path $projectRoot 'ClickLimit-Benchmark.wsb'
[IO.File]::WriteAllText($file,$configuration)
Write-Output "Configuracao preparada: $file"
Write-Output 'Abra esse arquivo quando quiser executar o teste dentro do Windows Sandbox. Este script nao inicia o Sandbox.'
