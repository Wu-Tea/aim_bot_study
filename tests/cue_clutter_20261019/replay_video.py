"""Offline production Vision/adapter probe; never opens an output device."""
import argparse,os,pathlib,subprocess,cv2
parser=argparse.ArgumentParser();parser.add_argument('video');args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];out=root/'runs/cue-clutter-20261019'
env={k.upper():v for k,v in os.environ.items()}
env['PATH']=str(root/'native/build/Release')+';D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;'+env.get('PATH','')
for version in ['before','after']:
 capture=cv2.VideoCapture(args.video)
 with (out/f'video-{version}.log').open('wb') as log:
  proc=subprocess.Popen([str(out/f'build/Release/video_{version}.exe'),str(root/'models/best_480x384.engine'),str(out/f'video-{version}.jsonl'),'60','190','.2','.65','1'],stdin=subprocess.PIPE,stdout=log,stderr=log,env=env)
  try:
   while True:
    ok,frame=capture.read()
    if not ok or capture.get(cv2.CAP_PROP_POS_MSEC)>=3280:break
    proc.stdin.write(cv2.cvtColor(frame[284:796,640:1280],cv2.COLOR_BGR2BGRA).tobytes())
  finally:
   proc.stdin.close();capture.release()
  if proc.wait():raise RuntimeError(f'{version} failed; see log')
 print(version,'complete')
