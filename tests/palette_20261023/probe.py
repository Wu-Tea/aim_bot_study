"""Only recolor the manually identified small diamond; preserve green foliage."""
import cv2,json,numpy as np,struct,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'runs/palette-20261023'
rows=[json.loads(s)for s in (root/'runs/cue-appearance-20261023/replay.jsonl').read_text().splitlines()]
video='E:/videoCut/Content 2026.10.23 - 20.17.45.03-cut-merged-1792758272156.mp4'
for index,cx,cy in [(120,277.5,191.5),(864,353.5,171.5)]:
 cap=cv2.VideoCapture(video);cap.set(cv2.CAP_PROP_POS_FRAMES,index);ok,full=cap.read();cap.release();assert ok
 h,w=full.shape[:2];bgr=full[(h-512)//2:(h+512)//2,(w-640)//2:(w+640)//2].copy()
 hsv=cv2.cvtColor(bgr,cv2.COLOR_BGR2HSV)
 mask=(hsv[:,:,0]>=20)&(hsv[:,:,0]<=35)&(hsv[:,:,1]>=120)&(hsv[:,:,2]>=120)
 yy,xx=np.indices(mask.shape);mask&=(abs(xx-cx)<12)&(abs(yy-cy)<12)
 hi=bgr.max(axis=2);lo=bgr.min(axis=2)
 variants=[bgr.copy(),bgr.copy(),bgr.copy()]
 variants[1][mask]=np.stack([hi,lo,hi],axis=2)[mask]
 variants[2][mask]=np.stack([hi,hi,lo],axis=2)[mask]
 d=rows[index]['detections'][0];path=out/f'probe-{index}.bin'
 with path.open('wb')as f:
  for v in variants:f.write(struct.pack('<5f',*d['box'],d['conf']));f.write(cv2.cvtColor(v,cv2.COLOR_BGR2BGRA).tobytes())
 result=subprocess.run([str(out/'build/Release/probe.exe'),str(path)],capture_output=True,text=True)
 print(index,int(mask.sum()),result.returncode,result.stdout,result.stderr,flush=True)
 if result.returncode:raise SystemExit(result.returncode)
