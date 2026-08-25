from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v101.py'], check=True)
src = Path('generated/EtherPlayerWin101.cpp')
out = Path('generated/EtherPlayerWin110.cpp')
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


rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V101_WINDOW";',
    'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V110_WINDOW";')
rep('text(g,L"v1.0.1  //  POLISHED",R(31,48,350,20),11,muted(),FontStyleBold);',
    'text(g,L"v1.1  //  LIBRARY INTELLIGENCE",R(31,48,390,20),11,muted(),FontStyleBold);')
rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.0.1 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.1 // EtherPlay",WS_OVERLAPPEDWINDOW,')

# Replace the old songs-only browse flag with proper library views + metadata editor state.
rep('''bool g_browseSongs = false;
size_t g_browseOffset = 0;''', '''enum class BrowseView { Root, Music, Artists, ArtistTracks, Albums, AlbumTracks };
BrowseView g_browseView = BrowseView::Root;
size_t g_browseOffset = 0;
std::wstring g_browseFilter;

constexpr size_t kNoTrack = static_cast<size_t>(-1);
size_t g_metaTrackIndex = kNoTrack;
std::array<HWND,8> g_metaEdits{};
HFONT g_metaFont{};
HBRUSH g_metaBrush{};
bool g_metaControlsShown = false;
ULONGLONG g_metaSavedUntil = 0;''')

# New screen/actions. Music is the canonical track list; Songs disappears from visible navigation.
rep('''    ActPipHome,
    ActPipPlaylist,
    ActBrowseMenuBase = 1000,
    ActBrowseTrackBase = 2000,
    ActQueueTrackBase = 3000''', '''    ActPipHome,
    ActPipPlaylist,
    ActSettings,
    ActSettingsSave,
    ActSettingsPrev,
    ActSettingsNext,
    ActBrowseMenuBase = 1000,
    ActBrowseTrackBase = 2000,
    ActQueueTrackBase = 3000,
    ActArtistBase = 4000,
    ActAlbumBase = 5000,
    ActFilteredTrackBase = 6000''')

# Metadata editor + metadata-driven artist/album grouping. Uses the exact .ethermeta sidecar contract from EtherPlay Song Lab.
anchor = 'std::wstring readCoverSidecar(const std::wstring& track) {'
helper = r'''size_t currentLibraryIndex() {
    const auto& q=g_state.queue();
    const size_t qi=g_state.queueIndex();
    return (!q.empty() && qi<q.size()) ? q[qi] : kNoTrack;
}

std::vector<std::wstring> uniqueArtists() {
    std::vector<std::wstring> values;
    for(const auto& t:g_state.library()) {
        std::wstring value=t.artist.empty()?L"unknown artist":t.artist;
        bool seen=false;
        for(const auto& existing:values) if(_wcsicmp(existing.c_str(),value.c_str())==0){seen=true;break;}
        if(!seen) values.push_back(value);
    }
    std::sort(values.begin(),values.end(),[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    return values;
}

std::vector<std::wstring> uniqueAlbums() {
    std::vector<std::wstring> values;
    for(const auto& t:g_state.library()) {
        std::wstring value=t.album.empty()?L"no album":t.album;
        bool seen=false;
        for(const auto& existing:values) if(_wcsicmp(existing.c_str(),value.c_str())==0){seen=true;break;}
        if(!seen) values.push_back(value);
    }
    std::sort(values.begin(),values.end(),[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    return values;
}

std::vector<size_t> filteredTrackIndices(bool byArtist,const std::wstring& value) {
    std::vector<size_t> result;
    for(size_t i=0;i<g_state.library().size();++i){
        const auto& t=g_state.library()[i];
        const std::wstring candidate=byArtist?(t.artist.empty()?L"unknown artist":t.artist):(t.album.empty()?L"no album":t.album);
        if(_wcsicmp(candidate.c_str(),value.c_str())==0)result.push_back(i);
    }
    return result;
}

std::wstring editText(HWND h) {
    if(!h)return {};
    const int n=GetWindowTextLengthW(h);
    std::wstring value((size_t)n+1,L'\0');
    GetWindowTextW(h,value.data(),n+1);
    value.resize((size_t)n);
    return value;
}

void setMetaControlsVisible(bool visible) {
    g_metaControlsShown=visible;
    for(HWND h:g_metaEdits) if(h)ShowWindow(h,visible?SW_SHOW:SW_HIDE);
}

void ensureMetaControls() {
    if(g_metaEdits[0]||!g_hwnd)return;
    if(!g_metaFont)g_metaFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    if(!g_metaBrush)g_metaBrush=CreateSolidBrush(RGB(20,18,20));
    struct Field{int x,y,w,h;DWORD extra;};
    const Field f[8]={
        {570,188,430,32,ES_AUTOHSCROLL},{570,248,430,32,ES_AUTOHSCROLL},{570,308,430,32,ES_AUTOHSCROLL},{570,368,430,32,ES_AUTOHSCROLL},
        {570,428,125,32,ES_AUTOHSCROLL},{715,428,125,32,ES_AUTOHSCROLL},{860,428,140,32,ES_AUTOHSCROLL},{570,506,430,96,ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL}
    };
    for(int i=0;i<8;i++){
        g_metaEdits[(size_t)i]=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|f[i].extra,f[i].x,f[i].y,f[i].w,f[i].h,g_hwnd,(HMENU)(INT_PTR)(9100+i),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_metaEdits[(size_t)i],WM_SETFONT,(WPARAM)g_metaFont,TRUE);
    }
    setMetaControlsVisible(false);
}

void loadMetaControls(size_t index) {
    ensureMetaControls();
    if(index>=g_state.library().size()){
        g_metaTrackIndex=kNoTrack;
        for(HWND h:g_metaEdits)if(h)SetWindowTextW(h,L"");
        return;
    }
    g_metaTrackIndex=index;
    const auto& t=g_state.library()[index];
    const std::wstring fields[8]={t.title,t.artist,t.album,t.genre,t.year,t.trackNumber,t.bpm,t.comment};
    for(int i=0;i<8;i++)SetWindowTextW(g_metaEdits[(size_t)i],fields[i].c_str());
    setMetaControlsVisible(true);
}

void openSettingsForTrack(size_t index=kNoTrack) {
    if(index==kNoTrack)index=currentLibraryIndex();
    if(index==kNoTrack && !g_state.library().empty())index=0;
    g_state.setScreen(Screen::Settings);
    loadMetaControls(index);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void saveMetaControls() {
    if(g_metaTrackIndex>=g_state.library().size())return;
    if(g_state.updateTrackMetadata(g_metaTrackIndex,
        editText(g_metaEdits[0]),editText(g_metaEdits[1]),editText(g_metaEdits[2]),editText(g_metaEdits[3]),
        editText(g_metaEdits[4]),editText(g_metaEdits[5]),editText(g_metaEdits[6]),editText(g_metaEdits[7]))) {
        g_metaSavedUntil=GetTickCount64()+2200;
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

''' + anchor
rep(anchor, helper)

replace_function('drawHeader', r'''void drawHeader(Graphics& g) {
    text(g,L"ETHERPLAYER",R(30,18,260,32),26,warmWhite(),FontStyleBold);
    text(g,L"v1.1  //  LIBRARY INTELLIGENCE",R(31,48,390,20),11,muted(),FontStyleBold);
    struct H{const wchar_t* label;Screen screen;float x;float w;int action;};
    H tabs[]={
        {L"hero",Screen::Hero,400,78,ActHero},
        {L"browse",Screen::Browse,482,88,ActBrowse},
        {L"queue",Screen::Queue,574,82,ActQueue},
        {L"remote",Screen::Remote,660,90,ActRemote},
        {L"settings",Screen::Settings,754,104,ActSettings}
    };
    for(auto& h:tabs){
        const bool on=g_state.screen()==h.screen;RectF rr=R(h.x,18,h.w,42);
        if(on)roundRect(g,rr,18,Color(235,28,24,12),Color(110,126,101,43));
        text(g,h.label,rr,13,on?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(rr,h.action);
    }
    RectF pip=R(930,16,104,44);roundRect(g,pip,18,Color(230,10,9,8),Color(80,99,80,40));
    text(g,L"pip",pip,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pip,ActPip);
}''')

replace_function('drawBrowseRoot', r'''void drawBrowseRoot(Graphics& g) {
    const wchar_t* items[]={L"music",L"playlists",L"artists",L"albums",L"queue",L"settings"};
    const wchar_t* notes[]={L"all local tracks",L"live playlist / playback order",L"metadata-driven artist view",L"metadata-driven album view",L"up next / drag / remove",L"song identity + metadata"};
    for(int i=0;i<6;i++){
        RectF row=R(70,102+i*68,430,62);
        const int action=ActBrowseMenuBase+i;
        const bool hot=g_hoverAction==action;
        if(hot)roundRect(g,row,18,Color(190,35,28,8),Color(105,173,137,45));
        text(g,items[i],R(82,108+i*68,250,34),31,i==0?amber():(hot?amber():warmWhite()),FontStyleRegular);
        text(g,notes[i],R(330,118+i*68,155,24),10,muted(),FontStyleRegular,StringAlignmentFar);
        addHit(row,action);
    }
    text(g,L"MUSIC IS THE TRACK LIBRARY // SONGS REMOVED AS A REDUNDANT CATEGORY",R(76,536,520,24),10,amber(),FontStyleBold);
    text(g,L"NOW PLAYING",R(680,170,240,22),12,amber(),FontStyleBold);
    drawCover(g,R(730,210,170,170));
    text(g,currentTitle(),R(675,394,280,42),20,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(675,430,280,22),12,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(690,466,250,70),true);
    text(g,L"Right-click music = Play Next / Edit Metadata",R(675,558,300,24),11,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
}''')

replace_function('drawBrowseSongs', r'''void drawBrowseSongs(Graphics& g) {
    RectF back=R(64,100,120,40);roundRect(g,back,16,Color(220,12,12,12),Color(70,90,75,45));
    text(g,L"← BACK",back,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(back,ActBrowseBack);
    text(g,L"music",R(64,148,450,70),54,warmWhite(),FontStyleRegular);
    text(g,std::to_wstring(g_state.library().size())+L" TRACKS",R(68,215,250,24),12,muted(),FontStyleBold);
    const size_t maxRows=7;
    float y=255;
    const auto& lib=g_state.library();
    for(size_t row=0;row<maxRows && g_browseOffset+row<lib.size();++row){
        const size_t idx=g_browseOffset+row;const auto& t=lib[idx];
        RectF rr=R(65,y,650,56);const int action=ActBrowseTrackBase+(int)idx;const bool hot=g_hoverAction==action;
        if(hot)roundRect(g,rr,15,Color(205,34,27,8),Color(105,166,133,43));
        text(g,t.title,R(82,y+5,460,26),17,hot?amber():warmWhite(),FontStyleBold);
        std::wstring sub=t.artist.empty()?L"unknown artist":t.artist;
        if(!t.album.empty())sub+=L"  //  "+t.album;
        text(g,sub,R(82,y+30,500,18),11,muted());
        text(g,L">",R(660,y,36,56),16,hot?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(rr,action);y+=60;
    }
    RectF add=R(808,270,200,54);roundRect(g,add,20,Color(235,20,17,10),Color(110,120,98,48));
    text(g,L"ADD MUSIC",add,14,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(add,ActFile);
    text(g,L"Click = play now\nRight-click = Play Next / Edit Metadata\nMouse wheel = scroll library",R(808,345,220,110),14,muted(),FontStyleRegular);
}''')

# Inject metadata-driven artist/album pages + Settings before drawBrowse.
anchor2 = 'void drawBrowse(Graphics& g) {'
extra = r'''void drawGroupList(Graphics& g,bool artists) {
    RectF back=R(64,100,120,40);roundRect(g,back,16,Color(220,12,12,12),Color(70,90,75,45));
    text(g,L"← BACK",back,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(back,ActBrowseBack);
    text(g,artists?L"artists":L"albums",R(64,148,450,70),54,warmWhite(),FontStyleRegular);
    const auto values=artists?uniqueArtists():uniqueAlbums();
    float y=245;
    for(size_t i=0;i<values.size()&&i<7;i++){
        const int action=(artists?ActArtistBase:ActAlbumBase)+(int)i;
        RectF row=R(66,y,650,58);const bool hot=g_hoverAction==action;
        if(hot)roundRect(g,row,16,Color(200,33,27,8),Color(100,168,133,43));
        text(g,values[i],R(84,y+8,510,28),19,hot?amber():warmWhite(),FontStyleBold);
        size_t count=0;for(const auto& t:g_state.library()){
            const std::wstring v=artists?(t.artist.empty()?L"unknown artist":t.artist):(t.album.empty()?L"no album":t.album);
            if(_wcsicmp(v.c_str(),values[i].c_str())==0)count++;
        }
        text(g,std::to_wstring(count)+L" tracks",R(590,y+8,100,28),11,muted(),FontStyleBold,StringAlignmentFar);
        addHit(row,action);y+=64;
    }
    text(g,L"Generated directly from EtherPlay .ethermeta identity data",R(68,700,560,24),11,muted());
}

void drawFilteredTracks(Graphics& g,bool byArtist) {
    RectF back=R(64,100,120,40);roundRect(g,back,16,Color(220,12,12,12),Color(70,90,75,45));
    text(g,L"← BACK",back,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(back,ActBrowseBack);
    text(g,g_browseFilter,R(64,150,680,58),38,warmWhite(),FontStyleBold);
    text(g,byArtist?L"ARTIST":L"ALBUM",R(68,210,220,22),11,amber(),FontStyleBold);
    const auto indices=filteredTrackIndices(byArtist,g_browseFilter);
    float y=250;
    for(size_t row=0;row<indices.size()&&row<7;row++){
        const size_t idx=indices[row];const auto& t=g_state.library()[idx];const int action=ActFilteredTrackBase+(int)idx;
        RectF rr=R(65,y,700,56);const bool hot=g_hoverAction==action;
        if(hot)roundRect(g,rr,15,Color(205,34,27,8),Color(105,166,133,43));
        text(g,t.title,R(82,y+5,500,26),17,hot?amber():warmWhite(),FontStyleBold);
        text(g,byArtist?(t.album.empty()?L"no album":t.album):(t.artist.empty()?L"unknown artist":t.artist),R(82,y+30,500,18),11,muted());
        text(g,L">",R(700,y,36,56),16,hot?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(rr,action);y+=60;
    }
}

void drawSettings(Graphics& g) {
    ensureMetaControls();
    setMetaControlsVisible(true);
    text(g,L"settings",R(62,92,360,58),48,warmWhite(),FontStyleBold);
    text(g,L"SONG IDENTITY // ETHERPLAY METADATA",R(66,150,420,24),12,amber(),FontStyleBold);

    roundRect(g,R(64,188,438,452),28,Color(238,7,7,8),Color(82,110,89,42));
    if(g_metaTrackIndex<g_state.library().size()){
        const auto& t=g_state.library()[g_metaTrackIndex];
        text(g,L"EDITING",R(94,218,180,20),10,amber(),FontStyleBold);
        text(g,t.title,R(94,250,350,64),28,warmWhite(),FontStyleBold);
        text(g,t.artist,R(94,320,350,30),15,muted(),FontStyleRegular);
        text(g,L"FILE",R(94,390,80,20),10,amber(),FontStyleBold);
        text(g,fs::path(t.path).filename().wstring(),R(94,416,340,44),14,warmWhite(),FontStyleBold);
        text(g,t.path,R(94,468,340,94),11,muted(),FontStyleRegular);
        text(g,L"TRACK "+std::to_wstring(g_metaTrackIndex+1)+L" / "+std::to_wstring(g_state.library().size()),R(94,580,300,24),11,muted(),FontStyleBold);
    } else {
        text(g,L"NO MUSIC SELECTED",R(94,260,340,44),22,muted(),FontStyleBold);
    }

    const wchar_t* labels[]={L"TITLE",L"ARTIST",L"ALBUM",L"GENRE"};
    const int ys[]={168,228,288,348};
    for(int i=0;i<4;i++)text(g,labels[i],R(570,(float)ys[i],160,18),10,muted(),FontStyleBold);
    text(g,L"YEAR",R(570,408,80,18),10,muted(),FontStyleBold);
    text(g,L"TRACK",R(715,408,80,18),10,muted(),FontStyleBold);
    text(g,L"BPM",R(860,408,80,18),10,muted(),FontStyleBold);
    text(g,L"COMMENT",R(570,484,120,18),10,muted(),FontStyleBold);

    RectF prev=R(64,654,132,46);roundRect(g,prev,18,Color(235,13,12,11),Color(80,105,84,43));text(g,L"← PREVIOUS",prev,12,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(prev,ActSettingsPrev);
    RectF next=R(210,654,132,46);roundRect(g,next,18,Color(235,13,12,11),Color(80,105,84,43));text(g,L"NEXT →",next,12,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(next,ActSettingsNext);
    RectF save=R(570,632,212,54);roundRect(g,save,20,Color(255,37,29,8),Color(185,242,195,61),1.5f);text(g,L"SAVE SONG INFO",save,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(save,ActSettingsSave);
    text(g,L"Non-destructive .ethermeta sidecar // queue, artists and albums refresh immediately",R(570,696,430,30),10,muted(),FontStyleRegular);
    if(GetTickCount64()<g_metaSavedUntil)text(g,L"SAVED // LIBRARY IDENTITY UPDATED",R(798,644,220,30),10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
}

''' + anchor2
rep(anchor2, extra)

replace_function('drawBrowse', r'''void drawBrowse(Graphics& g) {
    switch(g_browseView){
        case BrowseView::Root:drawBrowseRoot(g);break;
        case BrowseView::Music:drawBrowseSongs(g);break;
        case BrowseView::Artists:drawGroupList(g,true);break;
        case BrowseView::ArtistTracks:drawFilteredTracks(g,true);break;
        case BrowseView::Albums:drawGroupList(g,false);break;
        case BrowseView::AlbumTracks:drawFilteredTracks(g,false);break;
    }
}''')

replace_function('paint', r'''void paint(HWND hwnd) {
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
    Bitmap back(std::max(1L,rc.right),std::max(1L,rc.bottom),PixelFormat32bppPARGB);
    Graphics g(&back);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    SolidBrush bg(Color(255,2,2,3));g.FillRectangle(&bg,0,0,rc.right,rc.bottom);g_hits.clear();
    const bool metaVisible=g_state.presentation()==Presentation::BigScreen && g_state.screen()==Screen::Settings;
    if(!metaVisible)setMetaControlsVisible(false);
    if(g_state.presentation()==Presentation::Pip)drawPip(g);
    else{
        drawHeader(g);
        switch(g_state.screen()){
            case Screen::Hero:drawHero(g);break;
            case Screen::Browse:drawBrowse(g);break;
            case Screen::Queue:drawQueue(g);break;
            case Screen::Remote:drawRemote(g);break;
            case Screen::Settings:drawSettings(g);break;
        }
    }
    Graphics out(dc);out.DrawImage(&back,0,0);EndPaint(hwnd,&ps);
}''')

replace_function('perform', r'''void perform(int action) {
    if(action>=ActFilteredTrackBase && action<ActFilteredTrackBase+1000){
        const size_t idx=(size_t)(action-ActFilteredTrackBase);
        playSelectedTrack(idx,true);g_state.setScreen(Screen::Hero);return;
    }
    if(action>=ActAlbumBase && action<ActAlbumBase+1000){
        const auto values=uniqueAlbums();const size_t idx=(size_t)(action-ActAlbumBase);
        if(idx<values.size()){g_browseFilter=values[idx];g_browseView=BrowseView::AlbumTracks;g_browseOffset=0;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }
    if(action>=ActArtistBase && action<ActArtistBase+1000){
        const auto values=uniqueArtists();const size_t idx=(size_t)(action-ActArtistBase);
        if(idx<values.size()){g_browseFilter=values[idx];g_browseView=BrowseView::ArtistTracks;g_browseOffset=0;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }
    if(action>=ActBrowseTrackBase && action<ActQueueTrackBase){
        const size_t idx=(size_t)(action-ActBrowseTrackBase);
        playSelectedTrack(idx,true);g_state.setScreen(Screen::Hero);return;
    }
    if(action>=ActQueueTrackBase && action<ActArtistBase){
        const size_t qi=(size_t)(action-ActQueueTrackBase);
        if(g_state.playQueueIndex(qi) && openCurrentMedia() && g_player){g_player->Play();g_playing=true;g_paused=false;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }
    if(action>=ActBrowseMenuBase && action<ActBrowseMenuBase+6){
        const int idx=action-ActBrowseMenuBase;
        if(idx==0){g_browseView=BrowseView::Music;g_browseOffset=0;}
        else if(idx==1){g_state.setScreen(Screen::Queue);}
        else if(idx==2){g_browseView=BrowseView::Artists;g_browseOffset=0;}
        else if(idx==3){g_browseView=BrowseView::Albums;g_browseOffset=0;}
        else if(idx==4){g_state.setScreen(Screen::Queue);}
        else if(idx==5){openSettingsForTrack();return;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }
    switch(action){
        case ActHero:g_state.setScreen(Screen::Hero);break;
        case ActBrowse:g_state.setScreen(Screen::Browse);g_browseView=BrowseView::Root;g_browseOffset=0;break;
        case ActQueue:g_state.setScreen(Screen::Queue);break;
        case ActRemote:g_state.setScreen(Screen::Remote);break;
        case ActSettings:openSettingsForTrack();return;
        case ActSettingsSave:saveMetaControls();return;
        case ActSettingsPrev:
            if(!g_state.library().empty()){size_t idx=g_metaTrackIndex==kNoTrack?0:g_metaTrackIndex;if(idx>0)--idx;loadMetaControls(idx);}break;
        case ActSettingsNext:
            if(!g_state.library().empty()){size_t idx=g_metaTrackIndex==kNoTrack?0:g_metaTrackIndex;if(idx+1<g_state.library().size())++idx;loadMetaControls(idx);}break;
        case ActPip:togglePip();return;
        case ActPipMode:togglePipStyle();return;
        case ActCenter:playPause();return;
        case ActPrev:previousTap();return;
        case ActNext:nextTrack();return;
        case ActUp:g_state.setScreen(Screen::Browse);g_browseView=BrowseView::Root;break;
        case ActDown:g_state.setScreen(Screen::Queue);break;
        case ActPipHome:
            g_state.setScreen(Screen::Hero);
            if(g_state.presentation()==Presentation::Pip)togglePip();
            return;
        case ActPipPlaylist:
            g_state.setScreen(Screen::Queue);
            if(g_state.presentation()==Presentation::Pip)togglePip();
            return;
        case ActFile:openFile();return;
        case ActBrowseBack:
            if(g_browseView==BrowseView::ArtistTracks)g_browseView=BrowseView::Artists;
            else if(g_browseView==BrowseView::AlbumTracks)g_browseView=BrowseView::Albums;
            else g_browseView=BrowseView::Root;
            g_browseOffset=0;break;
        default:break;
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}''')

replace_function('quickActionAt', r'''void quickActionAt(int action) {
    size_t idx=kNoTrack;
    if(action>=ActBrowseTrackBase && action<ActQueueTrackBase)idx=(size_t)(action-ActBrowseTrackBase);
    else if(action>=ActFilteredTrackBase && action<ActFilteredTrackBase+1000)idx=(size_t)(action-ActFilteredTrackBase);
    if(idx>=g_state.library().size())return;
    HMENU menu=CreatePopupMenu();if(!menu)return;
    AppendMenuW(menu,MF_STRING,1,L"Play Next");
    AppendMenuW(menu,MF_STRING,2,L"Edit Metadata / Settings");
    POINT pt{};GetCursorPos(&pt);
    const int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,pt.x,pt.y,0,g_hwnd,nullptr);
    DestroyMenu(menu);
    if(cmd==1){g_state.playNext(idx);InvalidateRect(g_hwnd,nullptr,FALSE);}
    else if(cmd==2)openSettingsForTrack(idx);
}''')

# Browse wheel/keyboard now work on Music; B always returns to the browse root. Add S shortcut for Settings.
rep('''if(g_state.screen()==Screen::Browse && g_browseSongs && g_state.presentation()==Presentation::BigScreen){''',
    '''if(g_state.screen()==Screen::Browse && g_browseView==BrowseView::Music && g_state.presentation()==Presentation::BigScreen){''')
rep('''if(wp==VK_UP&&g_state.screen()==Screen::Browse&&g_browseSongs){if(g_browseOffset>0)g_browseOffset--;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp==VK_DOWN&&g_state.screen()==Screen::Browse&&g_browseSongs){if(g_browseOffset+7<g_state.library().size())g_browseOffset++;InvalidateRect(hwnd,nullptr,FALSE);return 0;}''',
'''if(wp==VK_UP&&g_state.screen()==Screen::Browse&&g_browseView==BrowseView::Music){if(g_browseOffset>0)g_browseOffset--;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp==VK_DOWN&&g_state.screen()==Screen::Browse&&g_browseView==BrowseView::Music){if(g_browseOffset+7<g_state.library().size())g_browseOffset++;InvalidateRect(hwnd,nullptr,FALSE);return 0;}''')
rep('''if(wp=='B'){g_state.setScreen(Screen::Browse);g_browseSongs=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;}''',
'''if(wp=='B'){g_state.setScreen(Screen::Browse);g_browseView=BrowseView::Root;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(wp=='S'){openSettingsForTrack();return 0;}''')

# Dark native edit controls and clean up resources on exit.
rep('''case WM_PAINT:paint(hwnd);return 0;''',
'''case WM_CTLCOLOREDIT:{
            HDC edc=(HDC)wp;SetTextColor(edc,RGB(244,242,235));SetBkColor(edc,RGB(20,18,20));SetBkMode(edc,OPAQUE);
            return (LRESULT)(g_metaBrush?g_metaBrush:GetStockObject(BLACK_BRUSH));
        }
        case WM_PAINT:paint(hwnd);return 0;''')
rep('''case WM_DESTROY:closeMedia();KillTimer(hwnd,kTimer);PostQuitMessage(0);return 0;''',
'''case WM_DESTROY:
            closeMedia();KillTimer(hwnd,kTimer);
            if(g_metaFont){DeleteObject(g_metaFont);g_metaFont=nullptr;}
            if(g_metaBrush){DeleteObject(g_metaBrush);g_metaBrush=nullptr;}
            PostQuitMessage(0);return 0;''')

# Queue reflects updated metadata automatically; make the visible description explicitly call it a playlist.
rep('text(g,L"UP NEXT",R(65,102,300,48),40,warmWhite(),FontStyleBold);',
    'text(g,L"PLAYLIST // UP NEXT",R(65,102,430,48),40,warmWhite(),FontStyleBold);')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8')
print(out)
