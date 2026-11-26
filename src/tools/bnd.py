import sys,struct,zlib,re,os,collections
def parse(path,outdir=None):
    d=open(path,'rb').read()
    assert d[:4]==b'bnd2'
    ver,plat,dbg,cnt,eoff=struct.unpack_from('<IIIII',d,4)
    offs=struct.unpack_from('<4I',d,0x18); flags,=struct.unpack_from('<I',d,0x28)
    names={}
    if flags&8:
        x=d[dbg:offs[0] if dbg<offs[0] else len(d)].decode('latin1')
        for m in re.finditer(r'<Resource id="([0-9a-fA-F]+)" type="([^"]*)" name="([^"]*)"',x):
            names[int(m.group(1),16)]=(m.group(2),m.group(3))
    res=[]
    for i in range(cnt):
        b=eoff+i*0x50
        rid,ih=struct.unpack_from('<QQ',d,b)
        us=struct.unpack_from('<4I',d,b+0x10); ds=struct.unpack_from('<4I',d,b+0x20); do=struct.unpack_from('<4I',d,b+0x30)
        io,tid,ic,fl,si=struct.unpack_from('<IIHBB',d,b+0x40)
        blobs=[]
        for k in range(4):
            sz=ds[k]&0x0fffffff
            if sz==0: blobs.append(b''); continue
            raw=d[offs[k]+do[k]:offs[k]+do[k]+sz]
            try: blobs.append(zlib.decompress(raw) if flags&1 else raw)
            except Exception as e: blobs.append(raw)
        res.append(dict(id=rid,type=tid,name=names.get(rid),blobs=blobs,imports=ic))
    return dict(ver=ver,flags=flags,res=res,names=names)
if __name__=='__main__':
    b=parse(sys.argv[1])
    c=collections.Counter((r['type'],(r['name'] or ('?',''))[0]) for r in b['res'])
    print(sys.argv[1],'ver',b['ver'],'flags',hex(b['flags']),'n',len(b['res']))
    for k,v in sorted(c.items()): print('  type %#x %-30s x%d'%(k[0],k[1],v))
