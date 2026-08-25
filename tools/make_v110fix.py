from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, 'tools/make_v110.py'], check=True)
path = Path('generated/EtherPlayerWin110.cpp')
s = path.read_text(encoding='utf-8')

# Final compatibility scrub: legacy input handlers from the v0.11 base still
# referenced the old boolean after v1.1 migrated Browse to BrowseView.
s = s.replace('g_browseSongs', '(g_browseView==BrowseView::Music)')

path.write_text(s, encoding='utf-8')
print(path)
