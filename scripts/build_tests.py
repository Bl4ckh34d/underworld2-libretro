"""Build Android ARMv7 test executables; does not access a device or game data."""
from pathlib import Path
import subprocess
from source_setup import ROOT, SOURCES, NDK, prepare
prepare()
llvm=NDK/'toolchains/llvm/prebuilt/windows-x86_64/bin'
out=ROOT/'build/tests';out.mkdir(parents=True,exist_ok=True)
def compile(source, name, cpp=False, flags=()):
    args=[str(llvm/('clang++.exe' if cpp else 'clang.exe')),'--target=armv7a-linux-androideabi23','-O2','-I'+str(ROOT/'include')]
    if cpp: args+=['-std=c++17','-static-libstdc++']
    subprocess.run(args+list(flags)+[str(source),'-ldl','-lm','-o',str(out/name)],check=True)
compile(ROOT/'tests/native_probe.cpp','native_probe',cpp=True)
if (ROOT/'tests/restart_fixture.c').exists():
    compile(ROOT/'tests/restart_fixture.c','restart_fixture.so',flags=['-shared','-fPIC'])
    compile(ROOT/'tests/restart_test.c','restart_test')
    from build_restart_core import build
    build(out/'restart_fixture.so',out/'restart_wrapped.so')
if (ROOT/'tests/uw1_softfloat.c').exists():
    compile(ROOT/'tests/uw1_softfloat.c','uw1_softfloat',flags=['-I'+str(SOURCES/'uw1-grandaddy/src')])
if (SOURCES/'nblood-libretro/libretro/tests/smoke.cpp').exists() and (ROOT/'scripts/build_nblood_tv.py').exists():
    compile(SOURCES/'nblood-libretro/libretro/tests/smoke.cpp','blood_smoke',cpp=True,flags=['-I'+str(SOURCES/'nblood-libretro/libretro/tests')])
print('Android test executables:',out)
