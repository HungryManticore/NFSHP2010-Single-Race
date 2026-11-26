# Balancing and weapon data for the Single Race mod, NFS Hot Pursuit (2010).
#   python build_data32.py balancing <GAMELOGIC\BALANCINGDATA.BNDL> <out>
#   python build_data32.py weapons   <GAMELOGIC\WEAPONS.BNDL> <out>
# balancing: every record a difficulty preset uses gets 7 per-car entries (the AI count option allows 7 AI; a
#   competitor without an entry crashes when the AI start driving - Remastered test 2026-11-14). New entries are
#   copies of existing ones (as Remastered sr_v1/v2): racer slots copy the last entry; cop slots: 4th = 3rd,
#   5th = 1st, 6th / 7th = 2nd.
# weapons: sets 2300100 (AI) and 2300101 (player) = copies of the online Hot Pursuit set 744442; the helper writes
#   the charges and the level into them in memory.
import sys, struct
from bnd32 import Bundle

HDR, DATA = 0x08, 0x20
RACER_RECORDS = [674399, 674844, 674848]            # RaceBalancingData (dde4138d): 7 pointers at data+8
COP_RECORDS = [688454, 688470, 688472]              # CopBalancingData (b5e6b6f1): 7 pointers at data+0
HPCOP_RECORDS = [744065]                            # HotPursuitCopBalancingData (2afd918d): 7 pointers at data+0
SLOT_OFF = {0xDDE4138D: 8, 0xB5E6B6F1: 0, 0x2AFD918D: 0}


def pad_record(b, rid, cop_rule):
    x = b.find(rid)
    d = bytearray(b.data(x))
    dp, h, i = struct.unpack_from('<III', d, HDR)
    assert dp == DATA and i == rid, rid
    so = DATA + SLOT_OFF[h]
    slots = list(struct.unpack_from('<7I', d, so))
    n = next((k for k, v in enumerate(slots) if v == 0), 7)
    assert all(v == 0 for v in slots[n:]) and n >= 1
    if n == 7:
        return 0
    io, = struct.unpack_from('<I', x['e'], 0x40)
    ic, = struct.unpack_from('<H', x['e'], 0x48)
    assert io + 16 * ic == len(d)
    imports = [d[io + 16 * k:io + 16 * k + 16] for k in range(ic)]
    imp_at = {struct.unpack_from('<I', e, 8)[0] & 0x7FFFFFFF: e for e in imports}
    size = slots[1] - slots[0] if n >= 2 else None
    if size is None:  # one entry: up to the next 16-aligned thing after it (the import table)
        size = io - slots[0]
    body = bytearray(d[:io])
    new_imps = []
    for k in range(n, 7):
        if cop_rule:
            src = {3: 2, 4: 0, 5: 1, 6: 1}[k] if k >= 3 else n - 1
            if src >= n:
                src = n - 1
        else:
            src = n - 1
        s = slots[src]
        while len(body) % 16:
            body.append(0)
        p = len(body)
        sub = bytearray(d[s:s + size])
        struct.pack_into('<I', sub, HDR, p + DATA)       # the sub-instance's own data pointer
        body += sub
        e = bytearray(imp_at[s])                          # its schema import, re-pointed
        struct.pack_into('<I', e, 8, 0x80000000 | p)
        new_imps.append(bytes(e))
        slots[k] = p
    struct.pack_into('<7I', body, so, *slots)
    while len(body) % 16:
        body.append(0)
    struct.pack_into('<I', x['e'], 0x40, len(body))
    struct.pack_into('<H', x['e'], 0x48, ic + len(new_imps))
    b.set_data(x, bytes(body) + b''.join(imports) + b''.join(new_imps))
    return 7 - n


def build_balancing(src, out):
    b = Bundle(open(src, 'rb').read())
    for rid in RACER_RECORDS + HPCOP_RECORDS:
        print(rid, '+%d entries' % pad_record(b, rid, False))
    for rid in COP_RECORDS:
        print(rid, '+%d entries' % pad_record(b, rid, True))
    open(out, 'wb').write(b.build())


def build_weapons(src, out):
    b = Bundle(open(src, 'rb').read())
    t = b.find(744442)
    for new in (2300100, 2300101):
        assert not b.has(new)
        e = bytearray(t['e'])
        struct.pack_into('<Q', e, 0, (struct.unpack_from('<Q', e, 0)[0] & ~0xFFFFFFFF) | new)
        d = bytearray(b.data(t))
        assert struct.unpack_from('<I', d, HDR + 8)[0] == 744442
        struct.pack_into('<I', d, HDR + 8, new)
        y = b.add(e, [bytes(d), None, None, None])
    open(out, 'wb').write(b.build())
    print('weapon sets 2300100 / 2300101 added')


if __name__ == '__main__':
    {'balancing': build_balancing, 'weapons': build_weapons}[sys.argv[1]](*sys.argv[2:4])
