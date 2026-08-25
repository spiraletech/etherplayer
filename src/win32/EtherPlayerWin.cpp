#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mfapi.h>
#include <mfplay.h>
#include <gdiplus.h>

#include "etherplayer/PlayerState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;
using etherplayer::BrowseSection;
using etherplayer::PlayerState;
using etherplayer::Presentation;
using etherplayer::Screen;

namespace {

constexpr int kBigW = 1080;
constexpr int kBigH = 760;
constexpr int kPipW = 500;
constexpr int kPipH = 190;
constexpr UINT_PTR kTimer = 1;
constexpr wchar_t kClassName[] = L"ETHERPLAYER_V01_WINDOW";

HWND g_hwnd{};
ULONG_PTR g_gdiplus{};
PlayerState g_state;
IMFPMediaPlayer* g_player{};
IMFPMediaItem* g_item{};
bool g_playing = false;
ULONGLONG g_duration = 0;
std::unique_ptr<Image> g_cover;
std::wstring g_coverPath;

struct HitZone {
    RectF rect;
    int action;
};
std::vector<HitZone> g_hits;

enum Action {
    ActHero = 1,
    ActBrowse,
    ActQueue,
    ActRemote,
    ActPip,
    ActCenter,
    ActPrev,
    ActNext,
    ActUp,
    ActDown,
    ActFile,
    ActQuick
};

template <typename T> void release(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

RectF R(float x, float y, float w, float h) { return RectF(x,y,w,h); }

bool hit(const RectF& r, int x, int y) {
    return x >= r.X && x <= r.X+r.Width && y >= r.Y && y <= r.Y+r.Height;
}

void addHit(const RectF& rect, int action) { g_hits.push_back({rect, action}); }

Color amber(BYTE a=255) { return Color(a, 242, 195, 61); }
Color warmWhite(BYTE a=255) { return Color(a, 244, 242, 235); }
Color muted(BYTE a=255) { return Color(a, 142, 134, 116); }

void roundRect(Graphics& g, const RectF& r, float radius, Color fill, Color stroke=Color(0,0,0,0), float strokeWidth=1.f) {
    GraphicsPath p;
    const float d = radius*2.f;
    p.AddArc(r.X,r.Y,d,d,180,90);
    p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);
    p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);
    p.CloseFigure();
    SolidBrush b(fill); g.FillPath(&b,&p);
    if (stroke.GetA()) { Pen pen(stroke,strokeWidth); g.DrawPath(&pen,&p); }
}

void text(Graphics& g, const std::wstring& value, const RectF& rect, float size, Color color, int style=FontStyleRegular,
          StringAlignment h=StringAlignmentNear, StringAlignment v=StringAlignmentNear) {
    FontFamily ff(L"Segoe UI");
    Font font(&ff,size,style,UnitPixel);
    SolidBrush brush(color);
    StringFormat fmt; fmt.SetAlignment(h); fmt.SetLineAlignment(v); fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(value.c_str(),-1,&font,rect,&fmt,&brush);
}

std::wstring currentTitle() {
    const auto* t=g_state.currentTrack(); return t?t->title:L"nothing playing";
}
std::wstring currentArtist() {
    const auto* t=g_state.currentTrack(); return t?t->artist:L"open EtherPlay music";
}

std::wstring readCoverSidecar(const std::wstring& track) {
    std::ifstream f(fs::path(track+L".ethercover"),std::ios::binary);
    std::string line;
    while(std::getline(f,line)) {
        auto eq=line.find('='); if(eq==std::string::npos) continue;
        std::string key=line.substr(0,eq), val=line.substr(eq+1);
        if(key!="image" && key!="source" && key!="path") continue;
        int n=MultiByteToWideChar(CP_UTF8,0,val.data(),(int)val.size(),nullptr,0);
        std::wstring out((size_t)std::max(n,0),L'\0');
        if(n>0) MultiByteToWideChar(CP_UTF8,0,val.data(),(int)val.size(),out.data(),n);
        return out;
    }
    return {};
}

void refreshCover() {
    g_cover.reset(); g_coverPath.clear();
    const auto* t=g_state.currentTrack(); if(!t) return;
    g_coverPath=readCoverSidecar(t->path);
    if(g_coverPath.empty()) return;
    auto img=std::make_unique<Image>(g_coverPath.c_str());
    if(img->GetLastStatus()==Ok) g_cover=std::move(img);
}

void drawCover(Graphics& g,const RectF& outer) {
    RectF shadow=R(outer.X+7,outer.Y+9,outer.Width,outer.Height);
    roundRect(g,shadow,24,Color(120,0,0,0));
    roundRect(g,outer,24,Color(255,8,8,8),Color(92,115,98,45));
    RectF inner=R(outer.X+12,outer.Y+12,outer.Width-24,outer.Height-24);
    if(!g_cover) {
        text(g,L"ETHERPLAYER",inner,18,Color(255,92,84,67),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        return;
    }
    const float iw=(float)g_cover->GetWidth(), ih=(float)g_cover->GetHeight();
    if(iw<=0||ih<=0) return;
    const float scale=std::min(inner.Width/iw,inner.Height/ih);
    const float w=iw*scale,h=ih*scale;
    RectF dst=R(inner.X+(inner.Width-w)/2.f,inner.Y+(inner.Height-h)/2.f,w,h);
    g.DrawImage(g_cover.get(),dst);
    SolidBrush veil(Color(18,0,0,0));g.FillRectangle(&veil,dst);
}

void drawAnalyzer(Graphics& g,const RectF& r,bool mini=false) {
    Pen grid(Color(60,120,96,30),1.f);
    for(int i=0;i<5;i++) { float x=r.X+r.Width*i/4.f; g.DrawLine(&grid,x,r.Y,x,r.GetBottom()); }
    for(int i=0;i<4;i++) { float y=r.Y+r.Height*i/3.f; g.DrawLine(&grid,r.X,y,r.GetRight(),y); }

    const int bars=mini?42:64;
    const float gap=2.f;
    const float bw=(r.Width-gap*(bars-1))/bars;
    const double t=GetTickCount64()/1000.0;
    const auto* track=g_state.currentTrack();
    const double seed=track?std::hash<std::wstring>{}(track->title)%997:0;
    SolidBrush brush(amber());
    for(int i=0;i<bars;i++) {
        double x=(double)i/(double)bars;
        double wave=.34+.20*std::sin(x*12.0+t*2.0+seed*.01)+.14*std::sin(x*31.0-t*1.3)+.10*std::sin(x*67.0+t*.7);
        wave=std::clamp(wave,0.08,0.94);
        const float h=(float)(wave*r.Height);
        g.FillRectangle(&brush,r.X+i*(bw+gap),r.GetBottom()-h,bw,h);
    }
}

void closeMedia() {
    if(g_player){g_player->Stop();g_player->Shutdown();}
    release(g_item); release(g_player); g_playing=false;g_duration=0;
}

bool openCurrentMedia() {
    const auto* t=g_state.currentTrack(); if(!t)return false;
    closeMedia();
    if(FAILED(MFPCreateMediaPlayer(nullptr,FALSE,0,nullptr,g_hwnd,&g_player))||!g_player)return false;
    if(FAILED(g_player->CreateMediaItemFromURL(t->path.c_str(),TRUE,0,&g_item))||!g_item){closeMedia();return false;}
    PROPVARIANT d{};PropVariantInit(&d);
    if(SUCCEEDED(g_item->GetDuration(MFP_POSITIONTYPE_100NS,&d))) {
        if(d.vt==VT_I8)g_duration=(ULONGLONG)d.hVal.QuadPart; else if(d.vt==VT_UI8)g_duration=d.uhVal.QuadPart;
    }
    PropVariantClear(&d);
    if(FAILED(g_player->SetMediaItem(g_item))){closeMedia();return false;}
    g_player->SetVolume(g_state.volume());
    refreshCover();
    return true;
}

void playPause() {
    if(!g_state.currentTrack()) {
        if(!g_state.library().empty()){g_state.selectLibraryTrack(0,true);openCurrentMedia();}
        else return;
    }
    if(!g_player && !openCurrentMedia())return;
    if(g_playing){g_player->Pause();g_playing=false;}else{g_player->Play();g_playing=true;}
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void changeTrack(bool next) {
    const bool ok=next?g_state.next():g_state.previous();
    if(!ok)return;
    openCurrentMedia();g_player->Play();g_playing=true;InvalidateRect(g_hwnd,nullptr,FALSE);
}

void openFile() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_hwnd;ofn.lpstrFile=path;ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"Audio\0*.mp3;*.wav\0All Files\0*.*\0";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn))return;
    auto tracks=g_state.library();
    const std::wstring p=path;
    auto it=std::find_if(tracks.begin(),tracks.end(),[&](const auto& t){return !_wcsicmp(t.path.c_str(),p.c_str());});
    if(it==tracks.end()) tracks.push_back({p,fs::path(p).stem().wstring(),L"unknown artist"});
    g_state.setLibrary(std::move(tracks));
    g_state.selectLibraryTrack(g_state.library().size()-1,true);openCurrentMedia();playPause();
}

void togglePip() {
    g_state.togglePip();
    const bool pip=g_state.presentation()==Presentation::Pip;
    SetWindowPos(g_hwnd,pip?HWND_TOPMOST:HWND_NOTOPMOST,0,0,pip?kPipW:kBigW,pip?kPipH:kBigH,SWP_NOMOVE|SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void drawControlPad(Graphics& g,float cx,float cy,float scale=1.f) {
    const float ring=74.f*scale;
    SolidBrush outer(Color(255,10,10,10));g.FillEllipse(&outer,cx-ring,cy-ring,ring*2,ring*2);
    Pen edge(Color(150,127,103,48),1.2f);g.DrawEllipse(&edge,cx-ring,cy-ring,ring*2,ring*2);
    SolidBrush center(Color(255,16,15,13));g.FillEllipse(&center,cx-32*scale,cy-32*scale,64*scale,64*scale);
    Pen glow(amber(190),2.f);g.DrawEllipse(&glow,cx-34*scale,cy-34*scale,68*scale,68*scale);
    text(g,g_playing?L"Ⅱ":L"▶",R(cx-32*scale,cy-32*scale,64*scale,64*scale),22*scale,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"⌂",R(cx-18*scale,cy-ring+4*scale,36*scale,28*scale),16*scale,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"◀",R(cx-ring+6*scale,cy-18*scale,34*scale,36*scale),17*scale,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"▶",R(cx+ring-40*scale,cy-18*scale,34*scale,36*scale),17*scale,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"≡",R(cx-18*scale,cy+ring-31*scale,36*scale,26*scale),18*scale,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(R(cx-34*scale,cy-34*scale,68*scale,68*scale),ActCenter);
    addHit(R(cx-28*scale,cy-ring,56*scale,34*scale),ActUp);
    addHit(R(cx-ring,cy-28*scale,42*scale,56*scale),ActPrev);
    addHit(R(cx+ring-42*scale,cy-28*scale,42*scale,56*scale),ActNext);
    addHit(R(cx-28*scale,cy+ring-34*scale,56*scale,34*scale),ActDown);
}

void drawHeader(Graphics& g) {
    text(g,L"ETHERPLAYER",R(30,18,260,32),26,warmWhite(),FontStyleBold);
    text(g,L"v0.1  //  ETHERPLAY PLATFORM",R(31,48,310,20),11,muted(),FontStyleBold);
    struct H{const wchar_t* label;Screen screen;float x;};
    H tabs[]={{L"hero",Screen::Hero,480},{L"browse",Screen::Browse,590},{L"queue",Screen::Queue,715},{L"remote",Screen::Remote,825}};
    for(auto& h:tabs){
        bool on=g_state.screen()==h.screen;RectF rr=R(h.x,18,105,42);
        if(on)roundRect(g,rr,18,Color(235,28,24,12),Color(110,126,101,43));
        text(g,h.label,rr,14,on?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(rr,ActHero+(int)h.screen);
    }
    RectF pip=R(950,18,90,42);roundRect(g,pip,18,Color(220,12,12,12),Color(70,90,75,45));
    text(g,g_state.presentation()==Presentation::Pip?L"expand":L"pip",pip,14,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pip,ActPip);
}

void drawHero(Graphics& g) {
    text(g,L"NOW PLAYING",R(120,98,220,24),13,amber(),FontStyleBold);
    RectF art=R(375,120,330,330);drawCover(g,art);
    text(g,currentTitle(),R(210,462,660,46),31,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(260,506,560,26),15,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(260,548,560,82),false);
    drawControlPad(g,540,682,.62f);
    RectF file=R(155,656,125,48);roundRect(g,file,20,Color(240,12,12,12),Color(85,80,72,52));text(g,L"FILE",file,14,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(file,ActFile);
    text(g,L"LEFT  previous / hold rewind     CENTER  play-pause     RIGHT  next / hold ff     DOWN  queue",R(285,728,650,20),11,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
}

void drawBrowse(Graphics& g) {
    text(g,L"music",R(72,105,430,78),62,amber(),FontStyleRegular);
    const wchar_t* items[]={L"playlists",L"artists",L"albums",L"songs",L"queue"};
    for(int i=0;i<5;i++) text(g,items[i],R(74,184+i*62,360,62),45,i==(int)g_state.browseSelection()-1?amber():warmWhite(),FontStyleRegular);
    RectF preview=R(650,155,320,400);roundRect(g,preview,26,Color(245,6,6,7),Color(80,100,83,45));
    text(g,L"NOW PLAYING",R(680,180,240,22),12,amber(),FontStyleBold);
    drawCover(g,R(735,220,150,150));
    text(g,currentTitle(),R(675,385,270,40),20,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(690,440,240,70),true);
    text(g,L"UP/DOWN or ring  browse   •   CENTER select   •   LEFT back   •   RIGHT quick action",R(70,665,900,34),14,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawControlPad(g,960,650,.55f);
}

void drawQueue(Graphics& g) {
    text(g,L"UP NEXT",R(65,105,300,48),40,warmWhite(),FontStyleBold);
    auto& q=g_state.queue();
    if(q.empty()) { text(g,L"queue is empty",R(65,180,400,40),22,muted()); return; }
    float y=175;
    for(size_t i=0;i<q.size()&&i<8;i++) {
        const auto idx=q[i]; if(idx>=g_state.library().size())continue;
        const auto& t=g_state.library()[idx];
        RectF row=R(65,y,650,58);roundRect(g,row,16,i==0?Color(220,42,33,8):Color(220,8,8,9),i==0?Color(150,205,166,53):Color(55,75,66,52));
        text(g,std::to_wstring(i+1),R(82,y,38,58),14,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        text(g,t.title,R(130,y+8,430,24),17,warmWhite(),FontStyleBold);
        text(g,t.artist,R(130,y+31,430,20),12,muted());
        y+=66;
    }
    text(g,L"PLAY NEXT is first-class. Reorder + persistent playlists land after the v0.1 interaction pass.",R(65,690,690,28),13,amber());
    drawControlPad(g,900,390,.72f);
}

void drawRemote(Graphics& g) {
    text(g,L"REMOTE MODE",R(80,110,460,62),46,warmWhite(),FontStyleBold);
    text(g,L"LOCAL CONTROL DECK // NETWORK BRIDGE LATER",R(82,176,550,26),14,amber(),FontStyleBold);
    RectF deck=R(80,235,920,410);roundRect(g,deck,34,Color(245,7,7,8),Color(80,102,83,45));
    drawCover(g,R(120,280,240,240));
    text(g,currentTitle(),R(410,280,470,60),32,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(412,342,430,28),16,muted());
    drawAnalyzer(g,R(410,405,450,92),true);
    drawControlPad(g,640,555,.55f);
    text(g,L"Remote v0.1 controls the same player state. Phone/LAN transport is deliberately not faked yet.",R(405,602,520,28),13,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
}

void drawPip(Graphics& g) {
    RectF art=R(18,20,130,130);drawCover(g,art);
    text(g,currentTitle(),R(170,24,260,32),20,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(170,57,260,22),12,muted());
    drawAnalyzer(g,R(170,88,250,42),true);
    RectF pp=R(432,58,48,48);roundRect(g,pp,23,Color(255,17,16,13),Color(160,202,161,51));text(g,g_playing?L"Ⅱ":L"▶",pp,18,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pp,ActCenter);
    RectF exp=R(430,116,52,28);text(g,L"EXPAND",exp,10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
    Bitmap back(rc.right,rc.bottom,PixelFormat32bppPARGB);Graphics g(&back);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    SolidBrush bg(Color(255,2,2,3));g.FillRectangle(&bg,0,0,rc.right,rc.bottom);g_hits.clear();
    if(g_state.presentation()==Presentation::Pip) drawPip(g); else {
        drawHeader(g);
        switch(g_state.screen()){
            case Screen::Hero:drawHero(g);break;
            case Screen::Browse:drawBrowse(g);break;
            case Screen::Queue:drawQueue(g);break;
            case Screen::Remote:drawRemote(g);break;
        }
    }
    Graphics out(dc);out.DrawImage(&back,0,0);EndPaint(hwnd,&ps);
}

void perform(int action) {
    switch(action){
        case ActHero:g_state.setScreen(Screen::Hero);break;
        case ActBrowse:g_state.setScreen(Screen::Browse);break;
        case ActQueue:g_state.setScreen(Screen::Queue);break;
        case ActRemote:g_state.setScreen(Screen::Remote);break;
        case ActPip:togglePip();return;
        case ActCenter:playPause();return;
        case ActPrev:changeTrack(false);return;
        case ActNext:changeTrack(true);return;
        case ActUp:g_state.setScreen(Screen::Browse);break;
        case ActDown:g_state.setScreen(Screen::Queue);break;
        case ActFile:openFile();return;
        default:break;
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
        case WM_PAINT:paint(hwnd);return 0;
        case WM_TIMER:InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);for(auto it=g_hits.rbegin();it!=g_hits.rend();++it)if(hit(it->rect,x,y)){perform(it->action);break;}return 0;}
        case WM_MOUSEWHEEL:{float v=g_state.volume()+((short)HIWORD(wp)>0?.04f:-.04f);g_state.setVolume(v);if(g_player)g_player->SetVolume(g_state.volume());InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        case WM_KEYDOWN:
            if(wp==VK_SPACE||wp==VK_RETURN){playPause();return 0;}
            if(wp==VK_LEFT){changeTrack(false);return 0;}
            if(wp==VK_RIGHT){changeTrack(true);return 0;}
            if(wp==VK_UP){g_state.setScreen(Screen::Browse);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp==VK_DOWN){g_state.setScreen(Screen::Queue);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='P'){togglePip();return 0;}
            if(wp=='R'){g_state.setScreen(Screen::Remote);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='B'){g_state.setScreen(Screen::Browse);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='Q'){g_state.setScreen(Screen::Queue);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            break;
        case WM_DESTROY:closeMedia();KillTimer(hwnd,kTimer);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);MFStartup(MF_VERSION);GdiplusStartupInput gd{};GdiplusStartup(&g_gdiplus,&gd,nullptr);
    g_state.loadEtherPlayLibrary();if(!g_state.library().empty()){g_state.selectLibraryTrack(0,true);refreshCover();}
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=WndProc;wc.lpszClassName=kClassName;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.style=CS_HREDRAW|CS_VREDRAW;
    RegisterClassExW(&wc);
    RECT r{0,0,kBigW,kBigH};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    g_hwnd=CreateWindowExW(0,kClassName,L"ETHERPLAYER v0.1 // EtherPlay",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!g_hwnd)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(g_hwnd,20,&dark,sizeof(dark));
    ShowWindow(g_hwnd,show);UpdateWindow(g_hwnd);SetTimer(g_hwnd,kTimer,50,nullptr);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    GdiplusShutdown(g_gdiplus);MFShutdown();CoUninitialize();return (int)msg.wParam;
}
