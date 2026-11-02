from pathlib import Path
import hashlib,json,struct,subprocess,sys,tempfile,unittest
from unittest.mock import patch
from test_disc_preparation import fixture
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import boot_prepare as boot

class InstalledPython(unittest.TestCase):
    def test_runtime_requirement(self):
        with patch.object(boot.sys,'version_info',(3,9)),patch.object(boot.struct,'calcsize',return_value=8):
            with self.assertRaisesRegex(RuntimeError,'64-bit Python 3.10'):boot.require_runtime()
        with patch.object(boot.sys,'version_info',(3,14)),patch.object(boot.struct,'calcsize',return_value=4):
            with self.assertRaisesRegex(RuntimeError,'64-bit Python 3.10'):boot.require_runtime()
        with patch.object(boot.sys,'version_info',(3,10)),patch.object(boot.struct,'calcsize',return_value=8):
            boot.require_runtime()

    def test_isolated_external_interpreter_prepares_and_reuses_cache(self):
        with tempfile.TemporaryDirectory(prefix='external python spaces ') as temp:
            root=Path(temp);mod,disc,cache=fixture(root,b'GMSE04',True)
            metadata=json.loads((mod/'assets/assets.json').read_text());metadata['mode']='quahog'
            (mod/'assets/assets.json').write_text(json.dumps(metadata))
            original=hashlib.sha256(disc.read_bytes()).hexdigest()
            result=root/'active.txt';log=root/'automatic-setup.log'
            command=[sys.executable,'-I','-u',str(ROOT/'tools/boot_prepare.py'),'--mod',str(mod),'--disc',str(disc),'--cache',str(cache),'--result',str(result),'--log',str(log)]
            first=subprocess.run(command,cwd=root,capture_output=True,text=True)
            self.assertEqual(first.returncode,0,first.stdout+first.stderr)
            self.assertIn('Using installed Python',first.stdout)
            destination=Path(result.read_text().strip());self.assertTrue((destination/'maps/quahog.arc').is_file())
            self.assertEqual(len((destination/'overrides.txt').read_text().splitlines()),3)
            self.assertFalse((mod/'runtime').exists());self.assertFalse(list(destination.glob('maps/plaza*')))
            second=subprocess.run(command,cwd=root,capture_output=True,text=True)
            self.assertEqual(second.returncode,0,second.stdout+second.stderr)
            self.assertIn('up to date',second.stdout)
            self.assertEqual(second.stdout,log.read_text())
            self.assertEqual(hashlib.sha256(disc.read_bytes()).hexdigest(),original)

if __name__=='__main__':unittest.main()
