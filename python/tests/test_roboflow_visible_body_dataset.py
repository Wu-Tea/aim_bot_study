import tempfile
import unittest
from pathlib import Path

from training.roboflow_visible_body import (
    RoboflowSource,
    load_roboflow_class_names,
    prepare_roboflow_visible_body_dataset,
    roboflow_base_key,
)


def _write_dataset(root: Path, names: list[str], samples: dict[str, list[tuple[str, str]]]):
    root.mkdir(parents=True, exist_ok=True)
    quoted = ", ".join(repr(name) for name in names)
    (root / "data.yaml").write_text(
        "\n".join(
            [
                "train: ../train/images",
                "val: ../valid/images",
                "test: ../test/images",
                "",
                f"nc: {len(names)}",
                f"names: [{quoted}]",
                "",
            ]
        ),
        encoding="utf-8",
    )
    for split, rows in samples.items():
        images_dir = root / split / "images"
        labels_dir = root / split / "labels"
        images_dir.mkdir(parents=True, exist_ok=True)
        labels_dir.mkdir(parents=True, exist_ok=True)
        for image_name, label_text in rows:
            (images_dir / image_name).write_bytes(b"fake image bytes")
            (labels_dir / Path(image_name).with_suffix(".txt").name).write_text(
                label_text,
                encoding="utf-8",
            )


class RoboflowVisibleBodyDatasetTests(unittest.TestCase):
    def test_load_roboflow_class_names_reads_inline_yaml_list(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            (root / "data.yaml").write_text("names: ['Body', 'Head', 'enemy']\n", encoding="utf-8")

            names = load_roboflow_class_names(root)

        self.assertEqual(names, {0: "Body", 1: "Head", 2: "enemy"})

    def test_roboflow_base_key_ignores_augmented_hash_suffix(self):
        key = roboflow_base_key(Path("12_10003_png.rf.c75c55f9b3917182c5d8921554a026d7.jpg"))

        self.assertEqual(key, "12_10003_png")

    def test_prepare_filters_head_labels_and_remaps_kept_boxes_to_single_class(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            source = tmp / "source"
            _write_dataset(
                source,
                ["Body", "Head", "enemy"],
                {
                    "train": [
                        (
                            "frame_001.rf.aaa.jpg",
                            "\n".join(
                                [
                                    "0 0.500000 0.500000 0.200000 0.400000",
                                    "1 0.500000 0.300000 0.060000 0.080000",
                                    "2 0.250000 0.500000 0.120000 0.300000",
                                    "",
                                ]
                            ),
                        ),
                    ],
                    "valid": [],
                },
            )
            output = tmp / "assembled"

            summary = prepare_roboflow_visible_body_dataset(
                sources=[RoboflowSource(source, "sample")],
                output_root=output,
                overwrite=True,
            )

            label_files = list((output / "labels" / "train").glob("*.txt"))
            self.assertEqual(len(label_files), 1)
            labels = label_files[0].read_text(encoding="utf-8").splitlines()
            self.assertEqual(
                labels,
                [
                    "0 0.500000 0.500000 0.200000 0.400000",
                    "0 0.250000 0.500000 0.120000 0.300000",
                ],
            )
            self.assertEqual(summary.images_written, 1)
            self.assertEqual(summary.boxes_written, 2)
            self.assertEqual(summary.boxes_dropped_by_class, 1)

    def test_prepare_dedupes_same_roboflow_base_across_sources(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            first = tmp / "first"
            second = tmp / "second"
            _write_dataset(
                first,
                ["body"],
                {
                    "train": [("shared_frame.rf.first.jpg", "0 0.5 0.5 0.2 0.4\n")],
                    "valid": [],
                },
            )
            _write_dataset(
                second,
                ["body"],
                {
                    "train": [("shared_frame.rf.second.jpg", "0 0.4 0.5 0.2 0.4\n")],
                    "valid": [],
                },
            )
            output = tmp / "assembled"

            summary = prepare_roboflow_visible_body_dataset(
                sources=[RoboflowSource(first, "first"), RoboflowSource(second, "second")],
                output_root=output,
                overwrite=True,
            )

            self.assertEqual(summary.images_written, 1)
            self.assertEqual(summary.images_dropped_as_duplicates, 1)
            self.assertEqual(len(list((output / "images" / "train").glob("*.jpg"))), 1)

    def test_prepare_can_keep_explicit_class_id_when_class_name_is_unhelpful(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            source = tmp / "warzone"
            _write_dataset(
                source,
                ["-", "head"],
                {
                    "train": [("frame_001.rf.aaa.jpg", "0 0.5 0.5 0.2 0.4\n1 0.5 0.3 0.05 0.07\n")],
                    "valid": [],
                },
            )
            output = tmp / "assembled"

            summary = prepare_roboflow_visible_body_dataset(
                sources=[RoboflowSource(source, "warzone")],
                output_root=output,
                keep_class_ids_by_source={"warzone": {0}},
                overwrite=True,
            )

            label_files = list((output / "labels" / "train").glob("*.txt"))
            self.assertEqual(summary.boxes_written, 1)
            self.assertEqual(label_files[0].read_text(encoding="utf-8"), "0 0.500000 0.500000 0.200000 0.400000\n")


if __name__ == "__main__":
    unittest.main()
