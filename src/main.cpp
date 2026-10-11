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
#include <fstream>
#include <filesystem>
#include <shellapi.h>
#include <commctrl.h>
#include "engine.hpp"
#include "layout.hpp"

namespace {
constexpr ULONG_PTR marker = 0x434C494D;
constexpr COLORREF background = RGB(15, 16, 18), panel = RGB(34, 36, 39);
constexpr COLORREF accent = RGB(255, 155, 32), ink = RGB(237, 236, 226), muted = RGB(147, 151, 156);
HWND window{}, status{}, metrics{}, chrome{};
HBRUSH backgroundBrush{}, panelBrush{}, editBrush{};
HFONT font{}, titleFont{}, smallFont{};
std::vector<HWND> settings;
std::atomic<bool> running{false}, cancel{false}, failed{false};
std::atomic<unsigned long long> accepted{0};
std::atomic<double> seconds{0};
std::atomic<unsigned long long> startedNs{0};
std::atomic<DWORD> errorCode{0};
std::atomic<std::uint64_t> rejectedEvents{0},requestedEvents{0};
std::atomic<std::uint64_t> sendNs{0},waitNs{0},sendCalls{0};
std::thread worker;
HANDLE stopEvent{};
unsigned long long received = 0;
ULONGLONG pending = 0, captureAt = 0;
std::wstring configPath;
bool testActive = false;
ULONGLONG testDeadline = 0;
constexpr UINT roundFinished = WM_APP + 10, receiverUpdated = WM_APP + 11;
bool draining = false, receiverUpdatePending = false;
ULONGLONG drainDeadline = 0, drainMinimum = 0;


using Options = clicklimit::Options;
bool headless = false;
int dpi = 96, scrollY = 0;
clicklimit::Layout layout;
bool layingOut = false;
HDC paintBuffer{};
HBITMAP paintBitmap{}, paintPrevious{};
int paintWidth = 0, paintHeight = 0;
void releasePaintBuffer(){
    if(paintBuffer){SelectObject(paintBuffer,paintPrevious);DeleteObject(paintBitmap);DeleteDC(paintBuffer);}
    paintBuffer=nullptr;paintBitmap=nullptr;paintWidth=paintHeight=0;
}

std::uint64_t nowNs() {
    static const auto frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER t{}; QueryPerformanceCounter(&t);
    const auto ticks=static_cast<std::uint64_t>(t.QuadPart), hz=static_cast<std::uint64_t>(frequency);
    return ticks/hz*1000000000ULL + ticks%hz*1000000000ULL/hz;
}
struct WindowsBackend {
    std::vector<INPUT> inputs;
    bool profile=false;
    std::uint64_t sending=0,waiting=0,calls=0;
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    ~WindowsBackend(){if(timer)CloseHandle(timer);}
    void prepare(const std::vector<clicklimit::Event>& events) {
        inputs.resize(events.size());
        const DWORD downs[]={MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_MIDDLEDOWN};
        const DWORD ups[]={MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEUP};
        const int left=GetSystemMetrics(SM_XVIRTUALSCREEN), top=GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width=std::max(2,GetSystemMetrics(SM_CXVIRTUALSCREEN)), height=std::max(2,GetSystemMetrics(SM_CYVIRTUALSCREEN));
        for(std::size_t i=0;i<events.size();++i){
            const auto& e=events[i];auto& input=inputs[i];input.type=INPUT_MOUSE;input.mi.dwExtraInfo=marker;
            if(e.kind==clicklimit::Kind::move){
                input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
                input.mi.dx=static_cast<LONG>((static_cast<long long>(e.x-left)*65535)/(width-1));
                input.mi.dy=static_cast<LONG>((static_cast<long long>(e.y-top)*65535)/(height-1));
            }else input.mi.dwFlags=e.kind==clicklimit::Kind::down?downs[static_cast<unsigned>(e.button)]:ups[static_cast<unsigned>(e.button)];
        }
    }
    clicklimit::SendResult send(unsigned count){
        const auto before=profile?::nowNs():0;
        SetLastError(0);const UINT n=SendInput(count,inputs.data(),sizeof(INPUT));
        const auto error=n==count?0u:GetLastError();
        if(profile){sending+=::nowNs()-before;++calls;}
        return {n,error};
    }
    clicklimit::SendResult release(clicklimit::Button button){
        const DWORD ups[]={MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEUP};
        INPUT e{};e.type=INPUT_MOUSE;e.mi.dwFlags=ups[static_cast<unsigned>(button)];e.mi.dwExtraInfo=marker;
        SetLastError(0);const auto n=SendInput(1,&e,sizeof(e));return {n,n==1?0u:GetLastError()};
    }
    void pause(std::atomic<bool>& stopped){if(!stopped.load(std::memory_order_relaxed))WaitForSingleObject(stopEvent,1);}
    std::uint64_t nowNs(){const auto now=::nowNs();if(!startedNs.load(std::memory_order_relaxed))startedNs.store(now,std::memory_order_relaxed);return now;}
    void waitUntil(std::uint64_t deadline,std::atomic<bool>& stopped){
        const auto before=profile?::nowNs():0;
        while(!stopped.load(std::memory_order_relaxed)){
            const auto now=nowNs();if(now>=deadline)break;
            const auto remaining=deadline-now;
            if(timer&&remaining>300000){
                LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>((remaining-150000)/100);
                if(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)){
                    HANDLE handles[]={stopEvent,timer};WaitForMultipleObjects(2,handles,FALSE,INFINITE);
                }else YieldProcessor();
            }else if(!timer&&remaining>20000000)WaitForSingleObject(stopEvent,1);
            else YieldProcessor();
        }
        if(profile)waiting+=::nowNs()-before;
    }
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
void stop() { pending = 0; captureAt = 0; testActive=false; cancel = true;if(stopEvent)SetEvent(stopEvent); }
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
    if(mode<0||mode>3)return false;
    if (mode >= 2) {o.interval = 0;o.responsive=mode==2;if(o.responsive)o.batch=16;}
    else {
        if (!parseNumber(102, mode == 0 ? .001 : .000001, mode == 0 ? 1000000 : 3600000, number)) return false;
        if(mode==0){
            // High CPS is a mean rate: amortize potentially slow per-call input
            // insertion using approximately 10 ms worth of cycles per batch.
            o.scheduledBatch=number>=1000?static_cast<unsigned>(std::min(1024.0,std::ceil(number/100.0))):1;
            o.interval=o.scheduledBatch/number;
        }else o.interval=number/1000.0;
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
    if (running.load() || draining) return;
    if (worker.joinable()) worker.join();
    cancel = false; failed = false; accepted = 0; seconds = 0; startedNs = 0; errorCode = 0; received = 0;rejectedEvents=0;requestedEvents=0;
    draining=false;drainDeadline=drainMinimum=0;
    ResetEvent(stopEvent);
    sendNs=0;waitNs=0;sendCalls=0;
    running = true;
    SetTimer(window,1,100,nullptr);
    worker = std::thread([o] {
        while(!cancel.load(std::memory_order_relaxed) &&
              ((GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_MENU)&0x8000))) Sleep(1);
        // Keep the emergency-key fallback outside the high-throughput loop.
        std::jthread watchdog([](std::stop_token token){
            while(!token.stop_requested()){
                if(emergency()){cancel.store(true,std::memory_order_relaxed);SetEvent(stopEvent);}
                Sleep(1);
            }
        });
        WindowsBackend backend;
        backend.profile=true;
        const auto result=clicklimit::run(o,cancel,backend,[](std::uint64_t count){accepted.store(count,std::memory_order_relaxed);});
        seconds.store(static_cast<double>(result.elapsedNs)/1e9,std::memory_order_relaxed);
        failed.store(result.failed);errorCode.store(result.error);
        rejectedEvents=result.rejectedEvents;requestedEvents=result.requestedEvents;
        sendNs=backend.sending;waitNs=backend.waiting;sendCalls=backend.calls;
        running.store(false);
        PostMessageW(window,roundFinished,0,0);
    });
}

void startFromControls(bool delay) {
    if (running.load() || testActive || draining) return;
    Options options;
    if (!readOptions(options)) {
        MessageBoxW(window, L"Confira os valores. CPS: 0,001 a 1.000.000. Intervalo: 0,000001 a 3.600.000 ms. Repeticoes: 1 a 100.000.000. Use ponto para decimais e uma posicao dentro das telas.", L"Configuracao invalida", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (delay) { pending = GetTickCount64() + 3000;SetTimer(window,1,100,nullptr); }
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

LRESULT CALLBACK scrollInput(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(message==WM_MOUSEWHEEL){SendMessageW(window,message,wp,lp);return 0;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,scrollInput,id);
    return DefSubclassProc(hwnd,message,wp,lp);
}
HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id=0, bool setting=false) {
    HWND c = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, x, y, w, h, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowSubclass(c,scrollInput,1,0);
    if (setting) settings.push_back(c);
    return c;
}
void label(const wchar_t* text, int x, int y, int w, int id) { control(L"STATIC", text, 0, x, y, w, 20, id); }
void edit(const wchar_t* text,int x,int y,int w,int id) { control(L"EDIT",text,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,x,y,w,28,id,true); }
void combo(int x,int y,int w,int id,std::initializer_list<const wchar_t*> values) {
    HWND c=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,x,y,w,160,id,true);
    SetWindowTheme(c, L"DarkMode_CFD", nullptr);
    for (auto text:values) SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    COMBOBOXINFO info{sizeof(info)};
    if(GetComboBoxInfo(c,&info)&&info.hwndList)SetWindowSubclass(info.hwndList,scrollInput,1,0);
}
void button(const wchar_t* text,int x,int y,int w,int id) { control(L"BUTTON",text,BS_OWNERDRAW|WS_TABSTOP,x,y,w,42,id); }
void paintText(HDC dc,const wchar_t* text,RECT r,HFONT f,COLORREF color,UINT format=DT_LEFT|DT_SINGLELINE|DT_VCENTER) {
    auto old=SelectObject(dc,f); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT); DrawTextW(dc,text,-1,&r,format); SelectObject(dc,old);
}
int px(int n){return MulDiv(n,dpi,96);}
RECT screenRect(clicklimit::Box b){return {px(b.x),px(b.y-scrollY),px(b.x+b.w),px(b.y+b.h-scrollY)};}
void stripe(HDC dc,clicklimit::Box b){
    const RECT r=screenRect(b);SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
    auto fill=CreateSolidBrush(accent);FillRect(dc,&r,fill);DeleteObject(fill);
    auto brush=CreateSolidBrush(background);auto old=SelectObject(dc,brush);auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    const int step=px(24), slant=px(13);
    for(int x=r.left-step;x<r.right+step;x+=step){POINT p[]={{x,r.top},{x+px(11),r.top},{x+px(11)+slant,r.bottom},{x+slant,r.bottom}};Polygon(dc,p,4);}
    SelectObject(dc,oldPen);SelectObject(dc,old);DeleteObject(brush);RestoreDC(dc,-1);
}
void card(HDC dc,clicklimit::Box b,const wchar_t* title){
    RECT r=screenRect(b);const int cut=px(12), notch=px(6);
    POINT p[]={{r.left+cut,r.top},{r.left+px(54),r.top},{r.left+px(62),r.top+notch},
      {r.left+px(110),r.top+notch},{r.left+px(118),r.top},{r.right-cut,r.top},
      {r.right,r.top+cut},{r.right,r.bottom-px(36)},{r.right-notch,r.bottom-px(30)},
      {r.right-notch,r.bottom-cut},{r.right-cut*2,r.bottom},{r.left+px(92),r.bottom},
      {r.left+px(84),r.bottom-notch},{r.left+px(42),r.bottom-notch},{r.left+px(34),r.bottom},
      {r.left+cut,r.bottom},{r.left,r.bottom-cut},{r.left,r.top+px(57)},
      {r.left+notch,r.top+px(51)},{r.left+notch,r.top+cut}};
    auto pen=CreatePen(PS_SOLID,1,RGB(101,104,108));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,panelBrush);
    Polygon(dc,p,static_cast<int>(std::size(p)));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    RECT t=screenRect({b.x+16,b.y+12,b.w-32,21});paintText(dc,title,t,smallFont,ink);
    RECT bar=screenRect({b.x+16,b.y+35,42,3});auto orange=CreateSolidBrush(accent);FillRect(dc,&bar,orange);DeleteObject(orange);
    for(int i=0;i<4;++i){
        const int x=r.right-px(58)+i*px(9),y=r.bottom-px(12);
        POINT marks[]={{x,y},{x+px(4),y},{x+px(9),y+px(5)},{x+px(5),y+px(5)}};
        auto f=CreateSolidBrush(i==3?accent:RGB(87,91,95));auto previous=SelectObject(dc,f);
        Polygon(dc,marks,4);SelectObject(dc,previous);DeleteObject(f);
    }
    for(int i=0;i<3;++i){RECT bolt=screenRect({b.x+b.w-24-i*7,b.y+17,3,3});FillRect(dc,&bolt,GetSysColorBrush(COLOR_GRAYTEXT));}
}
void drawScene(HDC dc){
    RECT client{};GetClientRect(window,&client);FillRect(dc,&client,backgroundBrush);
    const int w=layout.width;
    auto pen=CreatePen(PS_SOLID,1,RGB(25,26,28));auto old=SelectObject(dc,pen);
    for(int x=0;x<w;x+=40){MoveToEx(dc,px(x),0,nullptr);LineTo(dc,px(x),client.bottom);}
    SelectObject(dc,old);DeleteObject(pen);
    card(dc,layout.telemetry,L"TLM / PARES ACEITOS PELO WINDOWS");
    const wchar_t* titles[]={L"01 / VELOCIDADE",L"02 / MOUSE",L"03 / REPETICAO",L"04 / POSICAO"};
    for(int i=0;i<4;++i)card(dc,layout.cards[i],titles[i]);
    // Segmented rails with stepped orange bridges, inspired by the references.
    for(int y:{198,layout.test.y-10}){
        const int yy=px(y-scrollY),left=px(20),right=px(w-20),center=(left+right)/2;
        auto rail=CreatePen(PS_SOLID,1,RGB(95,99,101));auto prior=SelectObject(dc,rail);
        MoveToEx(dc,left,yy,nullptr);LineTo(dc,center-px(58),yy);LineTo(dc,center-px(50),yy-px(4));
        LineTo(dc,center+px(50),yy-px(4));LineTo(dc,center+px(58),yy);LineTo(dc,right,yy);
        SelectObject(dc,prior);DeleteObject(rail);
        stripe(dc,{w/2-35,y-6,70,3});
    }
    card(dc,layout.test,L"TEST / AREA PARA RECEBER CLIQUES");
    stripe(dc,{layout.test.x+16,layout.test.y+layout.test.h-13,layout.test.w-32,4});
}
void drawChrome(HDC dc){
    RECT r{};GetClientRect(chrome,&r);FillRect(dc,&r,backgroundBrush);
    paintText(dc,L"ClickLimit",{px(20),px(14),px(250),px(56)},titleFont,ink);
    paintText(dc,L"v0.8.0  /  INPUT LAB",{px(22),px(57),r.right-px(160),px(79)},smallFont,accent);
    auto pen=CreatePen(PS_SOLID,1,accent);auto previous=SelectObject(dc,pen);
    MoveToEx(dc,px(20),px(92),nullptr);LineTo(dc,px(82),px(92));LineTo(dc,px(91),px(86));
    LineTo(dc,r.right-px(95),px(86));LineTo(dc,r.right-px(86),px(92));LineTo(dc,r.right-px(20),px(92));
    SelectObject(dc,previous);DeleteObject(pen);
}
LRESULT CALLBACK chromeProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);drawChrome(dc);EndPaint(hwnd,&ps);return 0;}
    if(message==WM_PRINTCLIENT){drawChrome(reinterpret_cast<HDC>(wp));return 0;}
    return DefSubclassProc(hwnd,message,wp,lp);
}
void applyLayout(){
    if(layingOut)return;
    layingOut=true;
    RECT client{};GetClientRect(window,&client);
    layout=clicklimit::makeLayout(MulDiv(client.right,96,dpi));
    const int height=MulDiv(client.bottom,96,dpi), maxScroll=std::max(0,layout.contentHeight-height);
    scrollY=std::clamp(scrollY,0,maxScroll);
    HDWP moves=BeginDeferWindowPos(static_cast<int>(layout.controls.size()));
    for(const auto& p:layout.controls){
        HWND child=GetDlgItem(window,p.id);if(!child)continue;
        const auto r=screenRect(p.box);
        // Combo height includes its popup list; the visible row stays 30 logical pixels.
        const int h=(p.id==101||p.id==103||p.id==106||p.id==107)?px(180):r.bottom-r.top;
        const UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW;
        if(moves)moves=DeferWindowPos(moves,child,nullptr,r.left,r.top,r.right-r.left,h,flags);
        else SetWindowPos(child,nullptr,r.left,r.top,r.right-r.left,h,flags);
    }
    if(moves)EndDeferWindowPos(moves);
    else for(const auto& p:layout.controls){
        const auto r=screenRect(p.box);const int h=(p.id==101||p.id==103||p.id==106||p.id==107)?px(180):r.bottom-r.top;
        SetWindowPos(GetDlgItem(window,p.id),nullptr,r.left,r.top,r.right-r.left,h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);
    }
    if(chrome){
        SetWindowPos(chrome,HWND_TOP,0,0,client.right,px(100),SWP_NOACTIVATE);
        for(int i=0;i<3;++i)SetWindowPos(item(150+i),HWND_TOP,client.right-px(152-i*46),px(19),px(40),px(36),SWP_NOACTIVATE);
    }
    layingOut=false;
    // Invalidate parent AND child contents at their final positions. Moving pixels
    // from the previous geometry caused stale backgrounds while scrolling/resizing.
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
}
void createFonts(){
    for(HFONT f:{font,titleFont,smallFont})if(f)DeleteObject(f);
    font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    titleFont=CreateFontW(-px(32),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    smallFont=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");
    if(window)for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
}

void refresh() {
    const bool locked = running.load() || pending || captureAt || testActive || draining;
    for (HWND c:settings) enable(c,!locked);
    if (!locked) {
        enable(item(102),selection(101)<2); enable(item(103),selection(101)==3);
        enable(item(105),!checked(104));
        enable(item(110),checked(109)); enable(item(111),checked(109));
    }
    for(int id:{120,121,122,123,112}) enable(item(id),!locked);
    enable(item(120),!draining);
    set(120,draining?L"CONFERINDO RECEPCAO":running.load()||pending||captureAt||testActive?L"PARAR / CTRL+ALT+X":L"INICIAR / CTRL+ALT+S");
    wchar_t text[256]{};
    const double elapsed=running.load() && startedNs.load()?static_cast<double>(nowNs()-startedNs.load())/1e9:seconds.load();
    swprintf_s(text,L"%llu cliques aceitos     /     %.3f s     /     %.0f cliques/s     /     %llu recebidos no teste",
        accepted.load(),elapsed,elapsed>0?static_cast<double>(accepted.load())/elapsed:0,received);
    setText(metrics,text);
    std::wstring state;
    if(captureAt) state=L"CAPTURA EM "+std::to_wstring((captureAt-GetTickCount64()+999)/1000)+L" s  |  Posicione o cursor no local desejado.";
    else if(pending) state=L"INICIO EM "+std::to_wstring((pending-GetTickCount64()+999)/1000)+L" s  |  Posicione o cursor. Ctrl+Alt+X cancela.";
    else if(running.load()) state=checked(104)&&!testActive?L"INFINITO ATIVO  |  Ctrl + Alt + X para. Ctrl + Alt + S nao interrompe.":L"MOTOR ATIVO  |  Ctrl + Alt + X interrompe a rodada.";
    else if(draining) state=L"CONFERINDO RECEPCAO | Envio encerrado; aguardando a fila da janela.";
    else if(failed.load()) state=L"ENVIO INTERROMPIDO | "+std::to_wstring(rejectedEvents.load())+L" eventos nao aceitos de "+std::to_wstring(requestedEvents.load())+L". Codigo: "+std::to_wstring(errorCode.load());
    else if(accepted.load() && received==accepted.load()) state=L"RODADA CONFERIDA | Todos os cliques aceitos foram recebidos nesta janela.";
    else if(accepted.load() && received>0) state=L"DIFERENCA NA RECEPCAO | "+std::to_wstring(accepted.load()>received?accepted.load()-received:0)+L" cliques a menos nesta janela. Aceito nao significa recebido.";
    else state=L"PRONTO  |  Ctrl + Alt + S inicia. Ctrl + Alt + X para em qualquer janela.";
    setText(status,state);
    if(!running.load()&&sendCalls.load()){
        wchar_t diagnostic[256]{};
        swprintf_s(diagnostic,L"DIAGNOSTICO | Envio medio: %.3f ms/chamada | Espera total: %.3f s | %llu chamadas",
            static_cast<double>(sendNs.load())/sendCalls.load()/1e6,static_cast<double>(waitNs.load())/1e9,sendCalls.load());
        set(130,diagnostic);
    }
}

LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        window=hwnd;
        dpi=headless?96:static_cast<int>(GetDpiForWindow(hwnd));createFonts();
        metrics=control(L"STATIC",L"",0,0,0,1,1,140);
        label(L"Modo",0,0,1,201);combo(0,0,1,101,{L"Velocidade (CPS)",L"Intervalo (ms)",L"Turbo responsivo",L"Turbo sem espera"});
        label(L"Valor",0,0,1,202);edit(L"100",0,0,1,102);
        label(L"Lote turbo",0,0,1,203);combo(0,0,1,103,{L"1",L"16",L"64",L"256",L"1024"});
        label(L"Botao",0,0,1,204);combo(0,0,1,106,{L"Esquerdo",L"Direito",L"Meio"});
        label(L"Tipo de clique",0,0,1,205);combo(0,0,1,107,{L"Simples",L"Duplo"});
        control(L"BUTTON",L"Infinito - ate Ctrl+Alt+X",BS_AUTOCHECKBOX|WS_TABSTOP,0,0,1,1,104,true);
        label(L"Repeticoes por rodada",0,0,1,206);edit(L"1000",0,0,1,105);
        control(L"BUTTON",L"Usar posicao fixa",BS_AUTOCHECKBOX|WS_TABSTOP,0,0,1,1,109,true);
        label(L"X",0,0,1,207);edit(L"0",0,0,1,110);
        label(L"Y",0,0,1,208);edit(L"0",0,0,1,111);
        control(L"BUTTON",L"Capturar 3 s",BS_OWNERDRAW|WS_TABSTOP,0,0,1,1,112);
        button(L"INICIAR / CTRL+ALT+S",0,0,1,120);
        button(L"Salvar",0,0,1,121);button(L"Restaurar",0,0,1,122);button(L"Teste rapido",0,0,1,123);
        status=control(L"STATIC",L"",0,0,0,1,1,141);
        control(L"STATIC",L"Ctrl+Alt+X para. Turbo responsivo prioriza a parada. Sem espera pode deixar cliques na fila do Windows.",0,0,0,1,1,130);
        if(!headless && (!RegisterHotKey(hwnd,1,MOD_NOREPEAT|MOD_CONTROL|MOD_ALT,'S') ||
           !RegisterHotKey(hwnd,2,MOD_NOREPEAT|MOD_CONTROL|MOD_ALT,'X'))) {
            UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);
            MessageBoxW(hwnd,L"Ctrl+Alt+S ou Ctrl+Alt+X esta em uso. Feche outra instancia ou o programa que usa essas teclas.",L"Atalhos indisponiveis",MB_OK|MB_ICONERROR);
            return -1;
        }
        chrome=control(L"STATIC",L"",0,0,0,1,1,149);
        SetWindowSubclass(chrome,chromeProcedure,2,0);
        button(L"—",0,0,40,150);button(L"□",0,0,40,151);button(L"×",0,0,40,152);
        if(headless)defaults();else loadConfig(); applyLayout();refresh();return 0;
    }
    case WM_COMMAND:
        if(GetMessageExtraInfo()==static_cast<LPARAM>(marker)) return 0;
        switch(LOWORD(wp)) {
        case 150:if(!headless)ShowWindow(hwnd,SW_MINIMIZE);break;
        case 151:if(!headless)ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);break;
        case 152:SendMessageW(hwnd,WM_CLOSE,0,0);return 0;
        case 120:if(running.load()||pending||captureAt||testActive)stop();else startFromControls(true);break;
        case 121:saveConfig();break;
        case 122:if(!running.load()){defaults();set(130,L"Configuracoes restauradas. Clique Salvar para manter os padroes.");}break;
        case 112:if(!running.load()){captureAt=GetTickCount64()+3000;SetTimer(hwnd,1,100,nullptr);}break;
        case 123:
            if(!running.load()&&!pending&&!captureAt){
                RECT client{};GetClientRect(hwnd,&client);
                scrollY=std::max(0,layout.test.y+layout.test.h-MulDiv(client.bottom,96,dpi));applyLayout();
                POINT target{px(layout.test.x+layout.test.w/2),px(layout.test.y+layout.test.h/2-scrollY)};ClientToScreen(hwnd,&target);
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
    case roundFinished:
        draining=true;drainMinimum=GetTickCount64()+500;drainDeadline=GetTickCount64()+5000;
        SetTimer(hwnd,1,100,nullptr);refresh();return 0;
    case receiverUpdated:
        receiverUpdatePending=false;refresh();return 0;
    case WM_TIMER: {
        static bool wasActive=false;
        const bool active=running.load()||pending||captureAt||testActive||draining;
        if(!active&&!wasActive){KillTimer(hwnd,1);return 0;}
        wasActive=active;
        const auto now=GetTickCount64();
        if(draining && now>=drainMinimum && (received>=accepted.load() || received==0 || now>=drainDeadline))draining=false;
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
        if(GetMessageExtraInfo()==static_cast<LPARAM>(marker)){
            ++received;
            // During sending/draining the 100ms timer refreshes the UI.
            // Never post a refresh per input: it competes with the mouse queue.
            if(!running.load()&&!draining&&!receiverUpdatePending){receiverUpdatePending=true;PostMessageW(hwnd,receiverUpdated,0,0);}
        }
        return 0;
    case WM_CONTEXTMENU:return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX: {
        const auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ink);SetBkColor(dc,panel);return reinterpret_cast<LRESULT>(panelBrush);
    }
    case WM_CTLCOLOREDIT: {
        const auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ink);SetBkColor(dc,RGB(18,19,21));return reinterpret_cast<LRESULT>(editBrush);
    }
    case WM_DRAWITEM: {
        const auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(d->CtlType!=ODT_BUTTON)break;
        const bool disabled=(d->itemState&ODS_DISABLED)!=0;
        HBRUSH fill=CreateSolidBrush(disabled?panel:(d->itemState&ODS_SELECTED?RGB(83,57,27):RGB(44,45,47)));
        FillRect(d->hDC,&d->rcItem,fill);DeleteObject(fill);
        const auto border=CreateSolidBrush(disabled?RGB(70,72,74):accent);FrameRect(d->hDC,&d->rcItem,border);DeleteObject(border);
        wchar_t text[128]{};GetWindowTextW(d->hwndItem,text,128);
        paintText(d->hDC,text,d->rcItem,font,disabled?muted:ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(d->itemState&ODS_FOCUS){RECT r=d->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(d->hDC,&r);}return TRUE;
    }
    case WM_PRINTCLIENT:drawScene(reinterpret_cast<HDC>(wp));return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};const auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);
        if(!paintBuffer||paintWidth!=r.right||paintHeight!=r.bottom){
            releasePaintBuffer();paintBuffer=CreateCompatibleDC(dc);
            paintBitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,r.right),std::max<LONG>(1,r.bottom));
            if(paintBuffer&&paintBitmap){paintPrevious=static_cast<HBITMAP>(SelectObject(paintBuffer,paintBitmap));paintWidth=r.right;paintHeight=r.bottom;}
            else {if(paintBitmap)DeleteObject(paintBitmap);if(paintBuffer)DeleteDC(paintBuffer);paintBuffer=nullptr;paintBitmap=nullptr;}
        }
        if(paintBuffer){drawScene(paintBuffer);BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,paintBuffer,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);}
        else drawScene(dc);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_NCCALCSIZE:return 0;
    case WM_NCHITTEST:{
        POINT point{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};ScreenToClient(hwnd,&point);
        RECT r{};GetClientRect(hwnd,&r);const int edge=px(7);
        if(!IsZoomed(hwnd)){
            const bool left=point.x<edge,right=point.x>=r.right-edge,top=point.y<edge,bottom=point.y>=r.bottom-edge;
            if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;
            if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
            if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
        }
        if(point.y<px(100))return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE:if(window){set(151,IsZoomed(hwnd)?L"▣":L"□");applyLayout();}return 0;
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(lp);
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor)){
            info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
            info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
        }
        RECT minimum{0,0,px(460),px(360)};
        AdjustWindowRectEx(&minimum,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,0);
        info->ptMinTrackSize={minimum.right-minimum.left,minimum.bottom-minimum.top};return 0;
    }
    case WM_DPICHANGED: {
        dpi=HIWORD(wp);createFonts();const auto r=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);applyLayout();return 0;
    }
    case WM_MOUSEWHEEL: {
        static int remainder=0;
        remainder+=GET_WHEEL_DELTA_WPARAM(wp);
        const int steps=remainder/WHEEL_DELTA;remainder%=WHEEL_DELTA;
        if(steps){scrollY-=steps*70;applyLayout();}return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_VERT,&si);
        switch(LOWORD(wp)){case SB_LINEUP:scrollY-=35;break;case SB_LINEDOWN:scrollY+=35;break;
            case SB_PAGEUP:scrollY-=250;break;case SB_PAGEDOWN:scrollY+=250;break;case SB_THUMBTRACK:scrollY=si.nTrackPos;break;}
        applyLayout();return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_CLOSE:join();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:join();releasePaintBuffer();UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}

bool renderFile(const std::wstring& path,int width,int height){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap){DeleteDC(dc);return false;}auto old=SelectObject(dc,bitmap);
    // This window is never shown and rendering does not touch the user's desktop.
    drawScene(dc);drawChrome(dc);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        if(child==chrome)continue;
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
        SaveDC(dc);SetViewportOrgEx(dc,r.left,r.top,nullptr);
        IntersectClipRect(dc,0,0,r.right-r.left,r.bottom-r.top);
        SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
        wchar_t kind[32]{};GetClassNameW(child,kind,32);
        const bool editControl=std::wstring(kind)==L"Edit", comboControl=std::wstring(kind)==L"ComboBox";
        // Hidden native edits/combos do not paint their text. Render their current
        // state explicitly for the offscreen preview, without showing the window.
        if(editControl||comboControl){
            RECT box{0,0,r.right-r.left,px(30)};
            FillRect(dc,&box,editBrush);FrameRect(dc,&box,GetSysColorBrush(COLOR_GRAYTEXT));
            wchar_t text[128]{};GetWindowTextW(child,text,128);
            if(comboControl){const auto index=SendMessageW(child,CB_GETCURSEL,0,0);if(index>=0)SendMessageW(child,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(text));}
            box.left+=px(8);box.right-=px(comboControl?25:8);
            paintText(dc,text,box,font,IsWindowEnabled(child)?ink:muted);
            if(comboControl){box.left=r.right-r.left-px(22);box.right=r.right-r.left-px(3);paintText(dc,L"v",box,smallFont,muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        }
        RestoreDC(dc,-1);
    }
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+width*height*4;
    std::ofstream file(std::filesystem::path(path),std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    file.write(reinterpret_cast<const char*>(pixels),static_cast<std::streamsize>(width)*height*4);const bool ok=static_cast<bool>(file);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;
}
bool isolatedUiTest(const std::wstring& report){
    DWORD initialGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    unsigned checks=0;bool ok=true;
    ok=ok&&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION);
    POINT drag{px(100),px(30)},edge{px(2),px(160)};
    ClientToScreen(window,&drag);ClientToScreen(window,&edge);
    ok=ok&&SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM(drag.x,drag.y))==HTCAPTION;
    ok=ok&&SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))==HTLEFT;
    for(int i=0;i<120;++i){
        const int widths[]={480,820,1000,1440,460,799,800};
        const int heights[]={400,600,780};
        RECT bounds{0,0,widths[i%7],heights[i%3]};
        AdjustWindowRect(&bounds,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE);
        SetWindowPos(window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(i%2?-WHEEL_DELTA:WHEEL_DELTA)),0);
        RECT client{};GetClientRect(window,&client);
        ok=ok&&scrollY>=0&&scrollY<=std::max(0,layout.contentHeight-MulDiv(client.bottom,96,dpi));
        for(const auto& p:layout.controls){
            RECT actual{};GetWindowRect(GetDlgItem(window,p.id),&actual);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&actual),2);
            const auto expected=screenRect(p.box);
            ok=ok&&actual.left==expected.left&&actual.top==expected.top&&actual.right==expected.right;
            ++checks;
        }
        RECT header{};GetWindowRect(chrome,&header);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&header),2);
        ok=ok&&header.top==0&&header.bottom==px(100)&&header.right==client.right;
        SendMessageW(window,WM_PAINT,0,0);
        // Native themed controls initialize caches lazily. Check resource growth
        // after the first ten warm-up cycles, including every width/height.
        if(i==9)initialGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    }
    const DWORD finalGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    ok=ok&&finalGdi<=initialGdi;
    ok=ok&&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_VSCROLL);
    unsigned wheelChecks=0;
    for(int id:{101,103,106,107}){
        COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(item(id),&info);
        for(HWND target:{item(id),info.hwndList}){
            scrollY=0;applyLayout();const auto before=selection(id);
            SendMessageW(target,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
            ok=ok&&selection(id)==before&&scrollY==70;++wheelChecks;
        }
    }
    scrollY=0;applyLayout();
    SendMessageW(item(101),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA/2)),0);
    ok=ok&&scrollY==0;
    SendMessageW(item(101),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA/2)),0);
    ok=ok&&scrollY==70;
    // Exercise the stop UI without ever starting an input worker.
    running=true;testActive=true;pending=1;captureAt=1;cancel=false;ResetEvent(stopEvent);refresh();
    ok=ok&&IsWindowEnabled(item(120))&&value(120)==L"PARAR / CTRL+ALT+X";
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(120,BN_CLICKED),reinterpret_cast<LPARAM>(item(120)));
    ok=ok&&cancel.load()&&!testActive&&!pending&&!captureAt&&WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0;
    running=false;refresh();
    // Emulate late receiver messages locally; no system mouse input is injected.
    accepted=1000;received=990;seconds=1;SendMessageW(window,roundFinished,0,0);
    ok=ok&&draining;
    const auto previousExtra=SetMessageExtraInfo(static_cast<LPARAM>(marker));
    receiverUpdatePending=false;
    for(int i=0;i<10;++i)SendMessageW(window,WM_LBUTTONUP,0,0);
    ok=ok&&!receiverUpdatePending; // No refresh-message flood while draining.
    SetMessageExtraInfo(previousExtra);SendMessageW(window,receiverUpdated,0,0);
    ok=ok&&received==1000&&value(140).find(L"1000 recebidos")!=std::wstring::npos;
    drainMinimum=0;SendMessageW(window,WM_TIMER,1,0);
    ok=ok&&!draining&&value(141).find(L"RODADA CONFERIDA")!=std::wstring::npos;
    received=999;SendMessageW(window,roundFinished,0,0);drainMinimum=drainDeadline=0;
    SendMessageW(window,WM_TIMER,1,0);
    ok=ok&&!draining&&value(141).find(L"DIFERENCA")!=std::wstring::npos;
    running=true;receiverUpdatePending=false;
    const auto burstExtra=SetMessageExtraInfo(static_cast<LPARAM>(marker));
    for(int i=0;i<10000;++i)SendMessageW(window,WM_LBUTTONUP,0,0);
    SetMessageExtraInfo(burstExtra);
    ok=ok&&!receiverUpdatePending&&received==10999;
    running=false;accepted=received=0;refresh();
    // Exercise the real Windows timer, but never call prepare/send/SendInput.
    ResetEvent(stopEvent);std::atomic<bool> timerStop{false};WindowsBackend timing;
    const auto timingStart=::nowNs();
    for(int i=0;i<20;++i){const auto deadline=::nowNs()+1000000;timing.waitUntil(deadline,timerStop);ok=ok&&::nowNs()>=deadline;}
    const auto timerElapsed=::nowNs()-timingStart;
    const auto cancelStart=::nowNs();
    std::thread interrupter([&](){Sleep(10);timerStop=true;SetEvent(stopEvent);});
    timing.waitUntil(cancelStart+2000000000ULL,timerStop);interrupter.join();
    ok=ok&&timerStop.load()&&::nowNs()-cancelStart<1000000000ULL;
    std::ofstream file(std::filesystem::path(report),std::ios::binary);
    file<<(ok?"PASS":"FAIL")<<": 120 hidden resize/scroll cycles, "<<checks<<" native control geometry checks\n"
        <<"GDI objects after warm-up: "<<initialGdi<<" -> "<<finalGdi<<" (persistent paint cache included)\n"
        <<wheelChecks<<" dropdown/list wheel checks; high-resolution wheel accumulation; no visible scrollbar.\n"
        <<"Stop button and cancellation event verified without starting input worker.\n"
        <<"20 real timer waits of 1ms: "<<static_cast<double>(timerElapsed)/1e6<<" ms total; cancelable long wait verified; no input sent.\n"
        <<"Window never shown; hotkeys, timers and input generation disabled.\n";
    return ok&&static_cast<bool>(file);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    int argc{};wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    const bool uiTest=argc>=3&&std::wstring(argv[1])==L"--ui-test";
    const bool render=argc>=5&&std::wstring(argv[1])==L"--render";
    headless=render||uiTest;
    const int renderWidth=render?std::clamp(_wtoi(argv[3]),460,2400):1000;
    const int renderHeight=render?std::clamp(_wtoi(argv[4]),360,2000):740;
    const std::wstring renderPath=headless?argv[2]:L"";LocalFree(argv);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent)return 1;
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    configPath=path;configPath=configPath.substr(0,configPath.find_last_of(L"\\/")+1)+L"ClickLimit.ini";
    backgroundBrush=CreateSolidBrush(background);panelBrush=CreateSolidBrush(panel);editBrush=CreateSolidBrush(RGB(18,19,21));
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"ClickLimitWindow";
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=backgroundBrush;
    if(!RegisterClassW(&wc))return 1;
    const DWORD style=WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN;
    RECT bounds{0,0,renderWidth,renderHeight};// Custom nonclient layout uses the full window bounds as its client area.
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"ClickLimit",style,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    int exitCode=0;
    if(headless){exitCode=(uiTest?isolatedUiTest(renderPath):renderFile(renderPath,renderWidth,renderHeight))?0:2;DestroyWindow(hwnd);}
    else {
        ShowWindow(hwnd,show);UpdateWindow(hwnd);MSG msg{};int result{};
        while((result=GetMessageW(&msg,nullptr,0,0))>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        exitCode=result==-1?1:static_cast<int>(msg.wParam);
    }
    for(HFONT f:{font,titleFont,smallFont})if(f)DeleteObject(f);
    DeleteObject(backgroundBrush);DeleteObject(panelBrush);DeleteObject(editBrush);CloseHandle(stopEvent);return exitCode;
}
