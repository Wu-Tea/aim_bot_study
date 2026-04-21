import ctypes
import time
from dataclasses import dataclass

import cv2
import numpy as np

from dxcam.core import Device, Duplicator, Output
from dxcam.util.io import enum_dxgi_adapters, get_output_metadata
from dxcam._libs.d3d11 import (
    D3D11_BOX,
    D3D11_CPU_ACCESS_READ,
    D3D11_TEXTURE2D_DESC,
    D3D11_USAGE_STAGING,
    DXGI_FORMAT_B8G8R8A8_UNORM,
    ID3D11Texture2D,
)
from dxcam._libs.dxgi import DXGI_MAPPED_RECT, IDXGISurface


def _select_device_output(device_idx: int = 0, output_idx: int | None = None):
    devices = []
    outputs = []
    for adapter in enum_dxgi_adapters():
        device = Device(adapter)
        device_outputs = [Output(output) for output in device.enum_outputs()]
        if device_outputs:
            devices.append(device)
            outputs.append(device_outputs)

    device = devices[device_idx]
    available_outputs = outputs[device_idx]
    metadata = get_output_metadata()
    if output_idx is None:
        primary_candidates = [
            index
            for index, output in enumerate(available_outputs)
            if metadata.get(output.devicename, [None, False])[1]
        ]
        output_idx = primary_candidates[0] if primary_candidates else 0

    output = available_outputs[output_idx]
    output.update_desc()
    return device, output


@dataclass(slots=True)
class RegionStageSurface:
    width: int
    height: int
    device: object
    texture: object | None = None
    surface: object | None = None

    def __post_init__(self):
        self._create_texture()

    def _create_texture(self):
        desc = D3D11_TEXTURE2D_DESC()
        desc.Width = int(self.width)
        desc.Height = int(self.height)
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM
        desc.MipLevels = 1
        desc.ArraySize = 1
        desc.SampleDesc.Count = 1
        desc.SampleDesc.Quality = 0
        desc.Usage = D3D11_USAGE_STAGING
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ
        desc.MiscFlags = 0
        desc.BindFlags = 0
        texture = ctypes.POINTER(ID3D11Texture2D)()
        self.device.device.CreateTexture2D(
            ctypes.byref(desc),
            None,
            ctypes.byref(texture),
        )
        self.texture = texture

    def _surface(self):
        if self.surface is None:
            self.surface = self.texture.QueryInterface(IDXGISurface)
        return self.surface

    def map(self):
        rect = DXGI_MAPPED_RECT()
        self._surface().Map(ctypes.byref(rect), 1)
        return rect

    def unmap(self):
        if self.surface is not None:
            self.surface.Unmap()

    def release(self):
        if self.surface is not None:
            self.surface.Release()
            self.surface = None
        if self.texture is not None:
            self.texture.Release()
            self.texture = None


class BGRARegionProcessor:
    _CV_COLOR_MAP = {
        "RGB": cv2.COLOR_BGRA2RGB,
        "RGBA": cv2.COLOR_BGRA2RGBA,
        "BGR": cv2.COLOR_BGRA2BGR,
        "GRAY": cv2.COLOR_BGRA2GRAY,
        "BGRA": None,
    }

    def __init__(self, output_color: str = "RGB", buffer_slots: int = 4):
        if output_color not in self._CV_COLOR_MAP:
            raise ValueError(f"Unsupported output color: {output_color}")
        if int(buffer_slots) <= 0:
            raise ValueError("buffer_slots must be positive")
        self.output_color = output_color
        self._cv_code = self._CV_COLOR_MAP[output_color]
        self._buffer_slots = int(buffer_slots)
        self._output_buffers: list[np.ndarray | None] = [None] * self._buffer_slots
        self._gray_buffers: list[np.ndarray | None] = [None] * self._buffer_slots
        self._slot_index = -1

    @staticmethod
    def _pointer_address(ptr) -> int:
        if isinstance(ptr, int):
            return ptr
        value = getattr(ptr, "value", None)
        if value is not None:
            return int(value)
        if hasattr(ptr, "contents"):
            return ctypes.addressof(ptr.contents)
        return int(ctypes.cast(ptr, ctypes.c_void_p).value)

    def _mapped_image(self, rect, width: int, height: int):
        pitch = int(rect.Pitch)
        address = self._pointer_address(rect.pBits)
        raw = np.ctypeslib.as_array(
            (ctypes.c_uint8 * (pitch * height)).from_address(address)
        )
        image = raw.reshape(height, pitch // 4, 4)
        if (pitch // 4) != width:
            image = image[:, :width, :]
        return image

    def _next_slot(self) -> int:
        self._slot_index = (self._slot_index + 1) % self._buffer_slots
        return self._slot_index

    def _ensure_output_buffer(self, slot: int, shape: tuple[int, ...]):
        output = self._output_buffers[slot]
        if output is None or output.shape != shape:
            output = np.empty(shape, dtype=np.uint8)
            self._output_buffers[slot] = output
        return output

    def _ensure_gray_buffer(self, slot: int, shape: tuple[int, ...]):
        gray = self._gray_buffers[slot]
        if gray is None or gray.shape != shape:
            gray = np.empty(shape, dtype=np.uint8)
            self._gray_buffers[slot] = gray
        return gray

    def process(self, rect, width: int, height: int):
        slot = self._next_slot()
        image = self._mapped_image(rect, width, height)

        if self._cv_code is None:
            output = self._ensure_output_buffer(slot, (height, width, 4))
            np.copyto(output, image)
            return output
        if self._cv_code == cv2.COLOR_BGRA2GRAY:
            gray = self._ensure_gray_buffer(slot, (height, width))
            cv2.cvtColor(image, self._cv_code, dst=gray)
            output = self._ensure_output_buffer(slot, (height, width, 1))
            output[..., 0] = gray
            return output

        channels = 4 if self.output_color == "RGBA" else 3
        output = self._ensure_output_buffer(slot, (height, width, channels))
        cv2.cvtColor(image, self._cv_code, dst=output)
        return output


class DXCamFallbackBackend:
    def __init__(self, *, region: tuple[int, int, int, int], output_color: str = "RGB"):
        import dxcam

        self.region = region
        self.camera = dxcam.create(region=region, output_color=output_color)

    def grab(self):
        return self.camera.grab(region=self.region)

    def close(self):
        self.camera.release()


class DXGIRegionCaptureBackend:
    def __init__(
        self,
        *,
        region: tuple[int, int, int, int],
        output_color: str = "RGB",
        device=None,
        output=None,
        duplicator=None,
        stage_surface=None,
        processor=None,
        duplicator_factory=None,
        recovery_delay_seconds: float = 0.25,
    ):
        self.region = tuple(int(value) for value in region)
        self.output_color = output_color
        self.device = device
        self.output = output
        if self.device is None or self.output is None:
            self.device, self.output = _select_device_output()
        if getattr(self.output, "rotation_angle", 0) not in (0,):
            raise NotImplementedError("Rotated outputs use the dxcam fallback backend.")

        self.width = self.region[2] - self.region[0]
        self.height = self.region[3] - self.region[1]
        self._validate_region()
        self._duplicator_factory = duplicator_factory or (
            lambda *, output, device: Duplicator(output=output, device=device)
        )
        self._recovery_delay_seconds = max(0.0, float(recovery_delay_seconds))
        self._next_recovery_at = 0.0
        self.duplicator = duplicator or self._create_duplicator()
        self.stage_surface = stage_surface or RegionStageSurface(
            width=self.width,
            height=self.height,
            device=self.device,
        )
        self.processor = processor or BGRARegionProcessor(output_color=output_color)

    def _validate_region(self):
        left = self.output.desc.DesktopCoordinates.left
        top = self.output.desc.DesktopCoordinates.top
        width, height = self.output.resolution
        source_left = self.region[0] - left
        source_top = self.region[1] - top
        source_right = self.region[2] - left
        source_bottom = self.region[3] - top
        if not (0 <= source_left < source_right <= width and 0 <= source_top < source_bottom <= height):
            raise ValueError(
                f"Invalid region {self.region} for output size {width}x{height} at offset {(left, top)}"
            )

    def _source_box(self):
        desktop = self.output.desc.DesktopCoordinates
        return D3D11_BOX(
            left=self.region[0] - desktop.left,
            top=self.region[1] - desktop.top,
            front=0,
            right=self.region[2] - desktop.left,
            bottom=self.region[3] - desktop.top,
            back=1,
        )

    def _create_duplicator(self):
        return self._duplicator_factory(output=self.output, device=self.device)

    def _schedule_recovery_retry(self):
        self._next_recovery_at = time.perf_counter() + self._recovery_delay_seconds

    def _reset_recovery_retry(self):
        self._next_recovery_at = 0.0

    def _handle_output_change(self):
        time.sleep(0.1)
        self.close()
        self.output.update_desc()
        if getattr(self.output, "rotation_angle", 0) not in (0,):
            raise NotImplementedError("Rotated outputs use the dxcam fallback backend.")
        try:
            self.stage_surface = RegionStageSurface(
                width=self.width,
                height=self.height,
                device=self.device,
            )
            self.duplicator = self._create_duplicator()
        except Exception:
            self.stage_surface = None
            self.duplicator = None
            self._schedule_recovery_retry()
            return False
        self._reset_recovery_retry()
        return True

    def _ensure_duplicator(self):
        if self.duplicator is not None:
            return True
        if time.perf_counter() < self._next_recovery_at:
            return False
        return self._handle_output_change()

    def grab(self):
        if not self._ensure_duplicator():
            return None

        try:
            frame_updated = self.duplicator.update_frame()
        except Exception:
            self.close()
            self._schedule_recovery_retry()
            return None

        if not frame_updated:
            self._handle_output_change()
            return None
        if not self.duplicator.updated:
            return None

        source_box = self._source_box()
        self.device.im_context.CopySubresourceRegion(
            self.stage_surface.texture,
            0,
            0,
            0,
            0,
            self.duplicator.texture,
            0,
            ctypes.pointer(source_box),
        )
        self.duplicator.release_frame()
        rect = self.stage_surface.map()
        try:
            return self.processor.process(rect, self.width, self.height)
        finally:
            self.stage_surface.unmap()

    def close(self):
        if self.duplicator is not None:
            self.duplicator.release()
            self.duplicator = None
        if self.stage_surface is not None:
            release = getattr(self.stage_surface, "release", None)
            if release is not None:
                release()
            self.stage_surface = None


def create_capture_backend(*, region: tuple[int, int, int, int], output_color: str = "RGB"):
    try:
        return DXGIRegionCaptureBackend(region=region, output_color=output_color)
    except NotImplementedError:
        return DXCamFallbackBackend(region=region, output_color=output_color)
