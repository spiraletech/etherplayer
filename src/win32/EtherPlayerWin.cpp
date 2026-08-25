#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <gdiplus.h>

#include "etherplayer/PlayerState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;
using etherplayer::PlayerState;
using etherplayer::Presentation;
using etherplayer::Screen;

namespace {

constexpr int kBigW = 1080;
constexpr int kBigH = 760;
constexpr int kPipCardW = 540;
constexpr int kPipCardH = 190;
constexpr int kPipStripW = 680;
constexpr int kPipStripH = 112;
constexpr UINT_PTR kTimer = 1;
constexpr UINT kTimerMs = 33;
constexpr wchar_t kClassName[] = L"ETHERPLAYER_V011_WINDOW";
constexpr size_t kFFTSize = 1024;
constexpr size_t kSpectrumBars = 72;
constexpr float kPi = 3.14159265358979323846f;

HWND g_hwnd{};
ULONG_PTR g_gdiplus{};
PlayerState g_state;
IMFPMediaPlayer* g_player{};
IMFPMediaItem* g_item{};
bool g_playing = false;
bool g_paused = false;
ULONGLONG g_duration = 0;
std::unique_ptr<Image> g_cover;
std::wstring g_coverPath;

enum class PipStyle { Card, Strip };
PipStyle g_pipStyle = PipStyle::Card;
bool g_browseSongs = false;
size_t g_browseOffset = 0;
int g_hoverAction = 0;
int g_pressedAction = 0;
ULONGLONG g_pressStarted = 0;
ULONGLONG g_lastHoldStep = 0;

struct AudioFrame {
    std::array<float, kSpectrumBars> spectrum{};
};
std::vector<AudioFrame> g_audioFrames;
UINT32 g_analysisRate = 44100;
bool g_analysisReady = false;
std::array<float, kSpectrumBars> g_spectrumSmooth{};
std::array<float, kSpectrumBars> g_spectrumPeak{};

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
    ActPipMode,
    ActCenter,
    ActPrev,
    ActNext,
    ActUp,
    ActDown,
    ActFile,
    ActBrowseBack,
    ActBrowseMenuBase = 1000,
    ActBrowseTrackBase = 2000,
    ActQueueTrackBase = 3000
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
Color panel(BYTE a=245) { return Color(a, 7, 7, 8); }

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
    StringFormat fmt;
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(value.c_str(),-1,&font,rect,&fmt,&brush);
}

std::wstring currentTitle() {
    const auto* t=g_state.currentTrack(); return t?t->title:L"nothing playing";
}
std::wstring currentArtist() {
    const auto* t=g_state.currentTrack(); return t?t->artist:L"open music";
}

std::wstring readCoverSidecar(const std::wstring& track) {
    std::ifstream f(fs::path(track+L".ethercover"),std::ios::binary);
    std::string line;
    while(std::getline(f,line)) {
        auto eq=line.find('=');
        if(eq==std::string::npos) continue;
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
    if(g_coverPath.empty()) {
        fs::path audio(t->path);
        fs::path dir=audio.parent_path();
        const std::wstring stem=audio.stem().wstring();
        fs::path cands[]={dir/(stem+L".png"),dir/(stem+L".jpg"),dir/L"cover.png",dir/L"cover.jpg",dir/L"folder.jpg"};
        for(const auto& c:cands) {
            std::error_code ec;
            if(fs::exists(c,ec)){g_coverPath=c.wstring();break;}
        }
    }
    if(g_coverPath.empty()) return;
    auto img=std::make_unique<Image>(g_coverPath.c_str());
    if(img->GetLastStatus()==Ok) g_cover=std::move(img);
}

void drawCover(Graphics& g,const RectF& outer) {
    RectF shadow=R(outer.X+7,outer.Y+9,outer.Width,outer.Height);
    roundRect(g,shadow,24,Color(115,0,0,0));
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
    SolidBrush veil(Color(20,0,0,0));g.FillRectangle(&veil,dst);
}

void fft(std::array<std::complex<float>,kFFTSize>& a) {
    for(size_t i=1,j=0;i<kFFTSize;i++){
        size_t bit=kFFTSize>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(size_t len=2;len<=kFFTSize;len<<=1){
        const float ang=-2.f*kPi/(float)len;
        const std::complex<float> wlen(std::cos(ang),std::sin(ang));
        for(size_t i=0;i<kFFTSize;i+=len){
            std::complex<float>w(1.f,0.f);
            for(size_t j=0;j<len/2;j++){
                auto u=a[i+j],v=a[i+j+len/2]*w;
                a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;
            }
        }
    }
}

AudioFrame analyzeWindow(const std::array<float,kFFTSize>& samples,UINT32 rate) {
    AudioFrame frame{};
    std::array<std::complex<float>,kFFTSize> bins{};
    for(size_t i=0;i<kFFTSize;i++){
        const float hann=.5f-.5f*std::cos(2.f*kPi*(float)i/(float)(kFFTSize-1));
        bins[i]=std::complex<float>(samples[i]*hann,0.f);
    }
    fft(bins);
    auto averageBand=[&](float lo,float hi){
        size_t first=(size_t)std::clamp((int)(lo*(float)kFFTSize/(float)rate),1,(int)kFFTSize/2-1);
        size_t last=(size_t)std::clamp((int)(hi*(float)kFFTSize/(float)rate),(int)first+1,(int)kFFTSize/2);
        double total=0.0;
        for(size_t i=first;i<last;i++) total+=std::abs(bins[i]);
        return (float)(total/std::max<size_t>(1,last-first));
    };
    const float lowHz=35.f, highHz=18000.f, ratio=highHz/lowHz;
    for(size_t band=0;band<kSpectrumBars;band++){
        const float t0=(float)band/(float)kSpectrumBars;
        const float t1=(float)(band+1)/(float)kSpectrumBars;
        frame.spectrum[band]=averageBand(lowHz*std::pow(ratio,t0),lowHz*std::pow(ratio,t1));
    }
    return frame;
}

void normalizeAnalysis() {
    std::array<float,kSpectrumBars> maxima{};
    maxima.fill(.0001f);
    for(const auto& frame:g_audioFrames)
        for(size_t i=0;i<kSpectrumBars;i++) maxima[i]=std::max(maxima[i],frame.spectrum[i]);
    for(auto& frame:g_audioFrames)
        for(size_t i=0;i<kSpectrumBars;i++)
            frame.spectrum[i]=std::clamp(std::pow(frame.spectrum[i]/maxima[i],.42f),0.f,1.f);
    g_spectrumSmooth.fill(0.f);
    g_spectrumPeak.fill(0.f);
}

bool analyzeTrackAudio(const std::wstring& path) {
    g_audioFrames.clear(); g_analysisReady=false;
    IMFSourceReader* reader=nullptr;
    IMFMediaType* partial=nullptr;
    IMFMediaType* actual=nullptr;
    HRESULT hr=MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader);
    if(FAILED(hr)||!reader)return false;
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
    hr=MFCreateMediaType(&partial);
    if(SUCCEEDED(hr)){
        partial->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
        partial->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
        hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,partial);
    }
    if(FAILED(hr)){release(partial);release(reader);return false;}
    hr=reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&actual);
    UINT32 channels=0,rate=0,bits=0;
    if(SUCCEEDED(hr)&&actual){
        actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels);
        actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);
        actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bits);
    }
    if(channels==0||rate==0||bits!=16){release(actual);release(partial);release(reader);return false;}
    g_analysisRate=rate;
    std::array<float,kFFTSize> window{};
    size_t fill=0;
    bool done=false;
    while(!done){
        DWORD stream=0,flags=0;LONGLONG ts=0;IMFSample* sample=nullptr;
        hr=reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,&stream,&flags,&ts,&sample);
        if(FAILED(hr)){release(sample);break;}
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)done=true;
        if(sample){
            IMFMediaBuffer* buffer=nullptr;
            if(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))&&buffer){
                BYTE* data=nullptr;DWORD maxLen=0,curLen=0;
                if(SUCCEEDED(buffer->Lock(&data,&maxLen,&curLen))&&data){
                    const int16_t* pcm=reinterpret_cast<const int16_t*>(data);
                    const size_t frames=curLen/(sizeof(int16_t)*channels);
                    for(size_t i=0;i<frames;i++){
                        float mono=0.f;
                        for(UINT32 c=0;c<channels;c++) mono+=(float)pcm[i*channels+c]/32768.f;
                        mono/=(float)channels;
                        window[fill++]=mono;
                        if(fill==kFFTSize){
                            g_audioFrames.push_back(analyzeWindow(window,rate));
                            fill=0;
                            if(g_audioFrames.size()>30000){done=true;break;}
                        }
                    }
                    buffer->Unlock();
                }
                release(buffer);
            }
            release(sample);
        }
    }
    release(actual);release(partial);release(reader);
    if(g_audioFrames.empty())return false;
    normalizeAnalysis();g_analysisReady=true;return true;
}

void closeMedia() {
    if(g_player){g_player->Stop();g_player->Shutdown();}
    release(g_item); release(g_player);
    g_playing=false;g_paused=false;g_duration=0;
}

ULONGLONG playbackPosition() {
    if(!g_player)return 0;
    PROPVARIANT var{};PropVariantInit(&var);
    ULONGLONG value=0;
    if(SUCCEEDED(g_player->GetPosition(MFP_POSITIONTYPE_100NS,&var))){
        if(var.vt==VT_I8)value=(ULONGLONG)std::max<LONGLONG>(0,var.hVal.QuadPart);
        else if(var.vt==VT_UI8)value=var.uhVal.QuadPart;
    }
    PropVariantClear(&var);
    return value;
}

void seekAbsolute(ULONGLONG pos) {
    if(!g_player||!g_duration)return;
    PROPVARIANT p{};PropVariantInit(&p);p.vt=VT_I8;
    p.hVal.QuadPart=(LONGLONG)std::min(pos,g_duration);
    g_player->SetPosition(MFP_POSITIONTYPE_100NS,&p);PropVariantClear(&p);
}

void seekRelative(double seconds) {
    LONGLONG pos=(LONGLONG)playbackPosition()+(LONGLONG)(seconds*10000000.0);
    pos=std::max<LONGLONG>(0,std::min<LONGLONG>(pos,(LONGLONG)g_duration));
    seekAbsolute((ULONGLONG)pos);
}

float playbackProgress() {
    return g_duration?std::clamp((float)((double)playbackPosition()/(double)g_duration),0.f,1.f):0.f;
}

AudioFrame currentAudio() {
    if(!g_analysisReady||g_audioFrames.empty())return {};
    const double seconds=(double)playbackPosition()/10000000.0;
    size_t idx=(size_t)(seconds*(double)g_analysisRate/(double)kFFTSize);
    if(idx>=g_audioFrames.size())idx=g_audioFrames.size()-1;
    AudioFrame frame=g_audioFrames[idx];
    if(!g_playing && !g_paused)for(auto& value:frame.spectrum)value*=.12f;
    return frame;
}

void drawAnalyzer(Graphics& g,const RectF& r,bool mini=false) {
    const AudioFrame frame=currentAudio();
    Pen grid(Color(mini?35:52,120,96,30),1.f);
    const int columns=mini?5:8;
    const int rows=mini?3:5;
    for(int i=0;i<=columns;i++){float x=r.X+r.Width*(float)i/(float)columns;g.DrawLine(&grid,x,r.Y,x,r.GetBottom());}
    for(int i=0;i<=rows;i++){float y=r.Y+r.Height*(float)i/(float)rows;g.DrawLine(&grid,r.X,y,r.GetRight(),y);}
    const size_t bars=mini?48:kSpectrumBars;
    const float gap=mini?2.f:3.f;
    const float bw=(r.Width-gap*(float)(bars-1))/(float)bars;
    SolidBrush bar(amber());
    SolidBrush peak(Color(255,255,232,145));
    for(size_t i=0;i<bars;i++){
        const size_t source=(i*kSpectrumBars)/bars;
        const float target=frame.spectrum[source];
        const float attack=target>g_spectrumSmooth[source] ? .50f : .14f;
        g_spectrumSmooth[source]+=(target-g_spectrumSmooth[source])*attack;
        g_spectrumPeak[source]=std::max(g_spectrumSmooth[source],g_spectrumPeak[source]-.018f);
        const float shaped=std::pow(std::clamp(g_spectrumSmooth[source],0.f,1.f),.80f);
        const float h=std::max(2.f,shaped*r.Height);
        const float x=r.X+(bw+gap)*(float)i;
        g.FillRectangle(&bar,x,r.GetBottom()-h,bw,h);
        if(!mini){
            const float py=r.GetBottom()-std::pow(std::clamp(g_spectrumPeak[source],0.f,1.f),.80f)*r.Height;
            g.FillRectangle(&peak,x,py,bw,2.f);
        }
    }
}

bool openCurrentMedia() {
    const auto* t=g_state.currentTrack(); if(!t)return false;
    closeMedia();
    if(FAILED(MFPCreateMediaPlayer(nullptr,FALSE,0,nullptr,g_hwnd,&g_player))||!g_player)return false;
    if(FAILED(g_player->CreateMediaItemFromURL(t->path.c_str(),TRUE,0,&g_item))||!g_item){closeMedia();return false;}
    PROPVARIANT d{};PropVariantInit(&d);
    if(SUCCEEDED(g_item->GetDuration(MFP_POSITIONTYPE_100NS,&d))) {
        if(d.vt==VT_I8)g_duration=(ULONGLONG)d.hVal.QuadPart;
        else if(d.vt==VT_UI8)g_duration=d.uhVal.QuadPart;
    }
    PropVariantClear(&d);
    if(FAILED(g_player->SetMediaItem(g_item))){closeMedia();return false;}
    g_player->SetVolume(g_state.volume());
    refreshCover();
    analyzeTrackAudio(t->path);
    return true;
}

void playPause() {
    if(!g_state.currentTrack()) {
        if(!g_state.library().empty()){g_state.selectLibraryTrack(0,true);openCurrentMedia();}
        else return;
    }
    if(!g_player && !openCurrentMedia())return;
    if(g_playing){g_player->Pause();g_playing=false;g_paused=true;}
    else{g_player->Play();g_playing=true;g_paused=false;}
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void playSelectedTrack(size_t libraryIndex,bool enqueueRest=true) {
    if(!g_state.selectLibraryTrack(libraryIndex,enqueueRest))return;
    if(openCurrentMedia()&&g_player){g_player->Play();g_playing=true;g_paused=false;}
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void nextTrack() {
    if(!g_state.next())return;
    if(openCurrentMedia()&&g_player){g_player->Play();g_playing=true;g_paused=false;}
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void previousTrack() {
    if(!g_state.previous())return;
    if(openCurrentMedia()&&g_player){g_player->Play();g_playing=true;g_paused=false;}
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void previousTap() {
    if(playbackPosition()>30000000ULL)seekAbsolute(0);
    else previousTrack();
}

std::vector<std::wstring> chooseAudioFiles() {
    wchar_t buffer[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_hwnd;ofn.lpstrFile=buffer;ofn.nMaxFile=32768;
    ofn.lpstrFilter=L"Audio Files (*.mp3;*.wav)\0*.mp3;*.wav\0MP3 Files (*.mp3)\0*.mp3\0WAV Files (*.wav)\0*.wav\0\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_ALLOWMULTISELECT;
    if(!GetOpenFileNameW(&ofn))return {};
    std::vector<std::wstring> paths;
    const wchar_t* first=buffer;
    const wchar_t* next=first+wcslen(first)+1;
    if(*next==L'\0') {paths.emplace_back(first);return paths;}
    fs::path dir(first);
    while(*next){paths.push_back((dir/next).wstring());next+=wcslen(next)+1;}
    return paths;
}

void ingestFiles(const std::vector<std::wstring>& files) {
    if(files.empty())return;
    std::vector<size_t> added;
    for(const auto& p:files){
        const size_t idx=g_state.addTrackPath(p);
        if(idx!=(size_t)-1)added.push_back(idx);
    }
    if(added.empty())return;
    g_state.selectLibraryTrack(added.front(),false);
    for(size_t i=1;i<added.size();i++)g_state.addToQueue(added[i]);
    if(openCurrentMedia()&&g_player){g_player->Play();g_playing=true;g_paused=false;}
    g_state.saveEtherPlayLibrary();
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void openFile() { ingestFiles(chooseAudioFiles()); }

void togglePip() {
    g_state.togglePip();
    const bool pip=g_state.presentation()==Presentation::Pip;
    if(pip){
        g_pipStyle=PipStyle::Card;
        SetWindowPos(g_hwnd,HWND_TOPMOST,0,0,kPipCardW,kPipCardH,SWP_NOMOVE|SWP_SHOWWINDOW);
    } else {
        SetWindowPos(g_hwnd,HWND_NOTOPMOST,0,0,kBigW,kBigH,SWP_NOMOVE|SWP_SHOWWINDOW);
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void togglePipStyle() {
    if(g_state.presentation()!=Presentation::Pip)return;
    g_pipStyle=g_pipStyle==PipStyle::Card?PipStyle::Strip:PipStyle::Card;
    const bool strip=g_pipStyle==PipStyle::Strip;
    SetWindowPos(g_hwnd,HWND_TOPMOST,0,0,strip?kPipStripW:kPipCardW,strip?kPipStripH:kPipCardH,SWP_NOMOVE|SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void drawControlPad(Graphics& g,float cx,float cy,float scale=1.f,bool labels=false) {
    const float ring=74.f*scale;
    SolidBrush outer(Color(255,10,10,10));g.FillEllipse(&outer,cx-ring,cy-ring,ring*2,ring*2);
    Pen edge(Color(150,127,103,48),1.2f);g.DrawEllipse(&edge,cx-ring,cy-ring,ring*2,ring*2);
    Pen guide(Color(90,242,195,61),1.f);g.DrawEllipse(&guide,cx-ring-8*scale,cy-ring-8*scale,(ring+8*scale)*2,(ring+8*scale)*2);
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
    if(labels){
        text(g,L"HOME / UP",R(cx-90*scale,cy-ring-52*scale,180*scale,28*scale),13*scale,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        text(g,L"BACK / PREVIOUS\nREWIND",R(cx-ring-190*scale,cy-35*scale,170*scale,70*scale),12*scale,warmWhite(),FontStyleBold,StringAlignmentFar,StringAlignmentCenter);
        text(g,L"NEXT / FAST-FORWARD\nQUICK ACTION",R(cx+ring+20*scale,cy-35*scale,200*scale,70*scale),12*scale,warmWhite(),FontStyleBold,StringAlignmentNear,StringAlignmentCenter);
        text(g,L"QUEUE / SEEK\nDOWN",R(cx-100*scale,cy+ring+18*scale,200*scale,58*scale),12*scale,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    }
}

void drawHeader(Graphics& g) {
    text(g,L"ETHERPLAYER",R(30,18,260,32),26,warmWhite(),FontStyleBold);
    text(g,L"v0.11  //  BEHAVIOR PASS",R(31,48,310,20),11,muted(),FontStyleBold);
    struct H{const wchar_t* label;Screen screen;float x;int action;};
    H tabs[]={{L"hero",Screen::Hero,480,ActHero},{L"browse",Screen::Browse,590,ActBrowse},{L"queue",Screen::Queue,715,ActQueue},{L"remote",Screen::Remote,825,ActRemote}};
    for(auto& h:tabs){
        bool on=g_state.screen()==h.screen;RectF rr=R(h.x,18,105,42);
        if(on)roundRect(g,rr,18,Color(235,28,24,12),Color(110,126,101,43));
        text(g,h.label,rr,14,on?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(rr,h.action);
    }
    RectF pip=R(950,18,90,42);roundRect(g,pip,18,Color(220,12,12,12),Color(70,90,75,45));
    text(g,L"pip",pip,14,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pip,ActPip);
}

void drawHero(Graphics& g) {
    text(g,L"NOW PLAYING",R(120,90,220,24),13,amber(),FontStyleBold);
    RectF art=R(375,108,330,330);drawCover(g,art);
    text(g,currentTitle(),R(210,450,660,46),31,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(260,494,560,26),15,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(260,532,560,88),false);
    text(g,g_analysisReady?L"72-BAND AUDIO REACTIVE":L"ANALYZER IDLE",R(260,621,560,18),10,Color(255,133,117,73),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    drawControlPad(g,540,690,.58f);
    RectF file=R(145,654,130,50);roundRect(g,file,20,Color(240,12,12,12),Color(85,80,72,52));text(g,L"ADD MUSIC",file,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(file,ActFile);
}

void drawBrowseRoot(Graphics& g) {
    const wchar_t* items[]={L"music",L"playlists",L"artists",L"albums",L"songs",L"queue"};
    for(int i=0;i<6;i++){
        RectF row=R(70,102+i*68,430,62);
        const int action=ActBrowseMenuBase+i;
        const bool hot=g_hoverAction==action;
        if(hot)roundRect(g,row,18,Color(190,35,28,8),Color(105,173,137,45));
        text(g,items[i],R(82,row.Y,row.Width-20,row.Height),i==0?58:44,hot||i==0?amber():warmWhite(),FontStyleRegular,StringAlignmentNear,StringAlignmentCenter);
        addHit(row,action);
    }
    RectF preview=R(650,145,330,415);roundRect(g,preview,26,panel(),Color(80,100,83,45));
    text(g,L"NOW PLAYING",R(680,170,240,22),12,amber(),FontStyleBold);
    drawCover(g,R(730,210,170,170));
    text(g,currentTitle(),R(675,394,280,42),20,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(675,430,280,22),12,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(690,466,250,70),true);
    text(g,L"Mouse hover + click  •  wheel scroll  •  right-click songs = Play Next",R(78,668,850,26),13,muted());
}

void drawBrowseSongs(Graphics& g) {
    RectF back=R(64,100,120,40);roundRect(g,back,16,Color(220,12,12,12),Color(70,90,75,45));
    text(g,L"← BACK",back,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(back,ActBrowseBack);
    text(g,L"songs",R(64,148,450,70),54,warmWhite(),FontStyleRegular);
    text(g,std::to_wstring(g_state.library().size())+L" TRACKS",R(68,215,250,24),12,muted(),FontStyleBold);
    const size_t maxRows=7;
    float y=252.f;
    for(size_t row=0;row<maxRows;row++){
        const size_t idx=g_browseOffset+row;
        if(idx>=g_state.library().size())break;
        const auto& t=g_state.library()[idx];
        const int action=ActBrowseTrackBase+(int)idx;
        const bool hot=g_hoverAction==action;
        RectF rr=R(64,y,700,56);
        roundRect(g,rr,15,hot?Color(225,45,35,8):Color(210,8,8,9),hot?Color(150,214,169,51):Color(55,75,66,52));
        text(g,t.title,R(84,y+7,510,24),17,hot?amber():warmWhite(),FontStyleBold);
        text(g,t.artist,R(84,y+31,510,18),12,muted());
        text(g,L"›",R(715,y,30,56),22,hot?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(rr,action);
        y+=64.f;
    }
    RectF add=R(808,270,200,54);roundRect(g,add,20,Color(235,20,17,10),Color(110,120,98,48));
    text(g,L"ADD MUSIC",add,14,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(add,ActFile);
    text(g,L"Click = play now\nRight-click = Play Next\nMouse wheel = scroll library",R(808,345,210,100),14,muted(),FontStyleRegular);
}

void drawBrowse(Graphics& g) {
    if(g_browseSongs)drawBrowseSongs(g);else drawBrowseRoot(g);
}

void drawQueue(Graphics& g) {
    text(g,L"UP NEXT",R(65,102,300,48),40,warmWhite(),FontStyleBold);
    const auto& q=g_state.queue();
    if(q.empty()) { text(g,L"queue is empty",R(65,180,400,40),22,muted()); return; }
    float y=170;
    for(size_t i=0;i<q.size()&&i<8;i++) {
        const auto idx=q[i]; if(idx>=g_state.library().size())continue;
        const auto& t=g_state.library()[idx];
        const int action=ActQueueTrackBase+(int)i;
        const bool current=i==g_state.queueIndex();
        const bool hot=g_hoverAction==action;
        RectF row=R(65,y,650,58);
        roundRect(g,row,16,current?Color(225,42,33,8):(hot?Color(210,25,20,9):Color(220,8,8,9)),current?Color(150,205,166,53):(hot?Color(105,120,98,48):Color(55,75,66,52)));
        text(g,std::to_wstring(i+1),R(82,y,38,58),14,current?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        text(g,t.title,R(130,y+8,430,24),17,hot||current?amber():warmWhite(),FontStyleBold);
        text(g,t.artist,R(130,y+31,430,20),12,muted());
        addHit(row,action);y+=66;
    }
    text(g,L"Click any row to jump there. Right-click a Browse song to insert PLAY NEXT.",R(65,700,690,28),13,amber());
    drawControlPad(g,900,390,.72f);
}

void drawRemote(Graphics& g) {
    text(g,L"REMOTE",R(62,92,300,50),42,warmWhite(),FontStyleBold);
    text(g,L"CONTROL DECK // SAME PLAYER STATE",R(64,142,470,24),13,amber(),FontStyleBold);
    RectF mini=R(64,198,350,185);roundRect(g,mini,26,panel(),Color(80,100,83,45));
    drawCover(g,R(84,218,140,140));
    text(g,currentTitle(),R(244,226,150,48),20,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(244,278,150,22),12,muted());
    drawAnalyzer(g,R(244,315,145,40),true);

    const float cx=700.f,cy=405.f;
    drawControlPad(g,cx,cy,1.18f,true);
    roundRect(g,R(565,610,270,74),18,Color(230,10,9,8),Color(90,120,96,46));
    text(g,L"RING / SCROLL AREA",R(580,620,240,22),14,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"VOLUME  •  BROWSE  •  SCRUB",R(580,647,240,20),12,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"LOCAL REMOTE v0.11",R(64,645,300,24),12,muted(),FontStyleBold);
    text(g,L"LAN / PHONE TRANSPORT LATER — UI CONTRACT FIRST",R(64,672,440,22),11,Color(255,108,101,87));
}

void drawPipCard(Graphics& g) {
    RectF art=R(16,18,130,130);drawCover(g,art);
    text(g,currentTitle(),R(166,22,250,30),19,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(166,53,250,20),12,muted());
    drawAnalyzer(g,R(166,80,250,48),true);
    const float p=playbackProgress();
    roundRect(g,R(166,140,250,5),2,Color(255,45,42,36));
    roundRect(g,R(166,140,std::max(5.f,250.f*p),5),2,amber());
    RectF pp=R(438,48,50,50);roundRect(g,pp,24,Color(255,17,16,13),Color(160,202,161,51));
    text(g,g_playing?L"Ⅱ":L"▶",pp,18,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pp,ActCenter);
    RectF mode=R(426,108,84,28);text(g,L"STRIP",mode,10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(426,138,84,28);text(g,L"EXPAND",exp,10,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}

void drawPipStrip(Graphics& g) {
    text(g,currentTitle(),R(18,14,205,28),17,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(18,42,205,18),11,muted());
    drawAnalyzer(g,R(235,18,280,56),true);
    RectF prev=R(528,18,38,38);roundRect(g,prev,18,Color(245,13,12,10),Color(80,95,77,44));
    text(g,L"◀",prev,14,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(prev,ActPrev);
    RectF pp=R(570,12,50,50);roundRect(g,pp,24,Color(255,28,23,9),Color(160,202,161,51));
    text(g,g_playing?L"Ⅱ":L"▶",pp,17,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pp,ActCenter);
    RectF next=R(624,18,38,38);roundRect(g,next,18,Color(245,13,12,10),Color(80,95,77,44));
    text(g,L"▶",next,14,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(next,ActNext);
    const float p=playbackProgress();
    roundRect(g,R(18,78,497,4),2,Color(255,45,42,36));
    roundRect(g,R(18,78,std::max(4.f,497.f*p),4),2,amber());
    RectF mode=R(530,72,64,25);text(g,L"CARD",mode,9,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(598,72,66,25);text(g,L"EXPAND",exp,9,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}

void drawPip(Graphics& g) {
    if(g_pipStyle==PipStyle::Card)drawPipCard(g);else drawPipStrip(g);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
    Bitmap back(std::max(1L,rc.right),std::max(1L,rc.bottom),PixelFormat32bppPARGB);
    Graphics g(&back);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    SolidBrush bg(Color(255,2,2,3));g.FillRectangle(&bg,0,0,rc.right,rc.bottom);g_hits.clear();
    if(g_state.presentation()==Presentation::Pip)drawPip(g);
    else {
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
    if(action>=ActBrowseTrackBase && action<ActQueueTrackBase){
        const size_t idx=(size_t)(action-ActBrowseTrackBase);
        playSelectedTrack(idx,true);g_state.setScreen(Screen::Hero);return;
    }
    if(action>=ActQueueTrackBase){
        const size_t qi=(size_t)(action-ActQueueTrackBase);
        if(g_state.playQueueIndex(qi) && openCurrentMedia() && g_player){g_player->Play();g_playing=true;g_paused=false;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }
    if(action>=ActBrowseMenuBase && action<ActBrowseMenuBase+6){
        const int idx=action-ActBrowseMenuBase;
        if(idx==5){g_state.setScreen(Screen::Queue);}
        else {g_browseSongs=true;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }
    switch(action){
        case ActHero:g_state.setScreen(Screen::Hero);break;
        case ActBrowse:g_state.setScreen(Screen::Browse);g_browseSongs=false;break;
        case ActQueue:g_state.setScreen(Screen::Queue);break;
        case ActRemote:g_state.setScreen(Screen::Remote);break;
        case ActPip:togglePip();return;
        case ActPipMode:togglePipStyle();return;
        case ActCenter:playPause();return;
        case ActPrev:previousTap();return;
        case ActNext:nextTrack();return;
        case ActUp:g_state.setScreen(Screen::Browse);g_browseSongs=false;break;
        case ActDown:g_state.setScreen(Screen::Queue);break;
        case ActFile:openFile();return;
        case ActBrowseBack:g_browseSongs=false;break;
        default:break;
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

int actionAt(int x,int y) {
    for(auto it=g_hits.rbegin();it!=g_hits.rend();++it)if(hit(it->rect,x,y))return it->action;
    return 0;
}

void updateHover(int x,int y) {
    const int action=actionAt(x,y);
    if(action!=g_hoverAction){
        g_hoverAction=action;
        InvalidateRect(g_hwnd,nullptr,FALSE);
    }
    SetCursor(LoadCursor(nullptr,action?IDC_HAND:IDC_ARROW));
}

void quickActionAt(int action) {
    if(action>=ActBrowseTrackBase && action<ActQueueTrackBase){
        const size_t idx=(size_t)(action-ActBrowseTrackBase);
        g_state.playNext(idx);
        InvalidateRect(g_hwnd,nullptr,FALSE);
    }
}

void holdStep() {
    if(!g_pressedAction)return;
    const ULONGLONG now=GetTickCount64();
    if(now-g_pressStarted<430 || now-g_lastHoldStep<120)return;
    g_lastHoldStep=now;
    if(g_pressedAction==ActPrev)seekRelative(-4.0);
    else if(g_pressedAction==ActNext)seekRelative(4.0);
}

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
        case WM_PAINT:paint(hwnd);return 0;
        case WM_TIMER:holdStep();InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_MOUSEMOVE:updateHover(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;
        case WM_MOUSELEAVE:g_hoverAction=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:{
            const int action=actionAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            g_pressedAction=action;g_pressStarted=GetTickCount64();g_lastHoldStep=0;
            SetCapture(hwnd);
            if(action!=ActPrev && action!=ActNext)perform(action);
            return 0;
        }
        case WM_LBUTTONUP:{
            ReleaseCapture();
            const int action=g_pressedAction;
            const ULONGLONG held=GetTickCount64()-g_pressStarted;
            g_pressedAction=0;
            if((action==ActPrev||action==ActNext)&&held<430)perform(action);
            return 0;
        }
        case WM_LBUTTONDBLCLK:{
            const int action=actionAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            if(action==ActPrev){previousTrack();return 0;}
            if(action>=ActBrowseTrackBase&&action<ActQueueTrackBase){perform(action);return 0;}
            break;
        }
        case WM_RBUTTONDOWN:quickActionAt(actionAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)));return 0;
        case WM_MOUSEWHEEL:{
            const int delta=(short)HIWORD(wp);
            if(g_state.screen()==Screen::Browse && g_browseSongs && g_state.presentation()==Presentation::BigScreen){
                const size_t count=g_state.library().size();
                if(delta<0 && g_browseOffset+7<count)g_browseOffset++;
                if(delta>0 && g_browseOffset>0)g_browseOffset--;
            } else {
                g_state.setVolume(g_state.volume()+(delta>0 ? .04f : -.04f));
                if(g_player)g_player->SetVolume(g_state.volume());
            }
            InvalidateRect(hwnd,nullptr,FALSE);return 0;
        }
        case WM_DROPFILES:{
            HDROP drop=(HDROP)wp;
            const UINT count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
            std::vector<std::wstring> paths;
            for(UINT i=0;i<count;i++){
                const UINT n=DragQueryFileW(drop,i,nullptr,0);
                std::wstring path((size_t)n+1,L'\0');
                DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);
                paths.push_back(std::move(path));
            }
            DragFinish(drop);ingestFiles(paths);return 0;
        }
        case WM_KEYDOWN:
            if(wp==VK_SPACE){playPause();return 0;}
            if(wp==VK_ESCAPE&&g_state.screen()==Screen::Browse&&g_browseSongs){g_browseSongs=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp==VK_RETURN&&g_state.screen()==Screen::Browse&&g_browseSongs&&g_hoverAction>=ActBrowseTrackBase){perform(g_hoverAction);return 0;}
            if(wp==VK_LEFT){previousTap();return 0;}
            if(wp==VK_RIGHT){nextTrack();return 0;}
            if(wp==VK_UP&&g_state.screen()==Screen::Browse&&g_browseSongs){if(g_browseOffset>0)g_browseOffset--;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp==VK_DOWN&&g_state.screen()==Screen::Browse&&g_browseSongs){if(g_browseOffset+7<g_state.library().size())g_browseOffset++;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='P'){togglePip();return 0;}
            if(wp=='R'){g_state.setScreen(Screen::Remote);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='B'){g_state.setScreen(Screen::Browse);g_browseSongs=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='Q'){g_state.setScreen(Screen::Queue);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='O'){openFile();return 0;}
            break;
        case WM_DESTROY:closeMedia();KillTimer(hwnd,kTimer);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);
    GdiplusStartupInput gd{};GdiplusStartup(&g_gdiplus,&gd,nullptr);
    g_state.loadEtherPlayLibrary();
    if(!g_state.library().empty()){g_state.selectLibraryTrack(0,true);refreshCover();}

    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance=instance;wc.lpfnWndProc=WndProc;wc.lpszClassName=kClassName;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.style=CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;
    RegisterClassExW(&wc);

    RECT r{0,0,kBigW,kBigH};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.11 // EtherPlay",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!g_hwnd)return 1;
    DragAcceptFiles(g_hwnd,TRUE);
    BOOL dark=TRUE;DwmSetWindowAttribute(g_hwnd,20,&dark,sizeof(dark));
    ShowWindow(g_hwnd,show);UpdateWindow(g_hwnd);SetTimer(g_hwnd,kTimer,kTimerMs,nullptr);

    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    GdiplusShutdown(g_gdiplus);MFShutdown();CoUninitialize();return (int)msg.wParam;
}
