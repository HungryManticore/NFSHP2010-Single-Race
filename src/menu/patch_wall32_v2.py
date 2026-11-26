# WALL v2 (HP2010) on top of v1: the picker's Lua control sr_info.lua (Race Settings prompt in the footer on the
# to_main_menu input, which opens the helper's Race Settings menu).
#  - sr_info.lua (compiled with i386 luac5.1) added as a TextFile resource (type 0x70), id crc32("sr_info.lua");
#  - wall.json: script "sr_info", control "SRInfo", NavPromptRight -> Autolog.Controls.SRInfo.NavRight.
# Usage: patch_wall32_v2.py <WALL v1> <sr_info.luac> <out>
import sys, struct, re, binascii, json
sys.path.insert(0, sys.path[0] + '/..')
from bnd32 import Bundle
src, luac, out = sys.argv[1:4]
b = Bundle(open(src, 'rb').read())
code = open(luac, 'rb').read()
tmpl = next(x for x in b.ents if b.type(x) == 0x70)
assert bytes(b.data(tmpl))[4:16] == code[:12], 'bytecode header differs'
rid = binascii.crc32(b'sr_info.lua')
assert not b.has(rid)
e = bytearray(tmpl['e']); struct.pack_into('<Q', e, 0, rid)
p = struct.pack('<I', len(code)) + code
b.add(e, [p + bytes((-len(p)) % 16), None, None, None])

jr = b.find(binascii.crc32(b'wall.json')); raw = b.data(jr)
t = bytes(raw[4:4 + struct.unpack_from('<I', raw, 0)[0]]).decode('utf-8')
def sub(old, new):
    global t
    assert t.count(old) == 1, old
    t = t.replace(old, new)
sub('"NavPromptRight":           { "dataSource": "Autolog.Controls.Nav.NavRight" },',
    '"NavPromptRight":           { "dataSource": "Autolog.Controls.SRInfo.NavRight" },')
sub('"util":             "util"', '"util":             "util",\n        "sr_info":          "sr_info"')
m = re.search(r'"controls"\s*:\s*\{', t)
t = t[:m.end()] + '\n        "SRInfo": {"type": "lua", "script": "sr_info"},' + t[m.end():]
json.loads(t)
pj = struct.pack('<I', len(t.encode())) + t.encode()
b.set_data(jr, pj + bytes((-len(pj)) % 16))
open(out, 'wb').write(b.build())
print('ok', out)
