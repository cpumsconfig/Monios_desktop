with open('tools/build_uefi.py', 'rb') as f:
    b = f.read()
idx = b.find(b'load_font_optional')
with open('tools/debug_out.txt', 'w') as f:
    f.write('found at: %d\n' % idx)
    if idx >= 0:
        f.write('context: %r\n' % b[idx-2:idx+120])
