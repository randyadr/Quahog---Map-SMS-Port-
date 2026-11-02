"""Prepare the active launcher's disc automatically; never write game/ISO/settings."""
from pathlib import Path
from collections import deque
import argparse, contextlib, hashlib, json, os, re, shutil, struct, sys, tempfile, time, traceback
sys.path.insert(0,str(Path(__file__).resolve().parent))
import native_assets as world
SCHEMA='separate-worlds-v4'
RECENT_LOG=deque(maxlen=64)

def log(text):
    message='[Native map] '+str(text)
    RECENT_LOG.append(message);print(message,flush=True)

def record_failure(cache):
    # The game used to copy only the last error line to startup.log. Include
    # the preparation context and traceback in the same file the user sends.
    # Failure to write diagnostics must not replace the original exception.
    try:
        details=('\n'.join(RECENT_LOG)+'\n'+traceback.format_exc())[-32768:]
        with (cache.parent/'startup.log').open('a',encoding='utf-8') as stream:
            stream.write('\n=== Map preparation failure details ===\n'+details+'\n=== End preparation details ===\n')
    except (OSError,ValueError):pass
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def identity(mod,disc):
    # Fast cache invalidation; the DLL separately verifies every original stage's
    # complete bytes before using a cached replacement.
    st=disc.stat();d=world.arc.Disc(disc)
    try:
        value={'schema':SCHEMA,'disc':str(disc.resolve()),'size':st.st_size,'mtime_ns':st.st_mtime_ns,'game_id':d.game_id.decode(),'stage_table_sha256':hashlib.sha256(d.file('data/stagearc.bin')).hexdigest(),
          'assets_manifest':digest(mod/'assets/assets.json'),'assembler':digest(mod/'tools/native_assets.py'),'archive_reader':digest(mod/'tools/CJ_Archive.py'),'portal_locator':digest(mod/'tools/portal_location.py') if (mod/'tools/portal_location.py').exists() else '', 'mode':json.loads((mod/'assets/assets.json').read_text()).get('mode','quahog')}
    finally:d.close()
    return value

def cache_key(value):return hashlib.sha256(json.dumps(value,sort_keys=True).encode()).hexdigest()[:24]
def cache_valid(folder,value):
    try:
        state=json.loads((folder/'ready.json').read_text(encoding='utf-8'))
        if state['identity']!=value:return False
        for name,record in state['files'].items():
            relative=Path(name)
            if relative.is_absolute() or '..' in relative.parts:return False
            path=folder/relative
            if path.stat().st_size!=record['size'] or digest(path)!=record['sha256']:return False
        return 'overrides.txt' in state['files'] and 'maps/quahog.arc' in state['files']
    except (OSError,ValueError,KeyError,TypeError):return False

@contextlib.contextmanager
def lock_cache(folder):
    path=folder/'prepare.lock';f=os.fdopen(os.open(path,os.O_RDWR|os.O_CREAT,0o600),'r+b');f.write(b'0');f.flush();f.seek(0)
    started=time.monotonic()
    while True:
        try:
            if os.name=='nt':
                import msvcrt
                f.seek(0);msvcrt.locking(f.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
            break
        except OSError:
            if time.monotonic()-started>300:f.close();raise ValueError('Another game is still preparing these maps. Close that game and retry.')
            time.sleep(.2)
    try:yield
    finally:
        if os.name=='nt':f.seek(0);msvcrt.locking(f.fileno(),msvcrt.LK_UNLCK,1)
        else:fcntl.flock(f,fcntl.LOCK_UN)
        f.close()

def prepare(mod,disc,cache,force=False):
    mod=mod.resolve();disc=disc.resolve();cache=cache.resolve();cache.mkdir(parents=True,exist_ok=True)
    with lock_cache(cache):
        value=identity(mod,disc);key=cache_key(value);destination=cache/key
        if not force and cache_valid(destination,value):log('Prepared maps are up to date.');return destination
        log('First-use preparation for '+('Eclipse' if value['game_id']=='GMSE04' else 'Sunshine')+'. Please wait...')
        world.verify_assets(mod/'assets')
        with tempfile.TemporaryDirectory(prefix='.prepare-',dir=cache) as temporary:
            staged=Path(temporary)/key;staged.mkdir()
            world.prepare(disc,staged,mod/'assets',log)
            files={p.relative_to(staged).as_posix():{'size':p.stat().st_size,'sha256':digest(p)} for p in staged.rglob('*') if p.is_file()}
            (staged/'ready.json').write_text(json.dumps({'identity':value,'files':files},indent=2)+'\n',encoding='utf-8')
            if not cache_valid(staged,value):raise ValueError('Prepared map verification failed; previous cache kept')
            previous=cache/(key+'.previous')
            if previous.exists():shutil.rmtree(previous)
            moved=False
            try:
                if destination.exists():destination.rename(previous);moved=True
                staged.rename(destination)
            except BaseException:
                if moved and not destination.exists():previous.rename(destination)
                raise
            if previous.exists():shutil.rmtree(previous)
        log('Maps ready. Starting the game.');return destination

class Tee:
    """Visible console output plus the existing diagnostic log; no inherited handles."""
    def __init__(self,console,file):self.console=console;self.file=file
    def write(self,text):
        self.file.write(text)
        try:self.console.write(text)
        except (OSError,ValueError):pass
        return len(text)
    def flush(self):
        self.file.flush()
        try:self.console.flush()
        except (OSError,ValueError):pass

def require_runtime():
    if sys.version_info < (3,10) or struct.calcsize("P") != 8:
        raise RuntimeError("This Quahog edition requires installed 64-bit Python 3.10 or newer. Interpreter: "+sys.executable)

def main(argv=None):
    for stream in (sys.stdout,sys.stderr):
        if hasattr(stream,'reconfigure'):stream.reconfigure(encoding='utf-8',errors='backslashreplace')
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('mod','disc','cache','result'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--force',action='store_true');p.add_argument('--log',type=Path);a=p.parse_args(argv)
    with contextlib.ExitStack() as context:
        if a.log:
            a.log.parent.mkdir(parents=True,exist_ok=True)
            stream=context.enter_context(a.log.open('w',encoding='utf-8',buffering=1))
            context.enter_context(contextlib.redirect_stdout(Tee(sys.stdout,stream)))
            context.enter_context(contextlib.redirect_stderr(Tee(sys.stderr,stream)))
        try:
            require_runtime()
            log("Using installed Python "+sys.version.split()[0]+" (64-bit): "+sys.executable)
            destination=prepare(a.mod,a.disc,a.cache,a.force)
            a.result.write_text(str(destination)+'\n',encoding='utf-8',newline='\n')
        except Exception as error:
            record_failure(a.cache)
            log('ERROR: '+str(error));traceback.print_exc()
            return 1
    return 0
if __name__=='__main__':sys.exit(main())
