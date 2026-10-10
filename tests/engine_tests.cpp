#include "engine.hpp"
#include "layout.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace clicklimit;
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
struct FakeBackend {
    std::vector<Event> events;
    std::atomic<bool>* stop = nullptr;
    unsigned sends=0, stopAt=0, partial=~0u, releases=0, error=0, pauses=0;
    bool cancelOnPause=false;
    bool failRecovery=false;
    std::uint64_t time=0, latency=0;
    std::vector<unsigned> requests;
    void prepare(const std::vector<Event>& input){events=input;}
    SendResult send(unsigned n){
        requests.push_back(n);++sends;time+=latency;
        if(stopAt&&sends==stopAt)stop->store(true);
        return {std::min(n,partial),error};
    }
    SendResult release(Button){++releases;return {failRecovery?0u:1u,failRecovery?87u:0u};}
    std::uint64_t nowNs(){return time;}
    void waitUntil(std::uint64_t deadline,std::atomic<bool>&){time=std::max(time,deadline);}
    void pause(std::atomic<bool>& stopped){++pauses;time+=1000000;if(cancelOnPause)stopped.store(true);}
};
int main(){
    try {
        unsigned passed=0;const auto progress=[](std::uint64_t){};
        {
            Options o;o.infinite=true;o.interval=0;o.responsive=true;o.batch=1024;
            std::atomic<bool> stop{false};FakeBackend b;b.cancelOnPause=true;
            const auto r=run(o,stop,b,progress);
            require(r.cancelled&&r.clicks==16&&b.sends==1&&b.pauses==1,"stop during responsive pause; cap batch; no later send");++passed;
        }
        {
            Options o;o.interval=0;o.responsive=true;o.batch=1024;o.limit=17;o.clicks=2;
            std::atomic<bool> stop{false};FakeBackend b;const auto r=run(o,stop,b,progress);
            require(r.clicks==34&&b.requests.size()==2&&b.requests[0]==64&&b.requests[1]==4&&b.pauses==1,"responsive finite double tail");++passed;
        }
        for(unsigned button=0;button<3;++button)for(unsigned doubleClick=1;doubleClick<=2;++doubleClick){
            Options o;o.button=button;o.clicks=doubleClick;o.interval=0;o.limit=65;o.batch=64;
            std::atomic<bool> stop{false};FakeBackend backend;const auto r=run(o,stop,backend,progress);
            require(r.clicks==65*doubleClick&&r.cycles==65&&!r.failed,"finite tail/double count");
            require(r.requestedEvents==130*doubleClick&&r.acceptedEvents==r.requestedEvents&&r.rejectedEvents==0&&r.sendCalls==2,"all events accepted audit");
            require(backend.requests.size()==2&&backend.requests.back()==2*doubleClick,"tail batch size");
            for(unsigned i=0;i<backend.events.size();++i){require(backend.events[i].button==static_cast<Button>(button),"button");require(backend.events[i].kind==(i%2?Kind::up:Kind::down),"event ordering");}
            ++passed;
        }
        {
            Options o;o.infinite=true;o.limit=1;o.interval=0;o.batch=16;
            std::atomic<bool> stop{false};FakeBackend b;b.stop=&stop;b.stopAt=3;
            const auto r=run(o,stop,b,progress);
            require(r.clicks==48&&r.cancelled&&b.sends==3,"infinite cancellation/ignore finite limit");++passed;
        }
        for(unsigned partialCount:{0u,1u,2u,3u,5u}) {
            Options o;o.fixed=true;o.x=-100;o.y=200;o.interval=0;o.batch=16;o.limit=16;
            std::atomic<bool> stop{false};FakeBackend b;b.partial=partialCount;b.error=5;
            const auto r=run(o,stop,b,progress);
            const unsigned mouse=partialCount?partialCount-1:0;
            require(r.failed&&r.error==5&&r.clicks==(mouse+1)/2,"partial/error count including accepted recovery");
            require(r.requestedEvents==33+mouse%2&&r.acceptedEvents==partialCount+mouse%2&&r.rejectedEvents==33-partialCount&&r.sendCalls==1+mouse%2,"exact event acceptance audit including recovery");
            require(b.releases==mouse%2,"button-up recovery");
            require(b.events.front().kind==Kind::move&&b.events.front().x==-100,"fixed position prefix");++passed;
        }
        for(unsigned clicks:{1u,2u})for(unsigned partial=0;partial<1+16*clicks*2;++partial){
            Options o;o.fixed=true;o.infinite=true;o.interval=0;o.batch=16;o.clicks=clicks;
            std::atomic<bool> stop{false};FakeBackend b;b.partial=partial;
            const auto r=run(o,stop,b,progress);const auto mouse=partial?partial-1:0;
            require(r.failed&&b.requests.size()==1&&r.clicks==(mouse+1)/2,"every partial return stops without retry/duplication");
            require(r.acceptedEvents==partial+mouse%2&&r.requestedEvents==1+32*clicks+mouse%2&&r.rejectedEvents==1+32*clicks-partial,"every partial return audit");++passed;
        }
        {
            Options o;o.fixed=true;o.interval=0;o.batch=16;
            std::atomic<bool> stop{false};FakeBackend b;b.partial=2;b.failRecovery=true;
            const auto r=run(o,stop,b,progress);
            require(r.failed&&r.recoveryFailed&&r.clicks==0&&r.acceptedEvents==2&&r.requestedEvents==34&&r.rejectedEvents==32,"failed recovery must not invent accepted click");++passed;
        }
        {
            Options o;o.infinite=true;o.interval=0;o.batch=8;o.budgetNs=100000;
            std::atomic<bool> stop{false};FakeBackend b;b.latency=60000;
            const auto r=run(o,stop,b,progress);
            require(r.clicks==16&&r.withinBudget==8&&r.elapsedNs==120000,"100us overshoot must not be credited");++passed;
        }
        {
            Options o;o.limit=3;o.interval=.1;std::atomic<bool> stop{false};FakeBackend b;
            const auto r=run(o,stop,b,progress);require(r.elapsedNs==200000000&&r.clicks==3,"interval schedule");++passed;
        }
        {
            Options o;o.infinite=true;o.interval=0;std::atomic<bool> stop{true};FakeBackend b;
            const auto r=run(o,stop,b,progress);require(b.sends==0&&r.cancelled,"cancel before first batch");++passed;
        }
        for(int width:{440,480,640,799,800,850,1000,1440,1920}){
            const auto l=makeLayout(width);
            for(const auto& p:l.controls){require(p.box.x>=0&&p.box.y>=0&&p.box.w>0&&p.box.h>0,"positive control geometry");require(p.box.x+p.box.w<=width&&p.box.y+p.box.h<=l.contentHeight,"responsive control bounds");}
            for(unsigned i=0;i<l.controls.size();++i)for(unsigned j=i+1;j<l.controls.size();++j){
                const auto a=l.controls[i].box,b=l.controls[j].box;
                const bool overlaps=a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;
                require(!overlaps,"overlapping controls");
            }
            ++passed;
        }
        std::cout<<"PASS: "<<passed<<" engine/layout cases. In-memory backend; no Windows input events.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
