"""Validate locked dependencies and apply preserved changes without resetting work."""
from pathlib import Path
import hashlib, json, os, subprocess
ROOT=Path(__file__).resolve().parents[1]
SOURCES=Path(os.environ.get('PORT_SOURCES_DIR',str(ROOT/'sources'))).resolve()
NDK=Path(os.environ.get('ANDROID_NDK_HOME','C:/tools/android/android-ndk-r27d'))
CC=NDK/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
def prepare():
    for item in json.loads((ROOT/'dependencies.json').read_text()):
        directory=(SOURCES/item['path']).resolve()
        if not directory.is_relative_to(SOURCES): raise SystemExit('Invalid dependency path')
        def git(*args, check=True):
            return subprocess.run(['git','-C',str(directory),*args],capture_output=True,text=True,check=check)
        if git('rev-parse','HEAD').stdout.strip()!=item['revision']: raise SystemExit('Wrong source revision: '+str(directory))
        if git('remote','get-url','origin').stdout.strip()!=item['url']: raise SystemExit('Wrong source origin: '+str(directory))
        if item.get('patch'):
            patch=ROOT/item['patch']
            if hashlib.sha256(patch.read_bytes()).hexdigest()!=item['patch_sha256']: raise SystemExit('Patch checksum mismatch')
            if git('apply','--reverse','--check',str(patch),check=False).returncode:
                git('apply','--check',str(patch))
                git('apply',str(patch))
    for item in json.loads((ROOT/'vendored.json').read_text()):
        if hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest()!=item['sha256']: raise SystemExit('Vendored file checksum mismatch: '+item['path'])
if __name__=='__main__': prepare()
