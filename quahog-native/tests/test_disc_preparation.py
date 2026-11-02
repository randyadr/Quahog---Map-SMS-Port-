"""Full preparation on authored miniature discs, not a user ISO or game test.

Uses the real Disc/FST, stage table, RARC and cache code without mocks.
The wrapper/table records follow the upstream game's named-table lookup.
"""
from pathlib import Path
import hashlib,json,struct,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import native_assets as world
import boot_prepare as boot

def write_disc(path,game_id,files):
    tree={}
    for name,data in files.items():
        parts=name.split('/');node=tree
        for part in parts[:-1]:node=node.setdefault(part,{})
        node[parts[-1]]=data
    records=[];strings=bytearray(b'\0')
    def visit(name,value,parent):
        index=len(records);offset=len(strings);strings.extend(name.encode('ascii')+b'\0')
        record=[isinstance(value,dict),offset,parent,0,value];records.append(record)
        if record[0]:
            for child,data in sorted(value.items()):visit(child,data,index)
            record[3]=len(records)
    visit('',tree,0)
    fst_offset=0x500;fst_size=len(records)*12+len(strings)
    image=bytearray((fst_offset+fst_size+31)&~31);image[:6]=game_id
    image[0x1c:0x20]=b'\xc2\x33\x9f\x3d';struct.pack_into('>3I',image,0x424,fst_offset,fst_size,fst_size)
    for i,(directory,name,parent,end,value) in enumerate(records):
        if directory:words=(0x01000000|name,parent,end)
        else:
            image.extend(bytes(-len(image)%32));offset=len(image);image.extend(value)
            words=(name,offset,len(value))
        struct.pack_into('>3I',image,fst_offset+i*12,*words)
    image[fst_offset+12*len(records):fst_offset+fst_size]=strings
    path.write_bytes(image)

def fixture(root,game_id,wrapped):
    mod=root/'mod';assets=mod/'assets';assets.mkdir(parents=True);(mod/'tools').mkdir()
    # Keep cache invalidation tied to the actual assembler under test.
    for name in ('native_assets.py','CJ_Archive.py'):
        (mod/'tools'/name).write_bytes((ROOT/'tools'/name).read_bytes())
    names=['quahog.bmd','collision_stub.bmd']+['quahog_%02d.col'%i for i in range(10)]
    metadata={'mode':'quahog','files':{}}
    for name in names:
        data=('authored test payload '+name).encode('ascii');(assets/name).write_bytes(data)
        metadata['files'][name]={'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (assets/'assets.json').write_text(json.dumps(metadata))
    leaf=lambda name:world.Node('ScenarioArchiveName',b'episode',world.string(name))
    areas=[world.Node('ScenarioArchiveNameTable',b'area',children=[]) for _ in range(80)]
    areas[1].children=[leaf('dolpic0.arc'),leaf('dolpic1.arc')]
    areas[23].children=[leaf('unused.arc'),leaf('dolpic_ex0.arc')]
    table=world.Node('ScenarioArchiveNamesInStage',world.STAGE_TABLE_NAME,children=areas)
    if wrapped:
        table=world.Node('NameRefGrp',b'root',children=[world.Node('EventTable',b'events',children=[]),
            world.Node('NameRefGrp',b'wrapper',children=[table])])
    native_map=world.Node('Map',b'map',struct.pack('>7I',0,32,32,10000,50000,700,0))
    group=world.Node('IdxGroup','マップグループ'.encode('shift_jis'),prefix=struct.pack('>I',3),children=[])
    strategy=world.Node('Strategy',b'strategy',children=[group])
    lighting=struct.pack('>II',2,0)+world.string('照明群')+struct.pack('>I',1)+world.string('ambient')
    normal=world.Node('MarScene',b'normal',prefix=lighting,children=[native_map,
        world.Node('MapObjManager',b'manager',b'parameters'),strategy])
    indirect=world.Node('SmJ3DScn',b'indirect',prefix=struct.pack('>I',0),children=[])
    scene=world.Node('GroupObj',b'scene',children=[world.Node('LightAry','照明群'.encode('shift_jis'),children=[]),
        world.Node('AmbAry',b'ambient',children=[]),normal,indirect])
    archive=world.pack({'scene/map/scene.bin':scene.encode(),'scene/map/map/map.bmd':b'old map'})
    files={'data/stageArc.bin':table.encode(),
        'data/scene/dolpic_ex0.szs':world.arc.yaz0_encode_literal(archive),
        'data/scene/dolpic0.szs':world.arc.yaz0_encode_literal(archive),
        'data/scene/dolpic1.arc':archive}
    disc=root/'test.iso';write_disc(disc,game_id,files)
    return mod,disc,root/'cache'

class DiscPreparation(unittest.TestCase):
    def check_disc(self,game_id,wrapped):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);mod,disc,cache=fixture(root,game_id,wrapped)
            original=disc.read_bytes();destination=boot.prepare(mod,disc,cache)
            report=json.loads((destination/'preparation-report.json').read_text())
            self.assertEqual(report['game_id'],game_id.decode());self.assertEqual((report['area'],report['episode']),(23,1))
            self.assertEqual(len(report['plazas']),2)
            self.assertEqual(disc.read_bytes(),original)
            # Validate every actual output manifest hash/size against the real
            # source FST and the assembled RARC, not a stubbed prepare callback.
            source=world.arc.Disc(disc)
            try:
                rows=(destination/'overrides.txt').read_text().splitlines()
                self.assertEqual(rows[:2],['QUAHOG_NATIVE_OVERRIDES_2','23 1'])
                for line in rows[2:]:
                    name,file,old_hash,new_hash,size=line.split();before=source.file(name);after=(destination/file).read_bytes()
                    self.assertEqual((world.fnv64(before),len(before)),(old_hash,int(size)))
                    self.assertEqual(world.fnv64(after),new_hash)
                    resources,_=world.unpack(after);nodes=list(world.parse(resources['scene/map/scene.bin']).walk())
                    originals,_=world.unpack(before);old_nodes=list(world.parse(originals['scene/map/scene.bin']).walk())
                    # Lighting bindings and actor-group indices survive
                    # world cleanup exactly, including SJIS.
                    old_prefixes=[(n.type,n.name,n.prefix) for n in old_nodes if n.type in ('MarScene','SmJ3DScn','IdxGroup')]
                    self.assertEqual([(n.type,n.name,n.prefix) for n in nodes if n.type in ('MarScene','SmJ3DScn','IdxGroup')],old_prefixes)
                    self.assertEqual(sum(n.type=='Map' for n in nodes),1)
                    marker=b'QuahogNativeReady'
                    self.assertTrue(any(n.name==marker for n in nodes))
            finally:source.close()
            prepared=(destination/'maps/quahog.arc').stat().st_mtime_ns
            self.assertEqual(boot.prepare(mod,disc,cache),destination)
            self.assertEqual((destination/'maps/quahog.arc').stat().st_mtime_ns,prepared)
            self.assertFalse(list(cache.glob('.prepare-*')))
    def test_eclipse_wrapped_stage_table_full_preparation(self):self.check_disc(b'GMSE04',True)
    def test_sunshine_direct_stage_table_full_preparation(self):self.check_disc(b'GMSE01',False)

if __name__=='__main__':unittest.main(verbosity=2)
