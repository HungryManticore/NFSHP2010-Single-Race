# NFS11.exe field-name hash (0x811C70 -> 0xA56880): MSB-first CRC-32 with the exe's table at 0xD585A0.
import struct, pefile
_p = pefile.PE('/mnt/user-data/uploads/Need for Speed Hot Pursuit/NFS11.exe', fast_load=True)
T = struct.unpack('<256I', _p.get_data(0xD585A0 - 0x400000, 1024))
def h(s):
    b = s.encode() if isinstance(s, str) else s
    if not b: return 0
    if len(b) < 4:
        eax = b[0]
        for c in b:
            eax = ((eax << 8) | c) & 0xFFFFFFFF ^ T[eax & 0xFF]  # short path (unused for our names)
        return eax
    eax = (~((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3])) & 0xFFFFFFFF
    for c in b[4:]:
        eax = (((eax << 8) & 0xFFFFFFFF) | c) ^ T[eax >> 24]
    return (~eax) & 0xFFFFFFFF
if __name__ == '__main__':
    import sys
    for s in sys.argv[1:]: print('%08x %s' % (h(s), s))
