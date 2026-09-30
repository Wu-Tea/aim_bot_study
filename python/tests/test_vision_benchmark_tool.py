import json
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

from tools import benchmark_vision_dataset as bench


class VisionBenchmarkToolTests(unittest.TestCase):
    def test_box_iou_handles_overlap_and_empty_union(self):
        left = bench.Box(0, 0, 10, 10)
        right = bench.Box(5, 5, 15, 15)
        self.assertAlmostEqual(bench.box_iou(left, right), 25 / 175)
        self.assertEqual(bench.box_iou(bench.Box(0, 0, 0, 10), right), 0.0)

    def test_match_detections_counts_tp_fp_fn(self):
        truth = [bench.Box(0, 0, 10, 10, class_id=0), bench.Box(20, 20, 30, 30, class_id=1)]
        detections = [
            bench.Box(0, 0, 10, 10, class_id=7, conf=0.9),
            bench.Box(50, 50, 60, 60, class_id=7, conf=0.8),
        ]

        result = bench.match_detections(truth, detections, iou_threshold=0.5)

        self.assertEqual(result.tp, 1)
        self.assertEqual(result.fp, 1)
        self.assertEqual(result.fn, 1)

    def test_class_aware_matching_requires_same_class(self):
        truth = [bench.Box(0, 0, 10, 10, class_id=1)]
        detections = [bench.Box(0, 0, 10, 10, class_id=0, conf=0.9)]

        result = bench.match_detections(truth, detections, iou_threshold=0.5, class_aware=True)

        self.assertEqual((result.tp, result.fp, result.fn), (0, 1, 1))

    def test_parse_yolo_labels_filters_by_class_name_or_id(self):
        with tempfile.TemporaryDirectory() as tmp:
            label = Path(tmp) / "image.txt"
            label.write_text(
                textwrap.dedent(
                    """
                    0 0.5 0.5 0.2 0.4
                    1 0.5 0.5 0.1 0.1
                    """
                ).strip(),
                encoding="utf-8",
            )

            boxes = bench.parse_yolo_label_file(
                label,
                100,
                200,
                class_names=("body", "head"),
                selected_classes=bench.parse_target_classes("body"),
            )

        self.assertEqual(len(boxes), 1)
        self.assertEqual(boxes[0].class_id, 0)
        self.assertAlmostEqual(boxes[0].x1, 40.0)
        self.assertAlmostEqual(boxes[0].y1, 60.0)
        self.assertAlmostEqual(boxes[0].x2, 60.0)
        self.assertAlmostEqual(boxes[0].y2, 140.0)

    def test_center_crop_clips_labels_to_roi_coordinates(self):
        crop = bench.center_crop_window(100, 80, 40, 20)
        boxes = [bench.Box(45, 35, 80, 55, class_id=0)]

        clipped = bench.clip_boxes_to_crop(boxes, crop)

        self.assertEqual(crop, bench.CropWindow(left=30, top=30, width=40, height=20, source_width=100, source_height=80))
        self.assertEqual(len(clipped), 1)
        self.assertEqual(clipped[0], bench.Box(15, 5, 40, 20, class_id=0, conf=1.0))

    def test_crop_target_records_keep_original_identity_and_visible_fraction(self):
        crop = bench.center_crop_window(100, 80, 40, 20)
        boxes = [
            bench.Box(45, 35, 80, 55, class_id=0),
            bench.Box(0, 0, 10, 10, class_id=1),
        ]

        targets = bench.crop_targets_to_window(boxes, crop)

        self.assertEqual(len(targets), 1)
        self.assertEqual(targets[0].target_index, 0)
        self.assertEqual(targets[0].original, boxes[0])
        self.assertEqual(targets[0].cropped, bench.Box(15, 5, 40, 20, class_id=0, conf=1.0))
        self.assertAlmostEqual(targets[0].visible_fraction, (25 * 15) / (35 * 20))

    def test_crop_fits_source_rejects_silent_clamp(self):
        self.assertTrue(bench.crop_fits_source(640, 640, 640, 512))
        self.assertFalse(bench.crop_fits_source(320, 320, 640, 512))

    def test_target_buckets_use_tensor_space_and_visibility(self):
        target = bench.CroppedTarget(
            target_index=2,
            original=bench.Box(0, 0, 40, 40),
            cropped=bench.Box(0, 0, 20, 20),
            visible_fraction=0.25,
        )

        record = bench.build_target_record(
            target,
            crop_width=40,
            crop_height=40,
            tensor_width=80,
            tensor_height=80,
            matched=False,
            matched_iou=None,
        )

        self.assertEqual(record["size_bucket"], "medium")
        self.assertEqual(record["visibility_bucket"], "severe_clip")
        self.assertAlmostEqual(record["tensor_width"], 40.0)
        self.assertAlmostEqual(record["tensor_height"], 40.0)

    def test_matching_exposes_stable_ground_truth_indices(self):
        truth = [bench.Box(0, 0, 10, 10), bench.Box(20, 20, 30, 30)]
        detections = [
            bench.Box(20, 20, 30, 30, conf=0.8),
            bench.Box(0, 0, 10, 10, conf=0.9),
        ]

        result = bench.match_detections(truth, detections, iou_threshold=0.5)

        self.assertEqual({pair.ground_truth_index for pair in result.matches}, {0, 1})

    def test_benchmark_strict_crop_records_undersized_skip_without_inference(self):
        class Engine:
            def infer_rgb(self, _rgb, _conf):
                raise AssertionError("undersized input must not run inference")

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            image_dir = root / "valid" / "images"
            label_dir = root / "valid" / "labels"
            image_dir.mkdir(parents=True)
            label_dir.mkdir(parents=True)
            image_path = image_dir / "small.jpg"
            image_path.write_bytes(b"placeholder")
            (label_dir / "small.txt").write_text("", encoding="utf-8")
            records_path = root / "frames.jsonl"
            writer = bench.FrameRecordWriter(records_path, "wide320")
            dataset = bench.DatasetSplit(
                root=root,
                name="demo",
                split="valid",
                image_dir=image_dir,
                label_dir=label_dir,
                class_names=(),
            )
            fake_rgb = type("FakeRgb", (), {"shape": (320, 320, 3)})()
            try:
                with mock.patch.object(bench, "load_rgb_array", return_value=fake_rgb):
                    summary = bench.benchmark_split(
                        Engine(),
                        dataset,
                        conf=0.25,
                        iou_threshold=0.5,
                        selected_classes=(),
                        class_aware=False,
                        crop_width=640,
                        crop_height=512,
                        max_images=None,
                        warmup=0,
                        tensor_width=320,
                        tensor_height=256,
                        strict_crop_size=True,
                        frame_budgets_ms=(8.333,),
                        frame_writer=writer,
                    )
            finally:
                writer.close()

            records = [json.loads(line) for line in records_path.read_text(encoding="utf-8").splitlines()]

        self.assertEqual(summary.images, 0)
        self.assertEqual(summary.skipped_undersized, 1)
        self.assertEqual(records[0]["candidate_id"], "wide320")
        self.assertEqual(records[0]["skip_reason"], "source_smaller_than_requested_crop")

    def test_discover_yolo_splits_finds_valid_dataset(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "sample.yolo"
            (root / "valid" / "images").mkdir(parents=True)
            (root / "valid" / "labels").mkdir(parents=True)
            (root / "valid" / "images" / "a.jpg").write_bytes(b"not really an image")
            (root / "valid" / "labels" / "a.txt").write_text("0 0.5 0.5 0.2 0.2\n", encoding="utf-8")
            (root / "data.yaml").write_text("nc: 1\nnames: ['enemy']\n", encoding="utf-8")

            datasets = bench.discover_yolo_splits([Path(tmp)], ["valid"])

        self.assertEqual(len(datasets), 1)
        self.assertEqual(datasets[0].name, "sample.yolo")
        self.assertEqual(datasets[0].class_names, ("enemy",))

    def test_dataset_summary_metrics_include_timing_percentiles(self):
        summary = bench.DatasetSummary(dataset="demo", root="root", split="valid")
        summary.add(
            label_count=2,
            detection_count=3,
            match=bench.MatchResult(tp=1, fp=2, fn=1),
            timings={"infer_ms": 1.0, "gpu_total_ms": 2.0, "wall_ms": 3.0},
        )
        summary.add(
            label_count=1,
            detection_count=1,
            match=bench.MatchResult(tp=1, fp=0, fn=0),
            timings={"infer_ms": 3.0, "gpu_total_ms": 4.0, "wall_ms": 5.0},
        )

        metrics = summary.metrics()

        self.assertEqual(metrics["images"], 2)
        self.assertAlmostEqual(metrics["precision"], 2 / 4)
        self.assertAlmostEqual(metrics["recall"], 2 / 3)
        self.assertAlmostEqual(metrics["infer_ms"]["p50"], 2.0)
        self.assertEqual(metrics["deadline_misses"]["8.333"]["count"], 0)

    def test_parse_nvidia_smi_sample(self):
        sample = bench.parse_nvidia_smi_sample("0, 71, 12, 2048, 8192, 88.5, 63", timestamp=123.0)

        self.assertIsNotNone(sample)
        assert sample is not None
        self.assertEqual(sample.index, 0)
        self.assertEqual(sample.utilization_gpu_percent, 71.0)
        self.assertEqual(sample.memory_used_mb, 2048.0)
        self.assertEqual(sample.power_draw_w, 88.5)

    def test_gpu_resource_summary_and_efficiency(self):
        samples = [
            bench.GpuSample(timestamp=0.0, index=0, utilization_gpu_percent=20.0, memory_used_mb=1000.0, power_draw_w=50.0),
            bench.GpuSample(timestamp=1.0, index=0, utilization_gpu_percent=60.0, memory_used_mb=2000.0, power_draw_w=70.0),
        ]

        resource = bench.summarize_gpu_samples(samples, duration_seconds=2.0)
        efficiency = bench.build_efficiency_metrics(
            {
                "images": 100,
                "tp": 80,
                "fp": 10,
                "f1": 0.8,
                "gpu_total_ms": {"p50": 2.0, "p95": 4.0},
            },
            resource,
        )

        self.assertEqual(resource["sample_count"], 2)
        self.assertAlmostEqual(resource["utilization_gpu_percent"]["avg"], 40.0)
        self.assertAlmostEqual(resource["energy_joules"], 120.0)
        self.assertAlmostEqual(efficiency["images_per_second"], 50.0)
        self.assertAlmostEqual(efficiency["true_positive_per_kj"], 80 / 0.12)
        self.assertAlmostEqual(efficiency["f1_per_gpu_total_ms_p50"], 0.4)


if __name__ == "__main__":
    unittest.main()
