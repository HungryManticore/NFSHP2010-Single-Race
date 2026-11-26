# Single Race event set for NFS Hot Pursuit (2010), v2 (2026-11-26) = v1 + weapons: every Hot Pursuit (both sides)
# uses the Single Race weapon sets (player d0ed6077 = 2300101, AI 43c51331 = 2300100; WEAPONS overlay) and no per-car
# sets (OpponentData 2a48f62b = 0 for every racer and cop), so the helper's charges apply to everyone.
# v1:
# Same ids as the Remastered mod (picker_v8.json: route n -> Race 2100000+n, Hot Pursuit 2200000+n, Cop HP 2400000+n),
# limited to the routes whose source records exist in HP2010 (Remastered-only routes come later).
#   python build_v1.py <GAMELOGIC\CHALLENGEINSTANCES.BNDL> <GAMELOGIC\GAMEPLAY.BNDL> <picker_v8.json>
#                      <out CHALLENGEINSTANCES> <out GAMEPLAY> <out report.json>
# Events
#   - multiplayer routes (n < 100): built from the route's own online record (Race: its online Race, else online HP;
#     HP / Cop HP: its online HP, else online Race), as in Remastered v6+.
#   - career routes (n >= 100): template Race 744273 / HP 389908, with Location[], RestrictedRoadSections, name,
#     time of day (start, finish, cycle) and sun direction from the career event.
#   - Race: 7 AI racers + balancing + loading screens from career race 321314 COAST TO COAST; intro 712428.
#   - HP: 6 AI racers (572352's 4 + 572370's first 2) and 6 start cops (572392's cop), count fields 3 / 4;
#     balancing + loading screens from 572392 HOTTING UP; intro 0 (-> 278552).
#   - Cop HP: 7 AI racers (572338 RUN TO THE HILLS, triggers cleared), no AI cops, medals and balancing from 572338,
#     CopOfflineIntro 278552.
#   - every event: empty car list (every car), CopOfflineIntro 0 unless cop.
# GAMEPLAY: the 12 online location event lists (744417-744428) list the Single Race Race ids of picker_v8, so the
#   multiplayer picker (Single Race picker) shows them. HP2010's lists are shorter, so each array is rebuilt.
import sys, struct, json
from gen32 import *
from bnd32 import Bundle

RACE_BASE, HP_BASE, COP_BASE = 2100000, 2200000, 2400000
ONLINE_RACE, ONLINE_HP = 649479, 675155
RACE_TEMPLATE, HP_TEMPLATE = 744273, 389908
RACE_GRID = [2, 0, 7, 5, 6, 4, 1, 3]
HP_GRID = [3, 0, 1, 2, 4, 5, 6, 7]
RACE_INTRO, COP_INTRO = 712428, 278552
RACE_FROM, HP_FROM, COP_FROM = 321314, 572392, 572338
LOADING = (0xB4E09E08, 0x1A53B730)
BAL = (0xCC1A2667, 0x1C6E495F, 0xE8350282, 0xA745E92D, 0x8381EA95, 0xC9023F81, 0x395BFCD0, 0xD6271585)
F_TOD, F_TOD_FINISH, F_TOD_CYCLE, F_SUN = 0xDA6AC384, 0x21A309D6, 0xD4459243, 0xD6529487
ROUTE_SCALARS = (F_NAME, F_TOD, F_TOD_FINISH, F_TOD_CYCLE, F_SUN)
HP_RACERS, HP_COPS = 3, 4
F_PLAYER_SET, F_AI_SET = 0xD0ED6077, 0x43C51331
WEAPONS = {F_PLAYER_SET: 2300101, F_AI_SET: 2300100}
LIST_HASH = 0x69322E6B


def main(ci_path, gp_path, picker_path, out_ci, out_gp, out_report):
    g = Inst(open(ci_path, 'rb').read())
    ev = g.events
    pk = json.load(open(picker_path))
    v6 = {r['n']: r for r in pk['routes_v6']}

    def fields(e, hashes):
        out = {}
        for h in hashes:
            if h in g.defs[EV_HASH]:
                ft = g.field(h)[1]
                v = g.scalar(ev[e], h)
                out[h] = float(v) if ft in (1, 9) else int(v)
        return out

    race_opps = g.array(ev[RACE_FROM], F_OPP)[:7]
    hp_opps = g.array(ev[572352], F_OPP)[:4] + g.array(ev[572370], F_OPP)[:2]
    cop_t = g.array(ev[HP_FROM], F_COPS)[0]
    cop_racers = g.array(ev[COP_FROM], F_OPP)[:7]
    assert len(race_opps) == 7 and len(hp_opps) == 6 and len(cop_racers) == 7
    race_ovr = {**fields(RACE_FROM, LOADING + BAL), F_INTRO: RACE_INTRO, F_COP_INTRO: 0}
    hp_ovr = {**fields(HP_FROM, LOADING + BAL), F_INTRO: 0, F_COP_INTRO: 0, F_RACERS: HP_RACERS, F_COPCOUNT: HP_COPS, **WEAPONS}
    cop_ovr = {**fields(COP_FROM, LOADING + BAL), F_INTRO: 0, F_COP_INTRO: COP_INTRO, **WEAPONS}

    def build(n, race_tmpl, hp_tmpl, route, scalars):
        r = [g.opponent(p) for p in race_opps]
        g.make_event(RACE_BASE + n, race_tmpl, route if route else race_tmpl, 'Race', r, [], race_ovr, RACE_GRID,
                     route_scalars=scalars)
        r = [g.opponent(p, f2a48=0) for p in hp_opps]
        c = [g.opponent(cop_t, pct=0.0, trig=0, f2a48=0, flags=(0, 0)) for _ in range(6)]
        g.make_event(HP_BASE + n, hp_tmpl, route if route else hp_tmpl, 'HP', r, c, hp_ovr, HP_GRID,
                     route_scalars=scalars)
        r = [g.opponent(p, trig=0, f2a48=0) for p in cop_racers]
        m = [g.clone_sub(p) for p in g.array(ev[COP_FROM], F_MEDALS)]
        g.make_event(COP_BASE + n, hp_tmpl, route if route else hp_tmpl, 'HPCop', r, [], cop_ovr, HP_GRID,
                     route_scalars=scalars, medals=m)

    report, built, skipped = [], set(), []
    for e in pk['report']:
        if e['kind'] == 'excluded':
            continue
        n = e['race'] - RACE_BASE
        if n in built:
            continue
        if n < 100:
            o = v6[n]
            race_src = o['online_race'] if o['online_race'] in ev else o['online_hp']
            hp_src = o['online_hp'] if o['online_hp'] in ev else o['online_race']
            if race_src not in ev or hp_src not in ev:
                skipped.append(dict(n=n, event=e['event'], location=e['location'], source=[o['online_race'], o['online_hp']]))
                continue
            build(n, race_src, hp_src, None, ())
            src = [race_src, hp_src]
        else:
            ce = e['career_event']
            if ce not in ev:
                skipped.append(dict(n=n, event=e['event'], location=e['location'], source=[ce]))
                continue
            build(n, RACE_TEMPLATE, HP_TEMPLATE, ce, ROUTE_SCALARS)
            src = [ce]
        built.add(n)
        report.append(dict(n=n, event=e['event'], location=e['location'], race=RACE_BASE + n, hp=HP_BASE + n,
                           cop=COP_BASE + n, source=src))
    open(out_ci, 'wb').write(g.finish())

    # picker lists (GAMEPLAY)
    b = Bundle(open(gp_path, 'rb').read())
    lists = {}
    for lid, ids in pk['lists'].items():
        lid = int(lid)
        keep = [i for i in ids if i - RACE_BASE in built]
        x = b.find(lid)
        d = bytes(b.data(x))
        dp, h, i = struct.unpack_from('<III', d, 8)
        assert (dp, h, i) == (0x20, LIST_HASH, lid)
        io, = struct.unpack_from('<I', x['e'], 0x40)
        ic, = struct.unpack_from('<H', x['e'], 0x48)
        assert ic == 1 and io + 16 == len(d)
        imp = d[io:io + 16]
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
    json.dump(dict(built=report, skipped=skipped, lists=lists), open(out_report, 'w'), indent=1)
    print('built %d routes (%d events), skipped %d; lists: %s' % (len(built), 3 * len(built), len(skipped),
          {k: len(v) for k, v in lists.items()}))


if __name__ == '__main__':
    main(*sys.argv[1:7])
