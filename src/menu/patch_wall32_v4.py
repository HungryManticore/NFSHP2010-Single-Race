# WALL v4 (HP2010) on top of v3: the info box text and the distance show on the box's first tab too.
# In layout 646282 Event Description and Event Distance play their "OUT" animation while
# Autolog.Controls.Tab.CurrentItem(EQUALS:1, NOT) (= any tab but the second); that trigger -> Map.RouteLength(LTEQ:-1)
# (never true). The distance keeps its own OUT on Map.RouteLength(LTEQ:0).
# Usage: patch_wall32_v4.py <WALL v3> <out>
import sys, re
sys.path.insert(0, sys.path[0] + '/..')
from bnd32 import Bundle
src, out = sys.argv[1:3]
b = Bundle(open(src, 'rb').read())
x = b.find(646282); d = bytearray(b.data(x))
OLD, NEW = b'Autolog.Controls.Tab.CurrentItem(EQUALS:1, NOT)', b'Map.RouteLength(LTEQ:-1)'
done = []
for name in (b'Event Distance\0', b'Event Description\0'):
    i = d.index(name)
    o = d.index(OLD + b'\0', i)
    assert o - i < 0x300, (name, hex(o - i))
    d[o:o + len(OLD)] = NEW + bytes(len(OLD) - len(NEW))
    done.append((name, hex(o)))
b.set_data(x, bytes(d))
open(out, 'wb').write(b.build())
print('ok', out, done)
