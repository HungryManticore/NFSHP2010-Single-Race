import struct,re,sys
from bnd32 import Bundle
CUSTOM={1267012,1267125}
def palette(path):
  b=Bundle(open(path,'rb').read())
  for x in b.ents:
    d=b.data(x)
    if d is None or len(d)<0x20 or struct.unpack_from('<I',d,0xc)[0]!=0xd33a6af8: continue
    ic=struct.unpack_from('<H',x['e'],0x48)[0]; io=struct.unpack_from('<I',x['e'],0x40)[0]
    return [i for i in (struct.unpack_from('<I',d,io+16*k)[0] for k in range(1,ic)) if i not in CUSTOM]
def hpr_table(p):
  return {int(v):[int(c) for c in cs.split(',')] for v,n,cs in re.findall(r'\{(\d+), (\d+), \{([\d, ]+)\}\}',open(p).read())}
if __name__=='__main__':
  t=hpr_table('/home/claude/w/_mod_sources/SingleRace_helper/PaintTable.h')
  for v in sys.argv[1:]:
    p=palette('/mnt/user-data/uploads/Need for Speed Hot Pursuit/VEHICLES/VEH_%s_MS.BIN'%v)
    print(v,p,'| HPR',t.get(int(v)),'same' if t.get(int(v))==p else 'DIFF')
