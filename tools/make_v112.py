from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v111.py'], check=True)
src = Path('generated/EtherPlayerWin111.cpp')
out = Path('generated/EtherPlayerWin112.cpp')
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


rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V111_WINDOW";',
    'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V112_WINDOW";')
rep('text(g,L"v1.1.1  //  UI POLISH",R(31,48,350,20),11,muted(),FontStyleBold);',
    'text(g,L"v1.1.2  //  FINAL POLISH",R(31,48,350,20),11,muted(),FontStyleBold);')
rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.1.1 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.1.2 // EtherPlay",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,')

# Settings hover should never trigger a full parent repaint. Native edit controls sit above
# the double-buffered canvas, so passive mouse movement now only updates cursor state there.
replace_function('updateHover', r'''void updateHover(int x,int y) {
    const int action=actionAt(x,y);
    if(action!=g_hoverAction){
        g_hoverAction=action;
        if(!(g_state.presentation()==Presentation::BigScreen && g_state.screen()==Screen::Settings))
            InvalidateRect(g_hwnd,nullptr,FALSE);
    }
    SetCursor(LoadCursor(nullptr,action?IDC_HAND:IDC_ARROW));
}''')

# The parent window is fully double-buffered. Suppress background erase to avoid a black flash
# behind Win32 child edit controls, while WS_CLIPCHILDREN keeps parent painting out of their rects.
rep('case WM_PAINT:paint(hwnd);return 0;',
    'case WM_ERASEBKGND:return 1;\n        case WM_PAINT:paint(hwnd);return 0;')

# Settings intentionally suppresses the big-screen analyzer timer, but PIP must always animate.
# This prevents a PIP opened from Settings from inheriting a frozen/lagging frequency display.
rep('case WM_TIMER:holdStep();if(g_state.screen()!=Screen::Settings)InvalidateRect(hwnd,nullptr,FALSE);return 0;',
    'case WM_TIMER:holdStep();if(g_state.presentation()==Presentation::Pip || g_state.screen()!=Screen::Settings)InvalidateRect(hwnd,nullptr,FALSE);return 0;')

# Remove the long decorative divider strokes. Hero chrome should float like hardware controls,
# not look like underscored wireframe UI.
rep('''    Pen inner(Color(70,242,195,61),1.f);
    g.DrawLine(&inner,bay.X+34,bay.Y+18,bay.GetRight()-34,bay.Y+18);
''', '')
rep('''    Pen goldLine(Color(72,242,195,61),1.f);
    g.DrawLine(&goldLine,shell.X+32,shell.Y+18,shell.GetRight()-32,shell.Y+18);
''', '')

# Final Hero top chrome: gear -> Settings and E -> Hero/Home. Corner glyphs are removed.
rep('''    text(g,L"≡",R(shell.X+22,shell.Y+20,36,28),16,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"E",R(shell.X+shell.Width/2-18,shell.Y+20,36,28),13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    text(g,L"⋮",R(shell.GetRight()-58,shell.Y+20,36,28),18,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);''',
'''    RectF settingsGear=R(shell.X+shell.Width/2-62,shell.Y+20,36,28);
    text(g,L"⚙",settingsGear,15,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(settingsGear,ActSettings);
    RectF homeE=R(shell.X+shell.Width/2-18,shell.Y+20,36,28);
    text(g,L"E",homeE,13,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);
    addHit(homeE,ActHero);''')

# Remote: keep the same centered analyzer tuck, just lower it enough to clear QUEUE / SEEK / DOWN.
rep('    roundRect(g,R(225,558,630,88),18,Color(225,4,4,5),Color(74,104,83,39));',
    '    roundRect(g,R(225,590,630,88),18,Color(225,4,4,5),Color(74,104,83,39));')
rep('    drawAnalyzer(g,R(252,578,576,46),true);',
    '    drawAnalyzer(g,R(252,610,576,46),true);')
rep('    text(g,L"LIVE 72-BAND // ETHERPLAY AUDIO BUS",R(252,628,576,14),8,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);',
    '    text(g,L"LIVE 72-BAND // ETHERPLAY AUDIO BUS",R(252,660,576,14),8,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8')
print(out)
