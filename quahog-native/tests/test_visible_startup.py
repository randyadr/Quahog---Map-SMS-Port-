from pathlib import Path
import contextlib,io,tempfile,sys,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import boot_prepare as boot
class VisibleStartup(unittest.TestCase):
    def test_console_and_file_both_receive_failure(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);log=root/'automatic-setup.log';result=root/'active.txt';console=io.StringIO()
            args=['--mod',str(root),'--disc',str(root/'game.iso'),'--cache',str(root/'cache'),'--result',str(result),'--log',str(log)]
            with patch.object(boot,'prepare',side_effect=ValueError('fixture failure')),contextlib.redirect_stdout(console),contextlib.redirect_stderr(console):
                self.assertEqual(boot.main(args),1)
            for text in (console.getvalue(),log.read_text()):
                self.assertIn('fixture failure',text);self.assertIn('Traceback',text)
            self.assertFalse(result.exists())
    def test_console_log_and_result_on_success(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);log=root/'automatic-setup.log';result=root/'active.txt';console=io.StringIO()
            def success(*args):boot.log('prepared test map');return root/'prepared'
            args=['--mod',str(root),'--disc',str(root/'game.iso'),'--cache',str(root/'cache'),'--result',str(result),'--log',str(log)]
            with patch.object(boot,'prepare',side_effect=success),contextlib.redirect_stdout(console):
                self.assertEqual(boot.main(args),0)
            self.assertEqual(log.read_text(),console.getvalue());self.assertEqual(result.read_text(),str(root/'prepared')+'\n')
    def test_no_process_restart_or_hidden_process_controls(self):
        source=(ROOT/'src/auto_boot.hpp').read_text()
        for removed in ('CREATE_SUSPENDED','CREATE_NO_WINDOW','DETACHED_PROCESS','ResumeThread(', 'TerminateProcess(', 'CreateJobObject', 'TerminateJobObject', 'EnumWindows(', 'ExitProcess(', 'restart_with_memory', 'SW_HIDE'):
            self.assertNotIn(removed,source)
        self.assertIn('FALSE,CREATE_NEW_CONSOLE',source)
if __name__=='__main__':unittest.main()
