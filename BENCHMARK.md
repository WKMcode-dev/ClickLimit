# Buscar o limite sem confundir envio com recebimento

O programa nao pode garantir previamente que qualquer taxa sera aceita pelo Windows ou processada por qualquer aplicativo. Esta versao verifica o retorno de cada SendInput, registra eventos solicitados/aceitos/rejeitados e interrompe o envio na primeira falha parcial. Nao conta eventos rejeitados como cliques aceitos. Uma falha pode ter motivos nao identificaveis pelo Windows, incluindo restricoes de privilegio.

## Teste real separado do desktop principal

Requer Windows Sandbox instalado e habilitado. Este recurso nao estava disponivel na maquina de desenvolvimento; NENHUM resultado real de desempenho foi obtido nesta entrega. O benchmark nao e iniciado automaticamente pelo aplicativo ou pelos testes.

1. Extraia o ZIP. Abra `Preparar benchmark.cmd` (ou execute `tools/Prepare-Sandbox.ps1`) para gerar `ClickLimit-Benchmark.wsb`. Esse script apenas prepara a configuracao; nao abre o Sandbox.
2. Abra o arquivo .wsb quando desejar executar o teste. O programa injeta cliques SOMENTE no Windows da maquina virtual, sobre sua propria janela. Nao use o modo real no desktop principal.
3. O relatorio fica em `sandbox-results/benchmark.csv`, ao lado dos arquivos extraidos. O Sandbox recebe leitura da pasta do programa e escrita apenas nessa pasta de resultados. Rede, clipboard, impressora e microfone ficam desabilitados.
4. O benchmark usa tres repeticoes de cada lote (1, 16, 64, 256, 1024 e 4096), com janela de medicao de 1 segundo e ate 5 segundos para drenar os eventos restantes de cada rodada. Lotes maiores nao significam necessariamente maior velocidade recebida. Escape na janela de teste pede cancelamento.

O lancador do modo real exige o usuario padrao do Windows Sandbox e um arquivo de autorizacao criado dentro da VM. Essa verificacao e uma protecao contra execucao acidental, nao uma fronteira de seguranca. O executavel sem argumentos nao injeta entrada. O launcher nao desativa antivirus nem modifica a protecao do host.

## Colunas importantes

- `accepted_events` e `rejected_events`: contagem retornada pela API, incluindo movimento e pressionar/soltar; NAO sao CPS.
- `accepted_pairs`: pares completos aceitos. Movimento nao conta como clique.
- `accepted_pairs_in_window`: pares de lotes que terminaram dentro do segundo medido. Um lote que ultrapassou o prazo nao ganha credito retroativo.
- `received_pairs_in_window`: pressionar/soltar recebidos pelo destino dentro daquele mesmo segundo; e a medida de CPS recebidos deste ensaio.
- `received_pairs`: total recebido incluindo drenagem posterior.
- `drain_seconds`: atraso adicional para receber a fila depois que o motor acabou de enviar.
- `all_accepted`, `all_received`, `valid`: uma rodada valida exige aceitaçao integral, correspondencia exata de pares recebidos, ordem valida e ausencia de cancelamento. Uma rodada invalida encerra o ensaio, sem continuar sobre uma fila desconhecida.

Receber todos os cliques DEPOIS do prazo nao prova que todos foram recebidos dentro de 1 segundo. Compare sempre a janela de tempo, o tamanho do lote e a drenagem. Essa janela de destino tem instrumentacao e leitura de relogio; sua propria carga limita a taxa observada.

## Comparacao justa

Resultados de uma VM nao sao diretamente comparaveis a numeros anunciados em hardware fisico. Para alegar superar outro programa, execute ambos na mesma VM/hardware, no mesmo destino e com o mesmo tempo. Este benchmark atualmente mede o ClickLimit, nao automatiza programas concorrentes. Registre Windows, CPU, recursos atribuídos a VM, versao do programa e configuracao. Nao declare um recorde mundial sem evidencias externas.

## Simulacao

`ClickLimitBenchmark.exe --simulate simulated.csv` usa somente um destino em memoria com latencia artificial de 50 microssegundos por lote. Verifica contagens e formato do relatorio; todos os registros tem `mode=SIMULATED`. Esses valores nao medem Windows, SendInput, hardware ou cliques reais e nao servem como recorde.

Referencia da API: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput

Testes desta entrega: 125 casos de motor/layout, incluindo todas as posicoes de falha parcial em lotes simples/duplos e falha de recuperacao do botao. A recuperacao pressionar/soltar e auditada tambem: um clique recuperado so e contabilizado quando seu evento de soltura e aceito.
