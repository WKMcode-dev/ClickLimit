#include <windows.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <fstream>

namespace {
constexpr ULONG_PTR marker = 0x434C494D;
HWND window{}, rateBox{}, limitBox{}, statusBox{}, startButton{};
std::atomic<bool> running{false}, cancel{false}, partial{false};
std::atomic<unsigned long long> accepted{0};
std::atomic<double> elapsed{0};
std::atomic<DWORD> failure{0};
std::thread worker;
unsigned long long received = 0;
bool selfTest = false;
int testResult = 1;
ULONGLONG pendingUntil = 0;

void stop() { cancel = true; }
bool emergencyPressed() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
           (GetAsyncKeyState(VK_MENU) & 0x8000) &&
           (GetAsyncKeyState('X') & 0x8000);
}
void join() { stop(); if (worker.joinable()) worker.join(); }

void begin(unsigned rate, unsigned long long limit) {
    if (running.load()) return;
    if (worker.joinable()) worker.join();
    cancel = false; accepted = 0; elapsed = 0; failure = 0; partial = false;
    received = 0; running = true;
    worker = std::thread([rate, limit] {
        const unsigned batch = rate == 0 ? 64 : 1;
        std::vector<INPUT> inputs(batch * 2);
        for (unsigned i = 0; i < batch * 2; ++i) {
            inputs[i].type = INPUT_MOUSE;
            inputs[i].mi.dwFlags = i % 2 ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
            inputs[i].mi.dwExtraInfo = marker;
        }
        LARGE_INTEGER frequency{}, start{}, end{};
        // Starting with a hotkey must not turn the first clicks into Ctrl/Alt-clicks.
        while (!cancel.load() && !emergencyPressed() &&
               ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000))) Sleep(1);
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
        const auto origin = std::chrono::steady_clock::now();
        unsigned long long count = 0;
        while (!cancel.load() && count < limit) {
            if (emergencyPressed()) break;
            const UINT events = static_cast<UINT>(std::min<unsigned long long>(batch, limit - count) * 2);
            SetLastError(0);
            const UINT sent = SendInput(events, inputs.data(), sizeof(INPUT));
            count += sent / 2; accepted = count;
            if (sent != events) {
                partial = true; failure = GetLastError();
                if (sent % 2) {
                    INPUT release{}; release.type = INPUT_MOUSE;
                    release.mi.dwFlags = MOUSEEVENTF_LEFTUP; release.mi.dwExtraInfo = marker;
                    SendInput(1, &release, sizeof(INPUT));
                }
                break;
            }
            if (rate && count < limit) {
                const auto next = origin + std::chrono::nanoseconds(
                    static_cast<long long>(count * 1000000000ULL / rate));
                while (!cancel.load() && !emergencyPressed()) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= next) break;
                    if (next - now > std::chrono::milliseconds(2)) Sleep(1);
                    else SwitchToThread();
                }
            }
        }
        QueryPerformanceCounter(&end);
        elapsed = static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart;
        running = false;
    });
}

void startFromControls() {
    BOOL rateOK{}, limitOK{};
    const unsigned rate = GetDlgItemInt(window, 101, &rateOK, FALSE);
    const unsigned limit = GetDlgItemInt(window, 102, &limitOK, FALSE);
    if (!rateOK || !limitOK || rate > 1000000 || limit == 0 || limit > 100000000) {
        MessageBoxW(window, L"Velocidade: 0 a 1.000.000. Limite: 1 a 100.000.000 cliques.", L"Valores invalidos", MB_OK);
        return;
    }
    begin(rate, limit);
}

HWND control(const wchar_t* kind, const wchar_t* text, DWORD style,
             int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowW(kind, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}

LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        window = hwnd;
        control(L"STATIC", L"ClickLimit v0.2.0 - Auto-click Windows", 0, 20, 16, 450, 24);
        control(L"STATIC", L"Cliques/s (0 = turbo):", 0, 20, 56, 180, 22);
        rateBox = control(L"EDIT", L"100", WS_BORDER | ES_NUMBER | WS_TABSTOP, 210, 52, 140, 26, 101);
        control(L"STATIC", L"Limite de cliques por rodada:", 0, 20, 94, 190, 22);
        limitBox = control(L"EDIT", L"1000", WS_BORDER | ES_NUMBER | WS_TABSTOP, 210, 90, 140, 26, 102);
        startButton = control(L"BUTTON", L"Iniciar (Ctrl+Alt+S)", WS_TABSTOP, 20, 132, 195, 32, 103);
        control(L"BUTTON", L"Parar (Ctrl+Alt+X)", WS_TABSTOP, 235, 132, 195, 32, 104);
        control(L"STATIC", L"No alvo: Ctrl+Alt+S inicia; Ctrl+Alt+X para em qualquer janela.\nBotao esquerdo; o cursor continua livre. Cada rodada tem um limite.", 0, 20, 178, 475, 48);
        statusBox = control(L"STATIC", L"Parado. Nenhum clique enviado.", 0, 20, 236, 470, 65);
        control(L"STATIC", L"Area de teste: no espaco abaixo, use Ctrl+Alt+S para iniciar.", 0, 20, 310, 470, 25);
        if (!RegisterHotKey(hwnd, 1, MOD_NOREPEAT | MOD_CONTROL | MOD_ALT, 'S') ||
            !RegisterHotKey(hwnd, 2, MOD_NOREPEAT | MOD_CONTROL | MOD_ALT, 'X')) {
            UnregisterHotKey(hwnd, 1); UnregisterHotKey(hwnd, 2);
            MessageBoxW(hwnd, L"Nao foi possivel registrar Ctrl+Alt+S/X. Feche outra instancia ou o programa que usa essas teclas.", L"Atalhos indisponiveis", MB_OK | MB_ICONERROR);
            return -1;
        }
        SetTimer(hwnd, 1, 100, nullptr);
        return 0;
    case WM_COMMAND:
        if (GetMessageExtraInfo() == static_cast<LPARAM>(marker)) return 0;
        if (LOWORD(wp) == 103 && !running.load()) pendingUntil = GetTickCount64() + 3000;
        if (LOWORD(wp) == 104) { pendingUntil = 0; stop(); }
        return 0;
    case WM_HOTKEY:
        if (wp == 1) { pendingUntil = 0; if (running.load()) stop(); else startFromControls(); }
        if (wp == 2) { pendingUntil = 0; stop(); }
        return 0;
    case WM_LBUTTONUP:
        if (GetMessageExtraInfo() == static_cast<LPARAM>(marker)) ++received;
        return 0;
    case WM_TIMER: {
        if (pendingUntil && GetTickCount64() >= pendingUntil) { pendingUntil = 0; startFromControls(); }
        const bool active = running.load();
        EnableWindow(rateBox, !active); EnableWindow(limitBox, !active); EnableWindow(startButton, !active);
        std::wstring text = active ? L"Enviando" : L"Parado";
        text += L" | Pares aceitos pelo Windows: " + std::to_wstring(accepted.load());
        text += L"\nRecebidos na area de teste: " + std::to_wstring(received);
        if (!active && elapsed.load() > 0)
            text += L" | Tempo de envio: " + std::to_wstring(elapsed.load() * 1000) + L" ms";
        if (partial.load()) text += L"\nEnvio incompleto/bloqueado. Erro: " + std::to_wstring(failure.load());
        SetWindowTextW(statusBox, text.c_str());
        if (pendingUntil) SetWindowTextW(statusBox, L"Inicio em 3 segundos... posicione o cursor no alvo. Ctrl+Alt+X cancela.");
        static unsigned testTicks = 0;
        const bool timeout = selfTest && ++testTicks >= 50;
        if (selfTest && ((!active && accepted.load() > 0) || timeout)) {
            static unsigned drain = 0;
            if (++drain >= 5 || timeout) {
                testResult = accepted.load() == 10 && received == 10 && !partial.load() ? 0 : 1;
                std::ofstream report("self-test.txt");
                report << "accepted=" << accepted.load() << " received=" << received
                       << " seconds=" << elapsed.load() << " result=" << testResult << '\n';
                DestroyWindow(hwnd);
            }
        }
        return 0;
    }
    case WM_CLOSE: join(); DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        join(); UnregisterHotKey(hwnd, 1); UnregisterHotKey(hwnd, 2);
        PostQuitMessage(selfTest ? testResult : 0); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int show) {
    selfTest = std::wstring(arguments).find(L"--self-test") != std::wstring::npos;
    SetProcessDPIAware();
    WNDCLASSW wc{}; wc.lpfnWndProc = procedure; wc.hInstance = instance;
    wc.lpszClassName = L"ClickLimitWindow"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"ClickLimit v0.2.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 535, 465, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    POINT oldCursor{};
    if (selfTest) {
        GetCursorPos(&oldCursor); SetForegroundWindow(hwnd);
        POINT target{260, 370}; ClientToScreen(hwnd, &target); SetCursorPos(target.x, target.y);
        begin(20, 10);
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (selfTest) SetCursorPos(oldCursor.x, oldCursor.y);
    return static_cast<int>(msg.wParam);
}
