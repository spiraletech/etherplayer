from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v0131.py'], check=True)
src = Path('generated/EtherPlayerWin0131.cpp')
out = Path('generated/EtherPlayerWin101.cpp')
s = src.read_text(encoding='utf-8')


def rep(old, new):
    global s
    if old not in s:
        raise SystemExit('missing patch block:\n' + old[:240])
    s = s.replace(old, new, 1)


# Promote the PASS build to the EtherPlayer v1 line.
rep('constexpr wchar_t kClassName[] = L"ETHERPLAYER_V0131_WINDOW";',
    'constexpr wchar_t kClassName[] = L"ETHERPLAYER_V101_WINDOW";')
rep('text(g,L"v0.13.1  //  LAND PIP ART",R(31,48,350,20),11,muted(),FontStyleBold);',
    'text(g,L"v1.0.1  //  POLISHED",R(31,48,350,20),11,muted(),FontStyleBold);')
rep('g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v0.13.1 // EtherPlay",WS_OVERLAPPEDWINDOW,',
    'g_hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,kClassName,L"ETHERPLAYER v1.0.1 // EtherPlay",WS_OVERLAPPEDWINDOW,')

# Dedicated Vert-PIP shortcuts. Home returns to full Hero UI; playlist returns to full Queue UI.
rep('    ActBrowseBack,\n    ActBrowseMenuBase = 1000,',
    '    ActBrowseBack,\n    ActPipHome,\n    ActPipPlaylist,\n    ActBrowseMenuBase = 1000,')
rep('''    drawHeroControls(g,R(57,452,276,64));
    RectF mode=R(18,488,72,24);''',
'''    drawHeroControls(g,R(57,452,276,64));
    // Overlay dedicated PIP shortcuts over the hardware home / playlist glyphs.
    addHit(R(75,463,42,28),ActPipHome);
    addHit(R(273,479,42,26),ActPipPlaylist);
    RectF mode=R(18,488,72,24);''')
rep('''        case ActDown:g_state.setScreen(Screen::Queue);break;
        case ActFile:openFile();return;''',
'''        case ActDown:g_state.setScreen(Screen::Queue);break;
        case ActPipHome:
            g_state.setScreen(Screen::Hero);
            if(g_state.presentation()==Presentation::Pip) togglePip();
            return;
        case ActPipPlaylist:
            g_state.setScreen(Screen::Queue);
            if(g_state.presentation()==Presentation::Pip) togglePip();
            return;
        case ActFile:openFile();return;''')

# Remote keeps the centered analyzer tuck, but removes the literal FREQUENCY TUCK caption.
rep('    text(g,L"FREQUENCY TUCK",R(280,558,520,20),10,amber(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);\n', '')
rep('    roundRect(g,R(225,582,630,82),18,Color(225,4,4,5),Color(74,104,83,39));',
    '    roundRect(g,R(225,558,630,88),18,Color(225,4,4,5),Color(74,104,83,39));')
rep('    drawAnalyzer(g,R(252,600,576,44),true);',
    '    drawAnalyzer(g,R(252,578,576,46),true);')
rep('    text(g,L"LIVE 72-BAND // ETHERPLAY AUDIO BUS",R(252,646,576,14),8,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);',
    '    text(g,L"LIVE 72-BAND // ETHERPLAY AUDIO BUS",R(252,628,576,14),8,muted(),FontStyleBold,StringAlignmentCenter,StringAlignmentCenter);')

# Queue affordance text reflects both drag reorder and right-click removal.
rep('text(g,L"Click = jump  •  drag = reorder  •  numbering updates immediately",R(65,704,680,24),12,muted());',
    'text(g,L"Click = jump  •  drag = reorder  •  right-click = remove  •  numbering updates",R(65,704,760,24),12,muted());')

# Native Windows right-click removal. This removes the queue/playlist entry only — never the audio file.
rep('''        case WM_LBUTTONDOWN:{''',
'''        case WM_RBUTTONUP:{
            const int mx=GET_X_LPARAM(lp), my=GET_Y_LPARAM(lp);
            const int action=actionAt(mx,my);
            if(g_state.screen()==Screen::Queue && action>=ActQueueTrackBase && action<ActQueueTrackBase+1000){
                const size_t qi=(size_t)(action-ActQueueTrackBase);
                if(qi<g_state.queue().size()){
                    HMENU menu=CreatePopupMenu();
                    if(menu){
                        AppendMenuW(menu,MF_STRING,1,L"Remove from Playlist / Queue");
                        POINT pt{mx,my};ClientToScreen(hwnd,&pt);
                        const int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,pt.x,pt.y,0,hwnd,nullptr);
                        DestroyMenu(menu);
                        if(cmd==1){
                            const bool removingCurrent=qi==g_state.queueIndex();
                            const bool resume=g_playing;
                            const bool keepPaused=g_paused;
                            if(g_state.removeQueueItem(qi)){
                                if(g_state.queue().empty()){
                                    closeMedia();
                                    g_cover.reset();g_coverPath.clear();
                                    g_audioFrames.clear();g_analysisReady=false;
                                    g_spectrumSmooth.fill(0.f);g_spectrumPeak.fill(0.f);
                                } else if(removingCurrent){
                                    if(openCurrentMedia()&&g_player){
                                        if(resume){g_player->Play();g_playing=true;g_paused=false;}
                                        else if(keepPaused){g_playing=false;g_paused=true;}
                                    }
                                }
                                InvalidateRect(hwnd,nullptr,FALSE);
                            }
                        }
                    }
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN:{''')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8')
print(out)
