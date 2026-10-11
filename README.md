# ClickLimit v0.7.2

Laboratorio de cliques para Windows x64, em C++20/Win32. Interface industrial sci-fi em grafite e laranja, com paineis angulares e faixas diagonais. Janela redimensionavel: duas colunas em telas largas, uma coluna em janelas estreitas, rolagem vertical e escala por DPI.

## Como usar

Extraia o ZIP e abra ClickLimit.exe. O programa sempre inicia parado, mesmo quando o modo infinito foi salvo.

- Ctrl + Alt + S inicia. Se ja estiver clicando, esse atalho nao interrompe nem reinicia.
- Ctrl + Alt + X para o envio em qualquer janela e cancela as esperas de inicio/captura.
- O botao Iniciar espera tres segundos para voce posicionar o cursor.
- Fechar o programa tambem encerra o motor.

Mantenha apenas uma instancia aberta: os atalhos sao globais e usam teclas presentes em teclados 60%, sem Fn.

## Funcionalidades

### Velocidade

Escolha Velocidade (CPS), Intervalo (ms) ou Turbo. Valores decimais usam ponto. Turbo nao introduz espera entre lotes; o tamanho pode ser 1, 16, 64, 256 ou 1024 repeticoes. Lotes maiores podem atrasar a resposta ao comando de parada. Taxas e intervalos configurados sao alvos, nao garantias de desempenho.

### Mouse

Botao esquerdo, direito ou meio; clique simples ou duplo. Uma repeticao simples envia um par pressionar/soltar; uma repeticao dupla envia dois pares consecutivos. O reconhecimento de duplo clique depende do aplicativo e das configuracoes do Windows.

### Repeticao

Marque Infinito para ignorar o numero de repeticoes e continuar ate Ctrl + Alt + X. Nao ha limite automatico de tempo ou quantidade nesse modo. Falhas de entrada interrompem o envio e sao informadas; fechar o aplicativo encerra o motor.

Desmarque Infinito para usar uma rodada limitada. No modo Duplo, 100 repeticoes produzem 200 cliques individuais. O contador da telemetria informa os pares de eventos aceitos pelo Windows.

### Posicao

Desmarcada a posicao fixa, os cliques seguem a posicao atual do cursor. Para fixar, informe X/Y nas coordenadas do desktop ou clique Capturar (3 s) e posicione o cursor antes de terminar a contagem. Sao aceitas coordenadas negativas dentro do desktop virtual para monitores secundarios. A posicao fixa reposiciona o cursor durante o envio.

### Configuracoes e teste

Salvar grava ClickLimit.ini ao lado do executavel. Restaurar volta aos padroes da interface; clique Salvar para persistir esses padroes. O arquivo pessoal nao faz parte do pacote nem do repositorio.

Teste rapido envia 10 cliques do botao esquerdo para a area de teste da propria janela e confere recebimento. A area de teste tambem pode ser usada manualmente com outros modos. Os cliques sao reais, entao mantenha a janela visivel durante esse teste.

## Medicao e validacao

A telemetria distingue pares aceitos pelo Windows de cliques recebidos nesta janela. Ela nao mede o recebimento por outros aplicativos. A taxa exibida e a media de envio desde o inicio da rodada. O tempo inclui as esperas do motor e nao mede a drenagem completa da fila do destino.

Validacao do motor isolada: 24 casos aprovados com backend em memoria, sem SendInput, movimento do cursor ou atalhos globais. Inclui os tres botoes, clique simples/duplo, lote final, infinito, cancelamento, envio parcial, recuperacao de botao pressionado, agendamento e nove larguras entre 440 e 1920. Imagens de janelas largas e estreitas geradas fora da tela; controles nativos ocultos sao representados com seus valores atuais no renderizador.

Otimizacoes: buffers preparados uma vez; contagem local publicada a cada 16 lotes; leitura do relogio e consulta de teclado removidas do caminho de cada lote em Turbo. A verificacao adicional do atalho de parada roda em uma thread separada. Posicao fixa agora integra o movimento ao lote, evitando uma chamada separada por envio. Nao ha percentual de ganho ou recorde declarado: os testes desta versao nao medem a velocidade real de entrada no Windows.

O caso simulado de 100 microssegundos (0,1 ms) valida a contagem dentro do prazo, sem confundir o envio que terminou depois do prazo. Ele nao representa cliques reais nem garante execucao nesse intervalo.

## Compatibilidade

SendInput funciona em aplicativos comuns do Windows. Programas elevados podem exigir o mesmo nivel de privilegio; telas protegidas e programas que rejeitam entrada sintetica podem bloquear eventos. O Windows pode nao informar o motivo exato do bloqueio. A parada impede novos lotes; eventos ja inseridos podem continuar sendo processados.

## Compilacao

Requer Visual Studio com C++, Windows SDK e CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\ClickLimit.exe
```

## Referencias

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter

Testes isolados: ctest --test-dir build -C Release --output-on-failure. O executavel ClickLimitTests nao utiliza APIs de entrada do Windows.

## Novidades da v0.5

Reposicionamento em conjunto, descarte dos pixels antigos e repintura de todos os controles corrigem rastros durante scroll e resize. A faixa da barra de rolagem permanece estavel. O buffer de pintura e reutilizado, o temporizador para quando o aplicativo fica ocioso, e a compilacao Release utiliza otimizacao entre arquivos quando suportada. O tempo de envio comeca depois da preparacao dos buffers. Nao ha ganho real de cliques/s medido nesta versao.

Icone proprio incorporado em varias resolucoes ao executavel e metadados de versao 0.5.0. O teste rapido leva a area de teste para dentro da janela antes de clicar.

Validacao: 24 casos de motor/layout; 120 ciclos de resize/scroll na janela oculta; 3120 verificacoes de posicao dos controles; objetos GDI passaram de 18 para 23, incluindo o cache de pintura, sem crescimento por ciclo. Nenhuma janela exibida e nenhum clique enviado durante estes testes. A reproducao visual em seu monitor ainda pode revelar casos nao cobertos.

Assinatura: a entrega atual e **nao assinada**. Consulte SIGNING.md e tools/Sign-Release.ps1. O aviso de aplicativo desconhecido do SmartScreen pode continuar. Nenhuma configuracao de protecao do Windows foi alterada.

## Novidades da v0.6

Barra de rolagem da janela oculta, mantendo a rolagem pela roda do mouse. A roda sobre controles ou dropdowns rola o painel sem mudar a opcao selecionada, inclusive sobre a lista do dropdown. Cliques e teclado continuam selecionando opcoes. Deltas menores da roda sao acumulados, evitando perder movimentos em dispositivos de alta resolucao. O titulo da janela e do cabecalho e ClickLimit.

Testes isolados adicionados para os quatro dropdowns e suas listas, garantindo selecao preservada e deslocamento do painel, mais acumulacao da roda e ausencia do estilo de scrollbar na janela.

### Parada mais responsiva

Turbo responsivo (indice 2, substitui o Turbo antigo) usa lotes de no maximo 16 repeticoes e uma espera cancelavel entre eles. Prioriza a resposta ao comando de parada, com menor velocidade de envio. Turbo sem espera (indice 3) mantem o caminho de envio maximo e os lotes configuraveis; pode acumular eventos no destino. O contador mede pares aceitos, nao a drenagem da fila.

Ctrl+Alt+X sinaliza um evento para acordar esperas do motor. A verificacao adicional do teclado tambem passou a consultar a tecla com maior frequencia; nao ha prazo garantido em milissegundos. O botao Iniciar vira Parar durante operacao ou contagem. A parada cancela tambem a rodada de teste, sem esperar seu timeout. O Windows nao oferece nesta aplicacao um mecanismo para retirar cliques ja aceitos da fila do destino.

26 casos do motor/layout passaram, incluindo cancelamento durante a pausa responsiva sem envio posterior e lote final de clique duplo. Os testes sao simulados; nao medem a latencia de parada no desktop real.

## Novidades da v0.7

Auditoria de todos os eventos solicitados, aceitos e rejeitados em cada lote. O motor interrompe o envio parcial e informa a quantidade de eventos nao aceitos. O contador de cliques continua representando apenas pares completos retornados pela API.

Inclui ClickLimitBenchmark.exe e o guia BENCHMARK.md. O ensaio real, preparado para Windows Sandbox, mede 1 segundo por rodada, compara lotes ate 4096 e verifica os cliques recebidos e a drenagem. A simulacao usa apenas memoria e nao representa desempenho do Windows. Nenhum recorde ou garantia universal de aceitacao e declarado.

## Correcao da v0.7.2

O modo CPS passa a agrupar taxas de 1000 CPS ou mais em lotes correspondentes a aproximadamente 10 ms, ate 1024 repeticoes por chamada. A taxa e uma media: os cliques de um lote podem chegar em rajadas; o modo Intervalo continua enviando um ciclo por vez. O limite finito e respeitado exatamente, inclusive no lote final.

A espera usa temporizador de alta resolucao e evento de cancelamento, com espera ativa curta no fim. Sistemas sem esse temporizador usam uma alternativa que pode consumir mais CPU. Ao terminar uma rodada programada, o rodape informa tempo medio por chamada de envio, tempo total de espera e quantidade de chamadas.

Validacao: 127 casos de motor/layout, teste real do temporizador sem SendInput (20 esperas de 1 ms totalizaram aproximadamente 20,99 ms nesta execucao), cancelamento da espera e ensaios ocultos/simulados. Um modelo de 10 ms por chamada reproduz o teto de 100 CPS com lotes unitarios e valida a contagem/periodo com agrupamento. Isso nao comprova a causa da medicao do usuario ou ganho real de CPS; o novo diagnostico permite verificar a causa no proximo ensaio.

Referencia: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw

## Conferencia de recepcao (v0.7.2)

O fim do envio agora notifica a interface explicitamente. Atualizacoes dos cliques recebidos sao agrupadas, inclusive depois do fim do motor, evitando um contador visual congelado. A rodada aguarda pelo menos 500 ms; se esta janela recebeu parte dos cliques, aguarda ate 5 segundos para conferir o restante. Configuracoes ficam bloqueadas durante essa conferencia. Cliques aceitos e recebidos iguais geram RODADA CONFERIDA; diferenca persistente gera DIFERENCA NA RECEPCAO. Zero recebidos nao e prova de perda quando o destino e outro aplicativo. O tempo e CPS continuam referentes ao envio, nao ao processamento final do receptor.

Essa correcao melhora a medicao e nao promete elevar o limite real de envio. Para investigar desempenho, compare rodadas finitas em Turbo sem espera com lotes 1, 16 e 64, sobre a area de teste; confirme os totais apos a conferencia. Ctrl+Alt+X interrompe novos envios, mas nao retira eventos ja inseridos na fila.
