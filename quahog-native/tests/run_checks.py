"""Run offline tests. Needs Python 3.10+ and a host g++ compiler; no game files."""
from pathlib import Path
import os,subprocess,sys,tempfile
from pe_fixture import pe_fixture
ROOT=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,'-m','unittest','discover','-s',str(ROOT/'tests'),'-p','test_*.py','-q'],check=True)
with tempfile.TemporaryDirectory(prefix='quahog-checks-') as temp:
    tmp=Path(temp);fixture=tmp/'fixture.exe';fixture.write_bytes(pe_fixture())
    for name in ('portal','overrides','pe','auto_boot','python_policy','python_install'):
        exe=tmp/('test_'+name);cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk')]
        if name=='portal':cmd+=['-DQUAHOG_TEST',str(ROOT/'src/quahog_native.cpp')]
        cmd+=[str(ROOT/'tests'/('test_'+name+'.cpp')),'-o',str(exe)]
        subprocess.run(cmd,check=True)
        args=[str(exe)]
        if name not in ('auto_boot','python_install'):args.append(str(fixture if name=='pe' else tmp/name))
        subprocess.run(args,check=True)
    for minor in (3,4):
        exe=tmp/('test_legacy_'+str(minor))
        subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-DQUAHOG_TEST','-DLEGACY_MINOR='+str(minor),'-I'+str(ROOT/'sdk'),
            str(ROOT/'src/quahog_native.cpp'),str(ROOT/'tests/test_legacy_portal.cpp'),
            '-o',str(exe)],check=True)
        # LeakSanitizer cannot inspect /proc in some traced/container runners.
        # Keep address/undefined-behavior checks enabled; lifetime is tested
        # independently by test_overrides, including retained VFS buffers.
        env=dict(os.environ);env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
        env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(exe),str(tmp/('legacy'+str(minor)))],check=True,env=env)
