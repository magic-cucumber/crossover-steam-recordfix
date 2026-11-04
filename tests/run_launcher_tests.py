#!/usr/bin/env python3
"""Real Windows child-process tests; use --wine BIN --bottle NAME on macOS."""
from pathlib import Path
import argparse,json,os,subprocess,tempfile,shutil,struct
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('--wine');p.add_argument('--bottle');a=p.parse_args()
os.environ['RECORDFIX_NO_ERROR_DIALOG']='1'
build=a.build.resolve();launcher=build/'recordfix_preload.exe'
image=launcher.read_bytes();pe=struct.unpack_from('<I',image,60)[0]
assert struct.unpack_from('<H',image,pe+24+68)[0]==2, 'launcher must use Windows GUI subsystem'
print('PASS: launcher uses GUI subsystem (no console allocation)')
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
 # Emulate Steam's %command% expansion with inherited working directory and
 # environment. YAML intentionally has no executable, args, cwd or Steam IDs.
 steam_config=root/'steam.yaml'
 steam_config.write_text('preload:\n  - path: '+scalar(win(build/'steam_recordfix.dll'))+'\n    initialize: RecordFixInitialize\n',encoding='utf-8')
 env=dict(os.environ,RF_TEST_OUTPUT=win(result),RF_TEST_VALUE='inherited Steam context',SteamAppId='582010')
 subprocess.run(command('--steam',win(steam_config),'--',win(child),*args),cwd=root,env=env,check=True,timeout=90)
 lines=result.read_text(encoding='utf-8').splitlines();assert lines[0]=='inherited Steam context' and lines[2:]==args,lines
 probe=([a.wine,'--bottle',a.bottle,'--no-update'] if a.wine else [])+[str(child),'--probe-cwd']
 expected_cwd=subprocess.run(probe,cwd=root,capture_output=True,check=True,encoding='utf-8').stdout.strip()
 assert lines[1].lower().rstrip('\\')==expected_cwd.lower().rstrip('\\'),lines[1]
 for extra in [['--'],['--',win(launcher)],['--',win(root/'missing.exe')]]:
  assert subprocess.run(command('--steam',win(steam_config),*extra),timeout=45).returncode!=0
 print('PASS: Steam %command% mode retains args/cwd/environment and rejects missing/recursive targets')
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

probe=([a.wine,'--bottle',a.bottle,'--no-update'] if a.wine else [])+[str(build/'recordfix_launch_target.exe'),'--check-error-dialog',win(launcher)]
subprocess.run(probe,check=True,timeout=60)
print('PASS: fatal launch error shows MessageBox and exits after dismissal')
