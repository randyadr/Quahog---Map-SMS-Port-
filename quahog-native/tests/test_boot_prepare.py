from pathlib import Path
import json,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import boot_prepare as boot

class AutoPreparation(unittest.TestCase):
    def test_preparation_failure_preserves_startup_history_and_adds_details(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);path=root/'startup.log';path.write_text('existing host API/build log\n')
            boot.log('Quahog scaffold: scene structure diagnostic')
            try:raise ValueError('specific scene failure')
            except ValueError:boot.record_failure(root/'cache')
            text=path.read_text();self.assertTrue(text.startswith('existing host API/build log\n'))
            self.assertIn('Quahog scaffold: scene structure diagnostic',text)
            self.assertIn('Traceback',text);self.assertIn('ValueError: specific scene failure',text)
            # A blocked diagnostic path cannot turn the preparation error into
            # an unrelated I/O failure or prevent the native error dialog.
            path.unlink();path.mkdir()
            try:raise ValueError('original failure')
            except ValueError:boot.record_failure(root/'cache')
    def fixture(self,root):
        mod=root/'mod';(mod/'assets').mkdir(parents=True);(mod/'tools').mkdir();(mod/'assets/assets.json').write_text('{}')
        disc=root/'Eclipse.iso';disc.write_bytes(b'unchanged disc');cache=root/'cache'
        return mod,disc,cache
    @staticmethod
    def build(disc,output,assets,log):
        (output/'maps').mkdir();(output/'maps/quahog.arc').write_bytes(b'RARC prepared');(output/'overrides.txt').write_text('prepared');return {}
    def test_first_boot_prepares_then_reuses_cache_without_game_writes(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);mod,disc,cache=self.fixture(root);exe=root/'sms.exe';exe.write_bytes(b'original exe');settings=root/'settings.txt';settings.write_bytes(b'original settings');value={'game_id':'GMSE04','version':3}
            with patch.object(boot,'identity',return_value=value),patch.object(boot.world,'verify_assets'),patch.object(boot.world,'prepare',side_effect=self.build) as builder:
                path=boot.prepare(mod,disc,cache);again=boot.prepare(mod,disc,cache);self.assertEqual(path,again);self.assertEqual(builder.call_count,1)
                self.assertEqual(disc.read_bytes(),b'unchanged disc');self.assertEqual(exe.read_bytes(),b'original exe');self.assertEqual(settings.read_bytes(),b'original settings')
    def test_corrupt_cached_map_is_rebuilt(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);mod,disc,cache=self.fixture(root);value={'game_id':'GMSE04'}
            with patch.object(boot,'identity',return_value=value),patch.object(boot.world,'verify_assets'),patch.object(boot.world,'prepare',side_effect=self.build) as builder:
                path=boot.prepare(mod,disc,cache);(path/'maps/quahog.arc').write_bytes(b'damaged');boot.prepare(mod,disc,cache);self.assertEqual(builder.call_count,2);self.assertTrue(boot.cache_valid(path,value))
    def test_switching_iso_separates_cache(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);mod,disc,cache=self.fixture(root)
            with patch.object(boot,'identity',side_effect=[{'game_id':'GMSE04'},{'game_id':'GMSE01'}]),patch.object(boot.world,'verify_assets'),patch.object(boot.world,'prepare',side_effect=self.build):
                first=boot.prepare(mod,disc,cache);second=boot.prepare(mod,disc,cache);self.assertNotEqual(first,second);self.assertTrue(first.is_dir() and second.is_dir())
    def test_failed_preparation_preserves_working_cache(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);mod,disc,cache=self.fixture(root);value={'game_id':'GMSE04'}
            with patch.object(boot,'identity',return_value=value),patch.object(boot.world,'verify_assets'),patch.object(boot.world,'prepare',side_effect=self.build):path=boot.prepare(mod,disc,cache)
            with patch.object(boot,'identity',return_value=value),patch.object(boot.world,'verify_assets'),patch.object(boot.world,'prepare',side_effect=ValueError('bad ISO')):
                with self.assertRaisesRegex(ValueError,'bad ISO'):boot.prepare(mod,disc,cache,True)
            self.assertTrue(boot.cache_valid(path,value));self.assertFalse(list(cache.glob('.prepare-*')))
    def test_force_refresh_rebuilds_same_slot(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);mod,disc,cache=self.fixture(root);value={'game_id':'GMSE04'}
            with patch.object(boot,'identity',return_value=value),patch.object(boot.world,'verify_assets'),patch.object(boot.world,'prepare',side_effect=self.build) as builder:
                first=boot.prepare(mod,disc,cache);second=boot.prepare(mod,disc,cache,True);self.assertEqual(first,second);self.assertEqual(builder.call_count,2);self.assertFalse(list(cache.glob('*.previous')))
    def test_reject_cache_manifest_path_traversal(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);(root/'ready.json').write_text(json.dumps({'identity':{},'files':{'../outside':{'size':0,'sha256':''}}}));self.assertFalse(boot.cache_valid(root,{}))
    def test_disc_identity_changes_for_different_iso_and_asset_revisions(self):
        class Disc:
            game_id=b'GMSE04'
            def __init__(self,p):pass
            def file(self,path):return b'original table'
            def close(self):pass
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);mod,disc,cache=self.fixture(root)
            for name in ('native_assets.py','CJ_Archive.py'):(mod/'tools'/name).write_text('version1')
            with patch.object(boot.world.arc,'Disc',Disc):
                one=boot.identity(mod,disc);(mod/'tools/native_assets.py').write_text('version2');two=boot.identity(mod,disc)
                self.assertNotEqual(boot.cache_key(one),boot.cache_key(two));self.assertEqual(one['game_id'],'GMSE04')
if __name__=='__main__':unittest.main(verbosity=2)
