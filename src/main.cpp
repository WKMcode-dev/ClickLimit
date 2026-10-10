#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr ULONG_PTR marker = 0x434C494D;
constexpr COLORREF background = RGB(8, 13, 24), panel = RGB(16, 25, 40);
constexpr COLORREF accent = RGB(73, 231, 222), ink = RGB(218, 232, 244), muted = RGB(139, 164, 184);
HWND window{}, status{}, metrics{};
HBRUSH backgroundBrush{}, panelBrush{}, editBrush{};
HFONT font{}, titleFont{}, smallFont{};
std::vector<HWND> settings;
std::atomic<bool> running{false}, cancel{false}, failed{false};
std::atomic<unsigned long long> accepted{0};
std::atomic<double> seconds{0};
std::atomic<DWORD> errorCode{0};
std::thread worker;
unsigned long long received = 0;
ULONGLONG pending = 0, captureAt = 0;
std::wstring configPath;
bool testActive = false;
ULONGLONG testDeadline = 0;

struct Options {
    bool infinite = false, fixed = false;
    unsigned long long limit = 1000;
    unsigned button = 0, clicks = 1, batch = 64;
    double interval = .01;
    int x = 0, y = 0;
};

HWND item(int id) { return GetDlgItem(window, id); }
int selection(int id) { return static_cast<int>(SendMessageW(item(id), CB_GETCURSEL, 0, 0)); }
void select(int id, int value) { SendMessageW(item(id), CB_SETCURSEL, value, 0); }
bool checked(int id) { return SendMessageW(item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void check(int id, bool on) { SendMessageW(item(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
std::wstring value(int id) {
    wchar_t text[128]{}; GetWindowTextW(item(id), text, 128); return text;
}
void setText(HWND target, const std::wstring& text) {
    wchar_t previous[512]{}; GetWindowTextW(target, previous, 512);
    if (text != previous) SetWindowTextW(target, text.c_str());
}
void set(int id, const std::wstring& text) { setText(item(id), text); }
void enable(HWND target, bool on) { if ((IsWindowEnabled(target) != FALSE) != on) EnableWindow(target, on); }
bool emergency() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) &&
           (GetAsyncKeyState('X') & 0x8000);
}
void stop() { pending = 0; captureAt = 0; cancel = true; }
void join() { stop(); if (worker.joinable()) worker.join(); }

bool parseNumber(int id, double low, double high, double& result) {
    const auto text = value(id); wchar_t* end{};
    result = wcstod(text.c_str(), &end);
    return end != text.c_str() && *end == 0 && std::isfinite(result) && result >= low && result <= high;
}
bool readOptions(Options& o) {
    double number{};
    o.infinite = checked(104); o.fixed = checked(109);
    o.button = static_cast<unsigned>(selection(106));
    o.clicks = selection(107) == 1 ? 2 : 1;
    const unsigned batches[] = {1, 16, 64, 256, 1024};
    const int batchIndex = selection(103);
    if (batchIndex < 0 || batchIndex > 4 || o.button > 2) return false;
    o.batch = batches[batchIndex];
    if (!o.infinite) {
        if (!parseNumber(105, 1, 100000000, number) || floor(number) != number) return false;
        o.limit = static_cast<unsigned long long>(number);
    }
    const int mode = selection(101);
    if (mode == 2) o.interval = 0;
    else {
        if (!parseNumber(102, mode == 0 ? .001 : .000001, mode == 0 ? 1000000 : 3600000, number)) return false;
        o.interval = mode == 0 ? 1.0 / number : number / 1000.0;
    }
    if (o.fixed) {
        if (!parseNumber(110, -100000, 100000, number) || floor(number) != number) return false;
        o.x = static_cast<int>(number);
        if (!parseNumber(111, -100000, 100000, number) || floor(number) != number) return false;
        o.y = static_cast<int>(number);
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        if (o.x < x || o.y < y || o.x >= x + GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
            o.y >= y + GetSystemMetrics(SM_CYVIRTUALSCREEN)) return false;
    }
    return true;
}

void begin(const Options o) {
    if (running.load()) return;
    if (worker.joinable()) worker.join();
    cancel = false; failed = false; accepted = 0; seconds = 0; errorCode = 0; received = 0;
    running = true;
    worker = std::thread([o] {
        const DWORD downs[] = {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN};
        const DWORD ups[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
        const unsigned batch = o.interval == 0 ? o.batch : 1;
        const unsigned eventsPerCycle = o.clicks * 2;
        std::vector<INPUT> inputs(batch * eventsPerCycle);
        for (unsigned i = 0; i < inputs.size(); ++i) {
            inputs[i].type = INPUT_MOUSE; inputs[i].mi.dwExtraInfo = marker;
            inputs[i].mi.dwFlags = i % 2 ? ups[o.button] : downs[o.button];
        }
        while (!cancel.load() && !emergency() &&
               ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000))) Sleep(1);
        LARGE_INTEGER frequency{}, start{}, now{};
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
        unsigned long long cycles = 0;
        auto deadline = std::chrono::steady_clock::now();
        const auto spacing = std::chrono::nanoseconds(static_cast<long long>(o.interval * 1e9));
        while (!cancel.load() && !emergency() && (o.infinite || cycles < o.limit)) {
            const UINT n = static_cast<UINT>(o.infinite ? batch : std::min<unsigned long long>(batch, o.limit - cycles));
            if (o.fixed && !SetCursorPos(o.x, o.y)) { failed = true; errorCode = GetLastError(); break; }
            SetLastError(0);
            const UINT sent = SendInput(n * eventsPerCycle, inputs.data(), sizeof(INPUT));
            accepted.fetch_add(sent / 2);
            cycles += sent / eventsPerCycle;
            QueryPerformanceCounter(&now);
            seconds = static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart;
            if (sent != n * eventsPerCycle) {
                errorCode = GetLastError(); failed = true;
                if (sent % 2) {
                    INPUT release{}; release.type = INPUT_MOUSE; release.mi.dwFlags = ups[o.button];
                    release.mi.dwExtraInfo = marker; SendInput(1, &release, sizeof(INPUT));
                }
                break;
            }
            if (o.interval > 0 && (o.infinite || cycles < o.limit)) {
                // Avoid a burst of catch-up clicks after a slow/blocking SendInput call.
                deadline = std::max(deadline + spacing, std::chrono::steady_clock::now());
                while (!cancel.load() && !emergency()) {
                    const auto remaining = deadline - std::chrono::steady_clock::now();
                    if (remaining <= std::chrono::nanoseconds::zero()) break;
                    if (remaining > std::chrono::milliseconds(2)) Sleep(1); else SwitchToThread();
                }
            }
        }
        QueryPerformanceCounter(&now);
        seconds = static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart;
        running = false;
    });
}

void startFromControls(bool delay) {
    if (running.load() || testActive) return;
    Options options;
    if (!readOptions(options)) {
        MessageBoxW(window, L"Confira os valores. CPS: 0,001 a 1.000.000. Intervalo: 0,000001 a 3.600.000 ms. Repeticoes: 1 a 100.000.000. Use ponto para decimais e uma posicao dentro das telas.", L"Configuracao invalida", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (delay) pending = GetTickCount64() + 3000;
    else { pending = 0; begin(options); }
}

void defaults() {
    select(101, 0); set(102, L"100"); select(103, 2); check(104, false); set(105, L"1000");
    select(106, 0); select(107, 0); check(109, false); set(110, L"0"); set(111, L"0");
}
void loadConfig() {
    defaults();
    for (int id : {101, 103, 106, 107}) {
        const auto key = std::to_wstring(id);
        const int fallback = selection(id);
        const int v = GetPrivateProfileIntW(L"ClickLimit", key.c_str(), fallback, configPath.c_str());
        if (v >= 0 && v < SendMessageW(item(id), CB_GETCOUNT, 0, 0)) select(id, v);
    }
    for (int id : {102, 105, 110, 111}) {
        wchar_t text[128]{}; const auto key = std::to_wstring(id), fallback = value(id);
        GetPrivateProfileStringW(L"ClickLimit", key.c_str(), fallback.c_str(), text, 128, configPath.c_str());
        set(id, text);
    }
    // Opening the application never starts clicks, even when infinite was saved.
    check(104, GetPrivateProfileIntW(L"ClickLimit", L"104", 0, configPath.c_str()) != 0);
    check(109, GetPrivateProfileIntW(L"ClickLimit", L"109", 0, configPath.c_str()) != 0);
}
void saveConfig() {
    Options o; if (!readOptions(o)) { set(130, L"Configuracao invalida: corrija os valores antes de salvar."); return; }
    bool success = true;
    for (int id : {101, 102, 103, 104, 105, 106, 107, 109, 110, 111}) {
        const auto key = std::to_wstring(id);
        const auto v = id == 104 || id == 109 ? std::to_wstring(checked(id) ? 1 : 0) :
            (id == 101 || id == 103 || id == 106 || id == 107 ? std::to_wstring(selection(id)) : value(id));
        if (!WritePrivateProfileStringW(L"ClickLimit", key.c_str(), v.c_str(), configPath.c_str())) success = false;
    }
    set(130, success ? L"Configuracoes salvas em ClickLimit.ini, ao lado do executavel." : L"Nao foi possivel salvar. Extraia o programa para uma pasta gravavel.");
}

HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id=0, bool setting=false) {
    HWND c = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (setting) settings.push_back(c);
    return c;
}
void label(const wchar_t* text, int x, int y, int w) { control(L"STATIC", text, 0, x, y, w, 20); }
void edit(const wchar_t* text,int x,int y,int w,int id) { control(L"EDIT",text,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,x,y,w,28,id,true); }
void combo(int x,int y,int w,int id,std::initializer_list<const wchar_t*> values) {
    HWND c=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,x,y,w,160,id,true);
    SetWindowTheme(c, L"DarkMode_CFD", nullptr);
    for (auto text:values) SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
}
void button(const wchar_t* text,int x,int y,int w,int id) { control(L"BUTTON",text,BS_OWNERDRAW|WS_TABSTOP,x,y,w,42,id); }
void paintText(HDC dc,const wchar_t* text,RECT r,HFONT f,COLORREF color,UINT format=DT_LEFT|DT_SINGLELINE|DT_VCENTER) {
    auto old=SelectObject(dc,f); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT); DrawTextW(dc,text,-1,&r,format); SelectObject(dc,old);
}
void card(HDC dc,RECT r,const wchar_t* title) {
    FillRect(dc,&r,panelBrush); auto pen=CreatePen(PS_SOLID,1,RGB(35,62,80));
    auto old=SelectObject(dc,pen); auto brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    Rectangle(dc,r.left,r.top,r.right,r.bottom); SelectObject(dc,brush); SelectObject(dc,old); DeleteObject(pen);
    RECT titleRect{r.left+18,r.top+10,r.right-10,r.top+31}; paintText(dc,title,titleRect,smallFont,accent);
}

void refresh() {
    const bool locked = running.load() || pending || captureAt || testActive;
    for (HWND c:settings) enable(c,!locked);
    if (!locked) {
        enable(item(102),selection(101)!=2); enable(item(103),selection(101)==2);
        enable(item(105),!checked(104));
        enable(item(110),checked(109)); enable(item(111),checked(109));
    }
    for(int id:{120,121,122,123,112}) enable(item(id),!locked);
    wchar_t text[256]{};
    const double elapsed=seconds.load();
    swprintf_s(text,L"%llu cliques aceitos     /     %.3f s     /     %.0f cliques/s     /     %llu recebidos no teste",
        accepted.load(),elapsed,elapsed>0?static_cast<double>(accepted.load())/elapsed:0,received);
    setText(metrics,text);
    std::wstring state;
    if(captureAt) state=L"CAPTURA EM "+std::to_wstring((captureAt-GetTickCount64()+999)/1000)+L" s  |  Posicione o cursor no local desejado.";
    else if(pending) state=L"INICIO EM "+std::to_wstring((pending-GetTickCount64()+999)/1000)+L" s  |  Posicione o cursor. Ctrl+Alt+X cancela.";
    else if(running.load()) state=checked(104)&&!testActive?L"INFINITO ATIVO  |  Ctrl + Alt + X para. Ctrl + Alt + S nao interrompe.":L"MOTOR ATIVO  |  Ctrl + Alt + X interrompe a rodada.";
    else if(failed.load()) state=L"ENVIO INTERROMPIDO  |  Falha/bloqueio do Windows. Codigo: "+std::to_wstring(errorCode.load());
    else state=L"PRONTO  |  Ctrl + Alt + S inicia. Ctrl + Alt + X para em qualquer janela.";
    setText(status,state);
}

LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        window=hwnd;
        metrics=control(L"STATIC",L"",0,40,133,770,24);
        label(L"Modo",40,222,90); combo(40,245,156,101,{L"Velocidade (CPS)",L"Intervalo (ms)",L"Turbo"});
        label(L"Valor",210,222,85); edit(L"100",210,245,85,102);
        label(L"Lote turbo",315,222,95); combo(315,245,95,103,{L"1",L"16",L"64",L"256",L"1024"});
        label(L"Botao",465,222,130); combo(465,245,155,106,{L"Esquerdo",L"Direito",L"Meio"});
        label(L"Tipo de clique",645,222,145); combo(645,245,145,107,{L"Simples",L"Duplo"});
        control(L"BUTTON",L"Infinito - ate Ctrl+Alt+X",BS_AUTOCHECKBOX|WS_TABSTOP,40,360,265,23,104,true);
        label(L"Repeticoes por rodada",40,395,200); edit(L"1000",255,391,155,105);
        control(L"BUTTON",L"Usar posicao fixa",BS_AUTOCHECKBOX|WS_TABSTOP,465,353,250,23,109,true);
        label(L"X",465,390,20); edit(L"0",488,385,72,110);
        label(L"Y",580,390,20); edit(L"0",603,385,72,111);
        control(L"BUTTON",L"Capturar (3 s)",BS_OWNERDRAW|WS_TABSTOP,695,382,112,34,112);
        button(L"INICIAR  /  CTRL+ALT+S",20,466,265,120);
        button(L"Salvar",305,466,150,121); button(L"Restaurar",475,466,150,122); button(L"Teste rapido",645,466,185,123);
        status=control(L"STATIC",L"",0,20,525,810,24);
        control(L"STATIC",L"Cliques reais na posicao do mouse. Programas protegidos podem bloquear a entrada.",0,20,554,810,20,130);
        if(!RegisterHotKey(hwnd,1,MOD_NOREPEAT|MOD_CONTROL|MOD_ALT,'S') ||
           !RegisterHotKey(hwnd,2,MOD_NOREPEAT|MOD_CONTROL|MOD_ALT,'X')) {
            UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);
            MessageBoxW(hwnd,L"Ctrl+Alt+S ou Ctrl+Alt+X esta em uso. Feche outra instancia ou o programa que usa essas teclas.",L"Atalhos indisponiveis",MB_OK|MB_ICONERROR);
            return -1;
        }
        loadConfig(); refresh(); SetTimer(hwnd,1,100,nullptr); return 0;
    }
    case WM_COMMAND:
        if(GetMessageExtraInfo()==static_cast<LPARAM>(marker)) return 0;
        switch(LOWORD(wp)) {
        case 120:startFromControls(true);break;
        case 121:saveConfig();break;
        case 122:if(!running.load()){defaults();set(130,L"Configuracoes restauradas. Clique Salvar para manter os padroes.");}break;
        case 112:if(!running.load())captureAt=GetTickCount64()+3000;break;
        case 123:
            if(!running.load()&&!pending&&!captureAt){
                POINT target{425,620};ClientToScreen(hwnd,&target);
                Options o;o.fixed=true;o.x=target.x;o.y=target.y;o.limit=10;o.interval=.02;
                testActive=true;testDeadline=GetTickCount64()+6000;begin(o);
                set(130,L"Teste automatico: 10 cliques na area de teste desta janela.");
            }break;
        }
        refresh();return 0;
    case WM_HOTKEY:
        if(wp==1 && !running.load()&&!testActive&&!captureAt)startFromControls(false);
        if(wp==2)stop();
        refresh();return 0;
    case WM_TIMER: {
        const auto now=GetTickCount64();
        if(captureAt&&now>=captureAt){POINT p{};GetCursorPos(&p);set(110,std::to_wstring(p.x));set(111,std::to_wstring(p.y));check(109,true);captureAt=0;}
        if(pending&&now>=pending){pending=0;startFromControls(false);}
        if(testActive&&(!running.load()||now>=testDeadline)){
            if(now>=testDeadline)cancel=true;
            if(!running.load()&&(received==10||now>=testDeadline)){
                testActive=false;
                set(130,accepted.load()==10&&received==10&&!failed.load()?L"TESTE APROVADO: 10 cliques aceitos e 10 recebidos.":L"TESTE INCOMPLETO: confira os contadores e a posicao da janela.");
            }
        }
        refresh();return 0;
    }
    case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:
        if(GetMessageExtraInfo()==static_cast<LPARAM>(marker))++received;
        return 0;
    case WM_CONTEXTMENU:return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX: {
        const auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ink);SetBkColor(dc,panel);return reinterpret_cast<LRESULT>(panelBrush);
    }
    case WM_CTLCOLOREDIT: {
        const auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,accent);SetBkColor(dc,RGB(9,17,29));return reinterpret_cast<LRESULT>(editBrush);
    }
    case WM_DRAWITEM: {
        const auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(d->CtlType!=ODT_BUTTON)break;
        const bool disabled=(d->itemState&ODS_DISABLED)!=0;
        HBRUSH fill=CreateSolidBrush(disabled?panel:(d->itemState&ODS_SELECTED?RGB(22,80,85):RGB(18,43,57)));
        FillRect(d->hDC,&d->rcItem,fill);DeleteObject(fill);
        const auto border=CreateSolidBrush(disabled?RGB(39,58,72):accent);FrameRect(d->hDC,&d->rcItem,border);DeleteObject(border);
        wchar_t text[128]{};GetWindowTextW(d->hwndItem,text,128);
        paintText(d->hDC,text,d->rcItem,font,disabled?muted:ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(d->itemState&ODS_FOCUS){RECT r=d->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(d->hDC,&r);}return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};const auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);FillRect(dc,&r,backgroundBrush);
        auto pen=CreatePen(PS_SOLID,1,RGB(14,29,43));auto old=SelectObject(dc,pen);
        for(int x=0;x<850;x+=30){MoveToEx(dc,x,0,nullptr);LineTo(dc,x,650);}
        for(int y=0;y<650;y+=30){MoveToEx(dc,0,y,nullptr);LineTo(dc,850,y);}
        SelectObject(dc,old);DeleteObject(pen);
        paintText(dc,L"CLICK / LIMIT",RECT{20,17,620,58},titleFont,ink);
        paintText(dc,L"VELOCITY LAB    //    v0.3.0",RECT{22,60,500,82},smallFont,accent);
        paintText(dc,L"CTRL + ALT + X  /  PARAR",RECT{575,33,830,65},smallFont,accent,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        card(dc,RECT{20,100,830,160},L"TELEMETRIA / ENVIO AO WINDOWS");
        card(dc,RECT{20,181,430,295},L"01 / VELOCIDADE");card(dc,RECT{445,181,830,295},L"02 / MOUSE");
        card(dc,RECT{20,321,430,441},L"03 / REPETICAO");card(dc,RECT{445,321,830,441},L"04 / POSICAO");
        card(dc,RECT{20,592,830,643},L"AREA DE TESTE / POSICIONE AQUI PARA CONTAR CLIQUES RECEBIDOS");
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_CLOSE:join();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:join();UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDPIAware();
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    configPath=path;configPath=configPath.substr(0,configPath.find_last_of(L"\\/")+1)+L"ClickLimit.ini";
    backgroundBrush=CreateSolidBrush(background);panelBrush=CreateSolidBrush(panel);editBrush=CreateSolidBrush(RGB(9,17,29));
    font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    titleFont=CreateFontW(-32,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    smallFont=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"ClickLimitWindow";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=backgroundBrush;
    if(!RegisterClassW(&wc))return 1;
    const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;
    RECT bounds{0,0,850,660};AdjustWindowRect(&bounds,style,FALSE);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"ClickLimit / Velocity Lab v0.3.0",style,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};int result{};
    while((result=GetMessageW(&msg,nullptr,0,0))>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    DeleteObject(font);DeleteObject(titleFont);DeleteObject(smallFont);
    DeleteObject(backgroundBrush);DeleteObject(panelBrush);DeleteObject(editBrush);
    return result==-1?1:static_cast<int>(msg.wParam);
}
