#!/usr/bin/env python3
import pathlib,re,subprocess,sys,concurrent.futures,os
root=pathlib.Path(__file__).resolve().parent.parent
windows='--windows' in sys.argv
cc=os.environ.get('KOOM_CC', 'gcc' if os.name=='nt' else 'x86_64-w64-mingw32-gcc' if windows else 'cc')
out=root/('build-koom-windows' if windows else 'build-koom');out.mkdir(exist_ok=True)
source=root/'vendor/doomgeneric'
files=re.search(r'SRC_DOOM = (.*)',(source/'Makefile').read_text()).group(1).replace('doomgeneric_xlib.o','').split()
paths=[source/(f[:-2]+'.c') for f in files]+[source/'mus2mid.c',root/'koom/host.c',root/'koom/audio.c']
headers=list(source.glob('*.h'))+list((root/'vendor/miniaudio').glob('*.h'))+list((root/'vendor/TinySoundFont').glob('*.h'))
dependency_time=max(pathlib.Path(__file__).stat().st_mtime,*(p.stat().st_mtime for p in headers))
def compile(p):
 obj=out/(p.stem+'.o')
 if not obj.exists() or obj.stat().st_mtime<max(p.stat().st_mtime,dependency_time):
  subprocess.run([cc,'-O2','-std=gnu11','-DNORMALUNIX','-D_DEFAULT_SOURCE','-DFEATURE_SOUND','-DDOOMGENERIC_RESX=320','-DDOOMGENERIC_RESY=200','-I'+str(source),'-c',str(p),'-o',str(obj)],check=True,stdout=subprocess.DEVNULL)
 return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:objects=list(pool.map(compile,paths))
if windows:
 rc=out/'runtime.rc';rc.write_text('1 24 "'+(root/'koom/runtime.manifest').as_posix()+'"\n')
 resource=out/'runtime-resource.o'
 subprocess.run([os.environ.get('KOOM_WINDRES','windres' if os.name=='nt' else 'x86_64-w64-mingw32-windres'),str(rc),'-O','coff','-o',str(resource)],check=True)
 objects.append(str(resource))
subprocess.run([cc,*objects,'-o',str(out/('kalwer-koom.exe' if windows else 'kalwer-koom')),'-lm',*(['-static','-luser32','-ladvapi32','-lole32','-lwinmm'] if windows else ['-ldl','-lpthread'])],check=True)
