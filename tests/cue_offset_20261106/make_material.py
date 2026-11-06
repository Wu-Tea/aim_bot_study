"""Real video pixels with controlled detector-box contraction; no model rerun."""
import json, struct
from pathlib import Path
import cv2

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'runs/cue-offset-20261106'
video=json.loads((ROOT/'runs/incident-20261102/manifest.json').read_text(encoding='utf-8'))['source_video']
time=185.0565245
cap=cv2.VideoCapture(video);cap.set(cv2.CAP_PROP_POS_FRAMES,round(time*120))
ok,frame=cap.read();cap.release();assert ok
crop=frame[284:796,640:1280]
cv2.imwrite(str(OUT/'material-reference.png'),crop)
pixels=cv2.cvtColor(crop,cv2.COLOR_BGR2BGRA).tobytes()
# Original logged body; cue centre read from the actual visible diamond.
box=(263.833,120.917,311.667,282.667);cue=(277.5,105.5)
schedule=[]
with (OUT/'material.bin').open('wb') as out:
 for tick in range(55):
  scale=1 if tick<5 or tick>=45 else max(.2,1-(tick-4)*.04)
  visible=tick<30 or tick>=45
  rect=[cue[i%2]+(value-cue[i%2])*scale for i,value in enumerate(box)]
  detections=[(*rect,.9)] if visible else []
  out.write(struct.pack('<QI',1_000_000_000+tick*10_000_000,len(detections)))
  for d in detections:out.write(struct.pack('<5f',*d))
  out.write(pixels)
  schedule.append({'frame':tick,'scale':scale,'detections':detections})
(OUT/'material-schedule.json').write_text(json.dumps({'video':video,'video_s':time,'assumptions':'Pixels frozen from real recording. Boxes contract around cue to model progressive detector truncation, then disappear and recover. This is a controlled selector fixture, not new detector inference or live-game evidence.','frames':schedule},indent=2),encoding='utf-8')
