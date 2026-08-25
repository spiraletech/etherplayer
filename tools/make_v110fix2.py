from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v110fix.py'], check=True)
path = Path('generated/EtherPlayerWin110.cpp')
s = path.read_text(encoding='utf-8')

# The generic compatibility scrub can turn old boolean assignments into an
# invalid comparison assignment. Convert those explicitly to BrowseView state.
s = s.replace('(g_browseView==BrowseView::Music)=false', 'g_browseView=BrowseView::Root')
s = s.replace('(g_browseView==BrowseView::Music)=true', 'g_browseView=BrowseView::Music')

path.write_text(s, encoding='utf-8')
print(path)
