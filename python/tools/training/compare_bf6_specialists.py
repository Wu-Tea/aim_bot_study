"""Lock selection using native validation, then compare frozen BF6 holdout runs."""
import argparse
import json
import random
import sys
import time
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "python"))
from tools.analyze_fusion_video_replay import load_native_module
from tools.training.game_specialists import sha

NAMES = ["baseline", "balanced", "weighted"]


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf8")


def load_reports(out, phase):
    reports = {name: json.loads((out/f"evaluation/{name}_{phase}.json").read_text()) for name in NAMES}
    return reports


def gates(baseline, candidate):
    return {f"{domain}@{conf}/{metric}": candidate[f"{domain}@{conf}"][metric] >= baseline[f"{domain}@{conf}"][metric]
            for domain in ["bf6_full", "bf6_center"] for conf in [.4,.65]
            for metric in ["precision", "recall"]}


def select(out):
    reports = load_reports(out, "valid")
    checks = {name:gates(reports["baseline"]["metrics"], reports[name]["metrics"])
              for name in NAMES[1:]}
    eligible = [name for name in NAMES[1:] if all(checks[name].values())]
    best = max(eligible, key=lambda n: reports[n]["metrics"]["bf6_full@0.4"]["f1"]) if eligible else None
    exploratory = max(NAMES[1:], key=lambda n: reports[n]["metrics"]["bf6_full@0.4"]["f1"])
    dest = out/"locked-selection.json"
    if dest.exists():
        raise FileExistsError(dest)
    save(dest, dict(selected=best, best_exploratory=exploratory, eligible=eligible,
         protected_validation=checks, criterion="full-frame native validation F1@0.4 ranks eligible candidates only",
         models={n:dict(model=r["model"], sha256=r["model_sha256"]) for n,r in reports.items()}))
    print(dest, "selected", best, "exploratory", exploratory)


def timing(out):
    selected = json.loads((out/"locked-selection.json").read_text())
    native = load_native_module()
    engines = {name:native.NativeEngine(v["model"]) for name,v in selected["models"].items()}
    manifest = json.loads((out/"manifest.json").read_text())
    samples = [r for r in manifest["evaluation"] if r["domain"] == "bf6_full" and r["split"] == "test"]
    random.Random(20261002).shuffle(samples)
    images = [cv2.cvtColor(cv2.imread(r["image"]),cv2.COLOR_BGR2RGB) for r in samples[:40]]
    for _ in range(40):
        for engine in engines.values():
            engine.infer_rgb(images[0], .4)
    records = {name:[] for name in NAMES}
    rng = random.Random(20261003)
    for repeat in range(8):
        for i, image in enumerate(images):
            names=NAMES.copy();rng.shuffle(names)
            for name in names:
                before=time.perf_counter();raw=engines[name].infer_rgb(image,.4)
                records[name].append(dict(image_index=i,repeat=repeat,infer_ms=raw["infer_ms"],
                                         wall_ms=(time.perf_counter()-before)*1000))
    summary = {name:{f"{field}_p{p}":float(np.percentile([r[field] for r in rows],p))
                     for field in ["infer_ms","wall_ms"] for p in [50,95]}
               for name,rows in records.items()}
    save(out/"interleaved-timing.json",dict(summary=summary,records=records,
         seed=20261003,confidence=.4,images=40,repetitions=8,
         inference_gate={n:summary[n]["infer_ms_p50"] <= summary["baseline"]["infer_ms_p50"]*1.1 for n in NAMES[1:]},
         limitation="Isolated native infer_rgb; randomized model order with matched images; no training during measurement. Not capture-to-controller/live latency."))
    print(json.dumps(summary,indent=2))


def scores(counts):
    tp,fp,fn = counts.T
    return np.stack((tp/np.maximum(1,tp+fp),tp/np.maximum(1,tp+fn),
                     2*tp/np.maximum(1,2*tp+fp+fn)),axis=-1)


def compare(out):
    locked=json.loads((out/"locked-selection.json").read_text())
    reports=load_reports(out,"test")
    timing_report=json.loads((out/"interleaved-timing.json").read_text())
    checks={n:gates(reports["baseline"]["metrics"],reports[n]["metrics"]) for n in NAMES[1:]}
    changes={}; intervals={}
    for domain in ["bf6_full","bf6_center","cod_retention"]:
        rows={n:{r["parent"]:r for r in report["records"] if r["domain"]==domain}
              for n,report in reports.items()}
        ids=sorted(rows["baseline"])
        if any(set(r)!=set(ids) for r in rows.values()):
            raise ValueError("Matched holdout images changed")
        for name in NAMES[1:]:
            changes[f"{name}/{domain}"]=[]
            for key in ids:
                b,c=rows["baseline"][key],rows[name][key]
                if b["boxes"] != c["boxes"]:
                    raise ValueError("Matched annotations changed")
                bt,ct=b["thresholds"]["0.4"],c["thresholds"]["0.4"]
                old,new=set(bt["matched_truth"]),set(ct["matched_truth"])
                changes[f"{name}/{domain}"].append(dict(parent=key,image=b["image"],
                    gained=sorted(new-old),lost=sorted(old-new),fp_delta=ct["fp"]-bt["fp"]))
            # Paired resampling of complete images preserves multi-person dependence.
            indices=np.random.default_rng(20261002).integers(0,len(ids),size=(5000,len(ids)))
            for conf in [.4,.65]:
                arrays={n:np.array([[rows[n][key]["thresholds"][str(conf)][k] for k in ["tp","fp","fn"]]
                                    for key in ids]) for n in ["baseline",name]}
                delta=scores(arrays[name][indices].sum(axis=1))-scores(arrays["baseline"][indices].sum(axis=1))
                intervals[f"{name}/{domain}@{conf}"]={metric:list(np.percentile(delta[:,i],[2.5,97.5]))
                                                       for i,metric in enumerate(["precision","recall","f1"])}
    comparison=dict(selection=locked,holdout_protected=checks,
        promotion_eligible={n:all(checks[n].values()) and timing_report["inference_gate"][n] for n in NAMES[1:]},
        production_replacement=None,metrics={n:r["metrics"] for n,r in reports.items()},
        timing=timing_report["summary"],timing_gate=timing_report["inference_gate"],paired_image_bootstrap_95ci=intervals,
        per_case_changes=changes,status="EXPLORATORY / HOLDOUT_PRECISION_REGRESSION / MISSING_BACKGROUND_COVERAGE",
        limitations=["Only four nominal negative BF6 holdout images; background false-positive coverage is insufficient.",
                     "Shared public screenshot/recording source; grouping removes original/near duplicates, not all same-recording correlation.",
                     "Enemy-only unique images and imperfect public labels can omit visible teammates or people.",
                     "COD retention uses sources related to prior training; it is diagnostic, not unseen generalization.",
                     "No new enemy cue or fire authority is supplied by the person detector."])
    save(out/"comparison.json",comparison)
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(2,2,figsize=(12,8),layout="constrained")
    colors=["#667085","#4479d4","#dc8a20"]
    for ax,key,title in zip(axes.flat,
                           ["bf6_full@0.4","bf6_center@0.4","bf6_full@0.65","cod_retention@0.4"],
                           ["BF6 full image, confidence 0.40","BF6 center ROI, confidence 0.40",
                            "BF6 full image, confidence 0.65","COD retention diagnostic, confidence 0.40"]):
        for i,name in enumerate(NAMES):
            vals=[reports[name]["metrics"][key][m]*100 for m in ["precision","recall","f1"]]
            bars=ax.bar(np.arange(3)+(i-1)*.25,vals,.25,label=name,color=colors[i])
            ax.bar_label(bars,fmt="%.1f",fontsize=8,padding=2)
        ax.set_xticks(np.arange(3),["Precision","Recall","F1"]);ax.set_ylim(0,105)
        ax.set_title(title);ax.set_ylabel("Percent");ax.grid(axis="y",alpha=.2);ax.set_axisbelow(True)
    axes.flat[0].legend(loc="upper left",fontsize=8)
    fig.suptitle("Frozen holdout | baseline vs BF6 sampling 50% / 80% | IoU 0.50")
    fig.savefig(out/"comparison.png",dpi=160)
    plt.close(fig)
    print(out/"comparison.json")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=["select","timing","compare"])
    parser.add_argument("--out",type=Path,default=ROOT/"artifacts/bf6-specialist-20261001-v2")
    args=parser.parse_args()
    {"select":select,"timing":timing,"compare":compare}[args.action](args.out.resolve())


if __name__=="__main__":main()
