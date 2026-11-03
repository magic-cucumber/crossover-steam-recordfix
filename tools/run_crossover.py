#!/usr/bin/env python3
"""Run the Windows preload launcher in an existing CrossOver bottle."""
from pathlib import Path
import argparse,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('config',type=Path);p.add_argument('--bottle',required=True);p.add_argument('--validate',action='store_true');p.add_argument('--wine',default='/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine');p.add_argument('--build-dir',type=Path,default=root/'build');a=p.parse_args()
exe=a.build_dir/'recordfix_preload.exe'
if not exe.is_file():raise SystemExit('Build recordfix_preload.exe first.')
config=a.config.resolve()
if not config.is_file():raise SystemExit('YAML config not found.')
args=[a.wine,'--bottle',a.bottle,'--no-update',str(exe.resolve())]
if a.validate:args.append('--validate')
args.append('Z:'+str(config).replace('/','\\'))
raise SystemExit(subprocess.call(args))
