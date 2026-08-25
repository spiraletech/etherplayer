from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v110fix2.py'], check=True)
src = Path('generated/EtherPlayerWin110.cpp')
out = Path('generated/EtherPlayerWin111.cpp')
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


rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V110_WINDOW";',
    'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V111_WINDOW";')
rep('text(g,L"v1.1  //  LIBRARY INTELLIGENCE",R(31,48,390,20),11,muted(),FontStyleBold);',
    'text(g,L"v1.1.1  //  UI POLISH",R(31,48,350,20),11,muted(),FontStyleBold);')
rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.1 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.1.1 // EtherPlay",WS_OVERLAPPEDWINDOW,')

# Stable native metadata controls: created once, shown only when entering Settings.
replace_function('ensureMetaControls', r'''void ensureMetaControls() {
    if(g_metaEdits[0]||!g_hwnd)return;
    if(!g_metaFont)g_metaFont=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    if(!g_metaBrush)g_metaBrush=CreateSolidBrush(RGB(18,17,18));
    struct Field{int x,y,w,h;DWORD extra;};
    const Field f[8]={
        {550,210,470,34,ES_AUTOHSCROLL},{550,273,470,34,ES_AUTOHSCROLL},{550,336,470,34,ES_AUTOHSCROLL},{550,399,470,34,ES_AUTOHSCROLL},
        {550,462,135,34,ES_AUTOHSCROLL},{710,462,135,34,ES_AUTOHSCROLL},{870,462,150,34,ES_AUTOHSCROLL},{550,535,470,88,ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL}
    };
    for(int i=0;i<8;i++){
        g_metaEdits[(size_t)i]=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|WS_TABSTOP|f[i].extra,
            f[i].x,f[i].y,f[i].w,f[i].h,g_hwnd,(HMENU)(INT_PTR)(9100+i),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_metaEdits[(size_t)i],WM_SETFONT,(WPARAM)g_metaFont,TRUE);
        SendMessageW(g_metaEdits[(size_t)i],EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(9,9));
    }
    setMetaControlsVisible(false);
}''')

# Browse is now a clean navigation list: no redundant Playlists and no side-caption clutter.
replace_function('drawBrowseRoot', r'''void drawBrowseRoot(Graphics& g) {
    const wchar_t* items[]={L"music",L"artists",L"albums",L"queue",L"settings"};
    for(int i=0;i<5;i++){
        RectF row=R(70,108+i*72,430,62);
        const int action=ActBrowseMenuBase+i;
        const bool hot=g_hoverAction==action;
        if(hot)roundRect(g,row,18,Color(190,35,28,8),Color(105,173,137,45));
        text(g,items[i],R(84,111+i*72,350,48),32,i==0?amber():(hot?amber():warmWhite()),FontStyleRegular,StringAlignmentNear,StringAlignmentCenter);
        addHit(row,action);
    }

    text(g,L"NOW PLAYING",R(680,150,240,22),12,amber(),FontStyleBold);
    drawCover(g,R(730,192,170,170));
    text(g,currentTitle(),R(675,382,280,42),20,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,currentArtist(),R(675,421,280,22),12,muted(),FontStyleRegular,StringAlignmentCenter,StringAlignmentCenter);
    drawAnalyzer(g,R(690,458,250,70),true);
}''')

# Rebuild Settings as two intentional panels. Native edits sit inside framed wells and no longer
# get ShowWindow() spammed from the 30fps paint path.
replace_function('drawSettings', r'''void drawSettings(Graphics& g) {
    ensureMetaControls();
    text(g,L"settings",R(62,88,360,58),48,warmWhite(),FontStyleBold);
    text(g,L"SONG METADATA",R(66,146,260,22),11,amber(),FontStyleBold);

    RectF info=R(62,184,430,440);
    roundRect(g,R(info.X+8,info.Y+10,info.Width,info.Height),28,Color(105,0,0,0));
    roundRect(g,info,28,Color(240,7,7,8),Color(86,110,89,42));
    if(g_metaTrackIndex<g_state.library().size()){
        const auto& t=g_state.library()[g_metaTrackIndex];
        text(g,L"EDITING",R(92,216,160,20),10,amber(),FontStyleBold);
        text(g,t.title,R(92,246,350,66),28,warmWhite(),FontStyleBold);
        text(g,t.artist.empty()?L"unknown artist":t.artist,R(92,320,350,28),15,muted());
        text(g,L"FILE",R(92,382,80,18),9,amber(),FontStyleBold);
        text(g,fs::path(t.path).filename().wstring(),R(92,406,340,42),14,warmWhite(),FontStyleBold);
        text(g,t.path,R(92,456,340,88),10,muted());
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

# Five browse actions after Playlists removal.
rep('''if(action>=ActBrowseMenuBase && action<ActBrowseMenuBase+6){
        const int idx=action-ActBrowseMenuBase;
        if(idx==0){g_browseView=BrowseView::Music;g_browseOffset=0;}
        else if(idx==1){g_state.setScreen(Screen::Queue);}
        else if(idx==2){g_browseView=BrowseView::Artists;g_browseOffset=0;}
        else if(idx==3){g_browseView=BrowseView::Albums;g_browseOffset=0;}
        else if(idx==4){g_state.setScreen(Screen::Queue);}
        else if(idx==5){openSettingsForTrack();return;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }''', '''if(action>=ActBrowseMenuBase && action<ActBrowseMenuBase+5){
        const int idx=action-ActBrowseMenuBase;
        if(idx==0){g_browseView=BrowseView::Music;g_browseOffset=0;}
        else if(idx==1){g_browseView=BrowseView::Artists;g_browseOffset=0;}
        else if(idx==2){g_browseView=BrowseView::Albums;g_browseOffset=0;}
        else if(idx==3){g_state.setScreen(Screen::Queue);}
        else if(idx==4){openSettingsForTrack();return;}
        InvalidateRect(g_hwnd,nullptr,FALSE);return;
    }''')

# Settings is a static form, so do not redraw it on the analyzer's 33ms timer.
rep('case WM_TIMER:holdStep();InvalidateRect(hwnd,nullptr,FALSE);return 0;',
    'case WM_TIMER:holdStep();if(g_state.screen()!=Screen::Settings)InvalidateRect(hwnd,nullptr,FALSE);return 0;')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8')
print(out)
