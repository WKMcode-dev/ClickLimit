# ClickLimit v0.2.0

Auto-click funcional para Windows x64, escrito em C++20/Win32.

## Como usar

Abra ClickLimit.exe. Ele inicia parado.

1. Defina a velocidade em cliques por segundo: 100 e o padrao; 0 ativa turbo com lotes de 64 cliques.
2. Defina o limite de cliques da rodada: 1000 e o padrao; ao atingir o limite o envio para.
3. Posicione o cursor no alvo e pressione Ctrl + Alt + S para iniciar (ou alternar para parado).
4. Ctrl + Alt + X interrompe globalmente. O botao Parar tambem interrompe.

O botao Iniciar espera tres segundos para voce posicionar o cursor. Ctrl + Alt + X tambem cancela essa espera. Os atalhos usam teclas presentes em teclados 60%, sem Fn.

Sao pares de pressionar/soltar do botao esquerdo na posicao atual do mouse. O cursor permanece livre. Mantenha apenas uma instancia aberta para evitar conflito nos atalhos.

## Teste e medicao

O espaco vazio abaixo de Area de teste recebe e conta os cliques do proprio motor. Pares aceitos pelo Windows e cliques recebidos sao contadores distintos. Fora dessa area, o programa nao mede o que o aplicativo de destino recebeu.

Tempo de envio mede o intervalo do motor, incluindo o controle de velocidade; nao mede a drenagem de toda a fila do destino. Uma configuracao de velocidade e um alvo, nao garantia de taxa exata.

Validacao no desktop desta maquina:
- Rodada pequena: 10 pares aceitos e 10 cliques recebidos.
- Turbo: 1000 pares aceitos e 1000 cliques recebidos; cerca de 6149,49 ms em uma rodada. Nao e resultado de recorde ou benchmark isolado.
- Parada por Ctrl + Alt + X: rodada de 1000 interrompida em 915 pares, com 915 recebidos, na compilacao final.
- Compilacao Release x64 concluida. Runtime C++ vinculado estaticamente.

## Compatibilidade

Funciona por SendInput em aplicativos comuns do Windows. Nao ha garantia de funcionar em todo lugar: programas elevados podem exigir que ClickLimit seja executado no mesmo nivel de privilegio; telas protegidas e aplicativos que rejeitam entrada sintetica podem bloquear os eventos. Retornos parciais interrompem a rodada e exibem erro (o Windows pode nao informar o motivo).

Cliques reais podem acionar elementos no alvo: para medir, use primeiro a area de teste. A parada interrompe novos lotes; eventos ja inseridos podem continuar sendo processados. Nao altera configuracoes de seguranca do Windows.

## Compilacao

Requer Visual Studio com C++, Windows SDK e CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\ClickLimit.exe
```

## Proximos passos

Benchmark reproduzivel com repeticoes, tamanhos de lote configuraveis, medicao de drenagem e exportacao dos resultados. A meta e escalar ate um milhao de cliques medidos; nenhum recorde e declarado nesta versao.

## Referencias

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
