# Single Race event generator for NFS Hot Pursuit (2010): 32-bit records in GameChanger 411487
# (GAMELOGIC/CHALLENGEINSTANCES.BNDL).
#   instance: [P] 8-byte schema import slot, [P+8] u32 data ptr (= P+0x20), u32 type hash, u32 id, [P+0x20] data
#   arrays:   u32 ptr, u32 count, u32 element size (4)
#   root:     data at 0x20 = array of u32 instance pointers, sorted by event id
# New data goes between the last record and the import table; existing records are not touched except the root
# array pointer/count, which point to a new, longer copy.
import struct
from bnd32 import Bundle

GC_ID = 411487
DEF_ID = 224082
EV_HASH = 0x73226C8B           # ChallengeInstance (Remastered: 885312E1)
HDR, DATA = 0x08, 0x20         # instance header / data offsets

F_MODE, F_RACERS, F_OPP, F_COPS, F_CARS, F_INTRO = 0x58BEB7AF, 0xD3F44163, 0x76A943AB, 0x088E607C, 0xABEA5997, 0xBA99EF81
F_COPCOUNT, F_LOCATIONS, F_COP_INTRO, F_MEDALS = 0x98647E19, 0x67DEA92A, 0xCEA5CE5C, 0x7C7A9566
F_NAME, F_RRS = 0x4E616D65, 0x714F260B
MODES = {'Race': 256655, 'HP': 675150, 'HPCop': 256667}


def load_defs(b):
    """GeneSysDefinition resources (type 0x12): hash -> {field hash: (hash, type, count, offset, size)}, data size."""
    defs = {}
    for x in b.ents:
        if struct.unpack_from('<I', x['e'], 0x44)[0] != 0x12:
            continue
        d = b.data(x)
        h = struct.unpack_from('<I', d, 0)[0]
        fl, o = {}, 0x44
        while o + 20 <= len(d):
            t = struct.unpack_from('<5I', d, o)
            if t[0] == 0 and t[1] == 0:
                break
            fl[t[0]] = t
            o += 20
        defs[h] = fl
    return defs


class Inst:
    def __init__(self, raw):
        self.b = Bundle(raw)
        self.ent = self.b.find(GC_ID)
        self.d = bytes(self.b.data(self.ent))
        e = self.ent['e']
        self.imp_off, = struct.unpack_from('<I', e, 0x40)
        self.imp_cnt, = struct.unpack_from('<H', e, 0x48)
        assert self.imp_off + 16 * self.imp_cnt == len(self.d)
        self.imports = [struct.unpack_from('<IIII', self.d, self.imp_off + 16 * k) for k in range(self.imp_cnt)]
        self.imp_at = {off & 0x7FFFFFFF: (a, f) for a, f, off, z in self.imports}
        self.defs = load_defs(self.b)
        self.sizes = {h: max(fo + fs * fc for _, _, fc, fo, fs in v.values()) for h, v in self.defs.items()}
        dp, h, i = struct.unpack_from('<III', self.d, HDR)
        assert (dp, h, i) == (DATA, 0x3F409A86, GC_ID)
        self.root_ptr, self.root_cnt, es = struct.unpack_from('<III', self.d, DATA)
        assert es == 4
        self.events = {}
        for k in range(self.root_cnt):
            p, = struct.unpack_from('<I', self.d, self.root_ptr + 4 * k)
            dp, h, i = struct.unpack_from('<III', self.d, p + HDR)
            assert h == EV_HASH and dp == p + DATA
            self.events[i] = p
        self.new = bytearray()
        self.new_imports = []
        self.new_events = {}
        self.share = False      # identical new sub-instances are written once and shared (v3: import / size limits)
        self.shared = {}

    # ---- reading
    def field(self, h, sh=EV_HASH):
        return self.defs[sh][h]

    def inst_data(self, p):
        dp, h, i = struct.unpack_from('<III', self.d, p + HDR)
        return h, i, self.d[dp:dp + self.sizes[h]]

    def array(self, p, h, sh=EV_HASH):
        _, ft, _, fo, _ = self.field(h, sh)
        ap, cnt, es = struct.unpack_from('<III', self.d, p + DATA + fo)
        return [struct.unpack_from('<I', self.d, ap + es * k)[0] for k in range(cnt)]

    def scalar(self, p, h):
        _, ft, _, fo, _ = self.field(h)
        if ft == 2:
            return self.d[p + DATA + fo]
        if ft in (1, 9):
            return struct.unpack_from('<f', self.d, p + DATA + fo)[0]
        return struct.unpack_from('<I', self.d, p + DATA + fo)[0]

    def import_of(self, p, h):
        return self.imp_at[p + DATA + self.field(h)[3]][0]

    # ---- writing (offsets are absolute in the final blob)
    def alloc(self, n):
        while len(self.new) % 16:
            self.new.append(0)
        off = self.imp_off + len(self.new)
        self.new += bytes(n)
        return off

    def w(self, off, fmt, *v):
        struct.pack_into(fmt, self.new, off - self.imp_off, *v)

    def wb(self, off, data):
        self.new[off - self.imp_off:off - self.imp_off + len(data)] = data

    def rd_new(self, off, fmt):
        return struct.unpack_from(fmt, self.new, off - self.imp_off)

    def add_import(self, rid, flags, off):
        self.new_imports.append((rid, flags, 0x80000000 | off, 0))

    def new_instance(self, h, iid, data, sub_flags):
        key = (h, iid, bytes(data), sub_flags)
        if self.share and key in self.shared:
            return self.shared[key]
        P = self.alloc(DATA + len(data))
        if self.share:
            self.shared[key] = P
        self.add_import(DEF_ID, sub_flags, P)
        self.w(P + HDR, '<III', P + DATA, h, iid)
        self.wb(P + DATA, data)
        return P

    def clone_sub(self, p_sub, patch=None):
        h, i, data = self.inst_data(p_sub)
        flags = self.imp_at[p_sub][1]
        data = bytearray(data)
        if patch:
            patch(data)
        return self.new_instance(h, i, bytes(data), flags)

    def ptr_array(self, ptrs):
        off = self.alloc(max(4 * len(ptrs), 4))
        for k, p in enumerate(ptrs):
            self.w(off + 4 * k, '<I', p)
        return off

    def opponent(self, template_p, vehicle=None, pct=None, trig=None, f2a48=None, flags=None):
        def patch(b):
            if vehicle is not None: struct.pack_into('<I', b, 0x14, vehicle)
            if pct is not None: struct.pack_into('<f', b, 0x04, pct)
            if trig is not None: struct.pack_into('<I', b, 0x08, trig)
            if f2a48 is not None: struct.pack_into('<I', b, 0x0C, f2a48)
            if flags is not None: b[0x18], b[0x19] = flags
        return self.clone_sub(template_p, patch)

    def make_event(self, new_id, template_id, route_id, mode, racers, cops, overrides=None, grid_order=None,
                   route_scalars=(), array_from=None, medals=None):
        """New event from template_id (every field), with Location[] and RestrictedRoadSections from route_id,
        route_scalars (field hashes) copied from route_id, the given opponent lists (sub-instance pointers),
        an empty car list and the scalar overrides {field hash: value} (float -> f32, int -> u32 / byte)."""
        src, rsrc = self.events[template_id], self.events[route_id]
        array_from = dict(array_from or {})
        _, _, data = self.inst_data(src)
        _, _, rdata = self.inst_data(rsrc)
        data = bytearray(data)
        P = self.alloc(DATA + len(data))
        self.add_import(DEF_ID, self.imp_at[src][1], P)
        self.w(P + HDR, '<III', P + DATA, EV_HASH, new_id)
        D = P + DATA
        for fh, ft, fc, fo, fs in self.defs[EV_HASH].values():
            if fh in route_scalars:
                data[fo:fo + fs * fc] = rdata[fo:fo + fs * fc]
            elif ft == 6:
                rid, flags = self.imp_at[src + DATA + fo]
                if fh == F_MODE:
                    rid = MODES[mode]
                self.add_import(rid, flags, D + fo)
            elif ft == 0x1008:
                s = rsrc if fh in (F_LOCATIONS, F_RRS) else self.events[array_from.get(fh, template_id)]
                if fh == F_OPP:
                    subs = racers
                elif fh == F_COPS:
                    subs = cops
                elif fh == F_MEDALS and medals is not None:
                    subs = medals
                elif fh == F_LOCATIONS and grid_order:
                    def reorder(b):
                        ids = struct.unpack_from('<8I', b, 4)
                        struct.pack_into('<8I', b, 4, *[ids[k] for k in grid_order])
                    locs = self.array(s, fh)
                    subs = [self.clone_sub(locs[0], reorder)] + [self.clone_sub(p) for p in locs[1:]]
                else:
                    subs = [self.clone_sub(p) for p in self.array(s, fh)]
                ap = self.ptr_array(subs)
                es = struct.unpack_from('<I', data, fo + 8)[0] or 4
                struct.pack_into('<III', data, fo, ap, len(subs), es)
            elif ft == 0x1007:
                vals = [] if fh == F_CARS else self.array(src, fh)
                ap = self.alloc(max(4 * len(vals), 4))
                for k, v in enumerate(vals):
                    self.w(ap + 4 * k, '<I', v)
                es = struct.unpack_from('<I', data, fo + 8)[0] or 4
                struct.pack_into('<III', data, fo, ap, len(vals), es)
        struct.pack_into('<I', data, self.field(F_RACERS)[3], len(racers))
        struct.pack_into('<I', data, self.field(F_COPCOUNT)[3], len(cops))
        for fh, v in (overrides or {}).items():
            _, ft, _, fo, _ = self.field(fh)
            if ft == 2:
                data[fo] = v
            elif isinstance(v, float):
                struct.pack_into('<f', data, fo, v)
            else:
                struct.pack_into('<I', data, fo, v)
        self.wb(D, bytes(data))
        assert new_id not in self.events and new_id not in self.new_events
        self.new_events[new_id] = P
        return P

    def finish(self):
        allev = sorted(list(self.events.items()) + list(self.new_events.items()))
        rp = self.ptr_array([p for _, p in allev])
        root = bytearray(self.d[:self.imp_off])
        struct.pack_into('<II', root, DATA, rp, len(allev))
        while len(self.new) % 16:
            self.new.append(0)
        imps = bytearray()
        for t in list(self.imports) + self.new_imports:
            imps += struct.pack('<IIII', *t)
        blob = bytes(root) + bytes(self.new) + bytes(imps)
        e = self.ent['e']
        struct.pack_into('<I', e, 0x40, self.imp_off + len(self.new))
        n = len(self.imports) + len(self.new_imports)
        assert n < 0x10000
        struct.pack_into('<H', e, 0x48, n)
        self.b.set_data(self.ent, blob)
        return self.b.build()
