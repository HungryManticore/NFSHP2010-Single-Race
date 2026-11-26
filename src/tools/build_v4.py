# Single Race event set for NFS Hot Pursuit (2010), v3 (2026-11-26) = v2 + 23 of the 24 Remastered-only routes.
#   python build_v3.py <HP2010 GAMELOGIC\CHALLENGEINSTANCES.BNDL> <GAMELOGIC\GAMEPLAY.BNDL> <picker_v8.json>
#                      <Remastered CHALLENGEINSTANCES.BNDL (Single Race v8+)> <HP2010 TRIGGERS.BNDL>
#                      <Remastered TRIGGERS.BNDL> <out CI> <out GAMEPLAY> <out TRIGGERS> <out report.json> <out names.h>
# New routes: events built from the HP2010 templates (Race 744273 / HP 389908, as the career routes) with the route
# taken from the Remastered source record (its online Race or career event): Location[] (schema c008d95a, 44 bytes:
# box trigger, 8 grid points, respawn point, road) and RestrictedRoadSections (290d2b42, 9 bytes) - both schemas are
# identical in the two games - plus time of day (start, finish, cycle) and sun direction.
# Names: the Remastered strings are not in HP2010's language tables, so each route gets an empty HP2010 string id
# (248953...) whose text the helper sets at start-up (names.h).
# Triggers: TRIGGERS.BNDL holds box triggers (48 bytes), points (32 bytes), both sorted by id, and a 63x63 grid of
# lists. The new routes need 10 points (One Step Ahead grid 1088024-031, Turn It Up 1088279, Crimewave 1088277): they
# are appended after the last point (their ids are higher than every HP2010 id, so the table stays sorted and no
# index changes); the grid lists are left alone. One Step Ahead's finish box 977065 -> HP2010's 695336 (same box,
# 2.1 m away, turned 180 degrees). Cannonball (n 148) needs a new start box, which would shift the grid's indices:
# not ported.
import sys, os, struct, json
sys.path.insert(0, '/home/claude/w')
from gen32 import *
from bnd32 import Bundle
import build_v2 as V2

SKIP = {148: 'Cannonball: needs a new start box trigger (1070181)'}
TRIG_SUBST = {977065: 695336}
NEW_POINTS = list(range(1088024, 1088032)) + [1088277, 1088279]
NAME_IDS = [248953, 248954, 248955, 248956, 248958, 248959, 248960, 248961, 248962, 248963, 248964, 248970, 248971,
            248973, 248978, 248980, 248981, 248982, 248983, 248985, 248986, 248988, 248989, 248995]
LOC_HASH, RRS_HASH = 0xC008D95A, 0x290D2B42
R_EV = 0x885312E1


class Hpr:
    """Remastered (64-bit, uncompressed) GameChanger reader: event records and their sub-instances."""
    def __init__(self, path):
        from ev import load
        self.a = load(path, 8)
        self.d = self.a['d']
        self.D = self.a['defs'][R_EV]

    def arr(self, eid, h):
        dp = self.a['ev'][eid]
        fo = self.D[h][3]
        ap = struct.unpack_from('<Q', self.d, dp + fo)[0]
        c = struct.unpack_from('<I', self.d, dp + fo + 8)[0]
        out = []
        for k in range(c):
            p = struct.unpack_from('<Q', self.d, ap + 8 * k)[0]
            sdp = struct.unpack_from('<Q', self.d, p + 0x10)[0]
            sh, sid = struct.unpack_from('<II', self.d, p + 0x18)
            size = max(t[3] + t[2] * t[4] for t in self.a['defs'][sh].values())
            out.append((sh, sid, bytes(self.d[sdp:sdp + size])))
        return out

    def raw(self, eid, h):
        dp = self.a['ev'][eid]
        _, ft, fc, fo, fs = self.D[h]
        return bytes(self.d[dp + fo:dp + fo + fs * fc])


def patch_route(g, P, hp, src, grid_order, name_id):
    """Replace the Location[] / RestrictedRoadSections arrays and the route scalars of new event P."""
    D = P + DATA
    def subs_of(h):
        return g.array(g.events[V2.RACE_TEMPLATE], h)
    loc_flags = g.imp_at[subs_of(F_LOCATIONS)[0]][1]
    rrs_t = subs_of(F_RRS)
    rrs_flags = g.imp_at[rrs_t[0]][1] if rrs_t else loc_flags
    for fh, sh, flags in ((F_LOCATIONS, LOC_HASH, loc_flags), (F_RRS, RRS_HASH, rrs_flags)):
        subs = []
        for k, (h, sid, data) in enumerate(hp.arr(src, fh)):
            assert h == sh and len(data) == g.sizes[sh], (hex(h), len(data))
            data = bytearray(data)
            if fh == F_LOCATIONS:
                vals = list(struct.unpack_from('<11I', data, 0))
                vals = [TRIG_SUBST.get(v, v) for v in vals]
                if k == 0 and grid_order:
                    vals[1:9] = [vals[1 + i] for i in grid_order]
                struct.pack_into('<11I', data, 0, *vals)
            subs.append(g.new_instance(sh, sid, bytes(data), flags))
        ap = g.ptr_array(subs)
        fo = g.field(fh)[3]
        es = g.rd_new(D + fo + 8, '<I')[0] or 4
        g.w(D + fo, '<III', ap, len(subs), es)
    for fh in (V2.F_TOD, V2.F_TOD_FINISH, V2.F_TOD_CYCLE, V2.F_SUN):
        g.wb(D + g.field(fh)[3], hp.raw(src, fh))
    g.w(D + g.field(F_NAME)[3], '<I', name_id)


def build_triggers(hp_path, hpr_path, out_path):
    import bnd as rb
    b = Bundle(open(hp_path, 'rb').read())
    x = b.ents[0]
    d = bytearray(b.data(x))
    ver, A, nA, X, nX, B, nB, C = struct.unpack_from('<8I', d, 0)
    assert ver == 8 and X == B and nX == 0
    r = rb.parse(hpr_path)['res'][0]['blobs'][0]
    rB, rnB = 0x2A00, struct.unpack_from('<Q', r, 0x30)[0]
    rpts = {struct.unpack_from('<I', r, rB + 32 * k + 12)[0]: r[rB + 32 * k:rB + 32 * k + 32] for k in range(rnB)}
    last = struct.unpack_from('<I', d, B + 32 * (nB - 1) + 12)[0]
    assert all(i > last for i in NEW_POINTS) and NEW_POINTS == sorted(NEW_POINTS)
    add = b''.join(rpts[i] for i in NEW_POINTS)
    shift = len(add)
    pts_end = B + 32 * nB
    assert pts_end == C
    out = bytearray(d[:pts_end]) + add + d[pts_end:]
    struct.pack_into('<II', out, 0x18, nB + len(NEW_POINTS), C + shift)
    # grid cell pointers (63 x 63 entries of u32 offset, u32 value) follow the moved table start
    gx, gy = struct.unpack_from('<II', out, 0x20)
    C2 = C + shift
    for i in range(gx * gy):
        p, = struct.unpack_from('<I', out, C2 + 8 * i)
        if p:
            assert p >= C + 8 * gx * gy
            struct.pack_into('<I', out, C2 + 8 * i, p + shift)
    b.set_data(x, bytes(out))
    open(out_path, 'wb').write(b.build())
    return len(NEW_POINTS)


def main(ci, gp, picker, hpr_ci, hp_trig, hpr_trig, out_ci, out_gp, out_trig, out_report, out_names):
    hp = Hpr(hpr_ci)
    pk = json.load(open(picker))
    # v2 first (all routes that exist in HP2010), into memory
    import io, tempfile
    tmp = tempfile.mkdtemp()
    V2.main(ci, gp, picker, tmp + '/ci.bndl', tmp + '/gp.bndl', tmp + '/rep.json')
    rep = json.load(open(tmp + '/rep.json'))
    g = Inst(open(tmp + '/ci.bndl', 'rb').read())
    ev = g.events
    race_opps = g.array(ev[V2.RACE_FROM], F_OPP)[:7]
    hp_opps = g.array(ev[572352], F_OPP)[:4] + g.array(ev[572370], F_OPP)[:2]
    cop_t = g.array(ev[V2.HP_FROM], F_COPS)[0]
    cop_racers = g.array(ev[V2.COP_FROM], F_OPP)[:7]

    def fields(e, hashes):
        out = {}
        for h in hashes:
            if h in g.defs[EV_HASH]:
                ft = g.field(h)[1]
                v = g.scalar(ev[e], h)
                out[h] = float(v) if ft in (1, 9) else int(v)
        return out
    race_ovr = {**fields(V2.RACE_FROM, V2.LOADING + V2.BAL), F_INTRO: V2.RACE_INTRO, F_COP_INTRO: 0}
    hp_ovr = {**fields(V2.HP_FROM, V2.LOADING + V2.BAL), F_INTRO: 0, F_COP_INTRO: 0, F_RACERS: V2.HP_RACERS,
              F_COPCOUNT: V2.HP_COPS, **V2.WEAPONS}
    cop_ovr = {**fields(V2.COP_FROM, V2.LOADING + V2.BAL), F_INTRO: 0, F_COP_INTRO: V2.COP_INTRO, **V2.WEAPONS}

    names_out, added, skipped = [], [], []
    k = 0
    for s in rep['skipped']:
        n = s['n']
        if os.environ.get('ONLY') and n not in {int(x) for x in os.environ['ONLY'].split(',')}:
            continue
        if n in SKIP:
            skipped.append(dict(s, reason=SKIP[n]))
            continue
        src = next(i for i in s['source'] if i in hp.a['ev'])
        name_sid = struct.unpack('<I', hp.raw(src, F_NAME))[0]
        nid = NAME_IDS[k]; k += 1
        T = V2
        r = [g.opponent(p) for p in race_opps]
        P = g.make_event(T.RACE_BASE + n, T.RACE_TEMPLATE, T.RACE_TEMPLATE, 'Race', r, [], race_ovr, None)
        patch_route(g, P, hp, src, T.RACE_GRID, nid)
        r = [g.opponent(p, f2a48=0) for p in hp_opps]
        c = [g.opponent(cop_t, pct=0.0, trig=0, f2a48=0, flags=(0, 0)) for _ in range(6)]
        P = g.make_event(T.HP_BASE + n, T.HP_TEMPLATE, T.HP_TEMPLATE, 'HP', r, c, hp_ovr, None)
        patch_route(g, P, hp, src, T.HP_GRID, nid)
        r = [g.opponent(p, trig=0, f2a48=0) for p in cop_racers]
        m = [g.clone_sub(p) for p in g.array(ev[T.COP_FROM], F_MEDALS)]
        P = g.make_event(T.COP_BASE + n, T.HP_TEMPLATE, T.HP_TEMPLATE, 'HPCop', r, [], cop_ovr, None, medals=m)
        patch_route(g, P, hp, src, T.HP_GRID, nid)
        names_out.append((nid, s['event'].upper(), n, src, name_sid))
        added.append(dict(n=n, event=s['event'], location=s['location'], source=src, name_string=nid,
                          race=T.RACE_BASE + n, hp=T.HP_BASE + n, cop=T.COP_BASE + n))
    open(out_ci, 'wb').write(g.finish())

    # picker lists: every built route (v2's + the new ones)
    built = {e['n'] for e in rep['built']} | {e['n'] for e in added}
    b = Bundle(open(gp, 'rb').read())
    lists = {}
    for lid, ids in pk['lists'].items():
        lid = int(lid)
        keep = [i for i in ids if i - T.RACE_BASE in built]
        x = b.find(lid)
        d = bytes(b.data(x))
        io_, = struct.unpack_from('<I', x['e'], 0x40)
        imp = d[io_:io_ + 16]
        rec = bytearray(d[:0x2C])
        struct.pack_into('<III', rec, 0x20, 0x2C, len(keep), 4)
        rec += struct.pack('<%dI' % len(keep), *keep)
        while len(rec) % 16:
            rec.append(0)
        struct.pack_into('<I', x['e'], 0x40, len(rec))
        rec += imp
        b.set_data(x, rec)
        lists[lid] = keep
    open(out_gp, 'wb').write(b.build())

    npts = build_triggers(hp_trig, hpr_trig, out_trig)
    with open(out_names, 'w') as f:
        f.write('// Generated by build_v3.py: names of the Remastered-only Single Race routes, set by the helper into\n'
                '// empty HP2010 language strings (the ids the events use as their name).\n#pragma once\n#include <stdint.h>\n'
                'struct RouteName { uint32_t stringId; const wchar_t* text; };\nstatic const RouteName kRouteNames[] = {\n')
        for nid, nm, n, src, osid in names_out:
            f.write('    {%d, L"%s"},  // route %d, from %d (Remastered string %d)\n' % (nid, nm, n, src, osid))
        f.write('};\n')
    json.dump(dict(added=added, skipped=skipped, lists=lists, trigger_points_added=npts), open(out_report, 'w'), indent=1)
    print('added %d routes (%d events), skipped %s; %d trigger points; lists %s' % (len(added), 3 * len(added),
          [s['n'] for s in skipped], npts, {k: len(v) for k, v in lists.items()}))


if __name__ == '__main__':
    main(*sys.argv[1:12])
