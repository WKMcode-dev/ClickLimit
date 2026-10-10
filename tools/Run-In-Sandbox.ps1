$ErrorActionPreference = 'Stop'
# This path is invoked by the supplied .wsb only, never by the host test suite.
if ($env:USERNAME -ne 'WDAGUtilityAccount') { throw 'Execute somente dentro do Windows Sandbox.' }
New-Item -ItemType Directory -Path 'C:\ClickLimit-Isolated' -Force | Out-Null
[IO.File]::WriteAllText('C:\ClickLimit-Isolated\allow-live.txt','ClickLimit-Sandbox-Benchmark-v1')
& 'C:\ClickLimit\ClickLimitBenchmark.exe' --sandbox-live 'C:\Results\benchmark.csv'
Write-Output "Resultado do benchmark: $LASTEXITCODE. Consulte sandbox-results\benchmark.csv na pasta extraida."
