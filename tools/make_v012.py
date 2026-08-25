from pathlib import Path

src = Path('src/win32/EtherPlayerWin.cpp')
out = Path('generated/EtherPlayerWin012.cpp')
s = src.read_text(encoding='utf-8')

def rep(old, new):
    global s
    if old not in s:
        raise SystemExit('missing patch block:\n' + old[:180])
    s = s.replace(old, new, 1)

rep('constexpr int kPipCardW = 540;\nconstexpr int kPipCardH = 190;\nconstexpr int kPipStripW = 680;\nconstexpr int kPipStripH = 112;',
    'constexpr int kPipPortraitW = 370;\nconstexpr int kPipPortraitH = 560;\nconstexpr int kPipLandscapeW = 680;\nconstexpr int kPipLandscapeH = 112;')
rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V011_WINDOW";', 'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V012_WINDOW";')
rep('enum class PipStyle { Card, Strip };\nPipStyle g_pipStyle = PipStyle::Card;',
    'enum class PipStyle { PortraitHero, Landscape };\nPipStyle g_pipStyle = PipStyle::PortraitHero;\nWINDOWPLACEMENT g_bigPlacement{sizeof(WINDOWPLACEMENT)};\nbool g_haveBigPlacement = false;\nbool g_scrubbing = false;\nRectF g_scrubRect{};\nbool g_scrubRectValid = false;')

old = '''void togglePip() {
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
}'''
new = '''void snapPip(int w,int h) {
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int margin=18;
    SetWindowPos(g_hwnd,HWND_TOPMOST,work.right-w-margin,work.bottom-h-margin,w,h,SWP_SHOWWINDOW);
}

void togglePip() {
    const bool entering=g_state.presentation()!=Presentation::Pip;
    if(entering){
        g_bigPlacement.length=sizeof(g_bigPlacement);
        g_haveBigPlacement=GetWindowPlacement(g_hwnd,&g_bigPlacement)!=FALSE;
        g_state.setPresentation(Presentation::Pip);
        g_pipStyle=PipStyle::PortraitHero;
        snapPip(kPipPortraitW,kPipPortraitH);
    } else {
        g_state.setPresentation(Presentation::BigScreen);
        SetWindowPos(g_hwnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        if(g_haveBigPlacement) SetWindowPlacement(g_hwnd,&g_bigPlacement);
        else SetWindowPos(g_hwnd,HWND_NOTOPMOST,CW_USEDEFAULT,CW_USEDEFAULT,kBigW,kBigH,SWP_SHOWWINDOW);
    }
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

void togglePipStyle() {
    if(g_state.presentation()!=Presentation::Pip)return;
    g_pipStyle=g_pipStyle==PipStyle::PortraitHero?PipStyle::Landscape:PipStyle::PortraitHero;
    if(g_pipStyle==PipStyle::PortraitHero) snapPip(kPipPortraitW,kPipPortraitH);
    else snapPip(kPipLandscapeW,kPipLandscapeH);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}'''
rep(old,new)

# Add scrub helper before the control pad.
anchor='void drawControlPad(Graphics& g,float cx,float cy,float scale=1.f,bool labels=false) {'
helper='''void drawScrub(Graphics& g,const RectF& r,bool showTimes=true) {
    g_scrubRect=r;g_scrubRectValid=true;
    roundRect(g,r,3,Color(255,45,42,36));
    const float p=playbackProgress();
    roundRect(g,R(r.X,r.Y,std::max(5.f,r.Width*p),r.Height),3,amber());
    if(showTimes){
        auto fmt=[](ULONGLONG h){ULONGLONG sec=h/10000000ULL;wchar_t b[24]{};swprintf_s(b,L"%llu:%02llu",sec/60ULL,sec%60ULL);return std::wstring(b);};
        text(g,fmt(playbackPosition()),R(r.X,r.Y-22,70,18),10,muted());
        text(g,fmt(g_duration),R(r.GetRight()-70,r.Y-22,70,18),10,muted(),FontStyleRegular,StringAlignmentFar);
    }
}

'''+anchor
rep(anchor,helper)

rep('text(g,L"v0.11  //  BEHAVIOR PASS",R(31,48,310,20),11,muted(),FontStyleBold);',
    'text(g,L"v0.12  //  HERO + PIP PASS",R(31,48,330,20),11,muted(),FontStyleBold);')

old='''void drawHero(Graphics& g) {
    text(g,L"NOW PLAYING",R(120,90,220,24),13,amber(),FontStyleBold);
    RectF art=R(375,108,330,330);drawCover(g,art);
    text(g,currentTitle(),R(210,450,660,46),31,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(260,494,560,26),15,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(260,532,560,88),false);
    text(g,g_analysisReady?L"72-BAND AUDIO REACTIVE":L"ANALYZER IDLE",R(260,621,560,18),10,Color(255,133,117,73),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    drawControlPad(g,540,690,.58f);
    RectF file=R(145,654,130,50);roundRect(g,file,20,Color(240,12,12,12),Color(85,80,72,52));text(g,L"ADD MUSIC",file,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(file,ActFile);
}'''
new='''void drawHero(Graphics& g) {
    text(g,L"NOW PLAYING",R(110,88,220,24),13,amber(),FontStyleBold);
    drawCover(g,R(420,104,240,240));
    text(g,currentTitle(),R(210,360,660,40),28,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(260,400,560,22),14,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(245,447,590,92),false);
    text(g,g_analysisReady?L"72-BAND AUDIO REACTIVE":L"ANALYZER IDLE",R(245,541,590,18),10,Color(255,133,117,73),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    drawScrub(g,R(245,586,590,6),true);
    drawControlPad(g,540,680,.52f);
    RectF file=R(132,642,135,50);roundRect(g,file,20,Color(240,12,12,12),Color(85,80,72,52));text(g,L"ADD MUSIC",file,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(file,ActFile);
}'''
rep(old,new)

old='''void drawRemote(Graphics& g) {
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
}'''
new='''void drawRemote(Graphics& g) {
    text(g,L"REMOTE",R(62,88,300,50),42,warmWhite(),FontStyleBold);
    text(g,L"ETHERPLAYER CONTROL DECK",R(64,138,420,24),13,amber(),FontStyleBold);
    text(g,L"LOCAL / SAME PLAYER STATE",R(64,164,380,20),11,muted(),FontStyleBold);

    const float cx=540.f,cy=365.f;
    drawControlPad(g,cx,cy,1.30f,true);

    text(g,L"FREQUENCY LINK",R(300,545,480,22),11,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    roundRect(g,R(245,572,590,86),18,Color(220,4,4,5),Color(70,104,83,39));
    drawAnalyzer(g,R(270,590,540,48),true);
    text(g,L"LIVE 72-BAND // ETHERPLAY AUDIO BUS",R(270,642,540,16),9,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"REMOTE v0.12",R(64,688,240,20),11,muted(),FontStyleBold);
}'''
rep(old,new)

old='''void drawPipCard(Graphics& g) {
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
}'''
new='''void drawPipPortrait(Graphics& g) {
    text(g,L"ETHERPLAYER",R(24,18,220,24),16,warmWhite(),FontStyleBold);
    text(g,L"HERO PIP",R(244,20,92,20),10,amber(),FontStyleBold,StringAlignmentFar);
    drawCover(g,R(70,55,230,230));
    text(g,currentTitle(),R(34,300,302,34),22,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(54,334,262,20),11,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(42,370,286,62),true);
    drawScrub(g,R(42,454,286,5),false);
    drawControlPad(g,185,505,.48f);
    RectF mode=R(20,484,76,30);text(g,L"LANDSCAPE",mode,9,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(274,484,72,30);text(g,L"EXPAND",exp,9,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}

void drawPipLandscape(Graphics& g) {
    text(g,currentTitle(),R(18,14,205,28),17,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(18,42,205,18),11,muted());
    drawAnalyzer(g,R(235,18,280,56),true);
    RectF prev=R(528,18,38,38);roundRect(g,prev,18,Color(245,13,12,10),Color(80,95,77,44));text(g,L"◀",prev,14,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(prev,ActPrev);
    RectF pp=R(570,12,50,50);roundRect(g,pp,24,Color(255,28,23,9),Color(160,202,161,51));text(g,g_playing?L"Ⅱ":L"▶",pp,17,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pp,ActCenter);
    RectF next=R(624,18,38,38);roundRect(g,next,18,Color(245,13,12,10),Color(80,95,77,44));text(g,L"▶",next,14,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(next,ActNext);
    drawScrub(g,R(18,78,497,4),false);
    RectF mode=R(520,72,82,25);text(g,L"PORTRAIT",mode,9,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(602,72,64,25);text(g,L"EXPAND",exp,9,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}

void drawPip(Graphics& g) {
    if(g_pipStyle==PipStyle::PortraitHero)drawPipPortrait(g);else drawPipLandscape(g);
}'''
rep(old,new)

# Rename action handling to layout toggle.
rep('case ActPipMode:togglePipStyle();return;', 'case ActPipMode:togglePipStyle();return;')

# Reset scrub rect each paint.
rep('SolidBrush bg(Color(255,2,2,3));g.FillRectangle(&bg,0,0,rc.right,rc.bottom);g_hits.clear();',
    'SolidBrush bg(Color(255,2,2,3));g.FillRectangle(&bg,0,0,rc.right,rc.bottom);g_hits.clear();g_scrubRectValid=false;')

# Make scrub bar clickable / draggable before normal hover handling.
rep('case WM_MOUSEMOVE:updateHover(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;',
'''case WM_MOUSEMOVE:{
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            if(g_scrubbing&&g_scrubRectValid&&g_duration){float q=std::clamp(((float)x-g_scrubRect.X)/g_scrubRect.Width,0.f,1.f);seekAbsolute((ULONGLONG)((double)g_duration*q));InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            updateHover(x,y);return 0;
        }''')
rep('''case WM_LBUTTONDOWN:{
            const int action=actionAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));''',
'''case WM_LBUTTONDOWN:{
            int mx=GET_X_LPARAM(lp),my=GET_Y_LPARAM(lp);
            if(g_scrubRectValid&&hit(g_scrubRect,mx,my)&&g_duration){g_scrubbing=true;SetCapture(hwnd);float q=std::clamp(((float)mx-g_scrubRect.X)/g_scrubRect.Width,0.f,1.f);seekAbsolute((ULONGLONG)((double)g_duration*q));InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            const int action=actionAt(mx,my);''')
rep('''case WM_LBUTTONUP:{
            ReleaseCapture();
            const int action=g_pressedAction;''',
'''case WM_LBUTTONUP:{
            ReleaseCapture();
            if(g_scrubbing){g_scrubbing=false;return 0;}
            const int action=g_pressedAction;''')

rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.11 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.12 // EtherPlay",WS_OVERLAPPEDWINDOW,')

out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(s,encoding='utf-8')
print(out)
