import re

with open('tools/build_uefi.py', 'rb') as f:
    content = f.read()

# Find load_font_optional:
marker = b'load_font_optional:'
idx = content.index(marker)
with open('tools/debug_out.txt', 'w') as f:
    f.write('marker at byte: %d\n' % idx)
    f.write('surrounding bytes: %r\n' % content[idx-5:idx+80])
