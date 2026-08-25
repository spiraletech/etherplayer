from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v013.py'], check=True)
src = Path('generated/EtherPlayerWin013.cpp')
out = Path('generated/EtherPlayerWin0131.cpp')
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


rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V013_WINDOW";',
    'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V0131_WINDOW";')
rep('text(g,L"v0.13  //  POLISH + QUEUE",R(31,48,330,20),11,muted(),FontStyleBold);',
    'text(g,L"v0.13.1  //  LAND PIP ART",R(31,48,350,20),11,muted(),FontStyleBold);')

anchor = 'void drawPipLandscape(Graphics& g) {'
helper = r'''void drawCoverTight(Graphics& g,const RectF& outer) {
    RectF shadow=R(outer.X+5,outer.Y+6,outer.Width,outer.Height);
    roundRect(g,shadow,18,Color(110,0,0,0));
    roundRect(g,outer,18,Color(255,8,8,8),Color(92,115,98,45));
    RectF inner=R(outer.X+5,outer.Y+5,outer.Width-10,outer.Height-10);
    if(!g_cover) {
        text(g,L"E",inner,18,Color(255,92,84,67),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
        return;
    }
    const float iw=(float)g_cover->GetWidth(), ih=(float)g_cover->GetHeight();
    if(iw<=0||ih<=0) return;
    const float scale=std::min(inner.Width/iw,inner.Height/ih);
    const float w=iw*scale,h=ih*scale;
    RectF dst=R(inner.X+(inner.Width-w)/2.f,inner.Y+(inner.Height-h)/2.f,w,h);
    g.DrawImage(g_cover.get(),dst);
    SolidBrush veil(Color(15,0,0,0));g.FillRectangle(&veil,dst);
}

''' + anchor
rep(anchor, helper)

replace_function('drawPipLandscape', r'''void drawPipLandscape(Graphics& g) {
    roundRect(g,R(8,8,684,108),20,Color(250,7,7,8),Color(112,118,95,38));

    // Tight cover treatment for landscape PIP: use nearly the full player height.
    drawCoverTight(g,R(16,16,92,92));

    text(g,currentTitle(),R(122,16,172,24),15,warmWhite(),FontStyleBold);
    text(g,currentArtist(),R(122,40,172,16),10,muted());
    drawAnalyzer(g,R(122,59,324,32),true);
    drawScrub(g,R(122,101,324,4),false);

    RectF prev=R(468,31,40,40);roundRect(g,prev,19,Color(245,13,12,10),Color(80,95,77,44));text(g,L"◀",prev,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(prev,ActPrev);
    RectF pp=R(514,24,54,54);roundRect(g,pp,26,Color(255,28,23,9),Color(190,242,195,61));text(g,g_playing?L"Ⅱ":L"▶",pp,18,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(pp,ActCenter);
    RectF next=R(574,31,40,40);roundRect(g,next,19,Color(245,13,12,10),Color(80,95,77,44));text(g,L"▶",next,13,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(next,ActNext);
    RectF mode=R(626,22,54,28);text(g,L"VERT",mode,8,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(mode,ActPipMode);
    RectF exp=R(622,71,62,28);roundRect(g,exp,12,Color(220,13,12,10),Color(76,90,73,43));text(g,L"EXPAND",exp,8,warmWhite(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);addHit(exp,ActPip);
}''')

rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.13 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.13.1 // EtherPlay",WS_OVERLAPPEDWINDOW,')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8')
print(out)
