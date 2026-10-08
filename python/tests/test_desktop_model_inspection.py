"""Model inspection reuse must never conceal changed files or failed reads."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from desktop_app.runtime import RuntimeManager


class ModelInspectionTests(unittest.TestCase):
    def test_reuses_shape_but_rechecks_model_and_runtime_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); model=root/'model.engine'; model.write_bytes(b'one')
            manager=RuntimeManager(root); manager.executable=root/'runtime.exe'; manager.executable.write_bytes(b'exe')
            shape={'input_width':480,'input_height':384}
            with patch.object(manager,'_read_model_shape',return_value=shape) as read:
                first=manager.inspect_model('model.engine');first['input_width']=99
                self.assertEqual(manager.inspect_model('model.engine')['input_width'],480)
                self.assertEqual(read.call_count,1)
                model.write_bytes(b'changed model')
                manager.inspect_model('model.engine');self.assertEqual(read.call_count,2)
                manager.executable.write_bytes(b'new executable')
                manager.inspect_model('model.engine');self.assertEqual(read.call_count,3)
                model.unlink()
                with self.assertRaisesRegex(ValueError,'不存在'):manager.inspect_model('model.engine')

    def test_failure_and_mid_read_replacement_are_not_cached(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);model=root/'model.engine';model.write_bytes(b'one')
            manager=RuntimeManager(root);manager.executable=root/'runtime.exe';manager.executable.write_bytes(b'exe')
            with patch.object(manager,'_read_model_shape',side_effect=ValueError('bad')) as read:
                for _ in range(2):
                    with self.assertRaisesRegex(ValueError,'bad'):manager.inspect_model(model)
                self.assertEqual(read.call_count,2)
            def replace(_):
                model.write_bytes(b'changed during read')
                return {'input_width':480,'input_height':384}
            with patch.object(manager,'_read_model_shape',side_effect=replace):
                with self.assertRaisesRegex(ValueError,'发生变化'):manager.inspect_model(model)
            self.assertIsNone(manager._model_inspection)
