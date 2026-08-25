from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v012.py'], check=True)
src = Path('generated/EtherPlayerWin012.cpp')
out = Path('generated/EtherPlayerWin013.cpp')
s = src.read_text(encoding='utf-8')


def rep(old, new):
    global s
    if old not in s:
        raise SystemExit('missing patch block:\n' + old[:220])
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


rep('constexpr int kPipPortraitW = 370;\nconstexpr int kPipPortraitH = 560;\nconstexpr int kPipLandscapeW = 680;\nconstexpr int kPipLandscapeH = 112;',
    'constexpr int kPipPortraitW = 390;\nconstexpr int kPipPortraitH = 550;\nconstexpr int kPipLandscapeW = 700;\nconstexpr int kPipLandscapeH = 128;')
rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V012_WINDOW";', 'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V013_WINDOW";')
rep('bool g_scrubRectValid = false;', 'bool g_scrubRectValid = false;\nint g_dragQueueFrom = -1;\nint g_dragQueueOver = -1;\nbool g_dragQueueActive = false;\nPOINT g_dragQueueStart{};')
rep('text(g,L"v0.12  //  HERO + PIP PASS",R(31,48,330,20),11,muted(),FontStyleBold);',
    'text(g,L"v0.13  //  POLISH + QUEUE",R(31,48,330,20),11,muted(),FontStyleBold);')

replace_function('snapPip', r'''void snapPip(int clientW,int clientH) {
    DWORD style=(DWORD)GetWindowLongPtrW(g_hwnd,GWL_STYLE);
    DWORD exStyle=(DWORD)GetWindowLongPtrW(g_hwnd,GWL_EXSTYLE);
    RECT wr{0,0,clientW,clientH};
    AdjustWindowRectEx(&wr,style,FALSE,exStyle);
    const int outerW=wr.right-wr.left;
    const int outerH=wr.bottom-wr.top;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int margin=18;
    SetWindowPos(g_hwnd,HWND_TOPMOST,work.right-outerW-margin,work.bottom-outerH-margin,outerW,outerH,SWP_SHOWWINDOW);
}''')

anchor = 'void drawHeader(Graphics& g) {'
helper = r'''void drawHeroControls(Graphics& g,const RectF& bay) {
    roundRect(g,R(bay.X+5,bay.Y+8,bay.Width,bay.Height),30,Color(105,0,0,0));
    roundRect(g,bay,30,Color(248,8,8,8),Color(120,118,95,38),1.2f);
    Pen inner(Color(70,242,195,61),1.f);
    g.DrawLine(&inner,bay.X+34,bay.Y+18,bay.GetRight()-34,bay.Y+18);

    const float cy=bay.Y+bay.Height*.56f;
    RectF prev=R(bay.X+48,cy-25,58,50);
    RectF center=R(bay.X+bay.Width*.5f-34,cy-34,68,68);
    RectF next=R(bay.GetRight()-106,cy-25,58,50);
    roundRect(g,prev,22,Color(245,15,14,12),Color(72,100,80,42));
    roundRect(g,center,33,Color(255,17,15,10),Color(205,242,195,61),2.f);
    roundRect(g,next,22,Color(245,15,14,12),Color(72,100,80,42));
    text(g,L"◀◀",prev,15,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,g_playing?L"Ⅱ":L"▶",center,22,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"▶▶",next,15,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(prev,ActPrev);addHit(center,ActCenter);addHit(next,ActNext);

    RectF home=R(bay.X+18,bay.Y+11,42,28);
    RectF queue=R(bay.GetRight()-60,bay.GetBottom()-37,42,26);
    text(g,L"⌂",home,15,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"≡",queue,16,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(home,ActUp);addHit(queue,ActDown);
}'''
rep(anchor, helper + '\n\n' + anchor)

replace_function('drawHero', r'''void drawHero(Graphics& g) {
    RectF shell=R(325,78,430,642);
    roundRect(g,R(shell.X+12,shell.Y+16,shell.Width,shell.Height),34,Color(120,0,0,0));
    roundRect(g,shell,34,Color(248,6,6,7),Color(120,117,94,37),1.3f);
    Pen goldLine(Color(72,242,195,61),1.f);
    g.DrawLine(&goldLine,shell.X+32,shell.Y+18,shell.GetRight()-32,shell.Y+18);

    text(g,L"≡",R(shell.X+22,shell.Y+20,36,28),16,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"E",R(shell.X+shell.Width/2-18,shell.Y+20,36,28),13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"⋮",R(shell.GetRight()-58,shell.Y+20,36,28),18,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);

    drawCover(g,R(shell.X+93,shell.Y+62,244,244));
    text(g,L"NOW PLAYING",R(shell.X+70,shell.Y+316,shell.Width-140,18),10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentTitle(),R(shell.X+44,shell.Y+338,shell.Width-88,38),24,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(shell.X+70,shell.Y+378,shell.Width-140,21),12,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);

    drawAnalyzer(g,R(shell.X+54,shell.Y+416,shell.Width-108,82),false);
    text(g,L"35Hz        250Hz             1k             4k          18kHz",R(shell.X+58,shell.Y+500,shell.Width-116,15),8,Color(255,119,105,64),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawScrub(g,R(shell.X+55,shell.Y+534,shell.Width-110,6),true);
    drawHeroControls(g,R(shell.X+48,shell.Y+570,shell.Width-96,108));

    RectF file=R(112,647,142,50);
    roundRect(g,file,20,Color(238,11,11,12),Color(80,90,74,47));
    text(g,L"ADD MUSIC",file,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(file,ActFile);
}''')

replace_function('drawQueue', r'''void drawQueue(Graphics& g) {
    text(g,L"UP NEXT",R(65,102,300,48),40,warmWhite(),FontStyleBold);
    text(g,L"DRAG TO REORDER  //  PLAYBACK FOLLOWS THIS ORDER",R(67,149,520,22),11,amber(),FontStyleBold);
    const auto& q=g_state.queue();
    if(q.empty()) { text(g,L"queue is empty",R(65,190,400,40),22,muted()); return; }
    float y=184;
    for(size_t i=0;i<q.size()&&i<8;i++) {
        const auto idx=q[i]; if(idx>=g_state.library().size())continue;
        const auto& t=g_state.library()[idx];
        const int action=ActQueueTrackBase+(int)i;
        const bool current=i==g_state.queueIndex();
        const bool hot=g_hoverAction==action;
        const bool dragOver=g_dragQueueActive && g_dragQueueOver==(int)i;
        RectF row=R(65,y,700,58);
        roundRect(g,row,16,current?Color(225,42,33,8):(hot?Color(210,25,20,9):Color(220,8,8,9)),
                  dragOver?amber(210):(current?Color(150,205,166,53):(hot?Color(105,120,98,48):Color(55,75,66,52))),dragOver?2.f:1.f);
        text(g,L"≡",R(78,y,30,58),16,hot||dragOver?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        text(g,std::to_wstring(i+1),R(110,y,38,58),13,current?amber():muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        text(g,t.title,R(158,y+8,470,24),17,hot||current?amber():warmWhite(),FontStyleBold);
        text(g,t.artist,R(158,y+31,470,20),12,muted());
        if(current) text(g,L"PLAYING",R(650,y,92,58),10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        addHit(row,action);y+=66;
    }
    text(g,L"Click = jump  •  drag = reorder  •  numbering updates immediately",R(65,704,680,24),12,muted());
    drawControlPad(g,900,390,.76f);
}''')

replace_function('drawRemote', r'''void drawRemote(Graphics& g) {
    text(g,L"REMOTE",R(62,88,300,50),42,warmWhite(),FontStyleBold);
    text(g,L"ETHERPLAYER CONTROL DECK",R(64,138,420,24),13,amber(),FontStyleBold);
    text(g,L"LOCAL / SAME PLAYER STATE",R(64,164,380,20),11,muted(),FontStyleBold);

    const float cx=540.f,cy=348.f;
    drawControlPad(g,cx,cy,1.62f,true);

    text(g,L"FREQUENCY TUCK",R(280,558,520,20),10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    roundRect(g,R(225,582,630,82),18,Color(225,4,4,5),Color(74,104,83,39));
    drawAnalyzer(g,R(252,600,576,44),true);
    text(g,L"LIVE 72-BAND // ETHERPLAY AUDIO BUS",R(252,646,576,14),8,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
}''')

replace_function('drawPipPortrait', r'''void drawPipPortrait(Graphics& g) {
    roundRect(g,R(12,10,366,520),28,Color(248,6,6,7),Color(105,117,94,37));
    text(g,L"ETHERPLAYER",R(28,22,200,22),14,warmWhite(),FontStyleBold);
    text(g,L"HERO",R(294,22,60,20),9,amber(),FontStyleBold,StringAlignmentFar);
    drawCover(g,R(88,58,214,214));
    text(g,currentTitle(),R(38,286,314,32),21,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(58,318,274,18),10,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(48,352,294,58),true);
    drawScrub(g,R(48,430,294,5),false);
    drawHeroControls(g,R(57,452,276,64));
    RectF mode=R(18,488,72,24);text(g,L"LAND",mode,8,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(302,488,66,24);text(g,L"EXPAND",exp,8,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}''')

replace_function('drawPipLandscape', r'''void drawPipLandscape(Graphics& g) {
    roundRect(g,R(8,8,684,108),20,Color(250,7,7,8),Color(112,118,95,38));
    drawCover(g,R(18,20,68,68));
    text(g,currentTitle(),R(98,18,188,24),15,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(98,42,188,16),10,muted());
    drawAnalyzer(g,R(98,61,348,30),true);
    drawScrub(g,R(98,101,348,4),false);

    RectF prev=R(468,31,40,40);roundRect(g,prev,19,Color(245,13,12,10),Color(80,95,77,44));text(g,L"◀",prev,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(prev,ActPrev);
    RectF pp=R(514,24,54,54);roundRect(g,pp,26,Color(255,28,23,9),Color(190,242,195,61));text(g,g_playing?L"Ⅱ":L"▶",pp,18,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pp,ActCenter);
    RectF next=R(574,31,40,40);roundRect(g,next,19,Color(245,13,12,10),Color(80,95,77,44));text(g,L"▶",next,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(next,ActNext);
    RectF mode=R(626,22,54,28);text(g,L"VERT",mode,8,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(622,71,62,28);roundRect(g,exp,12,Color(220,13,12,10),Color(76,90,73,43));text(g,L"EXPAND",exp,8,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}''')

rep('''case WM_MOUSEMOVE:{
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            if(g_scrubbing&&g_scrubRectValid&&g_duration){float q=std::clamp(((float)x-g_scrubRect.X)/g_scrubRect.Width,0.f,1.f);seekAbsolute((ULONGLONG)((double)g_duration*q));InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            updateHover(x,y);return 0;
        }''', r'''case WM_MOUSEMOVE:{
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            if(g_scrubbing&&g_scrubRectValid&&g_duration){float q=std::clamp(((float)x-g_scrubRect.X)/g_scrubRect.Width,0.f,1.f);seekAbsolute((ULONGLONG)((double)g_duration*q));InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(g_dragQueueFrom>=0){
                if(!g_dragQueueActive && (std::abs(x-g_dragQueueStart.x)>5 || std::abs(y-g_dragQueueStart.y)>5)) g_dragQueueActive=true;
                if(g_dragQueueActive){int a=actionAt(x,y);g_dragQueueOver=(a>=ActQueueTrackBase)?a-ActQueueTrackBase:-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            }
            updateHover(x,y);return 0;
        }''')

rep('''            const int action=actionAt(mx,my);
            g_pressedAction=action;g_pressStarted=GetTickCount64();g_lastHoldStep=0;
            SetCapture(hwnd);
            if(action!=ActPrev && action!=ActNext)perform(action);
            return 0;''', r'''            const int action=actionAt(mx,my);
            if(action>=ActQueueTrackBase && g_state.screen()==Screen::Queue && g_state.presentation()==Presentation::BigScreen){
                g_dragQueueFrom=action-ActQueueTrackBase;g_dragQueueOver=g_dragQueueFrom;g_dragQueueActive=false;g_dragQueueStart={mx,my};SetCapture(hwnd);return 0;
            }
            g_pressedAction=action;g_pressStarted=GetTickCount64();g_lastHoldStep=0;
            SetCapture(hwnd);
            if(action!=ActPrev && action!=ActNext)perform(action);
            return 0;''')

rep('''            ReleaseCapture();
            if(g_scrubbing){g_scrubbing=false;return 0;}
            const int action=g_pressedAction;''', r'''            ReleaseCapture();
            if(g_scrubbing){g_scrubbing=false;return 0;}
            if(g_dragQueueFrom>=0){
                const int from=g_dragQueueFrom,over=g_dragQueueOver;const bool dragged=g_dragQueueActive;
                g_dragQueueFrom=-1;g_dragQueueOver=-1;g_dragQueueActive=false;
                if(dragged && over>=0) g_state.moveQueueItem((size_t)from,(size_t)over);
                else perform(ActQueueTrackBase+from);
                InvalidateRect(hwnd,nullptr,FALSE);return 0;
            }
            const int action=g_pressedAction;''')

rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.12 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.13 // EtherPlay",WS_OVERLAPPEDWINDOW,')

out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(s,encoding='utf-8')
print(out)
