# bnd2 v3 reader/writer for the original NFS Hot Pursuit (2010) bundles (zlib-compressed: flags bit 0x1;
# 0x27 = compressed + main/graphics blocks, as in the UI screens). Up to 4 data blocks.
# Unchanged resources keep their original compressed bytes; changed or added ones are compressed (zlib level 9).
import struct, zlib


class Bundle:
    def __init__(self, raw):
        self.raw = bytes(raw)
        assert raw[:4] == b'bnd2'
        self.ver, self.plat, self.dbg, self.cnt, self.eoff = struct.unpack_from('<IIIII', raw, 4)
        self.offs = list(struct.unpack_from('<4I', raw, 0x18))
        self.flags, = struct.unpack_from('<I', raw, 0x28)
        assert self.flags & 1, 'expected a compressed bundle'
        assert self.dbg == len(raw), 'debug data not supported'
        ends = self.offs[1:] + [len(raw)]
        self.blk_align = []
        for k in range(4):
            # alignment of each block's start (taken from the original)
            a = 0x80
            while a < 0x1000 and self.offs[k] % (a * 2) == 0:
                a *= 2
            self.blk_align.append(a)
        self.ents = []
        for i in range(self.cnt):
            b = self.eoff + i * 0x50
            e = bytearray(raw[b:b + 0x50])
            ds = struct.unpack_from('<4I', e, 0x20)
            do = struct.unpack_from('<4I', e, 0x30)
            comp = [raw[self.offs[k] + do[k]:self.offs[k] + do[k] + (ds[k] & 0x0FFFFFFF)] if ds[k] & 0x0FFFFFFF else None
                    for k in range(4)]
            odo = [do[k] if comp[k] is not None else None for k in range(4)]
            self.ents.append({'e': e, 'comp': comp, 'data': [None] * 4, 'changed': [False] * 4, 'odo': odo})

    def rid(self, x):
        return struct.unpack_from('<Q', x['e'], 0)[0]

    def type(self, x):
        return struct.unpack_from('<I', x['e'], 0x44)[0]

    def find(self, rid):
        """Resource by full 64-bit id, or by its low 32 bits when exactly one resource has them."""
        for x in self.ents:
            if self.rid(x) == rid:
                return x
        hits = [x for x in self.ents if self.rid(x) & 0xFFFFFFFF == rid]
        if len(hits) == 1:
            return hits[0]
        raise KeyError('%x: %d matches' % (rid, len(hits)))

    def has(self, rid):
        return any(self.rid(x) == rid or self.rid(x) & 0xFFFFFFFF == rid for x in self.ents)

    def data(self, x, k=0):
        if x['data'][k] is None:
            if x['comp'][k] is None:
                return None
            x['data'][k] = bytearray(zlib.decompress(x['comp'][k]))
            us = struct.unpack_from('<I', x['e'], 0x10 + 4 * k)[0] & 0x0FFFFFFF
            assert us == len(x['data'][k])
        return x['data'][k]

    def set_data(self, x, data, k=0):
        x['data'][k] = bytearray(data)
        x['changed'][k] = True

    def add(self, entry_bytes, blobs):
        """New resource: a 0x50-byte entry (its id, type, import fields as wanted) and up to 4 uncompressed blobs."""
        e = bytearray(entry_bytes)
        x = {'e': e, 'comp': [None] * 4, 'data': [None] * 4, 'changed': [False] * 4}
        for k, b in enumerate(blobs):
            if b is not None:
                x['data'][k] = bytearray(b)
                x['changed'][k] = True
            else:
                struct.pack_into('<I', e, 0x10 + 4 * k, 0)
                struct.pack_into('<I', e, 0x20 + 4 * k, 0)
        self.ents.append(x)
        return x

    def build(self):
        nblk = 4
        out = bytearray(self.raw[:self.eoff])
        hdr_end = self.eoff + 0x50 * len(self.ents)
        out += bytes(hdr_end - len(out))
        # first block starts where the original did if the header did not grow
        start0 = self.offs[0] if hdr_end <= self.offs[0] else -(-hdr_end // 0x10) * 0x10
        out += bytes(start0 - len(out))
        blocks = []
        for k in range(nblk):
            blk = bytearray()
            def orig_do(x):
                # original resources keep their place (changed ones too); added ones go at the end
                return x['odo'][k] if x.get('odo') and x['odo'][k] is not None else 1 << 31
            for x in sorted(self.ents, key=lambda x: (orig_do(x), self.ents.index(x))):
                if x['changed'][k]:
                    x['comp'][k] = zlib.compress(bytes(x['data'][k]), 9)
                    n = len(x['data'][k])
                    struct.pack_into('<I', x['e'], 0x10 + 4 * k, (struct.unpack_from('<I', x['e'], 0x10 + 4 * k)[0] & 0xF0000000) | n)
                    struct.pack_into('<I', x['e'], 0x20 + 4 * k, (struct.unpack_from('<I', x['e'], 0x20 + 4 * k)[0] & 0xF0000000) | len(x['comp'][k]))
                if x['comp'][k] is None:
                    continue
                while len(blk) % (16 if k == 0 else 0x80):
                    blk.append(0)
                struct.pack_into('<I', x['e'], 0x30 + 4 * k, len(blk))
                blk += x['comp'][k]
            blocks.append(blk)
        offs = []
        for k in range(nblk):
            if k > 0:
                while len(out) % 0x80:
                    out.append(0)
            offs.append(len(out))
            out += blocks[k]
        while len(out) % 0x80:
            out.append(0)
        for i, x in enumerate(self.ents):
            out[self.eoff + i * 0x50:self.eoff + i * 0x50 + 0x50] = x['e']
        struct.pack_into('<I', out, 0x10, len(self.ents))
        struct.pack_into('<I', out, 0x0C, len(out))
        for k in range(4):
            struct.pack_into('<I', out, 0x18 + 4 * k, offs[k])
        return bytes(out)
