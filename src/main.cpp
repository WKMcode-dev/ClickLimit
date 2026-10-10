#include <windows.h>
#include <iostream>

int main() {
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        std::cerr << "Relogio de alta resolucao indisponivel.\n";
        return 1;
    }
    std::cout << "ClickLimit - estrutura inicial\n"
              << "Frequencia do relogio: " << frequency.QuadPart << " Hz\n"
              << "Tamanho de INPUT: " << sizeof(INPUT) << " bytes\n"
              << "Um clique completo exige dois eventos.\n"
              << "Motor de cliques ainda nao implementado.\n";
}
