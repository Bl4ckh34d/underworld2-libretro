"""Cross-build Underworld Exhumed with the cooperative libretro platform backend."""
from pathlib import Path
import os,sys,subprocess,hashlib,concurrent.futures,urllib.request
from source_setup import ROOT, SOURCES, CC, prepare
SRC=SOURCES/'underworld-exhumed/uw2'
OUT=ROOT/'build/underworld2-tv'


def main():
    prepare()
    os.environ["EXHUME"]=str(SRC.parent/"exhume")
    sys.path.insert(0,str(SRC/"tools"))
    import portbuild,portcheck,sources
    OUT.mkdir(parents=True,exist_ok=True)
    vendor=SRC/'tools/nuked-opl3';vendor.mkdir(exist_ok=True)
    pins={'opl3.c':'59eb873fdb6d52bc7977a0fcbb97c1bae03dd71cc88e3acddbcc01b7121bfe03','opl3.h':'a84266b8d71a4929f15f573afbe407fd24310b77757d855835dacabf68763679'}
    for name,digest in pins.items():
        p=vendor/name
        if not p.exists():urllib.request.urlretrieve('https://raw.githubusercontent.com/nukeykt/Nuked-OPL3/765ec962e473aeb767e4cba74ffdc8f588ffbfe8/'+name,p)
        assert hashlib.sha256(p.read_bytes()).hexdigest()==digest
    portbuild.BACKEND='libretro'
    games=[Path(p) for p in sources.all_sources() if p.upper().endswith('.C') and not portcheck.dos_only(p)]+[Path(p) for p in sources.replay_sources()]
    ports=[Path(p) for p in portbuild.port_sources() if Path(p).name!='crash.c']+[vendor/'opl3.c']
    core=ROOT/'ports/underworld2/libretro.c'
    jobs=[(p,False) for p in games]+[(ROOT/'ports/underworld2/game_controls.c',False)]+[(p,True) for p in ports]+[(core,True),(ROOT/'ports/common/resources.c',True)]
    headers_time=max(p.stat().st_mtime for folder in [SRC/'src',SRC.parent/'exhume/runtime',ROOT/'ports/underworld2',ROOT/'include'] for p in folder.rglob('*.h'))
    cc=str(CC)
    base=[cc,'--target=armv7a-linux-androideabi21','-fPIC','-fno-strict-aliasing','-fwrapv','-w','-O2','-DUW_LIBRETRO','-DAUDIO_HAVE_OPL','-I'+str(vendor),'-I'+str(ROOT/'include')]
    def compile(job):
        p,isport=job;obj=OUT/(hashlib.sha1(str(p).encode()).hexdigest()[:12]+'.o')
        flags=list(portbuild.PORT_FLAGS if isport else portcheck.FLAGS)
        if os.getenv('UW2_DIAGNOSTIC_O0') and not isport:flags+=['-O0']
        if p.name=='main.c' and 'port' in p.parts:flags+=['-Dmain=uw2_port_main']
        if isport and p!=core:flags+=['-Dexit=plat_game_exit']
        args=base+flags+['-c',str(p),'-o',str(obj)]
        stamp=obj.with_suffix('.cmd');signature=' '.join(args)
        if obj.exists() and obj.stat().st_mtime>max(p.stat().st_mtime,headers_time) and stamp.exists() and stamp.read_text()==signature:return obj,''
        r=subprocess.run(args,capture_output=True,text=True,encoding='utf-8',errors='replace')
        if r.returncode:return None,str(p)+'\n'+r.stderr
        stamp.write_text(signature);return obj,r.stderr
    with concurrent.futures.ThreadPoolExecutor(6) as pool:results=list(pool.map(compile,jobs))
    (OUT/'build.log').write_text('\n'.join(log for _,log in results),encoding='utf-8')
    errors=[log for obj,log in results if obj is None]
    for e in errors:print(e[:1800])
    if errors:print('Failed files',len(errors));return 1
    r=subprocess.run([cc,'--target=armv7a-linux-androideabi21','-shared','-Wl,-Bsymbolic-functions','-Wl,--no-undefined','-o',str(OUT/'underworld2_libretro_android.so')]+[str(o) for o,_ in results]+['-Wl,--wrap='+n for n in 'malloc calloc realloc free fopen fdopen fclose open close mmap munmap'.split()]+['-lm','-ldl','-lz'],capture_output=True,text=True)
    print(r.stderr[-5000:]);print('UW2 build exit',r.returncode)
    if r.returncode:return r.returncode
    from build_restart_core import build
    build(OUT/'underworld2_libretro_android.so',ROOT/'release/underworld2_libretro_android.so')
    return 0
if __name__=='__main__':raise SystemExit(main())
