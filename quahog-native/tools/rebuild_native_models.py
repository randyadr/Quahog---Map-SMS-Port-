"""Convert the actual v165 meshes to native Sunshine BMD/COL. Randyadr."""
import sys, pathlib, struct, json, copy, hashlib
import numpy as np
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',required=True,help='Extracted Skin Selector 2.14.165 folder')
parser.add_argument('--output',default=str(pathlib.Path(__file__).resolve().parents[1]/'assets'))
args=parser.parse_args()
SRC=pathlib.Path(args.source).resolve()
OUT=pathlib.Path(args.output).resolve()
sys.path.insert(0,str(SRC))
import Skin_ModularMesh as mm
P16=mm.p16;P32=mm.p32;pad=mm.pad
template=(SRC/'Templates/Legacy_Model.bmd').read_bytes()
material_template=mm.materials(template)[0]

def chunk(tag,data):
 b=bytearray(tag.encode()+b'\0'*4+data);pad(b);P32(b,4,len(b));return b

def material(i,texture,flags):
 r=copy.deepcopy(material_template);r['name']='world_%03d'%i
 rec=bytearray(mm.unblob(r['record']));rec[0]=4 if flags in (2,3) else 1;r['record']=mm.blob(rec)
 def v(off,*values):r['values'][str(off)]=[mm.blob(x) if x is not None else None for x in values]
 v(1,struct.pack('>I',0));v(8,b'\xff'*4,None);v(12,bytes([0,1,0,2,2,0,255,255]),bytes([0,1,0,2,2,0,255,255]),None,None)
 v(20,b'\xff'*4,None);v(132,struct.pack('>H',texture),*[None]*7)
 v(188,bytes([0,0,4,255]),*[None]*15)
 v(228,bytes([255,15,8,10,15,0,0,0,1,0,7,4,5,7,0,0,0,1,0,255]),*[None]*15)
 v(6,bytes([1,3,0 if flags in (2,3) else 1,255]))
 v(326,bytes([4,20,0,7,0,255,255,255]));v(328,bytes([1 if flags in (2,3) else 0,4,5,5]))
 v(324,bytes(44));r['indirect']=mm.blob(bytes(312));return r

def bmd(groups,textures):
 # One root, one visible child: MapWarp may put other root children to sleep.
 points=np.concatenate([g[0]['p'] for g in groups]);lo=points.min(0);hi=points.max(0);radius=float(np.linalg.norm(points,axis=1).max())
 j=bytearray(24);j[:4]=b'JNT1';P16(j,8,2);P16(j,10,65535);pad(j);P32(j,12,len(j))
 for _ in range(2):j+=struct.pack('>HBB3f3hH3f7f',1,0,255,1,1,1,0,0,0,65535,0,0,0,radius,*lo,*hi)
 pad(j);P32(j,16,len(j));j+=struct.pack('>2H',0,1);pad(j);P32(j,20,len(j));j+=mm.strings(['root','world']);pad(j);P32(j,4,len(j))
 d=bytearray(20);d[:4]=b'DRW1';P16(d,8,1);P16(d,10,65535);pad(d);P32(d,12,len(d));d+=b'\0';pad(d);P32(d,16,len(d));d+=b'\0\1';pad(d);P32(d,4,len(d))
 e=chunk('EVP1',struct.pack('>HH4I',0,65535,0,0,0,0))
 v=bytearray(64);v[:4]=b'VTX1';P32(v,8,64)
 for attr,count,typ in [(9,1,4),(11,1,5),(13,1,4),(255,0,0)]:v+=struct.pack('>IIIB3x',attr,count,typ,0)
 for off,raw in [(12,bytes(12)),(24,b'\xff'*4),(32,bytes(8))]:pad(v);P32(v,off,len(v));v+=raw
 pad(v);P32(v,4,len(v))
 sh=bytearray(44);sh[:4]=b'SHP1';P16(sh,8,len(groups));P16(sh,10,65535);pad(sh);P32(sh,12,len(sh));recbase=len(sh);sh+=bytes(40*len(groups));pad(sh);P32(sh,16,len(sh));sh+=struct.pack('>'+str(len(groups))+'H',*range(len(groups)));pad(sh);P32(sh,24,len(sh))
 for attr in [0,9,11,13]:sh+=struct.pack('>II',attr,1)
 sh+=struct.pack('>II',255,0);pad(sh);P32(sh,28,len(sh));palbase=len(sh)
 packets=[];records=[];materials=[];material_keys={};shape_mats=[];packet_count=0
 for verts,tex,flags in groups:
  # J3DMaterial holds one shape pointer. Sharing one material index among
  # different shapes silently renders only the last assigned shape.
  shape_mats.append(len(materials));materials.append(material(len(materials),tex,flags))
  rec=bytearray(40);rec[0]=0;rec[1]=255;P16(rec,4,0);P16(rec,6,packet_count);P16(rec,8,packet_count);P16(rec,10,65535)
  lo=verts['p'].min(0);hi=verts['p'].max(0);struct.pack_into('>7f',rec,12,float(np.linalg.norm(verts['p'],axis=1).max()),*lo,*hi)
  pieces=[]
  for start in range(0,len(verts),60000):
   a=verts[start:start+60000];out=np.zeros(len(a),dtype=[('matrix','u1'),('p','>f4',3),('c','u1',4),('uv','>f4',2)]);out['p']=a['p'];out['uv']=a['uv'];out['c']=a['rgba']
   raw=bytearray(b'\x90'+struct.pack('>H',len(a))+out.tobytes());pad(raw);pieces.append(raw)
  P16(rec,2,len(pieces));records.append(rec);packets+=pieces;packet_count+=len(pieces)
 sh+=b'\0\0'*packet_count;pad(sh);P32(sh,32,len(sh));dlbase=len(sh);displays=[]
 for p in packets:displays.append((len(p),len(sh)-dlbase));sh+=p
 pad(sh);P32(sh,36,len(sh))
 for i in range(packet_count):sh+=struct.pack('>HHI',0,1,i)
 pad(sh);P32(sh,40,len(sh))
 for p in displays:sh+=struct.pack('>II',*p)
 pad(sh)
 for i,rec in enumerate(records):sh[recbase+i*40:recbase+(i+1)*40]=rec
 P32(sh,4,len(sh))
 inf=bytearray(24);inf[:4]=b'INF1';P16(inf,8,0);P16(inf,10,65535);P32(inf,12,packet_count);P32(inf,16,1);P32(inf,20,24)
 tree=[(16,0),(1,0),(16,1),(1,0)]
 for i,mi in enumerate(shape_mats):tree.extend([(17,mi),(1,0),(18,i),(2,0)])
 tree.extend([(2,0),(2,0),(0,0)]);inf+=b''.join(struct.pack('>HH',*x) for x in tree);pad(inf);P32(inf,4,len(inf))
 tex=[]
 for w,h,data in textures:
  head=bytearray(32);head[0]=6;head[1]=2;P16(head,2,w);P16(head,4,h);head[6]=head[7]=1;head[20]=head[21]=1;head[24]=1;P32(head,28,32);tex.append(bytes(head)+data)
 sections=[inf,v,e,d,j,sh,mm.write_materials(materials),mm.write_textures(tex)]
 raw=bytearray(b'J3D2bmd3'+bytes(24));P32(raw,12,len(sections));raw+=b''.join(sections);P32(raw,8,len(raw));return raw

def write_col(path,vertices,faces,types=None):
 if len(vertices)>32767:raise ValueError('Signed native COL vertex limit')
 if types is None:types=[0]*len(faces)
 groups=[]
 for typ in sorted(set(types)):
  f=[v for v,t in zip(faces,types) if t==typ]
  groups.extend((typ,f[i:i+30000]) for i in range(0,len(f),30000))
 b=bytearray(16);P32(b,0,len(vertices));P32(b,4,16);b+=np.asarray(vertices,dtype='>f4').tobytes();pad(b);P32(b,8,len(groups));P32(b,12,len(b));off=len(b);b+=bytes(len(groups)*24)
 for i,(typ,f) in enumerate(groups):
  pad(b);indices=len(b);b+=np.asarray(f,dtype='>i2').tobytes();zero=len(b);b+=bytes(len(f));data=len(b);b+=bytes(len(f))
  struct.pack_into('>HhHH4I',b,off+i*24,typ,len(f),0,0,indices,zero,data,0)
 pad(b);path.write_bytes(b);return {'file':path.name,'vertices':len(vertices),'triangles':len(faces),'bytes':len(b)}

def collision_parts(tris,prefix,types=None):
 verts=[];faces=[];t=[];lookup={};out=[]
 def flush():
  out.append(write_col(OUT/(prefix+'_%02d.col'%len(out)),verts,faces,t))
 for i,tri in enumerate(tris):
  keys=[tuple(map(float,p)) for p in tri]
  if faces and len(lookup)+sum(k not in lookup for k in keys)>32760:flush();verts=[];faces=[];t=[];lookup={}
  row=[]
  for k in keys:
   if k not in lookup:lookup[k]=len(verts);verts.append(k)
   row.append(lookup[k])
  faces.append(row);t.append(int(types[i]) if types is not None else 0)
 if faces:flush()
 return out

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 q=(SRC/'Source/Quahog/Quahog.bin').read_bytes();nt,nc,nv,np_,td,cd,vo,co,po=struct.unpack_from('<9I',q,20)
 dtype=np.dtype([('p','<f4',3),('uv','<f4',2),('rgba','u1',4)]);vertices=np.frombuffer(q,dtype,count=nv,offset=vo)
 groups=[]
 for i in range(nc):
  tx,flags,first,count=struct.unpack_from('<4I',q,cd+i*40);groups.append((vertices[first:first+count],tx,flags))
 textures=[]
 for i in range(nt):
  off,n,w,h=struct.unpack_from('<4I',q,td+i*16);textures.append((w,h,q[off:off+n]))
 print('Writing native Quahog model',flush=True);model=bmd(groups,textures);(OUT/'quahog.bmd').write_bytes(model)
 tris=np.frombuffer(q,'<f4',count=np_*9,offset=co).reshape(-1,3,3)
 # Real nozzle boxes provide their own dynamic collision; remove the old fixed proxy cubes.
 tris=np.concatenate([tris[:865023],tris[865059:]])
 print('Partitioning exact native collision',flush=True);cols=collision_parts(tris,'quahog')
 if len(cols)>12:raise ValueError('Exact collision needs more static slots than available: '+str(len(cols)))
 # Some registered static-collision actors require a model. Keep their
 # mandatory model invisible and underground; the main map supplies visuals.
 dummy=np.zeros(3,dtype=dtype);dummy['p']=[(0,-50000,0),(1,-50000,0),(0,-50000,1)]
 (OUT/'collision_stub.bmd').write_bytes(bmd([(dummy,0,0)],[(4,4,bytes(64))]))
 meta={'version':'0.4.4','mode':'quahog','author':'Randyadr','source':'Skin Selector 2.14.165 actual meshes','quahog_triangles':nv//3,'quahog_chunks':nc,'quahog_textures':nt,'collision':cols,'spawn':list(struct.unpack_from('<3f',q,64)),'return_portal':list(struct.unpack_from('<3f',q,80)),'native_format':'J3D2bmd3 + Sunshine COL; no custom world renderer','features':'Experimental native map and portal conversion; old scripted missions not migrated.'}
 meta['files']={p.name:{'size':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(OUT.iterdir()) if p.suffix in ('.bmd','.col')}
 (OUT/'assets.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps({k:v for k,v in meta.items() if k!='files'},indent=2),flush=True)
if __name__=='__main__':main()
