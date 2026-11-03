#!/usr/bin/env python3
from pathlib import Path
import subprocess,shutil,sys,os
root=Path(__file__).resolve().parents[1];build=root/'build';build.mkdir(exist_ok=True)
os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR',str(build/'zig-global-cache'))
os.environ.setdefault('ZIG_LOCAL_CACHE_DIR',str(build/'zig-local-cache'))
zig=shutil.which('zig')
if not zig:raise SystemExit('zig not found')
subprocess.run([sys.executable,str(root/'tools/generate_thunks.py'),str(build/'generated')],check=True)
files=[root/'src/recordfix.c',build/'generated/thunks.S',build/'generated/thunk_table.c']+[root/'third_party/minhook/src'/f for f in ['buffer.c','hook.c','trampoline.c','hde/hde64.c']]
common=[zig,'cc','-target','x86_64-windows-gnu','-O2']
subprocess.run(common+['-shared',*map(str,files),'-I'+str(root/'third_party/minhook/include'),'-o',str(build/'steam_recordfix.dll')],check=True)
subprocess.run(common+[str(root/'tests/smoke.c'),'-o',str(build/'recordfix_smoke.exe')],check=True)
yaml=root/'third_party/libyaml'
yaml_sources=[str(p) for p in sorted((yaml/'src').glob('*.c'))]
subprocess.run(common+['-municode',str(root/'src/launcher.c'),str(root/'src/config.c'),*yaml_sources,'-I'+str(yaml/'include'),'-DYAML_DECLARE_STATIC','-DYAML_VERSION_STRING="0.2.5"','-DYAML_VERSION_MAJOR=0','-DYAML_VERSION_MINOR=2','-DYAML_VERSION_PATCH=5','-o',str(build/'recordfix_preload.exe')],check=True)
subprocess.run(common+['-municode',str(root/'tests/launch_target.c'),'-o',str(build/'recordfix_launch_target.exe')],check=True)
