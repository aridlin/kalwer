#!/usr/bin/env python3
import pathlib,re,subprocess,sys,concurrent.futures,os
root=pathlib.Path(__file__).resolve().parent.parent
windows='--windows' in sys.argv
cc=os.environ.get('KOOM_CC', 'gcc' if os.name=='nt' else 'x86_64-w64-mingw32-gcc' if windows else 'cc')
out=root/('build-koom-windows' if windows else 'build-koom');out.mkdir(exist_ok=True)
source=root/'vendor/doomgeneric'
files=re.search(r'SRC_DOOM = (.*)',(source/'Makefile').read_text()).group(1).replace('doomgeneric_xlib.o','').split()
paths=[source/(f[:-2]+'.c') for f in files]+[root/'koom/host.c']
def compile(p):
 obj=out/(p.stem+'.o')
 if not obj.exists() or obj.stat().st_mtime<p.stat().st_mtime:
  subprocess.run([cc,'-O2','-std=gnu11','-DNORMALUNIX','-D_DEFAULT_SOURCE','-DDOOMGENERIC_RESX=320','-DDOOMGENERIC_RESY=200','-I'+str(source),'-c',str(p),'-o',str(obj)],check=True,stdout=subprocess.DEVNULL)
 return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:objects=list(pool.map(compile,paths))
subprocess.run([cc,*objects,'-o',str(out/('kalwer-koom.exe' if windows else 'kalwer-koom')),'-lm',*(['-static','-luser32','-ladvapi32'] if windows else [])],check=True)
