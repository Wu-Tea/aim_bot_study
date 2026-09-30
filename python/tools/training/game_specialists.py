"""Bounded, local game-detector research. Never edits production weights/config.

Usage: python python/tools/training/game_specialists.py prepare-delta|prepare-apex
       python python/tools/training/game_specialists.py train GAME START
       python python/tools/training/game_specialists.py evaluate GAME MODEL OUTPUT
"""
from __future__ import annotations

import hashlib
import json
import random
import re
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "artifacts/game-specialists-20260914"
SEED = 20260914


def sha(path):
    with Path(path).open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def save_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf8")


def labels(path):
    result = []
    for line in Path(path).read_text().splitlines():
        if not line.strip():
            continue
        a = list(map(float, line.split()))
        if len(a) != 5 or a[0] not in (0, 1) or not np.isfinite(a).all():
            raise ValueError(f"Invalid label: {path}")
        if not all(0 <= x <= 1 for x in a[1:]) or min(a[3:]) <= 0:
            raise ValueError(f"Invalid normalized box: {path}")
        a[0] = 0  # Both human classes map to person; not enemy authorization.
        result.append(a)
    return result


def phash(image):
    gray = cv2.resize(cv2.cvtColor(image, cv2.COLOR_BGR2GRAY), (32, 32))
    dct = cv2.dct(gray.astype(np.float32))[:8, :8].reshape(-1)
    bits = dct > np.median(dct[1:])
    return int.from_bytes(np.packbits(bits).tobytes(), "big")


def flash(image, boxes, seed):
    """Synthetic bounded local light veil, not a calibrated game renderer.

    No geometry changes. Darkened base contribution always >= 48%; the
    effect cannot replace a person with a white disk. Still requires visual QA.
    """
    rng = np.random.default_rng(seed)
    h, w = image.shape[:2]
    if boxes and rng.random() < .7:
        box = boxes[int(rng.integers(len(boxes)))]
        cx, cy = box[1] * w, (box[2] - box[4] * .25) * h
    else:
        cx, cy = rng.uniform(.1, .9) * w, rng.uniform(.1, .8) * h
    yy, xx = np.mgrid[:h, :w]
    sigma = rng.uniform(.06, .16) * min(w, h)
    gauss = np.exp(-((xx-cx)**2 + (yy-cy)**2) / (2*sigma*sigma))
    veil = np.clip(rng.uniform(.04, .13) + rng.uniform(.2, .39)*gauss, 0, .52)[..., None]
    base = image.astype(np.float32) * rng.uniform(.70, 1.0)
    color = np.array([rng.uniform(205, 245), rng.uniform(230, 255), 255])
    return np.uint8(np.clip(base*(1-veil) + color*veil, 0, 255))


def write_example(dest, split, sid, image, boxes):
    ip = dest / "images" / split / f"{sid}.jpg"
    lp = dest / "labels" / split / f"{sid}.txt"
    ip.parent.mkdir(parents=True, exist_ok=True)
    lp.parent.mkdir(parents=True, exist_ok=True)
    if ip.exists() or lp.exists():
        raise FileExistsError(f"Refusing to overwrite example {sid}")
    if not cv2.imwrite(str(ip), image, [cv2.IMWRITE_JPEG_QUALITY, 95]):
        raise IOError(ip)
    lp.write_text("".join("0 " + " ".join(f"{v:.8f}" for v in b[1:]) + "\n" for b in boxes))
    return dict(image=str(ip), label=str(lp), image_sha256=sha(ip), label_sha256=sha(lp))


def finish_dataset(dest, manifest):
    for split in ["train", "val", "test"]:
        (dest/"images"/split).mkdir(parents=True, exist_ok=True)
        (dest/"labels"/split).mkdir(parents=True, exist_ok=True)
    text = f"path: {dest.as_posix()}\ntrain: images/train\nval: images/val\ntest: images/test\nnames:\n  0: person\n"
    (dest/"dataset.yaml").write_text(text)
    save_json(dest/"manifest.json", manifest)
    print(dest, {s: sum(x['split'] == s for x in manifest['samples']) for s in ['train','val','test','val_flash','test_flash']}, flush=True)


def prepare_delta():
    source = Path("D:/datasets/roboflow_candidates/DeltaForceFireMode.v15i.yolo26")
    dest = OUT/"delta"
    if dest.exists():
        raise FileExistsError(dest)
    groups = {}
    for p in sorted(source.glob("*/images/*")):
        if p.suffix.lower() not in (".jpg", ".png", ".jpeg"):
            continue
        groups.setdefault(p.stem.split(".rf.")[0], []).append(p)
    buckets = {s: [] for s in ["train", "val", "test"]}
    for group, files in sorted(groups.items()):
        v = int(hashlib.sha256(group.encode()).hexdigest(),16)%10
        split = "test" if v == 0 else "val" if v == 1 else "train"
        rng = random.Random(f"{SEED}:{group}")
        buckets[split].append((group, rng.choice(files)))
    manifest = dict(source=str(source), license="CC-BY-4.0", seed=SEED,
                    grouping="pre-.rf. filename; one variant per group; global pHash distance >6",
                    limitation="Original session provenance unavailable. Filename and pHash grouping do not prove temporal independence.",
                    synthetic="bounded local light veil, not real flash acceptance", samples=[], exclusions=[])
    accepted_hashes = []
    for split, cap in [("test",160),("val",160),("train",1000)]:
        rng = random.Random(f"{SEED}:{split}"); candidates=buckets[split]; rng.shuffle(candidates)
        count=0; negatives=0
        for group,p in candidates:
            if count >= cap: break
            lp=p.parent.parent/"labels"/p.with_suffix('.txt').name
            boxes=labels(lp); image=cv2.imread(str(p))
            if image is None: raise IOError(p)
            if not boxes and negatives >= cap*.25: continue
            ph=phash(image)
            if any((ph^q).bit_count() <= 6 for q in accepted_hashes):
                manifest['exclusions'].append(dict(source=str(p),reason="perceptual_duplicate")); continue
            accepted_hashes.append(ph); sid=hashlib.sha256(str(p).encode()).hexdigest()[:16]
            rec=dict(split=split,group=group,source=str(p),source_sha256=sha(p),objects=len(boxes),synthetic=False)
            rec.update(write_example(dest,split,sid,image,boxes)); manifest['samples'].append(rec)
            if split != 'train' or count%2 == 0:
                fs=split if split=='train' else split+'_flash'
                aug=flash(image,boxes,SEED+int(sid[:8],16))
                fr=dict(rec,split=fs,synthetic=True,parent=sid)
                fr.update(write_example(dest,fs,sid+'_flash',aug,boxes)); manifest['samples'].append(fr)
                if fr['label_sha256'] != rec['label_sha256']: raise AssertionError('Augmentation changed labels')
            count+=1; negatives+=not boxes
    finish_dataset(dest,manifest)


def prepare_apex():
    selection=json.loads((OUT/'apex-selection.json').read_text()); dest=OUT/'apex'
    if dest.exists(): raise FileExistsError(dest)
    manifest=dict(source='PSImera/apex_enemy_detect',revision=selection['revision'],license=selection['license'],
                  seed=SEED,grouping='whole original DVR recording, all provided splits regrouped',
                  samples=[],exclusions=[])
    seen=[]
    for split in ['test','val','train']:
        for r in selection['sources']:
            if r['split'] != split: continue
            p=OUT/'external_apex'/r['remote_image']; lp=OUT/'external_apex'/r['remote_label']
            image=cv2.imread(str(p)); boxes=labels(lp)
            if image is None: raise IOError(p)
            ph=phash(image)
            if any((ph^q).bit_count() <= 4 for q in seen):
                manifest['exclusions'].append(dict(id=r['id'],reason='perceptual_duplicate'));continue
            seen.append(ph)
            rec=dict(r,source_sha256=sha(p),objects=len(boxes),synthetic=False)
            rec.update(write_example(dest,split,r['id'],image,boxes)); manifest['samples'].append(rec)
    finish_dataset(dest,manifest)


def train(game, start):
    from ultralytics import YOLO
    model_path=ROOT/'yolo26n.pt' if start=='clean' else ROOT/'models/best.pt'
    model=YOLO(str(model_path)); torch_threads=4
    import torch
    torch.set_num_threads(torch_threads)
    model.train(data=str(OUT/game/'dataset.yaml'),epochs=16,imgsz=512,batch=8,device=0,workers=0,
                project=str(OUT/'training'),name=f'{game}_{start}',exist_ok=False,cache=False,
                single_cls=True,seed=SEED,deterministic=True,optimizer='AdamW',lr0=.0003,lrf=.1,
                warmup_epochs=1,patience=8,rect=True,mosaic=0,close_mosaic=0,mixup=0,copy_paste=0,
                scale=.2,translate=.05,fliplr=.5,hsv_h=.01,hsv_s=.35,hsv_v=.25,
                degrees=0,shear=0,perspective=0,plots=True,save=True,save_period=4,amp=True)


def iou(a,b):
    lo=np.maximum(a[:2],b[:2]); hi=np.minimum(a[2:],b[2:]); inter=np.maximum(hi-lo,0).prod()
    return float(inter/max(1e-9,(a[2:]-a[:2]).prod()+(b[2:]-b[:2]).prod()-inter))


def evaluate(game, model_path, output, phase='all'):
    from ultralytics import YOLO
    import torch
    torch.set_num_threads(4)
    model=YOLO(model_path); manifest=json.loads((OUT/game/'manifest.json').read_text()); report={}
    splits = ['val','val_flash'] if phase == 'val' else ['test','test_flash'] if phase == 'test' else ['val','val_flash','test','test_flash']
    prior_groups=set()
    if game=='delta':
        prior_groups=set(json.loads((OUT/'prior-training-overlap.json').read_text())['groups'])
        splits += [s+'_unseen' for s in list(splits)]
    for split in splits:
        source_split=split.removesuffix('_unseen')
        entries=[s for s in manifest['samples'] if s['split']==source_split
                 and (not split.endswith('_unseen') or s['group'] not in prior_groups)]
        if not entries: continue
        tp=fp=fn=0; timings=[]; details=[]
        for start in range(0,len(entries),8):
            batch=entries[start:start+8]
            results=model.predict([x['image'] for x in batch],imgsz=(384,480),rect=False,
                                  conf=.4,iou=.7,classes=[0],device=0,verbose=False,batch=8)
            for rec,res in zip(batch,results):
                h,w=res.orig_shape; gt=[]
                for _,x,y,bw,bh in labels(rec['label']): gt.append(np.array([(x-bw/2)*w,(y-bh/2)*h,(x+bw/2)*w,(y+bh/2)*h]))
                matched=set(); hit=0; false=0
                for b in sorted(res.boxes.data.cpu().numpy(),key=lambda b:-float(b[4])):
                    choices=[(iou(b[:4],g),j) for j,g in enumerate(gt) if j not in matched]
                    overlap,j=max(choices,default=(0,-1))
                    if overlap>=.5: matched.add(j);hit+=1
                    else: false+=1
                miss=len(gt)-hit; tp+=hit;fp+=false;fn+=miss;timings.append(res.speed['inference'])
                details.append(dict(image=Path(rec['image']).name,tp=hit,fp=false,fn=miss))
        precision=tp/max(1,tp+fp);recall=tp/max(1,tp+fn)
        report[split]=dict(images=len(entries),objects=tp+fn,tp=tp,fp=fp,fn=fn,precision=precision,
                           recall=recall,f1=2*precision*recall/max(1e-9,precision+recall),
                           pytorch_batch8_inference_ms_median=float(np.median(timings)),details=details)
        print(game,Path(model_path).name,split,{k:v for k,v in report[split].items() if k!='details'},flush=True)
    save_json(output,dict(model=model_path,model_sha256=sha(model_path),dataset_manifest_sha256=sha(OUT/game/'manifest.json'),
                          conf=.4,match_iou=.5,input=[384,480],metrics=report,
                          prior_group_audit_sha256=sha(OUT/'prior-training-overlap.json') if game=='delta' else None,
                          limitation='Offline fixed-threshold box metrics; PyTorch batch timing is not native runtime latency.'))


if __name__=='__main__':
    command=sys.argv[1]
    if command=='prepare-delta': prepare_delta()
    elif command=='prepare-apex': prepare_apex()
    elif command=='train': train(*sys.argv[2:])
    elif command=='evaluate': evaluate(*sys.argv[2:])
    else: raise SystemExit(command)
