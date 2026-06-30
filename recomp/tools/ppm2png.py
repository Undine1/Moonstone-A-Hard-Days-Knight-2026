import sys, zlib, struct

def read_ppm(p):
    d = open(p, 'rb').read()
    assert d[:2] == b'P6'
    # parse header tokens
    i = 2; tok = []
    while len(tok) < 3:
        while i < len(d) and d[i] in b' \t\n\r': i += 1
        if d[i:i+1] == b'#':
            while i < len(d) and d[i] not in b'\n': i += 1
            continue
        s = i
        while i < len(d) and d[i] not in b' \t\n\r': i += 1
        tok.append(int(d[s:i]))
    i += 1  # single whitespace after maxval
    w, h, mx = tok
    return w, h, d[i:i + w*h*3]

def write_png(path, w, h, rgb):
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y*w*3:(y+1)*w*3]
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    w, h, rgb = read_ppm(src)
    write_png(dst, w, h, rgb)
    print(f'{dst}: {w}x{h}')
