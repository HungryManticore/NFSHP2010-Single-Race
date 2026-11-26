# Replaces the main menu's Wall icon (raster 0x20b90b5b, 64x64 A8R8G8B8) with the chequered flag (make_flag32.py output).
# Usage: patch_menu_icon32.py <MAIN_MENU.BNDL> <flag.bgra> <out>
import sys, struct
sys.path.insert(0, sys.path[0] + '/..')
from bnd32 import Bundle
src, px, out = sys.argv[1:4]
b = Bundle(open(src, 'rb').read())
x = b.find(0x20b90b5b)
h = b.data(x, 0)
assert struct.unpack_from('<IHH', h, 0xC) == (0x15, 64, 64)
d = open(px, 'rb').read()
assert len(d) == len(b.data(x, 1)) == 64 * 64 * 4
b.set_data(x, d, 1)
open(out, 'wb').write(b.build())
print('ok', out)
