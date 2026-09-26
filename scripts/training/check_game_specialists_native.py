"""Native inference on the six preserved real-game diagnostic inputs; no live outputs."""
import json
import sys
import time

import cv2
import numpy as np
from PIL import Image, ImageDraw

from game_specialists import ROOT, OUT, save_json, sha, labels, iou

sys.path.insert(0,str(ROOT))
from tools.analyze_fusion_video_replay import load_native_module


def holdout(engines, dest):
    """Validate exported engines with native stretch preprocessing, not PT letterbox."""
    result={}
    prior=set(json.loads((OUT/'prior-training-overlap.json').read_text())['groups'])
    for game in ['apex','delta']:
        samples=json.loads((OUT/game/'manifest.json').read_text())['samples']
        splits=['test'] if game=='apex' else ['test','test_flash','test_unseen','test_flash_unseen']
        result[game]={}
        for name in ['baseline',game]:
            result[game][name]={}
            for split in splits:
                entries=[s for s in samples if s['split']==split.removesuffix('_unseen')
                         and (not split.endswith('_unseen') or s['group'] not in prior)]
                tp=fp=fn=0
                for rec in entries:
                    bgr=cv2.imread(rec['image']);h,w=bgr.shape[:2]
                    rgb=cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB)
                    dets=engines[name].infer_rgb(rgb,.40)['detections']
                    gt=[np.array([(x-bw/2)*w,(y-bh/2)*h,(x+bw/2)*w,(y+bh/2)*h])
                        for _,x,y,bw,bh in labels(rec['label'])]
                    matched=set()
                    for d in sorted(dets,key=lambda d:-d['conf']):
                        box=np.array([d['x1'],d['y1'],d['x2'],d['y2']])
                        overlap,j=max([(iou(box,g),j) for j,g in enumerate(gt) if j not in matched],default=(0,-1))
                        if overlap>=.5:tp+=1;matched.add(j)
                        else:fp+=1
                    fn+=len(gt)-len(matched)
                precision=tp/max(1,tp+fp);recall=tp/max(1,tp+fn)
                result[game][name][split]=dict(images=len(entries),objects=tp+fn,tp=tp,fp=fp,fn=fn,
                    precision=precision,recall=recall,f1=2*precision*recall/max(1e-9,precision+recall))
    save_json(dest/'native-holdout.json',dict(conf=.4,match_iou=.5,metrics=result,
              preprocessing='Native infer_rgb stretches source to 480x384; differs from PT letterbox on square public images.',
              selection='Already locked before this export verification; no reselection using this output.'))


def main():
    native=load_native_module()
    source=ROOT/'artifacts/vision-video-audit-20260913'
    cases=json.loads((source/'selected-evidence.json').read_text())
    paths={'baseline':ROOT/'models/best_480x384.engine',
           'apex':OUT/'exports/apex/apex_480x384.engine',
           'delta':OUT/'exports/delta/delta_480x384.engine'}
    engines={name:native.NativeEngine(str(path)) for name,path in paths.items()}
    images=[]; records=[]
    for case in cases:
        game='apex' if case['clip'].startswith('A') else 'delta'
        path=source/f"case{case['case']}_{case['clip']}_f{case['frame']}_input.png"
        bgr=cv2.imread(str(path)); rgb=cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB)
        panels=[]; record=dict(case=case['case'],clip=case['clip'],frame=case['frame'],input_sha256=sha(path),models={})
        for name in ['baseline',game]:
            engine=engines[name]
            for _ in range(15): engine.infer_rgb(rgb,.05)
            raw=engine.infer_rgb(rgb,.05)
            dets=raw['detections']; timings=[]
            for _ in range(40):
                before=time.perf_counter();engine.infer_rgb(rgb,.40)
                timings.append((time.perf_counter()-before)*1000)
            kept=[d for d in dets if d['conf']>=.40]
            record['models'][name]=dict(detections_floor05=dets,kept40=kept,
                                        offline_call_ms_p50=float(np.median(timings)),
                                        offline_call_ms_p95=float(np.percentile(timings,95)))
            panel=Image.new('RGB',(640,548),(20,23,29)); panel.paste(Image.fromarray(rgb),(0,36))
            draw=ImageDraw.Draw(panel)
            draw.text((10,10),f"Case {case['case']} {case['clip']} | {name} | solid >= .40, thin < .40",fill='white')
            for d in sorted(dets,key=lambda d:d['conf']):
                color='#64ef90' if d['conf']>=.4 else '#ffc967'
                box=(d['x1'],d['y1']+36,d['x2'],d['y2']+36)
                draw.rectangle(box,outline=color,width=3 if d['conf']>=.4 else 1)
                label=f"{d['conf']:.3f}"; xy=(max(0,d['x1']),max(36,d['y1']+36))
                draw.rectangle(draw.textbbox(xy,label),fill='black')
                draw.text(xy,label,fill=color)
            panels.append(panel)
        row=Image.new('RGB',(1280,548));row.paste(panels[0],(0,0));row.paste(panels[1],(640,0))
        images.append(row);records.append(record)
    dest=OUT/'native-check';dest.mkdir(exist_ok=False)
    for i,row in enumerate(images): row.save(dest/f'case{i+1}-comparison.jpg',quality=92)
    for game,rows in [('apex',images[:3]),('delta',images[3:])]:
        sheet=Image.new('RGB',(1280,len(rows)*548))
        for i,row in enumerate(rows):sheet.paste(row,(0,i*548))
        sheet.save(dest/f'{game}-comparison.jpg',quality=92)
    save_json(dest/'results.json',dict(models={n:dict(path=str(p),sha256=sha(p)) for n,p in paths.items()},
              native_pyd_sha256=sha(ROOT/'native/vision_native/build/Release/vision_native_cpp.cp311-win_amd64.pyd'),
              records=records,limitation='Six diagnostic frames, no population recall or live tracking claim; timing is isolated offline Python-to-native call.'))
    holdout(engines,dest)
    print('native check complete',flush=True)


if __name__=='__main__': main()
