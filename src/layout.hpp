#pragma once
#include <algorithm>
#include <array>
#include <vector>

namespace clicklimit {
struct Box { int x, y, w, h; };
struct Placement { int id; Box box; };
struct Layout {
    int width = 0, contentHeight = 0;
    bool narrow = false;
    Box telemetry{}, test{};
    std::array<Box,4> cards{};
    std::vector<Placement> controls;
};
inline Layout makeLayout(int width) {
    Layout l; l.width = std::max(width, 440); l.narrow = l.width < 800;
    const int m = 20, gap = 16, full = l.width - 2*m;
    const int column = l.narrow ? full : (full-gap)/2;
    l.telemetry = {m,108,full,80};
    for (int i=0;i<4;++i) {
        const int row = l.narrow ? i : i/2;
        const int col = l.narrow ? 0 : i%2;
        l.cards[i] = {m+col*(column+gap),210+row*150,column,134};
    }
    auto add=[&](int id,int x,int y,int w,int h){l.controls.push_back({id,{x,y,w,h}});};
    add(140,m+16,142,full-32,38);
    // Each control keeps a stable minimum width; cards stack instead of shrinking text.
    const auto a=l.cards[0];
    const int modeWidth=std::max(146,(a.w-56)*42/100);
    const int valueWidth=std::max(78,(a.w-56)*23/100);
    const int batchWidth=a.w-56-modeWidth-valueWidth;
    int x=a.x+16;
    add(201,x,a.y+47,modeWidth,20);add(101,x,a.y+73,modeWidth,30);x+=modeWidth+12;
    add(202,x,a.y+47,valueWidth,20);add(102,x,a.y+73,valueWidth,30);x+=valueWidth+12;
    add(203,x,a.y+47,batchWidth,20);add(103,x,a.y+73,batchWidth,30);
    const auto b=l.cards[1];const int half=(b.w-44)/2;
    add(204,b.x+16,b.y+47,half,20);add(106,b.x+16,b.y+73,half,30);
    add(205,b.x+28+half,b.y+47,half,20);add(107,b.x+28+half,b.y+73,half,30);
    const auto c=l.cards[2];
    add(104,c.x+16,c.y+44,c.w-32,24);
    add(206,c.x+16,c.y+83,180,20);add(105,c.x+206,c.y+78,c.w-222,30);
    const auto d=l.cards[3];const int numberWidth=std::max(62,(d.w-214)/2);
    add(109,d.x+16,d.y+41,d.w-32,24);
    add(207,d.x+16,d.y+82,16,20);add(110,d.x+36,d.y+76,numberWidth,30);
    add(208,d.x+numberWidth+48,d.y+82,16,20);add(111,d.x+numberWidth+68,d.y+76,numberWidth,30);
    add(112,d.x+d.w-124,d.y+76,108,30);
    const int actions=c.y+(l.narrow?150:0)+154;
    if(l.narrow){
        add(120,m,actions,full,44);
        const int third=(full-24)/3;
        add(121,m,actions+56,third,40);add(122,m+third+12,actions+56,third,40);
        add(123,m+(third+12)*2,actions+56,full-2*(third+12),40);
    }else{
        const int start=std::max(240,full*33/100), other=(full-start-36)/3;
        add(120,m,actions,start,44);add(121,m+start+12,actions,other,44);
        add(122,m+start+other+24,actions,other,44);add(123,m+start+other*2+36,actions,full-start-other*2-36,44);
    }
    const int footer=actions+(l.narrow?112:60);
    add(141,m,footer,full,42);add(130,m,footer+46,full,l.narrow?60:40);
    l.test={m,footer+(l.narrow?118:98),full,78};l.contentHeight=l.test.y+l.test.h+20;
    return l;
}
}
