# ClickLimit / Velocity Lab v0.3.0

Laboratorio de cliques para Windows x64, em C++20/Win32. Interface sci-fi com tema escuro, detalhes em ciano e telemetria. O visual e uma primeira proposta, pronta para adaptar a uma referencia futura.

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

Validacao no desktop desta maquina:
- Teste rapido: 10 pares aceitos e 10 recebidos.
- Infinito com limite finito configurado como 1: ultrapassou o limite e terminou pelo comando de parada com 1226 pares aceitos e 1226 recebidos.
- Configuracoes salvas e recarregadas apos reabrir, incluindo Infinito. O aplicativo permaneceu parado ao abrir.
- Interface renderizada e inspecionada. Compilacao Release x64 concluida com runtime C++ estatico.

Nenhum recorde de velocidade e declarado. O proximo passo de desempenho e um benchmark com repeticoes, comparacao de lotes e medicao de drenagem.

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
