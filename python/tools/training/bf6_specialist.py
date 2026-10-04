"""Local, reproducible BF6 weighted fine-tune and native detector comparison.

prepare -> baseline -> train balanced|weighted -> export NAME -> evaluate NAME
Never changes production weights, configuration, or source datasets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import struct
import sys
import time
from collections import defaultdict
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "python"))
from tools import benchmark_vision_dataset as bench
from tools.analyze_fusion_video_replay import load_native_module
from tools.training.game_specialists import phash, sha

OUT = ROOT / "artifacts/bf6-specialist-20261001"
SEED = 20261001
SOURCES = [
    Path("D:/datasets/Battlefield 6 players.v3-roboflow-instant-2--eval-.yolo26"),
    Path("D:/datasets/Battlefield 6.v1i.yolo26"),
]


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf8")


def group_records(records, distance=6):
    """Unify original identities and near duplicates across *all* supplied splits."""
    parent = list(range(len(records)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def merge(i, j):
        parent[root(i)] = root(j)

    seen = {}
    for i, rec in enumerate(records):
        for key in [("original", rec["original_id"]), ("bytes", rec["sha256"])]:
            if key in seen:
                merge(i, seen[key])
            seen[key] = i
        for j in range(i):
            if (rec["phash"] ^ records[j]["phash"]).bit_count() <= distance:
                merge(i, j)
    groups = defaultdict(list)
    for i, rec in enumerate(records):
        groups[root(i)].append(rec)
    return list(groups.values())


def group_split(group):
    # A source test image can never enter fine-tuning or checkpoint selection.
    return max((r["split"] for r in group), key={"train": 0, "valid": 1, "test": 2}.get)


def schedule(bf6_count, replay_count, length, fraction, seed):
    """Same epoch length; only the BF6/replay sampling ratio changes."""
    if min(bf6_count, replay_count, length) <= 0 or not 0 < fraction < 1:
        raise ValueError("positive pools/length and a fraction strictly between 0 and 1 required")
    rng = random.Random(seed)
    entries = []
    for domain, count, amount in [("bf6", bf6_count, round(length * fraction)),
                                  ("replay", replay_count, length-round(length*fraction))]:
        indices = []
        while len(indices) < amount:
            cycle = list(range(count))
            rng.shuffle(cycle)
            indices.extend(cycle)
        entries.extend((domain, i) for i in indices[:amount])
    rng.shuffle(entries)
    return entries


def read_record(path, split, source, selected_classes=()):
    image = cv2.imread(str(path))
    if image is None:
        raise IOError(path)
    height, width = image.shape[:2]
    label = path.parent.parent / "labels" / path.with_suffix(".txt").name
    if not label.is_file():
        raise FileNotFoundError(label)
    rows = [line.split() for line in label.read_text().splitlines() if line.strip()]
    for row in rows:
        values = np.asarray(row, dtype=float)
        if not np.isfinite(values).all() or values[0] != int(values[0]):
            raise ValueError(f"Invalid annotation: {label}")
        if not (len(row) == 5 or len(row) >= 7 and len(row) % 2 == 1):
            raise ValueError(f"Unsupported annotation: {label}")
        if not ((values[1:] >= 0) & (values[1:] <= 1)).all():
            raise ValueError(f"Non-normalized annotation: {label}")
    boxes = bench.parse_yolo_label_file(label, width, height, selected_classes=selected_classes)
    return dict(image=str(path), label=str(label), source=source, split=split,
                width=width, height=height, original_id=path.stem.split(".rf.")[0],
                sha256=sha(path), label_sha256=sha(label), phash=phash(image),
                boxes=[[0, (b.x1+b.x2)/2/width, (b.y1+b.y2)/2/height,
                        b.width/width, b.height/height] for b in boxes])


def write_view(rec, dest, split, name, view):
    image = cv2.imread(rec["image"])
    height, width = image.shape[:2]
    boxes = [bench.Box((x-bw/2)*width, (y-bh/2)*height,
                       (x+bw/2)*width, (y+bh/2)*height)
             for _, x, y, bw, bh in rec["boxes"]]
    if view == "center":
        crop = bench.center_crop_window(width, height, 640, 512)
        image = image[crop.top:crop.top+crop.height, crop.left:crop.left+crop.width]
        boxes = bench.clip_boxes_to_crop(boxes, crop)
        width, height = crop.width, crop.height
    image = cv2.resize(image, (640, 512), interpolation=cv2.INTER_LINEAR)
    rows = [[0, (b.x1+b.x2)/2/width, (b.y1+b.y2)/2/height,
             b.width/width, b.height/height] for b in boxes]
    ip = dest / split / "images" / f"{name}.jpg"
    lp = dest / split / "labels" / f"{name}.txt"
    ip.parent.mkdir(parents=True, exist_ok=True)
    lp.parent.mkdir(parents=True, exist_ok=True)
    if ip.exists() or lp.exists():
        raise FileExistsError(ip)
    if not cv2.imwrite(str(ip), image, [cv2.IMWRITE_JPEG_QUALITY, 95]):
        raise IOError(ip)
    lp.write_text("".join("0 " + " ".join(f"{v:.8f}" for v in b[1:]) + "\n" for b in rows))
    return dict(image=str(ip), label=str(lp), boxes=rows, view=view,
                parent=rec["original_id"], source=rec["source"],
                original_sha256=rec["sha256"], image_sha256=sha(ip), label_sha256=sha(lp))


def dataset_yaml(dest):
    for split in ["train", "valid", "test"]:
        (dest/split/"images").mkdir(parents=True, exist_ok=True)
        (dest/split/"labels").mkdir(parents=True, exist_ok=True)
    (dest/"data.yaml").write_text(
        f"path: {dest.as_posix()}\ntrain: train/images\nval: valid/images\n"
        "test: test/images\nnames:\n  0: person\n")


def prepare():
    if OUT.exists():
        raise FileExistsError(f"Refusing to overwrite frozen experiment: {OUT}")
    records = []
    for source, folder in enumerate(SOURCES):
        for split in ["train", "valid", "test"]:
            records.extend(read_record(p, split, source)
                           for p in bench.iter_image_files(folder/split/"images"))
    groups = group_records(records)
    bf6 = []
    for group in groups:
        split = group_split(group)
        # Prefer player polygons, which include teammates, over enemy-only labels.
        rec = min(group, key=lambda r: (r["source"], r["image"]))
        bf6.append(dict(rec, assigned_split=split,
                        members=[{k: r[k] for k in ["image", "source", "split", "original_id", "sha256"]}
                                 for r in group]))
    train = [r for r in bf6 if r["assigned_split"] == "train"]
    held = [r for r in bf6 if r["assigned_split"] != "train"]
    replay = []
    retention = []
    rng = random.Random(SEED)
    for source, (folder, classes) in enumerate([
        ("BO7-V1.v10-v6.yolo26", ("0",)),
        ("COD MW Warzone.v2i.yolo26", ("0",)),
        ("Cod WZ.v2i.yolo26", ("0", "1", "2")),
    ]):
        root = ROOT/"models/train"/folder
        candidates = list(bench.iter_image_files(root/"test/images")) if (root/"test/images").is_dir() else []
        if not candidates:
            candidates = list(bench.iter_image_files(root/"valid/images"))
        rng.shuffle(candidates)
        local_hold = []
        for p in candidates:
            if len(local_hold) >= 60:
                break
            rec = read_record(p, "test", f"cod{source}", classes)
            if rec["boxes"] and not any((rec["phash"] ^ r["phash"]).bit_count() <= 6 for r in held+local_hold):
                local_hold.append(rec)
        retention.extend(local_hold)
        candidates = list(bench.iter_image_files(root/"train/images"))
        rng.shuffle(candidates)
        for p in candidates:
            if len([r for r in replay if r["source"] == f"cod{source}"]) >= 150:
                break
            rec = read_record(p, "train", f"cod{source}", classes)
            if rec["original_id"] in {r["original_id"] for r in retention+replay}:
                continue
            if not rec["boxes"] or any((rec["phash"] ^ r["phash"]).bit_count() <= 6
                                        for r in held+retention+replay):
                continue
            replay.append(rec)
    rng.shuffle(replay)
    replay = replay[:len(train)]
    if len(replay) < len(train) or min(sum(r["assigned_split"] == s for r in bf6)
                                      for s in ["valid", "test"]) < 40:
        raise ValueError("Insufficient independent pools for the frozen design")
    OUT.mkdir(parents=True)
    eval_records = []
    for rec in held:
        split = rec["assigned_split"]
        for view in ["full", "center"]:
            row = write_view(rec, OUT/f"evaluation-data/bf6_{view}", split, rec["original_id"], view)
            eval_records.append(dict(row, domain=f"bf6_{view}", split=split))
    for rec in retention:
        row = write_view(rec, OUT/"evaluation-data/cod_retention", "test", f"{rec['source']}_{rec['original_id']}", "full")
        eval_records.append(dict(row, domain="cod_retention", split="test"))
    for domain in ["bf6_full", "bf6_center", "cod_retention"]:
        dataset_yaml(OUT/"evaluation-data"/domain)
    pools = {"bf6": [], "replay": []}
    for domain, entries in [("bf6", train), ("replay", replay)]:
        for rec in entries:
            views = ["full", "center"] if domain == "bf6" else ["full"]
            for view in views:
                name = f"{domain}_{rec['source']}_{rec['original_id']}_{view}"
                pools[domain].append(write_view(rec, OUT/"pool", "train", name, view))
    length = len(train)*5
    schedules = {}
    for name, fraction in [("balanced", .5), ("weighted", .8)]:
        dest = OUT/"datasets"/name
        entries = schedule(len(pools["bf6"]), len(pools["replay"]), length, fraction, SEED)
        schedules[name] = []
        for i, (domain, index) in enumerate(entries):
            rec = pools[domain][index]
            for kind, ext in [("images", ".jpg"), ("labels", ".txt")]:
                src = Path(rec["image" if kind == "images" else "label"])
                dst = dest/"train"/kind/f"{i:05d}_{domain}{ext}"
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
            schedules[name].append(dict(domain=domain, parent=rec["parent"], view=rec["view"]))
        dataset_yaml(dest)
        # Validation is identical for both trainers; model selection never reads test.
        yaml = dest/"data.yaml"
        text = yaml.read_text().replace("val: valid/images", f"val: {(OUT/'evaluation-data/bf6_full/valid/images').as_posix()}")
        yaml.write_text(text)
    save(OUT/"manifest.json", dict(seed=SEED, sources=[str(p) for p in SOURCES],
         source_records=records, grouped_records=bf6, replay=replay, retention=retention,
         evaluation=eval_records, schedules=schedules,
         grouping="original .rf identity + exact bytes + transitive pHash distance <=6; test > valid > train",
         label_policy="player polygons -> enclosing person box; prefer player when duplicate; COD head labels excluded",
         limitation="Unknown recording identities: near-duplicate grouping does not prove independence of entire videos. Enemy-only unique images may omit teammates."))
    save(OUT/"frozen-contract.json", dict(seed=SEED, baseline_pt=str(ROOT/"models/best.pt"),
         baseline_pt_sha256=sha(ROOT/"models/best.pt"), baseline_engine_sha256=sha(ROOT/"models/best_480x384.engine"),
         manifest_sha256=sha(OUT/"manifest.json"), epochs=16, epoch_examples=length, batch=8,
         fractions={"balanced": .5, "weighted": .8}, input=[384,480], confidence=[.4,.65], match_iou=.5,
         selection="Native validation bf6_full F1; eligible when precision and recall both >= baseline; center crop also protected. Test never selects checkpoint.",
         native_timing_gate="matched isolated inference p50 <= baseline*1.10; live runtime latency not established",
         cohorts="native box heights <24, 24..64, >=64; negative frames; centered ROI; COD retention is prior-source diagnostic, not unseen generalization"))
    print("prepared", {s:sum(r["assigned_split"]==s for r in bf6) for s in ["train","valid","test"]},
          "replay",len(replay),"retention",len(retention),"epoch examples",length,flush=True)


def train(name):
    from ultralytics import YOLO
    import torch
    torch.set_num_threads(4)
    model = YOLO(str(ROOT/"models/best.pt"))
    model.train(data=str(OUT/"datasets"/name/"data.yaml"), epochs=16, imgsz=480,
        batch=8, device=0, workers=0, project=str(OUT/"training"), name=name,
        exist_ok=False, cache=False, single_cls=True, seed=SEED, deterministic=True,
        optimizer="AdamW", lr0=.0003, lrf=.1, warmup_epochs=1, patience=100,
        rect=True, mosaic=0, close_mosaic=0, mixup=0, copy_paste=0, scale=.2,
        translate=.05, fliplr=.5, hsv_h=.01, hsv_s=.35, hsv_v=.25,
        degrees=0, shear=0, perspective=0, plots=True, save=True, amp=True)


def export(name):
    from ultralytics import YOLO
    import tensorrt as trt
    dest = OUT/"exports"/name
    dest.mkdir(parents=True, exist_ok=False)
    pt = dest/f"bf6_{name}.pt"
    shutil.copy2(OUT/"training"/name/"weights/best.pt", pt)
    onnx = YOLO(str(pt)).export(format="onnx", imgsz=(384,480), batch=1, half=False,
                              dynamic=False, simplify=True, opset=17, device=0, nms=False)
    logger = trt.Logger(trt.Logger.WARNING)
    builder = trt.Builder(logger)
    network = builder.create_network(0)
    parser = trt.OnnxParser(network, logger)
    if not parser.parse_from_file(str(onnx)):
        raise RuntimeError("\n".join(str(parser.get_error(i)) for i in range(parser.num_errors)))
    assert network.num_inputs == network.num_outputs == 1
    assert tuple(network.get_input(0).shape) == (1,3,384,480)
    assert tuple(network.get_output(0).shape) == (1,300,6)
    assert network.get_input(0).dtype == network.get_output(0).dtype == trt.float32
    config = builder.create_builder_config()
    config.set_memory_pool_limit(trt.MemoryPoolType.WORKSPACE, 2<<30)
    config.set_flag(trt.BuilderFlag.FP16)
    plan = builder.build_serialized_network(network, config)
    if plan is None:
        raise RuntimeError("TensorRT build failed")
    metadata = dict(names={"0":"person"},task="detect",batch=1,imgsz=[384,480],
                    half=True,end2end=True,source_sha256=sha(pt),tensorrt=trt.__version__)
    encoded = json.dumps(metadata).encode()
    engine = dest/f"bf6_{name}_480x384.engine"
    engine.write_bytes(struct.pack("<I",len(encoded))+encoded+bytes(plan))
    save(dest/"export.json",dict(metadata,engine=str(engine),engine_sha256=sha(engine),
                               checkpoint=str(pt),onnx_sha256=sha(onnx)))
    print(engine,flush=True)


def evaluate(name, phase):
    manifest = json.loads((OUT/"manifest.json").read_text())
    model = ROOT/"models/best_480x384.engine" if name == "baseline" else OUT/f"exports/{name}/bf6_{name}_480x384.engine"
    native = load_native_module()
    engine = native.NativeEngine(str(model))
    records = [r for r in manifest["evaluation"] if r["split"] == phase]
    raw_records = []
    metrics = {}
    for domain in sorted(set(r["domain"] for r in records)):
        samples = [r for r in records if r["domain"]==domain]
        first = cv2.cvtColor(cv2.imread(samples[0]["image"]),cv2.COLOR_BGR2RGB)
        for _ in range(30): engine.infer_rgb(first,.05)
        for rec in samples:
            rgb = cv2.cvtColor(cv2.imread(rec["image"]), cv2.COLOR_BGR2RGB)
            before=time.perf_counter(); raw=engine.infer_rgb(rgb,.05)
            wall=(time.perf_counter()-before)*1000
            truths=[bench.Box((x-w/2)*640,(y-h/2)*512,(x+w/2)*640,(y+h/2)*512)
                    for _,x,y,w,h in rec["boxes"]]
            row=dict(image=rec["image"],parent=rec["parent"],domain=domain,boxes=rec["boxes"],
                     detections=raw["detections"],infer_ms=raw.get("infer_ms"),
                     gpu_ms=raw.get("gpu_total_ms"),wall_ms=wall,thresholds={})
            for conf in [.4,.65]:
                detections=[bench.Box(d['x1'],d['y1'],d['x2'],d['y2'],conf=d['conf'])
                            for d in raw["detections"] if d["conf"]>=conf]
                match=bench.match_detections(truths,detections,iou_threshold=.5)
                row["thresholds"][str(conf)]=dict(tp=match.tp,fp=match.fp,fn=match.fn,
                                                 matched_truth=[m.ground_truth_index for m in match.matches])
            raw_records.append(row)
        rows=[r for r in raw_records if r["domain"]==domain]
        for conf in [.4,.65]:
            counts={k:sum(r["thresholds"][str(conf)][k] for r in rows) for k in ["tp","fp","fn"]}
            p=counts["tp"]/max(1,counts["tp"]+counts["fp"])
            recall=counts["tp"]/max(1,counts["tp"]+counts["fn"])
            small={}
            for key,lo,hi in [("small",0,24),("medium",24,64),("large",64,float("inf"))]:
                total=hits=0
                for r in rows:
                    for i,b in enumerate(r["boxes"]):
                        if lo<=b[4]*384<hi:
                            total+=1;hits+=i in r["thresholds"][str(conf)]["matched_truth"]
                small[key]=dict(objects=total,hits=hits,recall=hits/max(1,total))
            negative=[r for r in rows if not r["boxes"]]
            metrics[f"{domain}@{conf}"]=dict(counts,images=len(rows),precision=p,recall=recall,
                f1=2*p*recall/max(1e-9,p+recall),size_cohorts=small,negative_images=len(negative),
                negative_fp=sum(r["thresholds"][str(conf)]["fp"] for r in negative))
        metrics[domain+"_timing"]=dict(
            infer_ms_p50=float(np.median([r["infer_ms"] for r in rows])),
            infer_ms_p95=float(np.percentile([r["infer_ms"] for r in rows],95)),
            wall_ms_p50=float(np.median([r["wall_ms"] for r in rows])),
            wall_ms_p95=float(np.percentile([r["wall_ms"] for r in rows],95)))
    dest=OUT/f"evaluation/{name}_{phase}.json"
    if dest.exists():raise FileExistsError(dest)
    save(dest,dict(model=str(model),model_sha256=sha(model),manifest_sha256=sha(OUT/"manifest.json"),
                   native_sha256=sha(ROOT/"native/build/Release/vision_native_cpp.cp311-win_amd64.pyd"),
                   phase=phase,metrics=metrics,records=raw_records,
                   limitation="Offline native detections; approximate center crop of already-resized public images, not exact gameplay capture. Timing is isolated infer_rgb, not controller latency."))
    print(json.dumps(metrics,indent=2),flush=True)


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=["prepare","train","export","evaluate"])
    parser.add_argument("name",nargs="?",choices=["baseline","balanced","weighted"])
    parser.add_argument("--phase",choices=["valid","test"],default="valid")
    parser.add_argument("--out", type=Path, default=OUT, help="Fresh artifact directory for the complete experiment")
    args=parser.parse_args()
    OUT = args.out.resolve()
    if args.action=="prepare":prepare()
    elif args.action=="train":train(args.name)
    elif args.action=="export":export(args.name)
    else:evaluate(args.name,args.phase)


if __name__=="__main__":main()
