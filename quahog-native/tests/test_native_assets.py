"""Format safety and scene migration tests; does not claim a game playtest."""
from pathlib import Path
import sys, struct, unittest, tempfile, hashlib, json
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import native_assets as w

class Formats(unittest.TestCase):
    def test_nested_archive_retains_file_ids(self):
        files={'scene/map/scene.bin':b'abc','scene/map/map/map.bmd':bytes(range(256))*7,'scene/message/dialogue.bmg':b'msg','scene/x':b''}
        ids={'scene/map/scene.bin':(153,0x11),'scene/message/dialogue.bmg':(900,0x11)}
        packed=w.pack(files,ids);result,ri=w.unpack(packed)
        self.assertEqual(result,files);self.assertEqual(ri['scene/map/scene.bin'][0],153);self.assertEqual(ri['scene/message/dialogue.bmg'][0],900)
        self.assertEqual(len(set(v[0] for v in ri.values())),len(files))
    def test_record_with_unknown_payload_round_trips(self):
        n=w.Node('GroupObj','シーン'.encode('shift_jis'),children=[w.Node('IdxGroup',b'Actors',children=[w.Node('CustomActor',b'Keep Me',b'\x12\xff\0opaque\0')],prefix=struct.pack('>I',3))])
        raw=n.encode();p=w.parse(raw)
        self.assertEqual(p.encode(),raw);self.assertEqual(p.children[0].prefix,struct.pack('>I',3));self.assertEqual(p.children[0].children[0].payload,b'\x12\xff\0opaque\0')
    def test_native_scenes_read_lighting_before_child_records(self):
        # TSmJ3DScn::loadSuper -> TLightMap::load, then PtrList's count/children.
        prefixes=[struct.pack('>I',0),struct.pack('>II',2,0)+w.string('照明群')+struct.pack('>I',1)+w.string('Ambient Light')]
        for kind in ('MarScene','SmJ3DScn'):
            for prefix in prefixes:
                with self.subTest(kind=kind,prefix_size=len(prefix)):
                    native_map=w.Node('Map',b'map',struct.pack('>7I',0,32,32,10000,50000,700,0))
                    node=w.Node(kind,b'normal scene',children=[native_map],prefix=prefix)
                    outer=w.Node('GroupObj',b'root',children=[node]);raw=outer.encode();parsed=w.parse(raw)
                    self.assertEqual(parsed.encode(),raw);self.assertEqual(parsed.children[0].prefix,prefix)
                    self.assertEqual([n.type for n in parsed.walk()],['GroupObj',kind,'Map'])
    def test_empty_native_scene_round_trips(self):
        for kind in ('MarScene','SmJ3DScn'):
            raw=w.Node(kind,b'empty',prefix=struct.pack('>I',0),children=[]).encode()
            parsed=w.parse(raw);self.assertEqual(parsed.children,[]);self.assertEqual(parsed.encode(),raw)
    def test_malformed_scene_lighting_is_not_hidden_as_opaque_data(self):
        payloads=[b'',struct.pack('>I',0xffffffff),struct.pack('>IIH',1,0,500)+b'x',struct.pack('>I',0)]
        for payload in payloads:
            node=w.Node('MarScene',b'bad',payload)
            outer=w.Node('GroupObj',b'root',children=[node])
            with self.subTest(payload=payload),self.assertRaises(w.SceneFormatError):w.parse(outer.encode())
    def test_bad_scene_children_propagate_through_outer_groups(self):
        for payload in (struct.pack('>III',0,1,4096)+b'bad child',struct.pack('>II',0,0)+b'trailing'):
            node=w.Node('SmJ3DScn',b'bad',payload)
            with self.assertRaises(w.SceneFormatError):w.parse(w.Node('GroupObj',b'root',children=[node]).encode())
    def test_missing_or_duplicate_map_diagnostics_include_scene_structure(self):
        for children,count in (([],0),([w.Node('Map',b'a'),w.Node('Map',b'b')],2)):
            node=w.Node('MarScene',b'scene',children=children,prefix=struct.pack('>I',0))
            with self.assertRaisesRegex(ValueError,'found '+str(count)+'.*root=MarScene'):
                w.require_scene_map(node,'Quahog scaffold')
    def test_invalid_records_fail_closed(self):
        b=bytearray(w.Node('Map',b'map',bytes(30)).encode())
        for bad in (b[:-1],b'\0'*40,bytes(b[:4])+b'XX'+bytes(b[6:])):
            with self.assertRaises(ValueError):w.parse(bad)
    def test_plaza_map_preserves_warps_and_translucency(self):
        # Two priority groups, three child references; opaque remainder is retained.
        xlu=struct.pack('>10I',2,2,0,1,0,2,1,0,3,0)[:-4]
        pools=struct.pack('>5I',32,32,10000,50000,700)
        tail=struct.pack('>I',1)+b'warp payload retained'
        n=w.Node('Map',b'map',xlu+pools+tail);off=w.map_offset(n.payload)
        w.enlarge_map(n)
        self.assertEqual(n.payload[:off],xlu);self.assertEqual(n.payload[off+20:],tail)
        self.assertEqual(struct.unpack_from('>5I',n.payload,off),(64,64,22000,250000,700))
    def test_stage_table_uses_actual_indices(self):
        leaf=lambda name:w.Node('ScenarioArchiveName',b'episode',w.string(name))
        table=w.Node('NameRefGrp',b'stages',children=[w.Node('NameRefGrp',b'area',children=[leaf('empty.arc')]) for _ in range(24)])
        table.children[23].children=[leaf('other.arc'),leaf('dolpic_ex0.arc')]
        parsed,slots=w.stage_slots(table.encode());found=[s for s in slots if s[2]=='dolpic_ex0.arc'][0]
        self.assertEqual(found[:2],(23,1));found[3].payload=w.string('quahog_native.arc')
        self.assertIn(b'quahog_native.arc',parsed.encode());self.assertNotIn(b'quahog_native.arc',table.encode())
    def wrapped_stages(self):
        leaf=lambda name:w.Node('ScenarioArchiveName',b'episode',w.string(name))
        areas=[w.Node('ScenarioArchiveNameTable',b'area',children=[]) for _ in range(80)]
        areas[1].children=[leaf('dolpic0.arc'),leaf('dolpic1.arc')]
        areas[23].children=[leaf('other.arc'),leaf('dolpic_ex0.arc')]
        areas[79].children=[leaf('last.arc')]
        table=w.Node('ScenarioArchiveNamesInStage',w.STAGE_TABLE_NAME,children=areas)
        # Unrelated records before the table must not shift any area index.
        root=w.Node('NameRefGrp',b'root',children=[w.Node('EventTable',b'events',children=[]),
            w.Node('NameRefGrp',b'wrapper',children=[table])])
        return root,table
    def test_wrapped_native_stage_table_preserves_empty_area_indices(self):
        root,_=self.wrapped_stages();raw=root.encode();parsed,slots=w.stage_slots(raw)
        self.assertEqual([(s[0],s[1],s[2]) for s in slots],[(1,0,'dolpic0.arc'),(1,1,'dolpic1.arc'),
            (23,0,'other.arc'),(23,1,'dolpic_ex0.arc'),(79,0,'last.arc')])
        self.assertEqual(parsed.encode(),raw)
    def test_native_stage_table_direct_root(self):
        _,table=self.wrapped_stages();_,slots=w.stage_slots(table.encode())
        self.assertEqual(next(s[:2] for s in slots if s[2]=='dolpic_ex0.arc'),(23,1))
    def test_stage_table_game_name_wins_over_unrelated_typed_table(self):
        root,_=self.wrapped_stages()
        root.children.insert(0,w.Node('ScenarioArchiveNamesInStage',b'unrelated',children=[]))
        _,slots=w.stage_slots(root.encode());self.assertEqual(len(slots),5)
    def test_stage_table_ambiguous_name_is_rejected(self):
        root,table=self.wrapped_stages();root.children.append(table)
        with self.assertRaisesRegex(ValueError,'Ambiguous stage table'):w.stage_slots(root.encode())
    def test_stage_table_type_lookup_without_original_name(self):
        root,table=self.wrapped_stages();table.name=b'custom name'
        _,slots=w.stage_slots(root.encode());self.assertEqual(next(s[:2] for s in slots if s[2]=='dolpic_ex0.arc'),(23,1))
        root.children.append(w.Node('ScenarioArchiveNamesInStage',b'other',children=[]))
        with self.assertRaisesRegex(ValueError,'Ambiguous stage table'):w.stage_slots(root.encode())
    def test_stage_table_does_not_flatten_nested_episode_groups(self):
        root,table=self.wrapped_stages()
        table.children[23].children=[w.Node('NameRefGrp',b'bad',children=table.children[23].children)]
        with self.assertRaisesRegex(ValueError,'Nested episode array inside area 23, episode 0'):w.stage_slots(root.encode())
    def test_stage_table_bad_area_does_not_shift_later_indices(self):
        root,table=self.wrapped_stages();table.children[5]=w.Node('UnknownArea',b'bad',b'opaque')
        with self.assertRaisesRegex(ValueError,'Unreadable episode array at area 5'):w.stage_slots(root.encode())
    def test_world_assembly_keeps_core_removes_old_gameplay(self):
        native_map=w.Node('Map',b'map',struct.pack('>7I',0,32,32,10000,50000,700,0))
        group=w.Node('GroupObj','マップグループ'.encode('shift_jis'),children=[native_map,w.Node('MapObjManager',b'manager',b'params'),w.Node('Goomba',b'old enemy',bytes(100))])
        scene=w.Node('GroupObj',b'Scene',children=[group])
        files={'scene/map/scene.bin':scene.encode(),'scene/map/sp/old.sb':b'oldscript','scene/map/map/map.btk':b'oldanimation','scene/map/map/sea.bmd':b'original sea'}
        packed,removed=w.build_quahog(w.pack(files),ROOT/'assets');out,_=w.unpack(packed);nodes=list(w.parse(out['scene/map/scene.bin']).walk())
        self.assertFalse(any(n.type=='Goomba' for n in nodes));self.assertIn('Goomba:old enemy',removed)
        self.assertEqual(sum(n.type=='MapStaticObj' for n in nodes),9)
        self.assertEqual(out['scene/map/map/sea.bmd'],b'original sea')
        self.assertNotIn('scene/map/sp/old.sb',out);self.assertNotIn('scene/map/map/map.btk',out)
        self.assertEqual(out['scene/map/map/map.bmd'],(ROOT/'assets/quahog.bmd').read_bytes())
        self.assertIn('scene/map/map/riccosea.col',out)
    def test_shipped_assets_hashes(self):
        m=w.verify_assets(ROOT/'assets')
        self.assertEqual(len(m['collision']),10)
        self.assertEqual(sum(p['triangles'] for p in m['collision']),865075)
    def test_native_collision_offsets_indices_and_counts(self):
        for path in (ROOT/'assets').glob('*.col'):
            data=path.read_bytes();nv,vo,ng,go=struct.unpack_from('>4I',data)
            self.assertLessEqual(nv,32767);self.assertLessEqual(vo+12*nv,len(data));self.assertLessEqual(go+24*ng,len(data))
            for i in range(ng):
                typ,n,flags,padding,io,a,b,extra=struct.unpack_from('>HhHH4I',data,go+24*i)
                self.assertGreater(n,0);self.assertLessEqual(n,32767);self.assertEqual(flags,0)
                self.assertLessEqual(io+n*6,len(data));self.assertLessEqual(a+n,len(data));self.assertLessEqual(b+n,len(data))
                inds=struct.unpack_from('>'+str(n*3)+'h',data,io);self.assertGreaterEqual(min(inds),0);self.assertLess(max(inds),nv)

if __name__=='__main__':unittest.main(verbosity=2)
