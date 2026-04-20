import importlib

import torch


def load_native_fastpath_module(module_name: str = "vision_native"):
    try:
        return importlib.import_module(module_name)
    except ImportError:
        return None


class NativeFastPathPreprocessor:
    name = "native"

    def __init__(self, module):
        self.module = module

    def prepare(self, fast_path, frame_source):
        native_frame = getattr(frame_source, "native_frame", None)
        if native_frame is None:
            raise RuntimeError("Native fast preprocessor requires frame_source.native_frame.")

        prepare_into_tensor = getattr(self.module, "prepare_into_tensor", None)
        if prepare_into_tensor is None:
            raise RuntimeError("Native fast preprocessor module is missing prepare_into_tensor().")

        stream_ptr = 0
        if fast_path.gpu_input.is_cuda:
            stream_ptr = int(torch.cuda.current_stream(device=fast_path.gpu_input.device).cuda_stream)

        prepare_into_tensor(
            native_frame=native_frame,
            dst_ptr=int(fast_path.gpu_input.data_ptr()),
            width=int(fast_path.gpu_input.shape[-1]),
            height=int(fast_path.gpu_input.shape[-2]),
            channels=int(fast_path.gpu_input.shape[-3]),
            dtype=str(fast_path.gpu_input.dtype).replace("torch.", ""),
            stream_ptr=stream_ptr,
        )
