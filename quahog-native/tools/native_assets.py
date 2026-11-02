"""Native scene/RARC assembly. No executable or ISO writes. Author: Randyadr."""
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
import re, struct, json, hashlib
import CJ_Archive as arc
U32=lambda b,o=0: struct.unpack_from('>I',b,o)[0]
U16=lambda b,o=0: struct.unpack_from('>H',b,o)[0]

def key(b):
    n=0
    for c in b:n=(n*3+c)&65535
    return n

def string(s):
    b=s.encode('shift_jis') if isinstance(s,str) else s
    if len(b)>65535:raise ValueError('Scene string too long')
    return struct.pack('>H',len(b))+b

def read_string(b,p):
    if p+2>len(b):raise ValueError('Truncated scene string')
    n=U16(b,p);p+=2
    if p+n>len(b):raise ValueError('Truncated scene string data')
    return b[p:p+n],p+n

@dataclass
class Node:
    type: str
    name: bytes
    payload: bytes=b''
    children: object=None
    prefix: bytes=b''
    def encode(self):
        t=self.type.encode('ascii')
        payload=self.payload if self.children is None else self.prefix+struct.pack('>I',len(self.children))+b''.join(n.encode() for n in self.children)
        body=struct.pack('>H',key(t))+string(t)+struct.pack('>H',key(self.name))+string(self.name)+payload
        return struct.pack('>I',len(body)+4)+body
    def walk(self):
        yield self
        for child in self.children or []:yield from child.walk()

SCENE_CONTAINERS={'MarScene','SmJ3DScn'}
CONTAINERS={'GroupObj','NameRefGrp','IdxGroup','Strategy','LightAry','AmbAry','ScenarioArchiveNameTable','ScenarioArchiveNamesInStage','ScenarioArchiveNames','StageArcNameTable','StageArcNames','MarioPositionObj'}|SCENE_CONTAINERS

class SceneFormatError(ValueError):
    """A known scene container is malformed; never hide it as an opaque leaf."""

def scene_children_offset(payload):
    # TSmJ3DScn::loadSuper loads TLightMap before the inherited object list.
    # Each lighting reference is a u32 slot and a length-prefixed name. Its
    # size varies; using a fixed 0/4-byte offset loses all scene descendants.
    if len(payload)<4:raise SceneFormatError('Missing scene light-map count')
    count=U32(payload);pos=4
    if count>10000 or count>(len(payload)-4)//6:
        raise SceneFormatError('Invalid scene light-map count: '+str(count))
    try:
        for _ in range(count):
            if pos+4>len(payload):raise ValueError('Missing light-map slot')
            _,pos=read_string(payload,pos+4)
    except (ValueError,struct.error) as error:
        raise SceneFormatError('Truncated scene light-map entry: '+str(error)) from error
    if pos+4>len(payload):raise SceneFormatError('Missing scene child count after light map')
    return pos

def parse(b):
    if len(b)<12 or U32(b)!=len(b):raise ValueError('Invalid scene record length')
    typ,p=read_string(b,6)
    if not re.fullmatch(rb'[A-Za-z_][A-Za-z_0-9]*',typ):raise ValueError('Invalid scene record type')
    if U16(b,4)!=key(typ):raise ValueError('Invalid scene type hash')
    if p+2>len(b):raise ValueError('Missing scene name hash')
    h=U16(b,p);name,p=read_string(b,p+2)
    if h!=key(name):raise ValueError('Invalid scene name hash')
    n=Node(typ.decode(),name,b[p:])
    # Exact record framing, not scanning arbitrary payload for strings.
    strict=n.type in SCENE_CONTAINERS
    try:offsets=(scene_children_offset(n.payload),) if strict else (4,) if n.type=='IdxGroup' else (0,)
    except SceneFormatError as error:raise SceneFormatError(n.type+': '+str(error)) from error
    for off in offsets:
        if len(n.payload)<off+4:continue
        count=U32(n.payload,off)
        if count>10000:
            if strict:raise SceneFormatError(n.type+': Invalid scene child count '+str(count))
            continue
        if not count and n.type not in CONTAINERS and not n.type.endswith(('Table','Ary')):continue
        pos=off+4;children=[]
        try:
            for _ in range(count):
                size=U32(n.payload,pos)
                if size<12 or pos+size>len(n.payload):raise ValueError('Child length')
                children.append(parse(n.payload[pos:pos+size]));pos+=size
            if pos!=len(n.payload):
                if strict:raise SceneFormatError(n.type+': Trailing data after scene children')
                continue
        except SceneFormatError:raise
        except (ValueError,struct.error) as error:
            if strict:raise SceneFormatError(n.type+': Invalid scene child record: '+str(error)) from error
            continue
        n.children=children;n.prefix=n.payload[:off];break
    return n

def unpack(raw):
    a=arc.RARC(arc.yaz0_decode(raw) if raw[:4]==b'Yaz0' else raw)
    files={};ids={}
    for name,(e,off,size) in a.items.items():
        files[name]=a.src[a.data_start+off:a.data_start+off+size]
        ids[name]=(U16(a.src,e),a.src[e+4])
    return files,ids

def pack(files,ids=None):
    """General nested RARC writer; retain existing file IDs and compression flags."""
    ids=ids or {};roots={p.split('/')[0] for p in files}
    if len(roots)!=1:raise ValueError('Archive needs one root')
    root=next(iter(roots));dirs={root:[]}
    for path in sorted(files):
        parts=path.split('/')
        if any(p in ('','..','.') for p in parts):raise ValueError('Unsafe archive path')
        for i in range(1,len(parts)):
            d='/'.join(parts[:i]);dirs.setdefault(d,[])
    names=sorted(dirs,key=lambda x:(x.count('/'),x));index={p:i for i,p in enumerate(names)}
    strings=bytearray();offsets={}
    def st(s):
        if s not in offsets:offsets[s]=len(strings);strings.extend(s.encode('shift_jis')+b'\0')
        return offsets[s]
    nodes=[];entries=[];data=bytearray();nextid=max([v[0] for v in ids.values() if v[0]!=65535]+[-1])+1
    for d in names:
        first=len(entries);leaf=d.rsplit('/',1)[-1]
        for child in names:
            if child.rpartition('/')[0]!=d:continue
            c=child.rsplit('/',1)[-1];entries.append(struct.pack('>HHIIII',65535,key(c.encode('shift_jis')),0x02000000|st(c),index[child],16,0))
        for path,payload in sorted(files.items()):
            if path.rpartition('/')[0]!=d:continue
            c=path.rsplit('/',1)[-1];fid,flags=ids.get(path,(nextid,0x11))
            if path not in ids:nextid+=1
            if fid>=65535:raise ValueError('RARC file ID overflow')
            data.extend(bytes(-len(data)%32));off=len(data);data.extend(payload)
            flags=(flags&~0x60)|0x11 # MRAM data, preserving internal compression bits
            entries.append(struct.pack('>HHIIII',fid,key(c.encode('shift_jis')),flags<<24|st(c),off,len(payload),0))
        for c,target in [('.',index[d]),('..',index.get(d.rpartition('/')[0],0xFFFFFFFF))]:
            entries.append(struct.pack('>HHIIII',65535,key(c.encode()),0x02000000|st(c),target,16,0))
        nodes.append(struct.pack('>4sIHHI',b'ROOT' if d==root else leaf.upper().encode('ascii','replace')[:4].ljust(4,b' '),st(leaf),key(leaf.encode('shift_jis')),len(entries)-first,first))
    b=bytearray(64);b[:4]=b'RARC';struct.pack_into('>I',b,8,32)
    node_off=len(b);b+=b''.join(nodes);b+=bytes(-len(b)%32)
    entry_off=len(b);b+=b''.join(entries);b+=bytes(-len(b)%32)
    string_off=len(b);b+=strings;b+=bytes(-len(b)%32)
    data_off=len(b);data+=bytes(-len(data)%32);b+=data
    struct.pack_into('>5I',b,12,data_off-32,len(data),len(data),0,0)
    struct.pack_into('>6I',b,32,len(nodes),node_off-32,len(entries),entry_off-32,len(strings),string_off-32)
    struct.pack_into('>HBBI',b,56,nextid,0,0,0);struct.pack_into('>I',b,4,len(b))
    result=bytes(b);got,_=unpack(result)
    if got!=files:raise ValueError('Archive round-trip failed')
    return result

def unique(files,suffix):
    found=[p for p in files if p.endswith(suffix)]
    if len(found)!=1:raise ValueError('Expected one '+suffix+', found '+str(found))
    return found[0]

def static(name,alias):
    return Node('MapStaticObj',name.encode(),struct.pack('>9f',0,0,0,0,0,0,1,1,1)+string('')+struct.pack('>I',0)+string(alias))

ALIASES=['riccoSea']+['riccoSeaPollutionS'+str(i) for i in range(5)]+['mareSeaPollutionS0','mareSeaPollutionS12','mareSeaPollutionS34567']
NO_MODEL={'riccoSea','mareSeaPollutionS34567'}

def map_offset(p):
    groups=U32(p);off=4
    if groups>64:raise ValueError('Unsupported Map translucent groups')
    for _ in range(groups):
        count=U32(p,off);off+=4+count*8
        if count>10000 or off>len(p):raise ValueError('Invalid Map group')
    if off+24>len(p):raise ValueError('Truncated Map collision/warp settings')
    return off

def enlarge_map(node,quahog=False):
    off=map_offset(node.payload);old=struct.unpack_from('>5I',node.payload,off)
    if quahog:
        # Exact mesh (865,075 planes). Generous dynamic and broadphase headroom.
        node.payload=struct.pack('>7I',0,128,128,1000000,5000000,20000,0)
    else:
        values=(max(old[0],64),max(old[1],64),old[2]+12000,old[3]+200000,old[4])
        node.payload=node.payload[:off]+struct.pack('>5I',*values)+node.payload[off+20:]

CORE={'Map','MapObjManager','MapObjBaseManager','ItemManager','Sky','SunMgr','SunModel','SunsetModel','Shimmer','Pollution','StickyStain','Mario','MarioPositionObj','Light','IdxLight','AmbColor','normalLight','shadowLight','MLight','Camera','PolarCamera','MapDrawWall'}

def clean_scene(node,removed):
    if node.children is not None:
        node.children=[n for n in node.children if clean_scene(n,removed)]
        return True
    if node.type in CORE or any(x in node.type for x in ('Camera','Cube','Position','Light','Amb','Table','Manager','Conductor')):
        # Camera/trigger cubes from the old secret course cannot govern Quahog.
        # Their table nodes are emptied below when framed as child arrays.
        return True
    removed.append(node.type+':'+node.name.decode('shift_jis','replace'));return False

def require_scene_map(scene,label,log=None):
    nodes=list(scene.walk());maps=[n for n in nodes if n.type=='Map']
    types={}
    for node in nodes:types[node.type]=types.get(node.type,0)+1
    summary='root='+scene.type+', records='+str(len(nodes))+', types='+','.join(k+':'+str(v) for k,v in sorted(types.items())[:24])
    if log:log(label+' scene: '+summary)
    if len(maps)!=1:raise ValueError(label+': expected one Map record, found '+str(len(maps))+'; '+summary)
    return maps[0]

def build_quahog(raw,assets,common_types=(),log=None):
    files,ids=unpack(raw);scene_path=unique(files,'/map/scene.bin');scene=parse(files[scene_path]);removed=[]
    if scene.encode()!=files[scene_path]:raise ValueError('Quahog scaffold scene did not round-trip')
    native_map=require_scene_map(scene,'Quahog scaffold',log)
    if not clean_scene(scene,removed):raise ValueError('Unsupported scene root')
    enlarge_map(native_map,True)
    # Native hit objects need a loaded map manager. Refuse rather than publish
    # a stage that would dereference a null manager.
    table_types=set()
    for path,data in files.items():
        if path.endswith('/map/tables.bin'):table_types.update(n.type for n in parse(data).walk())
    if 'MapObjManager' not in set(common_types)|table_types|{n.type for n in scene.walk()}:
        raise ValueError('Scaffold lacks MapObjManager; unsupported stage layout')
    group=next((n for n in scene.walk() if n.name=='マップグループ'.encode('shift_jis') and n.children is not None),None)
    if group is None:raise ValueError('Scaffold lacks native map group')
    root=scene_path.split('/')[0]
    folder=root+'/map/map/'
    for i,alias in enumerate(ALIASES,1):
        group.children.append(static('QuahogNativeReady' if i==1 else 'QuahogCollision'+str(i),alias))
        files[folder+alias.lower()+'.col']=(assets/('quahog_%02d.col'%i)).read_bytes()
        if alias not in NO_MODEL:files[folder+alias.lower()+'.bmd']=(assets/'collision_stub.bmd').read_bytes()
    files[root+'/map/map.col']=(assets/'quahog_00.col').read_bytes()
    files[folder+'map.bmd']=(assets/'quahog.bmd').read_bytes()
    # Strip scripts, old map animation and fixed camera recordings.
    for p in list(files):
        if '/map/sp/' in p or ('/map/map/map.' in p and not p.endswith('.bmd')):
            del files[p]
        elif p.endswith('/map/tables.bin'):
            tables=parse(files[p])
            for n in tables.walk():
                if n.children is not None and ('Cube' in n.type or 'キューブ' in n.name.decode('shift_jis','replace')):n.children=[]
            files[p]=tables.encode()
    for n in scene.walk():
        if n.children is not None and ('Cube' in n.type or 'キューブ' in n.name.decode('shift_jis','replace')):n.children=[]
    files[scene_path]=scene.encode()
    return pack(files,ids),removed

STAGE_TABLE_NAME='ステージ毎シナリオアーカイブ名群'.encode('shift_jis')

def stage_slots(raw,log=None):
    """Find the same named table as TApplication::initialize_nlogoAfter.

    stageArc.bin can wrap ScenarioArchiveNamesInStage in NameRefGrp records.
    Only that table's direct children are areas; only their direct children
    are episodes. Recursively flattening them would change stage numbers.
    """
    document=parse(raw)
    nodes=list(document.walk())
    named=[n for n in nodes if n.name==STAGE_TABLE_NAME]
    typed=[n for n in nodes if n.type=='ScenarioArchiveNamesInStage']
    if named:
        if len(named)!=1:raise ValueError('Ambiguous stage table: multiple game-named stage lists')
        table=named[0]
    elif typed:
        if len(typed)!=1:raise ValueError('Ambiguous stage table: multiple ScenarioArchiveNamesInStage lists')
        table=typed[0]
    else:
        # Retain support for direct two-level tables. Never guess which child
        # of an unknown wrapper is an area list.
        table=document
        if table.children is None or not all(g.children is not None and
                all(n.children is None for n in g.children) for g in table.children):
            types=','.join(n.type for n in (document.children or [])[:8])
            raise ValueError('Cannot locate stage list: root='+document.type+'; children='+types)
    if table.children is None:raise ValueError('Stage list has no readable area array: '+table.type)
    if len(table.children)>256:raise ValueError('Stage list exceeds the game area index range')
    if log:log('Stage table: root='+document.type+', selected='+table.type+', areas='+str(len(table.children)))
    out=[]
    for area,group in enumerate(table.children):
        if group.children is None:raise ValueError('Unreadable episode array at area '+str(area)+': '+group.type)
        if table.type=='ScenarioArchiveNamesInStage' and group.type!='ScenarioArchiveNameTable':
            raise ValueError('Unsupported area record at index '+str(area)+': '+group.type)
        if len(group.children)>256:raise ValueError('Episode index range exceeded at area '+str(area))
        for episode,n in enumerate(group.children):
            where='area '+str(area)+', episode '+str(episode)+' ('+n.type+')'
            if n.children is not None:raise ValueError('Nested episode array inside '+where)
            try:
                value,end=read_string(n.payload,0)
                if end!=len(n.payload):raise ValueError('Unexpected trailing archive payload')
                archive=value.decode('ascii')
                if '\0' in archive:raise ValueError('Archive name contains a NUL')
            except (ValueError,UnicodeDecodeError) as error:
                raise ValueError('Invalid stage archive at '+where+': '+str(error)) from error
            out.append((area,episode,archive,n))
    if not out:raise ValueError('No stage archives in table')
    return document,out

def verify_assets(assets):
    meta=json.loads((assets/'assets.json').read_text())
    for name,info in meta['files'].items():
        p=assets/name
        if p.name!=name:raise ValueError('Unsafe manifest name')
        b=p.read_bytes()
        if len(b)!=info['size'] or hashlib.sha256(b).hexdigest()!=info['sha256']:raise ValueError('Damaged asset: '+name)
    return meta

def fnv64(data):
    value=14695981039346656037
    for byte in data:
        value=((value^byte)*1099511628211)&0xffffffffffffffff
    return format(value,'016x')

def prepare(disc_path,output,assets,log=print):
    verify_assets(assets);disc=arc.Disc(disc_path)
    try:
        fst=disc.files();_,slots=stage_slots(disc.file('data/stagearc.bin'),log)
        def stem(s):return PurePosixPath(s.lower()).stem
        target=next((s for s in slots if stem(s[2])=='dolpic_ex0'),None)
        if target is None:raise ValueError('Selected ISO has no dolpic_ex0 slot; nothing installed')
        area,episode,archive,_=target
        if not 16<=area<=254 or not 0<=episode<=254:raise ValueError('Unsupported native stage slot')
        log('Quahog target: area='+str(area)+', episode='+str(episode)+', archive='+archive)
        def archive_path(name):
            basename=stem(name)
            for ext in ('.szs','.arc'):
                p='data/scene/'+basename+ext
                if p in fst:return p
            raise ValueError('Missing original stage archive '+name)
        output.mkdir(parents=True,exist_ok=True);(output/'maps').mkdir(exist_ok=True)
        rows=['QUAHOG_NATIVE_OVERRIDES_2',str(area)+' '+str(episode)]
        def save(path,filename,original,replacement):
            (output/filename).write_bytes(replacement)
            log('Verifying prepared archive: '+filename)
            rows.append(' '.join([path,filename,fnv64(original),fnv64(replacement),str(len(original))]))
        path=archive_path(archive);log('Preparing native Quahog in existing stage '+path)
        common_types={n.type for n in parse(disc.file('data/scenecmn.bin')).walk()} if 'data/scenecmn.bin' in fst else set()
        raw=disc.file(path);world,removed=build_quahog(raw,assets,common_types,log)
        save(path,'maps/quahog.arc',raw,world)
        plazas=sorted({archive_path(s[2]) for s in slots if s[0]==1 and re.fullmatch(r'dolpic\d+',stem(s[2]))})
        if not plazas:raise ValueError('No normal Delfino Plaza archives in area 1')
        location=None
        for p in plazas:
            raw=disc.file(p)
            if location is None:
                from portal_location import market
                try:
                    files,_=unpack(raw);scene=parse(files[unique(files,'/map/scene.bin')])
                    location=market(scene,files[unique(files,'/map/map.col')])
                except (ValueError,KeyError,struct.error) as e:log('Portal location scan: '+str(e))
        if location:
            (output/'portal-layout.txt').write_text(' '.join(map(str,location['position']+[location['yaw']]))+'\n')
            log('Market portal: '+str(location))
        else:log('Market wall not resolved; use F1 > Quahog > Place portal here at the marked wall. Direct travel remains available.')
        (output/'overrides.txt').write_text('\n'.join(rows)+'\n',encoding='ascii',newline='\n')
        report={'mode':'quahog','portal_location':location,'game_id':disc.game_id.decode(),'area':area,'episode':episode,'base_stage':archive,'plazas':plazas,'removed_scaffold_objects':removed,'engine_executable_modified':False,'iso_modified':False,'stage_table_modified':False,'activation':'native mod DLL owns all file overrides'}
        (output/'preparation-report.json').write_text(json.dumps(report,indent=2)+'\n')
        return report
    finally:disc.close()
