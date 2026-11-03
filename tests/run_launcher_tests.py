#!/usr/bin/env python3
"""Real Windows child-process tests; use --wine BIN --bottle NAME on macOS."""
from pathlib import Path
import argparse,json,os,subprocess,tempfile,shutil
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('--wine');p.add_argument('--bottle');a=p.parse_args()
build=a.build.resolve();launcher=build/'recordfix_preload.exe'
def win(path):return str(path) if os.name=='nt' else 'Z:'+str(path).replace('/','\\')
def command(*args):return ([a.wine,'--bottle',a.bottle,'--no-update'] if a.wine else [])+[str(launcher),*map(str,args)]
def scalar(s):return json.dumps(str(s),ensure_ascii=False)
with tempfile.TemporaryDirectory(prefix='preload test ',dir=build) as tmp:
 root=Path(tmp);child=root/'child game.exe';shutil.copy2(build/'recordfix_launch_target.exe',child)
 result=root/'result.txt';config=root/'test.yaml'
 args=['plain','with spaces','quote"inside','C:\\trailing\\','','中文']
 def write(text):config.write_text(text,encoding='utf-8')
 valid='\n'.join(['executable: '+scalar(win(child)), 'working_directory: '+scalar(win(root)), 'arguments: '+json.dumps(args,ensure_ascii=False),'preload:','  - path: '+scalar(win(build/'steam_recordfix.dll')),'    initialize: RecordFixInitialize','environment:','  RF_TEST_OUTPUT: '+scalar(win(result)),'  RF_TEST_VALUE: "a value with spaces"','injection_timeout_ms: 30000',''])
 write(valid);subprocess.run(command('--validate',win(config)),check=True,timeout=45)
 subprocess.run(command(win(config)),check=True,timeout=90)
 lines=result.read_text(encoding='utf-8').splitlines();assert lines[0]=='a value with spaces';assert lines[2:]==args,lines
 assert lines[1].lower().rstrip('\\')==win(root).lower().rstrip('\\'),lines[1]
 print('PASS: initialized before child main; UTF-8 YAML, quoted arguments, empty argument, cwd and environment preserved')
 relative=valid.replace(scalar(win(child)),scalar('child game.exe')).replace(scalar(win(root)),scalar('.')).replace(scalar(win(build/'steam_recordfix.dll')),scalar('../steam_recordfix.dll'))
 write(relative);subprocess.run(command(win(config)),check=True,timeout=90)
 assert result.read_text(encoding='utf-8').splitlines()[2:]==args
 print('PASS: executable, working directory and DLL paths resolve relative to YAML')
 for text in [valid+'unknown_option: true\n', valid+'executable: bad.exe\n',valid+'---\nexecutable: bad.exe\n','executable: [broken\n']:
  result.unlink(missing_ok=True);write(text)
  r=subprocess.run(command(win(config)),timeout=45)
  assert r.returncode!=0 and not result.exists(),r.returncode
 print('PASS: unknown fields, duplicate fields, multiple documents and malformed YAML rejected before child start')

# Initialization failure is tested separately from YAML validation: the child
# must never reach main, and a just-created suspended process must be cleaned up.
with tempfile.TemporaryDirectory(prefix='preload failure ',dir=build) as tmp:
 root=Path(tmp);result=root/'result.txt';config=root/'test.yaml'
 valid='\n'.join(['executable: '+scalar(win(build/'recordfix_launch_target.exe')),'preload:','  - path: '+scalar(win(build/'steam_recordfix.dll')),'    initialize: NoSuchInitializationExport','environment:','  RF_TEST_OUTPUT: '+scalar(win(result)),''])
 config.write_text(valid,encoding='utf-8')
 r=subprocess.run(command(win(config)),timeout=90)
 assert r.returncode!=0 and not result.exists(),r.returncode
 print('PASS: failed initialization aborts the suspended child before main')
