"""Full-frame native scan with original pixels, actual video PTS, and no output device."""
import argparse,json,os,subprocess,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];baseline=root/'runs/new-video-study-20261025'
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=baseline)
parser.add_argument('--reuse-timestamps',action='store_true');parser.add_argument('--clips',nargs='+');args=parser.parse_args()
out=args.output;out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env['PATH']=str(root/'native/build/Release')+';D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;'+env['PATH']
rows=json.loads((baseline/'selected.json').read_text(encoding='utf-8'))
for r in rows:
 if args.clips and r['id'] not in args.clips:continue
 dest=out/r['id'];dest.mkdir(exist_ok=True)
 if (dest/'complete.json').exists():continue
 if args.reuse_timestamps:
  ns=list(map(int,(baseline/r['id']/'timestamps.txt').read_text().split()))
 else:
  probe=subprocess.run(['ffprobe','-v','error','-select_streams','v','-show_entries','frame=best_effort_timestamp_time','-of','json',r['path']],capture_output=True,check=True)
  pts=[float(f['best_effort_timestamp_time']) for f in json.loads(probe.stdout)['frames']]
  ns=[round(1e9+(t-pts[0])*1e9) for t in pts]
 assert all(b>a for a,b in zip(ns,ns[1:]))
 (dest/'timestamps.txt').write_text('\n'.join(map(str,ns))+'\n')
 print('START',r['id'],len(ns),'frames',flush=True);start=time.monotonic()
 with (dest/'decode.txt').open('wb') as de,(dest/'native.txt').open('wb') as ne:
  dec=subprocess.Popen(['ffmpeg','-v','error','-i',r['path'],'-vf','crop=640:512','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=de)
  proc=subprocess.Popen([str(baseline/'build/Release/scan.exe'),str(root/'models/best_480x384.engine'),str(dest/'vision'),str(dest/'timestamps.txt'),'190','.2','.65','1','cue-owned'],stdin=dec.stdout,stdout=ne,stderr=ne,env=env)
  dec.stdout.close()
  try:
   rc=proc.wait();dc=dec.wait()
   if rc or dc:raise RuntimeError((r['id'],rc,dc))
  finally:
   if proc.poll() is None:proc.terminate()
   if dec.poll() is None:dec.terminate()
 count=sum(1 for _ in (dest/'vision.jsonl').open())
 assert count==len(ns)
 (dest/'complete.json').write_text(json.dumps({'frames':count,'elapsed_s':time.monotonic()-start,'video':r['path']},indent=2),encoding='utf-8')
 print('DONE',r['id'],count,'frames',round(time.monotonic()-start,2),'s',flush=True)
