# WALL v3 (HP2010) on top of v2: event list cleanup and the info box.
#  Event list layout 646250 (6 rows):
#   - Title N text VisibleItems[N].NameWithNew -> .Name (no "(NEW)");
#   - Circle N (medal circle) and Subtitle N (mode name) visibility -> Autolog.Adapter.Items._length(LTEQ:0) (false).
#   (The time / weather icons stay bound to the events: the helper writes the Race Settings conditions into the
#    Single Race event records.)
#  Info box layout 646282 (online events have no description, so it showed only the name):
#   - Event Description text CurrentItem.Description -> Autolog.Controls.SRInfo.Summary (sr_info.lua, from the helper);
#   - every visibility / trigger on CurrentItem.Description -> CurrentItem.EventId (always set), so the description
#     and "Event Distance" show;
#   - Game Mode Title Racer / Cop ("{TierName} SERIES": empty for these events) hidden: Map.RouteLength(LTEQ:-1).
#  sr_info.lua replaced (Summary added).
# Usage: patch_wall32_v3.py <WALL v2> <sr_info.luac> <out>
import sys, struct, re, binascii
sys.path.insert(0, sys.path[0] + '/..')
from bnd32 import Bundle
src, luac, out = sys.argv[1:4]
b = Bundle(open(src, 'rb').read())
FALSE = b'Autolog.Adapter.Items._length(LTEQ:0)'

def put(d, off, old, new, length_field):
    assert bytes(d[off:off + len(old)]) == old and d[off + len(old)] == 0, (hex(off), old)
    assert len(new) <= len(old)
    d[off:off + len(old)] = new + bytes(len(old) - len(new))
    if length_field:
        assert struct.unpack_from('<I', d, off - 12)[0] == 0x0C and struct.unpack_from('<I', d, off - 8)[0] == len(old) + 1
        struct.pack_into('<I', d, off - 8, len(new) + 1)

n_edit = 0
x = b.find(646250); d = bytearray(b.data(x))
for n in range(1, 7):
    old = b'Autolog.Adapter.VisibleItems[%d].NameWithNew' % n
    offs = [m.start() for m in re.finditer(re.escape(old) + b'\0', d)]
    assert len(offs) == 1, old
    put(d, offs[0], old, b'Autolog.Adapter.VisibleItems[%d].Name' % n, True); n_edit += 1
    for tag, old in ((b'Circle %d\0' % n, b'Autolog.Adapter.Items._length(GTEQ:%d)' % n),
                     (b'Subtitle %d\0' % n, b'Autolog.Adapter.VisibleItems[%d].GameModeName' % n)):
        i = d.index(tag); put(d, i + len(tag), old, FALSE, False); n_edit += 1
assert b'NameWithNew' not in d
b.set_data(x, bytes(d))

x = b.find(646282); d = bytearray(b.data(x))
DESC = b'Autolog.Adapter.CurrentItem.Description'
for m in list(re.finditer(re.escape(DESC) + b'\0', d)):
    o = m.start()
    if struct.unpack_from('<I', d, o - 12)[0] == 0x0C and struct.unpack_from('<I', d, o - 8)[0] == len(DESC) + 1:
        put(d, o, DESC, b'Autolog.Controls.SRInfo.Summary', True)
    else:
        put(d, o, DESC, b'Autolog.Adapter.CurrentItem.EventId', False)
    n_edit += 1
for tag, old in ((b'Game Mode Title Racer\0', b'Players.GetLocalCompetitor.IsCop(NOT)'),
                 (b'Game Mode Title Cop\0', b'Players.GetLocalCompetitor.IsCop')):
    i = d.index(tag); put(d, i + len(tag), old, b'Map.RouteLength(LTEQ:-1)', False); n_edit += 1
assert DESC not in d
b.set_data(x, bytes(d))

code = open(luac, 'rb').read()
lr = b.find(binascii.crc32(b'sr_info.lua'))
pl = struct.pack('<I', len(code)) + code
b.set_data(lr, pl + bytes((-len(pl)) % 16))
open(out, 'wb').write(b.build())
print('ok', out, n_edit, 'edits')
