from pathlib import Path
import sys,unittest,tempfile,json,struct
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from test_disc_preparation import fixture
import native_assets as w
import boot_prepare as boot
import portal_location as location

class IndependentMap(unittest.TestCase):
    def test_only_quahog_stage_is_overridden(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);mod,disc,cache=fixture(root,b'GMSE04',True)
            (mod/'tools/portal_location.py').write_bytes(Path(location.__file__).read_bytes())
            result=boot.prepare(mod,disc,cache)
            rows=(result/'overrides.txt').read_text().splitlines()
            self.assertTrue(boot.cache_valid(result,boot.identity(mod,disc)))
            self.assertEqual(rows[1],'23 1')
            self.assertEqual(len(rows[2:]),1)
            self.assertEqual(rows[2].split()[0],'data/scene/dolpic_ex0.szs')
            self.assertEqual([p.name for p in (result/'maps').iterdir()],['quahog.arc'])
    def test_market_wall_and_floor(self):
        # Actual actor payload layout: position first; model kind later.
        scene=w.Node('GroupObj',b'test',children=[
            w.Node('Fruit',b'coconut',struct.pack('>9f',-250,20,0,0,0,0,1,1,1)+w.string('FruitCoconut')),
            w.Node('Fruit',b'papaya',struct.pack('>9f',250,20,0,0,0,0,1,1,1)+w.string('FruitPapaya'))])
        v=[(-1000,0,-400),(1000,0,-400),(1000,700,-400),(-1000,700,-400),(-2000,0,-2000),(2000,0,-2000),(2000,0,2000),(-2000,0,2000)]
        faces=[(0,1,2),(0,2,3),(4,6,5),(4,7,6)]
        go=16+len(v)*12;io=go+24
        b=struct.pack('>4I',len(v),16,1,go)+b''.join(struct.pack('>3f',*p) for p in v)
        b+=struct.pack('>HhHH4I',0,len(faces),0,0,io,io+len(faces)*6,io+len(faces)*7,0)
        b+=b''.join(struct.pack('>3h',*f) for f in faces)+bytes(len(faces)*2)
        p=location.market(scene,b)
        self.assertIsNotNone(p);self.assertEqual(p['position'],[0,0,-325]);self.assertEqual(p['yaw'],0)
        scene.children.pop();self.assertIsNone(location.market(scene,b))
    def test_malformed_collision_rejected(self):
        with self.assertRaises(ValueError):list(location.triangles(bytes(8)))
        with self.assertRaises(ValueError):list(location.triangles(struct.pack('>4I',100,16,1,16)))
if __name__=='__main__':unittest.main()
