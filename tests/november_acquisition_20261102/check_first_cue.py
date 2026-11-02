import cv2,json,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[2]/'runs/incident-20261102';O=R/'fix'
M=json.loads((R/'manifest.json').read_text(encoding='utf-8'));cap=cv2.VideoCapture(M['source_video'])
for t in [546.95,547.0,547.05,547.1,547.15,547.2,547.25,547.3]:
 cap.set(cv2.CAP_PROP_POS_FRAMES,round(t*120));ok,f=cap.read();assert ok
 p=O/f'first_cue_{t}.bgra';p.write_bytes(cv2.cvtColor(f[284:796,640:1280],cv2.COLOR_BGR2BGRA).tobytes())
 r=subprocess.check_output([str(R/'analysis/build/Release/geometry_probe.exe'),'--cue-scan',str(p)],encoding='utf-8')
 print(t,r.strip())
cap.release()
