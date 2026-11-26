# WALL v5 (HP2010) on top of v4: the location map's info row shows "N TRACKS" instead of the gold-medal count
# (as Remastered's WALL v6-v8). Layout 729188:
#  - Racer Medal / Cop Medal visibility -> Autolog.Controls.EventMap.State(EQUALS:9) (false; State is 0-2);
#  - counter text CurrentItem.ProportionEventsAced -> Autolog.Controls.SRInfo.Tracks (sr_info.lua: "N TRACKS" from
#    CurrentItem.NumEvents, the location's Single Race list);
#  - its word wrap off (text property block 2f3bc1ae: 32-bit layout, u32 0x0C at +0x58, wrap byte +0x5D; Remastered +0x71).
#  sr_info.lua replaced (Tracks).
# Usage: patch_wall32_v5.py <WALL v4> <sr_info.luac> <out>
import sys, struct, re, binascii
sys.path.insert(0, sys.path[0] + '/..')
from bnd32 import Bundle
src, luac, out = sys.argv[1:4]
b = Bundle(open(src, 'rb').read())
x = b.find(729188); d = bytearray(b.data(x))
FALSE = b'Autolog.Controls.EventMap.State(EQUALS:9)'
def put(off, old, new, length_field):
    assert bytes(d[off:off + len(old)]) == old and d[off + len(old)] == 0, (hex(off), old)
    assert len(new) <= len(old)
    d[off:off + len(old)] = new + bytes(len(old) - len(new))
    if length_field:
        assert struct.unpack_from('<I', d, off - 12)[0] == 0x0C and struct.unpack_from('<I', d, off - 8)[0] == len(old) + 1
        struct.pack_into('<I', d, off - 8, len(new) + 1)
for tag, old in ((b'Cop Medal\0', b'Autolog.Adapter.CurrentItem.IsCopLocation'),
                 (b'Racer Medal\0', b'Autolog.Adapter.CurrentItem.IsCopLocation(NOT)')):
    i = d.index(tag); put(i + len(tag), old, FALSE, False)
OLD = b'Autolog.Adapter.CurrentItem.ProportionEventsAced'
o = d.index(OLD + b'\0'); put(o, OLD, b'Autolog.Controls.SRInfo.Tracks', True)
p = d.index(struct.pack('<I', 0x2F3BC1AE), o)
assert p - o < 0x80 and struct.unpack_from('<I', d, p + 0x58)[0] == 0x0C and d[p + 0x5D] == 1, hex(p)
d[p + 0x5D] = 0
b.set_data(x, bytes(d))
code = open(luac, 'rb').read()
lr = b.find(binascii.crc32(b'sr_info.lua'))
pl = struct.pack('<I', len(code)) + code
b.set_data(lr, pl + bytes((-len(pl)) % 16))
open(out, 'wb').write(b.build())
print('ok', out)
