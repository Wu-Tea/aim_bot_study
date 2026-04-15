import tempfile
import unittest
from pathlib import Path
import subprocess
import sys

from training.person_dataset import (
    convert_crowdhuman_record_to_yolo_lines,
    discover_crowdhuman_layout,
    write_dataset_yaml,
)


class CrowdHumanConversionTests(unittest.TestCase):
    def test_visible_box_is_converted_to_one_class_yolo_label(self):
        record = {
            "ID": "sample_001",
            "gtboxes": [
                {
                    "tag": "person",
                    "vbox": [10, 20, 40, 60],
                    "fbox": [8, 18, 44, 68],
                    "extra": {},
                }
            ],
        }

        labels = convert_crowdhuman_record_to_yolo_lines(
            record,
            image_width=100,
            image_height=100,
            box_kind="visible",
        )

        self.assertEqual(labels, ["0 0.300000 0.500000 0.400000 0.600000"])

    def test_ignored_and_non_person_boxes_are_filtered_out(self):
        record = {
            "ID": "sample_002",
            "gtboxes": [
                {
                    "tag": "person",
                    "vbox": [10, 20, 40, 60],
                    "extra": {"ignore": 1},
                },
                {
                    "tag": "mask",
                    "vbox": [12, 24, 30, 30],
                    "extra": {},
                },
                {
                    "tag": "person",
                    "vbox": [20, 10, 20, 30],
                    "extra": {},
                },
            ],
        }

        labels = convert_crowdhuman_record_to_yolo_lines(
            record,
            image_width=100,
            image_height=100,
            box_kind="visible",
        )

        self.assertEqual(labels, ["0 0.300000 0.250000 0.200000 0.300000"])

    def test_boxes_are_clamped_to_image_bounds_before_normalization(self):
        record = {
            "ID": "sample_003",
            "gtboxes": [
                {
                    "tag": "person",
                    "vbox": [-10, 10, 30, 40],
                    "extra": {},
                }
            ],
        }

        labels = convert_crowdhuman_record_to_yolo_lines(
            record,
            image_width=100,
            image_height=100,
            box_kind="visible",
        )

        self.assertEqual(labels, ["0 0.100000 0.300000 0.200000 0.400000"])


class CrowdHumanLayoutDiscoveryTests(unittest.TestCase):
    def test_discover_prefers_standard_root_layout(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            images_dir = root / "Images"
            images_dir.mkdir()
            (root / "annotation_train.odgt").write_text("", encoding="utf-8")
            (root / "annotation_val.odgt").write_text("", encoding="utf-8")

            layout = discover_crowdhuman_layout(root)

        self.assertEqual(layout.train_images_dir, images_dir)
        self.assertEqual(layout.val_images_dir, images_dir)
        self.assertEqual(layout.train_annotations, root / "annotation_train.odgt")
        self.assertEqual(layout.val_annotations, root / "annotation_val.odgt")


class DatasetYamlTests(unittest.TestCase):
    def test_write_dataset_yaml_uses_single_person_class(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            dataset_root = Path(tmpdir) / "person_detect_visible"
            dataset_root.mkdir(parents=True)

            yaml_path = write_dataset_yaml(dataset_root)
            content = yaml_path.read_text(encoding="utf-8")

        self.assertIn("path:", content)
        self.assertIn("train: images/train", content)
        self.assertIn("val: images/val", content)
        self.assertIn("names:", content)
        self.assertIn("0: person", content)


class TrainingScriptImportTests(unittest.TestCase):
    def test_prepare_person_dataset_script_imports_without_package_errors(self):
        repo_root = Path(__file__).resolve().parent.parent

        result = subprocess.run(
            [sys.executable, str(repo_root / "tools" / "prepare_person_dataset.py"), "--help"],
            capture_output=True,
            text=True,
            cwd=repo_root,
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Prepare a one-class person dataset", result.stdout)

if __name__ == "__main__":
    unittest.main()
