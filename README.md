# ClickLimit

Laboratório de velocidade de cliques para Windows x64.

## Tecnologia inicial

C++20 nativo, API Win32, compilação Release e CMake. É uma escolha inicial para controlar alocações e acessar diretamente a entrada do Windows; a superioridade será avaliada por medições, não presumida.

- SendInput: envio de eventos de mouse em lotes pré-alocados.
- QueryPerformanceCounter: medição de intervalos com alta resolução.
- Um clique completo = pressionar + soltar (dois eventos).

## Estado atual

Estrutura inicial criada. O executável inicial verifica o relógio e informa o tamanho dos eventos; ainda não gera cliques.

## Plano de implementação

1. Criar janela receptora e tecla de emergência.
2. Implementar motor com lotes configuráveis e verificar retornos parciais/erros.
3. Separar eventos aceitos pelo Windows de cliques completos recebidos pela janela.
4. Medir preparação, envio e drenagem da fila separadamente.
5. Comparar lotes com várias repetições; registrar ambiente, mediana e dispersão.
6. Escalar até 1.000.000 de cliques depois dos testes pequenos.

Não usar incremento de contador como prova de geração de cliques. Não interpretar tempo arredondado para zero como duração zero. A taxa de inserção não prova que o receptor processou todos os eventos.

## Compilação

Requer CMake, compilador C++ e Windows SDK. Em um terminal de desenvolvimento do Visual Studio:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\ClickLimit.exe
```

## Referências

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
