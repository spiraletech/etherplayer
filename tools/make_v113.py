from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v112.py'], check=True)
src = Path('generated/EtherPlayerWin112.cpp')
out = Path('generated/EtherPlayerWin113.cpp')
s = src.read_text(encoding='utf-8')


def rep(old, new):
    global s
    if old not in s:
        raise SystemExit('missing patch block:\n' + old[:260])
    s = s.replace(old, new, 1)


def replace_function(name, code):
    global s
    marker = f'void {name}('
    start = s.find(marker)
    if start < 0:
        raise SystemExit('missing function: ' + name)
    brace = s.find('{', start)
    if brace < 0:
        raise SystemExit('missing function brace: ' + name)
    depth = 0
    end = -1
    for i in range(brace, len(s)):
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end < 0:
        raise SystemExit('unterminated function: ' + name)
    s = s[:start] + code.rstrip() + s[end:]


rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V112_WINDOW";',
    'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V113_WINDOW";')
rep('text(g,L"v1.1.2  //  FINAL POLISH",R(31,48,350,20),11,muted(),FontStyleBold);',
    'text(g,L"v1.1.3  //  COVER ART PATCH",R(31,48,370,20),11,muted(),FontStyleBold);')

rep('''    ActSettingsPrev,
    ActSettingsNext,
    ActBrowseMenuBase = 1000,''', '''    ActSettingsPrev,
    ActSettingsNext,
    ActSettingsArtwork,
    ActBrowseMenuBase = 1000,''')

anchor = 'std::wstring readCoverSidecar(const std::wstring& track) {'
helper = r'''std::wstring readCoverSidecar(const std::wstring& track);
void refreshCover();

std::string utf8FromWide(const std::wstring& value) {
    if(value.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string out((size_t)n,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n,nullptr,nullptr);
    return out;
}

bool writeCoverSidecar(const std::wstring& track,const std::wstring& imagePath) {
    if(track.empty()||imagePath.empty())return false;
    std::error_code ec;
    if(!fs::exists(fs::path(imagePath),ec))return false;
    std::ofstream f(fs::path(track+L".ethercover"),std::ios::binary|std::ios::trunc);
    if(!f)return false;
    f<<"image="<<utf8FromWide(imagePath)<<"\n";
    return static_cast<bool>(f);
}

std::wstring coverPathForTrack(const std::wstring& track) {
    std::wstring path=readCoverSidecar(track);
    if(!path.empty())return path;
    fs::path audio(track);fs::path dir=audio.parent_path();const std::wstring stem=audio.stem().wstring();
    fs::path cands[]={dir/(stem+L".png"),dir/(stem+L".jpg"),dir/L"cover.png",dir/L"cover.jpg",dir/L"folder.jpg"};
    for(const auto& c:cands){std::error_code ec;if(fs::exists(c,ec))return c.wstring();}
    return {};
}

void drawSettingsArtwork(Graphics& g,const std::wstring& track,const RectF& outer) {
    roundRect(g,outer,20,Color(255,8,8,8),Color(92,115,98,45));
    RectF inner=R(outer.X+8,outer.Y+8,outer.Width-16,outer.Height-16);
    const std::wstring path=coverPathForTrack(track);
    if(path.empty()){
        text(g,L"NO ART",inner,12,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        return;
    }
    Image image(path.c_str());
    if(image.GetLastStatus()!=Ok){
        text(g,L"ART ERROR",inner,11,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        return;
    }
    const float iw=(float)image.GetWidth(),ih=(float)image.GetHeight();
    if(iw<=0||ih<=0)return;
    const float scale=std::min(inner.Width/iw,inner.Height/ih);
    const float w=iw*scale,h=ih*scale;
    g.DrawImage(&image,R(inner.X+(inner.Width-w)/2.f,inner.Y+(inner.Height-h)/2.f,w,h));
}

void chooseArtworkForMetaTrack() {
    if(g_metaTrackIndex>=g_state.library().size())return;
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner=g_hwnd;
    ofn.lpstrFile=file;
    ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0All Files\0*.*\0\0";
    ofn.nFilterIndex=1;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&ofn))return;
    const auto& track=g_state.library()[g_metaTrackIndex];
    if(!writeCoverSidecar(track.path,file))return;
    if(currentLibraryIndex()==g_metaTrackIndex)refreshCover();
    g_metaSavedUntil=GetTickCount64()+2200;
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

''' + anchor
rep(anchor, helper)

replace_function('drawSettings', r'''void drawSettings(Graphics& g) {
    ensureMetaControls();
    text(g,L"settings",R(62,88,360,58),48,warmWhite(),FontStyleBold);
    text(g,L"SONG METADATA + COVER ART",R(66,146,320,22),11,amber(),FontStyleBold);

    RectF info=R(62,184,430,440);
    roundRect(g,R(info.X+8,info.Y+10,info.Width,info.Height),28,Color(105,0,0,0));
    roundRect(g,info,28,Color(240,7,7,8),Color(86,110,89,42));
    if(g_metaTrackIndex<g_state.library().size()){
        const auto& t=g_state.library()[g_metaTrackIndex];
        drawSettingsArtwork(g,t.path,R(92,214,154,154));
        text(g,L"EDITING",R(270,216,150,18),10,amber(),FontStyleBold);
        text(g,t.title,R(270,242,190,70),23,warmWhite(),FontStyleBold);
        text(g,t.artist.empty()?L"unknown artist":t.artist,R(270,316,190,24),13,muted());

        RectF art=R(92,384,154,42);
        roundRect(g,art,16,Color(245,21,18,10),Color(135,177,143,49));
        text(g,L"CHOOSE ARTWORK",art,11,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(art,ActSettingsArtwork);

        text(g,L"FILE",R(270,384,60,18),9,amber(),FontStyleBold);
        text(g,fs::path(t.path).filename().wstring(),R(270,406,190,38),12,warmWhite(),FontStyleBold);
        text(g,t.path,R(92,456,340,78),9,muted());
        text(g,L"TRACK "+std::to_wstring(g_metaTrackIndex+1)+L" / "+std::to_wstring(g_state.library().size()),R(92,574,260,22),10,muted(),FontStyleBold);
    }else{
        text(g,L"NO MUSIC SELECTED",R(92,260,340,44),22,muted(),FontStyleBold);
    }

    RectF form=R(526,154,510,480);
    roundRect(g,R(form.X+7,form.Y+9,form.Width,form.Height),26,Color(100,0,0,0));
    roundRect(g,form,26,Color(237,7,7,8),Color(72,102,83,40));

    const wchar_t* labels[]={L"TITLE",L"ARTIST",L"ALBUM",L"GENRE"};
    const float lys[]={188,251,314,377};
    const float fys[]={205,268,331,394};
    for(int i=0;i<4;i++){
        text(g,labels[i],R(550,lys[i],160,18),10,muted(),FontStyleBold);
        roundRect(g,R(545,fys[i],480,44),10,Color(255,15,14,15),Color(64,108,89,43));
    }
    text(g,L"YEAR",R(550,440,80,18),10,muted(),FontStyleBold);
    text(g,L"TRACK",R(710,440,80,18),10,muted(),FontStyleBold);
    text(g,L"BPM",R(870,440,80,18),10,muted(),FontStyleBold);
    roundRect(g,R(545,457,145,44),10,Color(255,15,14,15),Color(64,108,89,43));
    roundRect(g,R(705,457,145,44),10,Color(255,15,14,15),Color(64,108,89,43));
    roundRect(g,R(865,457,160,44),10,Color(255,15,14,15),Color(64,108,89,43));
    text(g,L"COMMENT",R(550,510,120,18),10,muted(),FontStyleBold);
    roundRect(g,R(545,530,480,98),10,Color(255,15,14,15),Color(64,108,89,43));

    RectF prev=R(62,646,136,48);
    RectF next=R(212,646,136,48);
    roundRect(g,prev,18,Color(235,13,12,11),Color(80,105,84,43));
    roundRect(g,next,18,Color(235,13,12,11),Color(80,105,84,43));
    text(g,L"← PREVIOUS",prev,12,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"NEXT →",next,12,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(prev,ActSettingsPrev);addHit(next,ActSettingsNext);

    RectF save=R(796,650,226,50);
    roundRect(g,save,19,Color(255,38,30,8),Color(190,242,195,61),1.5f);
    text(g,L"SAVE SONG INFO",save,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(save,ActSettingsSave);
    if(GetTickCount64()<g_metaSavedUntil)
        text(g,L"SAVED",R(704,662,76,20),10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
}''')

rep('''        case ActSettingsSave:saveMetaControls();return;
        case ActSettingsPrev:''', '''        case ActSettingsSave:saveMetaControls();return;
        case ActSettingsArtwork:chooseArtworkForMetaTrack();return;
        case ActSettingsPrev:''')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8')
print(out)
