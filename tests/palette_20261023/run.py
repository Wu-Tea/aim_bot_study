"""Use original decoded frames and recorded raw person boxes (not their labels)."""
from pathlib import Path
import json, struct, subprocess, sys
root=Path(__file__).resolve().parents[2]
out=root/'runs/palette-20261023'
cases={
 'diamond':('E:/videoCut/Content 2026.10.23 - 20.17.45.03-cut-merged-1792758272156.mp4','runs/cue-appearance-20261023/replay.jsonl'),
 'nearpass':('C:/Users/Administrator/Videos/NVIDIA/Content/Content 2026.10.23 - 22.52.40.01.mp4','runs/video-225240-20261023/vision.jsonl'),
}
yellow_only='--yellow-only' in sys.argv
names=[a for a in sys.argv[1:] if a!='--yellow-only'] or list(cases)
for name in names:
 video,record=cases[name]
 rows=[json.loads(x) for x in (root/record).read_text().splitlines()]
 with (out/(name+'-decode.txt')).open('wb') as de,(out/(name+'-native.txt')).open('wb') as ne:
  decoder=subprocess.Popen(['ffmpeg','-v','error','-i',video,'-vf','crop=640:512','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=de)
  proc=subprocess.Popen([str(out/'build/Release/replay.exe'),str(out/(name+('-yellow' if yellow_only else '')+'.csv'))]+(['yellow-only'] if yellow_only else []),stdin=subprocess.PIPE,stdout=ne,stderr=ne)
  try:
   for row in rows:
    pixels=decoder.stdout.read(640*512*4)
    if len(pixels)!=640*512*4:raise RuntimeError('short video')
    det=[d for d in row['detections'] if d['class']==0]
    proc.stdin.write(struct.pack('<QI',round(1e9+row['t']*1e9),len(det)))
    for d in det:proc.stdin.write(struct.pack('<5f',*d['box'],d['conf']))
    proc.stdin.write(pixels)
   if decoder.stdout.read(1):raise RuntimeError('extra frames')
   proc.stdin.close()
   if proc.wait() or decoder.wait():raise RuntimeError('native/decode failed')
  finally:
   if proc.poll() is None:proc.terminate()
   if decoder.poll() is None:decoder.terminate()
 print(name,len(rows),flush=True)
