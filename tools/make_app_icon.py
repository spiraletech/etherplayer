from pathlib import Path
import struct, math

OUT = Path('generated')
OUT.mkdir(parents=True, exist_ok=True)

# EtherPlayer black/gold E icon. Pure stdlib so CI needs no imaging package.
def render(size, scale=4):
    n = size * scale
    pix = [(0, 0, 0, 0)] * (n * n)

    def setp(x, y, c):
        if 0 <= x < n and 0 <= y < n:
            pix[y * n + x] = c

    def inside_round(x, y, left, top, right, bottom, radius):
        if x < left or x >= right or y < top or y >= bottom:
            return False
        cx = min(max(x, left + radius), right - radius - 1)
        cy = min(max(y, top + radius), bottom - radius - 1)
        dx, dy = x - cx, y - cy
        return dx * dx + dy * dy <= radius * radius

    margin = int(n * .035)
    radius = int(n * .19)

    for y in range(n):
        for x in range(n):
            if inside_round(x, y, margin, margin, n - margin, n - margin, radius):
                d = math.hypot(x - n / 2, y - n / 2) / (n * .71)
                v = max(4, min(14, int(12 - 5 * d)))
                setp(x, y, (v, v, v, 255))

    gold = (218, 163, 48, 255)
    bright = (250, 205, 92, 255)
    dark = (112, 72, 13, 255)
    outer = (margin, margin, n - margin, n - margin, radius)
    border = max(2 * scale, int(n * .014))
    inner_margin = margin + border
    inner_radius = max(1, radius - border)

    for y in range(n):
        for x in range(n):
            if inside_round(x, y, *outer) and not inside_round(
                x, y, inner_margin, inner_margin, n - inner_margin, n - inner_margin, inner_radius
            ):
                t = (x + y) / (2 * n)
                c = bright if t < .48 else gold if t < .72 else dark
                setp(x, y, c)

    ex0, ex1 = int(n * .33), int(n * .70)
    stem = max(scale * 3, int(n * .067))
    y0, y1 = int(n * .23), int(n * .77)
    top_h = max(scale * 3, int(n * .067))
    mid_h = max(scale * 3, int(n * .060))
    mid_y = int(n * .48)

    shadow = (80, 50, 8, 180)
    dx = dy = scale * 2
    for y in range(y0 + dy, y1 + dy):
        for x in range(ex0 + dx, ex0 + stem + dx):
            setp(x, y, shadow)
    for y in range(y0 + dy, y0 + top_h + dy):
        for x in range(ex0 + dx, ex1 + dx):
            setp(x, y, shadow)
    for y in range(mid_y + dy, mid_y + mid_h + dy):
        for x in range(ex0 + dx, int(n * .66) + dx):
            setp(x, y, shadow)
    for y in range(y1 - top_h + dy, y1 + dy):
        for x in range(ex0 + dx, ex1 + dx):
            setp(x, y, shadow)

    def egold(y):
        t = (y - y0) / max(1, y1 - y0)
        if t < .12:
            return bright
        if t > .88:
            return (180, 120, 24, 255)
        return (230, 174, 53, 255)

    for y in range(y0, y1):
        for x in range(ex0, ex0 + stem):
            setp(x, y, egold(y))
    for y in range(y0, y0 + top_h):
        for x in range(ex0, ex1):
            setp(x, y, egold(y))
    for y in range(mid_y, mid_y + mid_h):
        for x in range(ex0, int(n * .66)):
            setp(x, y, egold(y))
    for y in range(y1 - top_h, y1):
        for x in range(ex0, ex1):
            setp(x, y, egold(y))

    highlight = (255, 223, 125, 255)
    for y in range(y0, y1):
        for x in range(ex0, min(ex0 + scale, n)):
            setp(x, y, highlight)
    for y in range(y0, min(y0 + scale, n)):
        for x in range(ex0, ex1):
            setp(x, y, highlight)

    out = []
    for oy in range(size):
        for ox in range(size):
            rs = gs = bs = aa = 0
            for yy in range(scale):
                for xx in range(scale):
                    r, g, b, a = pix[(oy * scale + yy) * n + (ox * scale + xx)]
                    rs += r * a
                    gs += g * a
                    bs += b * a
                    aa += a
            count = scale * scale
            A = aa // count
            if aa:
                R, G, B = min(255, rs // aa), min(255, gs // aa), min(255, bs // aa)
            else:
                R = G = B = 0
            out.append((R, G, B, A))
    return out


def dib_entry(size, pixels):
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = pixels[y * size + x]
            xor += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    mask = bytes(mask_row * size)
    header = struct.pack('<IIIHHIIIIII', 40, size, size * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    return header + bytes(xor) + mask


sizes = [16, 24, 32, 48, 64, 128, 256]
entries = [(size, dib_entry(size, render(size))) for size in sizes]
header = struct.pack('<HHH', 0, 1, len(entries))
offset = 6 + 16 * len(entries)
directory = []
for size, data in entries:
    wh = 0 if size == 256 else size
    directory.append(struct.pack('<BBBBHHII', wh, wh, 0, 0, 1, 32, len(data), offset))
    offset += len(data)

ico_path = (OUT / 'etherplayer.ico').resolve()
ico_path.write_bytes(header + b''.join(directory) + b''.join(data for _, data in entries))

rc_path = OUT / 'etherplayer.rc'
rc_path.write_text(
    '101 ICON "' + ico_path.as_posix() + '"\n\n'
    '1 VERSIONINFO\n'
    'FILEVERSION 1,0,0,0\n'
    'PRODUCTVERSION 1,0,0,0\n'
    'FILEFLAGSMASK 0x3fL\n'
    'FILEOS 0x40004L\n'
    'FILETYPE 0x1L\n'
    'BEGIN\n'
    '  BLOCK "StringFileInfo"\n'
    '  BEGIN\n'
    '    BLOCK "040904B0"\n'
    '    BEGIN\n'
    '      VALUE "CompanyName", "EtherTech\\0"\n'
    '      VALUE "FileDescription", "ETHERPLAYERv1.0\\0"\n'
    '      VALUE "FileVersion", "1.0.0.0\\0"\n'
    '      VALUE "InternalName", "ETHERPLAYERv1.0\\0"\n'
    '      VALUE "OriginalFilename", "ETHERPLAYERv1.0.exe\\0"\n'
    '      VALUE "ProductName", "ETHERPLAYERv1.0\\0"\n'
    '      VALUE "ProductVersion", "1.0.0.0\\0"\n'
    '    END\n'
    '  END\n'
    '  BLOCK "VarFileInfo"\n'
    '  BEGIN\n'
    '    VALUE "Translation", 0x409, 1200\n'
    '  END\n'
    'END\n',
    encoding='ascii'
)
print(ico_path)
print(rc_path.resolve())
