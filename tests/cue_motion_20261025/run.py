"""Original-pixel/PTS native replay of the user's jump/slide recordings."""
import argparse,json,os,subprocess,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--tag',default='before');p.add_argument('--hdr',action='store_true');a=p.parse_args()
out=root/'runs/cue-motion-20261025';destroot=out/a.tag;destroot.mkdir(parents=True,exist_ok=True)
exe=out/'before/scan.exe' if a.tag.startswith('before') else root/'runs/new-video-study-20261025/build/Release/scan.exe'
vf='crop=640:512'
if a.hdr:vf+=',zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=mobius:desat=0,zscale=t=bt709:m=bt709:r=full,format=bgra'
env=os.environ.copy();env['PATH']=str(root/'native/build/Release')+';D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;'+env['PATH']
clips=[]
for i,stamp in enumerate(['12.19.03.04','12.20.24.05']):
 path=Path('C:/Users/Administrator/Videos/NVIDIA/QQNT')/f'QQNT 2026.10.25 - {stamp}.mp4'
 clips.append(dict(id=f'q{i+1}',path=str(path)))
(out/'selected.json').write_text(json.dumps(clips,indent=2),encoding='utf-8')
for c in clips:
 dest=destroot/c['id'];dest.mkdir(exist_ok=True)
 if (dest/'complete.json').exists():continue
 cached=out/'before'/c['id']/'timestamps.txt'
 if cached.exists():ns=list(map(int,cached.read_text().split()))
 else:
  probe=subprocess.run(['ffprobe','-v','error','-select_streams','v','-show_entries','frame=best_effort_timestamp_time','-of','json',c['path']],capture_output=True,check=True)
  pts=[float(f['best_effort_timestamp_time']) for f in json.loads(probe.stdout)['frames']]
  ns=[round(1e9+(t-pts[0])*1e9) for t in pts]
 assert all(b>a for a,b in zip(ns,ns[1:]))
 (dest/'timestamps.txt').write_text('\n'.join(map(str,ns))+'\n')
 print('START',c['id'],len(ns),flush=True);start=time.monotonic()
 with (dest/'decode.txt').open('wb') as de,(dest/'native.txt').open('wb') as ne:
  dec=subprocess.Popen(['ffmpeg','-v','error','-i',c['path'],'-vf',vf,'-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=de)
  proc=subprocess.Popen([str(exe),str(root/'models/best_480x384.engine'),str(dest/'vision'),str(dest/'timestamps.txt'),'190','.2','.65','1','cue-owned'],stdin=dec.stdout,stdout=ne,stderr=ne,env=env);dec.stdout.close()
  try:
   rc=proc.wait();dc=dec.wait();assert rc==dc==0,(rc,dc)
  finally:
   if proc.poll() is None:proc.terminate()
   if dec.poll() is None:dec.terminate()
 count=sum(1 for _ in (dest/'vision.jsonl').open());assert count==len(ns)
 (dest/'complete.json').write_text(json.dumps(dict(frames=count,elapsed_s=time.monotonic()-start,video=c['path'],decode_filter=vf),indent=2))
 print('DONE',c['id'],count,flush=True)
