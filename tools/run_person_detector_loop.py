import argparse
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MODEL_PATH = PROJECT_ROOT / "models" / "train" / "best.pt"
DEFAULT_DATASET_YAML = PROJECT_ROOT / "models" / "train" / "cod_combined_single_cls.yaml"
DEFAULT_TRAIN_PROJECT = PROJECT_ROOT / "runs" / "person_train"
DEFAULT_VAL_PROJECT = PROJECT_ROOT / "runs" / "person_val"
DEFAULT_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "person_detector_loops"


def _parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Run a stable baseline-valid/train/valid loop for the one-class person detector."
    )
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL_PATH, help="Starting model checkpoint.")
    parser.add_argument("--data", type=Path, default=DEFAULT_DATASET_YAML, help="YOLO dataset YAML.")
    parser.add_argument("--loops", type=int, default=2, help="Number of train+valid loops to run.")
    parser.add_argument("--epochs-per-loop", type=int, default=1, help="Training epochs per loop.")
    parser.add_argument("--train-imgsz", type=int, default=640, help="Training image size for loop runs.")
    parser.add_argument("--val-imgsz", type=int, default=640, help="Validation image size.")
    parser.add_argument("--batch", type=int, default=8, help="Batch size for both train and validation.")
    parser.add_argument("--device", default="0", help="CUDA device id, e.g. 0.")
    parser.add_argument("--workers", type=int, default=0, help="Stable Windows default is 0.")
    parser.add_argument("--cache", default="false", help="Training cache mode passed to train_person_detector.py.")
    parser.add_argument("--warmup-epochs", type=float, default=1.0, help="Training warmup epochs passed through.")
    parser.add_argument("--close-mosaic", type=int, default=0, help="Training close_mosaic passed through.")
    parser.add_argument("--mosaic", type=float, default=0.0, help="Training mosaic probability passed through.")
    parser.add_argument("--erasing", type=float, default=0.0, help="Training random erasing probability passed through.")
    parser.add_argument(
        "--auto-augment",
        default="none",
        help="Training auto augment policy passed through: none, randaugment, augmix, or autoaugment.",
    )
    parser.add_argument("--scale", type=float, default=0.25, help="Training scale augmentation gain passed through.")
    parser.add_argument("--translate", type=float, default=0.05, help="Training translate augmentation gain passed through.")
    parser.add_argument("--fliplr", type=float, default=0.5, help="Training horizontal flip probability passed through.")
    parser.add_argument("--hsv-h", type=float, default=0.01, help="Training HSV hue gain passed through.")
    parser.add_argument("--hsv-s", type=float, default=0.45, help="Training HSV saturation gain passed through.")
    parser.add_argument("--hsv-v", type=float, default=0.25, help="Training HSV value gain passed through.")
    parser.add_argument("--optimizer", default="auto", help="Training optimizer passed through.")
    parser.add_argument("--lr0", type=float, default=None, help="Training initial learning rate passed through.")
    parser.add_argument("--lrf", type=float, default=None, help="Training final learning-rate fraction passed through.")
    parser.add_argument("--cos-lr", action="store_true", help="Use cosine learning-rate schedule for training.")
    parser.add_argument("--patience", type=int, default=100, help="Training early-stopping patience passed through.")
    parser.add_argument("--run-prefix", default="cod_loop", help="Prefix for Ultralytics run names.")
    parser.add_argument("--train-project", type=Path, default=DEFAULT_TRAIN_PROJECT, help="Training output project dir.")
    parser.add_argument("--val-project", type=Path, default=DEFAULT_VAL_PROJECT, help="Validation output project dir.")
    parser.add_argument("--artifact-dir", type=Path, default=DEFAULT_ARTIFACT_DIR, help="Loop logs and summary dir.")
    parser.add_argument("--skip-baseline", action="store_true", help="Skip the initial baseline validation.")
    parser.add_argument("--plots", action="store_true", help="Write validation plots.")
    parser.add_argument("--dry-run", action="store_true", help="Print commands and write summary without executing them.")
    return parser.parse_args(argv)


def _timestamp():
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def _stringify(command):
    return subprocess.list2cmdline([str(part) for part in command])


def _write_console(text):
    try:
        sys.stdout.write(text)
        sys.stdout.flush()
    except UnicodeEncodeError:
        encoding = sys.stdout.encoding or "utf-8"
        sys.stdout.buffer.write(text.encode(encoding, errors="replace"))
        sys.stdout.buffer.flush()


def _python_command(script, *args):
    return [sys.executable, str(PROJECT_ROOT / "tools" / script), *[str(arg) for arg in args]]


def _base_env():
    env = os.environ.copy()
    env["PYTHONUNBUFFERED"] = "1"
    env.setdefault("YOLO_CONFIG_DIR", str(PROJECT_ROOT / "artifacts" / "ultralytics"))
    env.setdefault("MPLCONFIGDIR", str(PROJECT_ROOT / "artifacts" / "mpl"))
    return env


def _run_step(command, *, log_path, dry_run):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    rendered = _stringify(command)
    print(f"[Loop] command: {rendered}")
    with log_path.open("w", encoding="utf-8") as log_file:
        log_file.write(rendered + "\n\n")
        if dry_run:
            log_file.write("[Loop] dry-run: command not executed.\n")
            return 0
        process = subprocess.Popen(
            [str(part) for part in command],
            cwd=str(PROJECT_ROOT),
            env=_base_env(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            _write_console(line)
            log_file.write(line)
        return process.wait()


def _validation_command(args, *, model, name):
    command = _python_command(
        "validate_person_detector.py",
        "--model",
        model,
        "--data",
        args.data,
        "--imgsz",
        args.val_imgsz,
        "--batch",
        args.batch,
        "--device",
        args.device,
        "--workers",
        args.workers,
        "--project",
        args.val_project,
        "--name",
        name,
        "--exist-ok",
    )
    if args.plots:
        command.append("--plots")
    return command


def _training_command(args, *, model, name):
    command = _python_command(
        "train_person_detector.py",
        "--model",
        model,
        "--data",
        args.data,
        "--epochs",
        args.epochs_per_loop,
        "--imgsz",
        args.train_imgsz,
        "--batch",
        args.batch,
        "--device",
        args.device,
        "--workers",
        args.workers,
        "--cache",
        args.cache,
        "--warmup-epochs",
        args.warmup_epochs,
        "--close-mosaic",
        args.close_mosaic,
        "--mosaic",
        args.mosaic,
        "--erasing",
        args.erasing,
        "--auto-augment",
        args.auto_augment,
        "--scale",
        args.scale,
        "--translate",
        args.translate,
        "--fliplr",
        args.fliplr,
        "--hsv-h",
        args.hsv_h,
        "--hsv-s",
        args.hsv_s,
        "--hsv-v",
        args.hsv_v,
        "--optimizer",
        args.optimizer,
        "--patience",
        args.patience,
        "--project",
        args.train_project,
        "--name",
        name,
        "--exist-ok",
    )
    if args.lr0 is not None:
        command.extend(["--lr0", args.lr0])
    if args.lrf is not None:
        command.extend(["--lrf", args.lrf])
    if args.cos_lr:
        command.append("--cos-lr")
    return command


def main(argv=None):
    args = _parse_args(argv)
    if args.loops < 0:
        raise SystemExit("--loops must be >= 0")
    run_id = f"{args.run_prefix}_{_timestamp()}"
    loop_dir = args.artifact_dir / run_id
    loop_dir.mkdir(parents=True, exist_ok=True)

    current_model = args.model
    steps = []
    summary = {
        "run_id": run_id,
        "start_model": str(args.model),
        "data": str(args.data),
        "loops": args.loops,
        "epochs_per_loop": args.epochs_per_loop,
        "train_imgsz": args.train_imgsz,
        "val_imgsz": args.val_imgsz,
        "batch": args.batch,
        "workers": args.workers,
        "cache": args.cache,
        "warmup_epochs": args.warmup_epochs,
        "close_mosaic": args.close_mosaic,
        "mosaic": args.mosaic,
        "erasing": args.erasing,
        "auto_augment": args.auto_augment,
        "scale": args.scale,
        "translate": args.translate,
        "fliplr": args.fliplr,
        "hsv_h": args.hsv_h,
        "hsv_s": args.hsv_s,
        "hsv_v": args.hsv_v,
        "optimizer": args.optimizer,
        "lr0": args.lr0,
        "lrf": args.lrf,
        "cos_lr": args.cos_lr,
        "patience": args.patience,
        "steps": steps,
    }

    def run_named_step(step_name, command):
        log_path = loop_dir / f"{len(steps) + 1:02d}_{step_name}.log"
        returncode = _run_step(command, log_path=log_path, dry_run=args.dry_run)
        step_record = {
            "name": step_name,
            "command": _stringify(command),
            "log": str(log_path),
            "returncode": returncode,
        }
        steps.append(step_record)
        if returncode != 0:
            (loop_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
            raise SystemExit(returncode)

    if not args.skip_baseline:
        baseline_name = f"{run_id}_baseline"
        run_named_step("baseline_val", _validation_command(args, model=current_model, name=baseline_name))

    for index in range(1, args.loops + 1):
        train_name = f"{run_id}_train_{index:02d}"
        run_named_step(f"train_{index:02d}", _training_command(args, model=current_model, name=train_name))
        trained_model = args.train_project / train_name / "weights" / "best.pt"
        if not args.dry_run and not trained_model.exists():
            raise SystemExit(f"Expected trained model was not created: {trained_model}")
        current_model = trained_model
        val_name = f"{run_id}_val_{index:02d}"
        run_named_step(f"val_{index:02d}", _validation_command(args, model=current_model, name=val_name))

    summary["final_model"] = str(current_model)
    (loop_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"[Loop] complete | summary={loop_dir / 'summary.json'}")


if __name__ == "__main__":
    main()
