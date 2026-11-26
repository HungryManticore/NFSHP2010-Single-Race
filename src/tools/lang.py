import sys,struct; sys.path.insert(0,'/home/claude/w/hp2010')
from bnd32 import Bundle
_b=Bundle(open('/mnt/user-data/uploads/Need for Speed Hot Pursuit/UI/LANGUAGE/0001.BNDL','rb').read())
_d=bytes(_b.data(_b.ents[0],0)); _one,_cnt,_oi,_oe=struct.unpack_from('<4I',_d,0); IDS=struct.unpack_from('<%dI'%_cnt,_d,_oi)
S={}
for i,k in enumerate(IDS):
    p,n=struct.unpack_from('<II',_d,_oe+8*i); S[k]=_d[p:p+2*n].decode('utf-16-le','replace').rstrip('\0')
