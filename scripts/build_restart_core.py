"""Package a synchronous legacy core with a full engine reload on Reset."""
from pathlib import Path
import argparse, os, subprocess
ROOT=Path(__file__).resolve().parents[1]
LLVM=Path(os.environ.get('ANDROID_NDK_HOME','C:/tools/android/android-ndk-r27d'))/'toolchains/llvm/prebuilt/windows-x86_64/bin'
def build(payload, output):
    payload,output=Path(payload).resolve(),Path(output).resolve()
    assert payload!=output
    output.parent.mkdir(parents=True,exist_ok=True)
    asm=output.with_suffix('.payload.S')
    asm.write_text('.section .rodata\n.balign 4096\n.global engine_blob_start\n.hidden engine_blob_start\nengine_blob_start:\n.incbin "'+payload.as_posix()+'"\n.global engine_blob_end\n.hidden engine_blob_end\nengine_blob_end:\n.section .note.GNU-stack,"",%progbits\n')
    exports=output.with_suffix('.exports');exports.write_text('{ global: retro_*; local: *; };\n')
    sources=Path(os.environ.get('PORT_SOURCES_DIR',str(ROOT/'sources')))
    include=next(p for p in [ROOT/'include',sources/'libretro-common/include',sources/'nblood-libretro/libretro',sources/'scummvm/backends/platform/libretro/deps/libretro-common/include'] if (p/'libretro.h').exists())
    subprocess.run([str(LLVM/'clang.exe'),'--target=armv7a-linux-androideabi23','-O2','-fPIC','-shared',
        '-I'+str(include),str(ROOT/'ports/common/restart_core.c'),str(asm),
        '-Wl,--no-undefined','-Wl,-Bsymbolic','-Wl,--version-script='+str(exports),'-ldl','-o',str(output)],check=True)
    asm.unlink();exports.unlink()
    print(output,output.stat().st_size)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('payload');p.add_argument('output');a=p.parse_args();build(a.payload,a.output)
