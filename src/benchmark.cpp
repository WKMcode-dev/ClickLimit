#include <windows.h>
#include <atomic>
#include <thread>
#include <fstream>
#include <filesystem>
#include <iostream>
#include "engine.hpp"

using namespace clicklimit;
constexpr ULONG_PTR tag=0x434c424d;
constexpr std::uint64_t duration=1000000000;
std::uint64_t clockNs(){
    static const auto hz=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return static_cast<std::uint64_t>(f.QuadPart);}();
    LARGE_INTEGER t{};QueryPerformanceCounter(&t);const auto n=static_cast<std::uint64_t>(t.QuadPart);
    return n/hz*duration+n%hz*duration/hz;
}
struct State {
    std::atomic<bool> stop{false},done{false};
    std::atomic<std::uint64_t> start{0};
    std::uint64_t received=0,inWindow=0,malformed=0;
    bool down=false,closed=false;
    Result result;
} state;
HWND target{};
LRESULT CALLBACK receiver(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if((msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP)&&GetMessageExtraInfo()==static_cast<LPARAM>(tag)){
        if(msg==WM_LBUTTONDOWN){if(state.down)++state.malformed;state.down=true;}
        else if(state.down){state.down=false;++state.received;const auto start=state.start.load();if(start&&clockNs()-start<=duration)++state.inWindow;}
        else ++state.malformed;
        return 0;
    }
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){state.stop=true;return 0;}
    if(msg==WM_CLOSE){state.closed=true;state.stop=true;return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
struct WindowsSink {
    std::vector<INPUT> inputs;
    void prepare(const std::vector<Event>& events){
        inputs.resize(events.size());
        const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int w=std::max(2,GetSystemMetrics(SM_CXVIRTUALSCREEN)),h=std::max(2,GetSystemMetrics(SM_CYVIRTUALSCREEN));
        for(unsigned i=0;i<events.size();++i){auto& e=inputs[i];e.type=INPUT_MOUSE;e.mi.dwExtraInfo=tag;
            if(events[i].kind==Kind::move){e.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;e.mi.dx=static_cast<LONG>((static_cast<long long>(events[i].x-x)*65535)/(w-1));e.mi.dy=static_cast<LONG>((static_cast<long long>(events[i].y-y)*65535)/(h-1));}
            else e.mi.dwFlags=events[i].kind==Kind::down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
        }
    }
    SendResult send(unsigned n){
        // Keep this controlled benchmark pointed at its own receiving window.
        if(GetForegroundWindow()!=target){state.stop=true;return {0,ERROR_CANCELLED};}
        SetLastError(0);const auto accepted=SendInput(n,inputs.data(),sizeof(INPUT));return {accepted,accepted==n?0u:GetLastError()};
    }
    SendResult release(Button){INPUT e{};e.type=INPUT_MOUSE;e.mi.dwFlags=MOUSEEVENTF_LEFTUP;e.mi.dwExtraInfo=tag;SetLastError(0);const auto n=SendInput(1,&e,sizeof(e));return {n,n==1?0u:GetLastError()};}
    std::uint64_t nowNs(){const auto n=clockNs();if(!state.start.load())state.start=n;return n;}
    void waitUntil(std::uint64_t deadline,std::atomic<bool>& stop){while(!stop&&clockNs()<deadline)SwitchToThread();}
    void pause(std::atomic<bool>&){Sleep(1);}
};
struct SimulatedSink {
    std::uint64_t time=0,received=0;std::vector<Event> events;
    void prepare(const std::vector<Event>& e){events=e;}
    SendResult send(unsigned n){received+=(n-1)/2;time+=50000;return {n,0};}
    SendResult release(Button){return {1,0};}
    std::uint64_t nowNs(){return time;}
    void waitUntil(std::uint64_t deadline,std::atomic<bool>&){time=deadline;}
    void pause(std::atomic<bool>&){time+=1000000;}
};
int wmain(int argc,wchar_t** argv){
    const bool simulation=argc==3&&std::wstring(argv[1])==L"--simulate";
    const bool live=argc==3&&std::wstring(argv[1])==L"--sandbox-live";
    if(!simulation&&!live){std::cout<<"Use --simulate report.csv (no input), or the supplied Windows Sandbox launcher.\n";return 2;}
    if(live){
        std::ifstream marker("C:\\ClickLimit-Isolated\\allow-live.txt");std::string permission;std::getline(marker,permission);
        if(permission!="ClickLimit-Sandbox-Benchmark-v1"){std::cerr<<"Live mode unavailable. Launch through the supplied Windows Sandbox configuration.\n";return 3;}
    }
    std::ofstream csv{std::filesystem::path(argv[2])};if(!csv)return 4;
    csv<<"mode,repeat,batch,requested_events,accepted_events,rejected_events,accepted_pairs,accepted_pairs_in_window,received_pairs,received_pairs_in_window,send_seconds,drain_seconds,all_accepted,all_received,valid,error\n";
    if(live){
        WNDCLASSW wc{};wc.lpfnWndProc=receiver;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ClickLimitBenchmarkReceiver";wc.hbrBackground=GetSysColorBrush(COLOR_WINDOW);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc))return 5;
        target=CreateWindowW(wc.lpszClassName,L"ClickLimit benchmark - VM only - Escape stops",WS_OVERLAPPEDWINDOW,100,100,900,650,nullptr,nullptr,wc.hInstance,nullptr);
        if(!target)return 6;ShowWindow(target,SW_SHOW);UpdateWindow(target);SetForegroundWindow(target);
    }
    bool ok=true;
    for(unsigned repeat=1;repeat<=3&&ok;++repeat)for(unsigned batch:{1u,16u,64u,256u,1024u,4096u}){
        Options o;o.infinite=true;o.fixed=true;o.interval=0;o.batch=batch;o.budgetNs=duration;
        Result r;std::uint64_t received=0,inWindow=0;double drain=0;bool wellFormed=true;
        if(simulation){
            std::atomic<bool> stop{false};SimulatedSink sink;r=run(o,stop,sink,[](std::uint64_t){});received=sink.received;inWindow=r.withinBudget;
        }else{
            state.stop=false;state.done=false;state.start=0;state.received=state.inWindow=state.malformed=0;state.down=false;
            POINT p{400,300};ClientToScreen(target,&p);o.x=p.x;o.y=p.y;
            std::thread sender([&](){WindowsSink sink;state.result=run(o,state.stop,sink,[](std::uint64_t){});state.done=true;});
            std::uint64_t completedAt=0;MSG msg{};
            for(;;){
                if(state.done&&!completedAt)completedAt=clockNs();
                // Poll the drain deadline between messages, even under a huge queue.
                if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}else SwitchToThread();
                if(completedAt&&(state.received>=state.result.clicks||clockNs()-completedAt>5000000000ULL))break;
            }
            sender.join();r=state.result;received=state.received;inWindow=state.inWindow;drain=static_cast<double>(clockNs()-completedAt)/duration;
            wellFormed=!state.malformed&&!state.down&&!state.closed;
        }
        const bool accepted=r.rejectedEvents==0&&!r.failed;
        const bool delivered=received==r.clicks&&wellFormed;
        const bool valid=accepted&&delivered&&!r.cancelled;
        csv<<(simulation?"SIMULATED":"WINDOWS_SANDBOX")<<','<<repeat<<','<<batch<<','<<r.requestedEvents<<','<<r.acceptedEvents<<','<<r.rejectedEvents<<','<<r.clicks<<','<<r.withinBudget<<','<<received<<','<<inWindow<<','<<static_cast<double>(r.elapsedNs)/duration<<','<<drain<<','<<accepted<<','<<delivered<<','<<valid<<','<<r.error<<'\n';csv.flush();
        if(!valid){ok=false;break;}
    }
    if(target)DestroyWindow(target);
    std::cout<<(simulation?"SIMULATED; not a Windows performance record. ":"Sandbox benchmark complete. ")<<(ok?"PASS\n":"INCOMPLETE; inspect report.\n");
    return ok?0:1;
}
