import struct,sys
sys.path.insert(0,'/home/claude/w/_mod_sources/SingleRace_events')
from bnd import parse
H="/mnt/user-data/uploads/Need for Speed Hot Pursuit/GAMELOGIC/CHALLENGEINSTANCES.BNDL"
R="/mnt/user-data/uploads/Need For Speed Hot Pursuit Remastered/GAMELOGIC/CHALLENGEINSTANCES.BAK"
def load(P,w):
  b=parse(P)
  defs={}
  for r in b['res']:
    if r['type']==0x12:
      d=r['blobs'][0]; h=struct.unpack_from('<I',d,0)[0]
      off=0x44 if w==4 else 0x48
      fl=[];o=off
      while o+20<=len(d):
        t=struct.unpack_from('<5I',d,o)
        if t[0]==0 and t[1]==0: break
        fl.append(t);o+=20
      defs[h]={t[0]:t for t in fl}
  gc=[r for r in b['res'] if r['id']&0xffffffff==411487][0]
  d=gc['blobs'][0]
  P_=lambda o: struct.unpack_from('<I' if w==4 else '<Q',d,o)[0]
  hdr=0x8 if w==4 else 0x10; base=0x20 if w==4 else 0x30
  ap=P_(base); cnt=struct.unpack_from('<I',d,base+w)[0]
  ev={}
  imp_off=gc['imports']
  # imports table at end: entries (u64 id, u32 off, u32 0)
  ic=gc['imports']; it=d[len(d)-16*ic:]
  imps={}
  for k in range(ic):
    a,f,off,z=struct.unpack_from('<IIII',it,16*k); imps[off&0x7fffffff]=a
  for k in range(cnt):
    p=P_(ap+w*k)
    dp=P_(p+hdr); h,i=struct.unpack_from('<II',d,p+hdr+w)
    ev[i]=dp
  return dict(d=d,w=w,ev=ev,defs=defs,imps=imps,P=P_)
def fld(G,dp,h,eh):
  t=G['defs'][eh][h]; ft,fo=t[1],t[3]; d=G['d'];w=G['w']
  if ft==6: return G['imps'].get(dp+fo)
  if ft in (0x1007,0x1008):
    ap=G['P'](dp+fo); c,es=struct.unpack_from('<II',d,dp+fo+w)
    return [ (G['P'] if ft==0x1008 else (lambda o: struct.unpack_from('<I',d,o)[0]))(ap+es*j) for j in range(c)]
  if ft in (1,9): return round(struct.unpack_from('<f',d,dp+fo)[0],3)
  if ft==2: return d[dp+fo]
  return struct.unpack_from('<%dI'%t[2],d,dp+fo) if t[2]>1 else struct.unpack_from('<I',d,dp+fo)[0]
