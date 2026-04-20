import ctypes
import threading
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
    D3D11_USAGE_DEFAULT,
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

    def map(self):
        rect = DXGI_MAPPED_RECT()
        self._surface().Map(ctypes.byref(rect), 1)
        return rect

    def unmap(self):
        surface = self.surface
        if surface is None:
            return
        surface.Unmap()

    def _surface(self):
        if self.surface is None:
            self.surface = self.texture.QueryInterface(IDXGISurface)
        return self.surface

    def release(self):
        if self.surface is not None:
            self.surface.Release()
            self.surface = None
        if self.texture is not None:
            self.texture.Release()
            self.texture = None


@dataclass(slots=True)
class RegionGpuSurface:
    width: int
    height: int
    device: object
    texture: object | None = None

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
        desc.Usage = D3D11_USAGE_DEFAULT
        desc.CPUAccessFlags = 0
        desc.MiscFlags = 0
        desc.BindFlags = 0
        texture = ctypes.POINTER(ID3D11Texture2D)()
        self.device.device.CreateTexture2D(
            ctypes.byref(desc),
            None,
            ctypes.byref(texture),
        )
        self.texture = texture

    def release(self):
        if self.texture is not None:
            self.texture.Release()
            self.texture = None


@dataclass(slots=True, frozen=True)
class DXGINativeFrame:
    texture: object
    texture_ptr: int | None
    width: int
    height: int
    slot_index: int
    pixel_format: str = "BGRA8"


@dataclass(slots=True, frozen=True)
class CaptureFrameData:
    frame: np.ndarray | None
    frame_shape: tuple[int, ...] | None = None
    native_frame: object | None = None
    frame_loader: object | None = None
    roi_loader: object | None = None
    frame_release: object | None = None


@dataclass(slots=True)
class NativeFrameSlot:
    slot_index: int
    surface: object
    in_use: bool = False

    @property
    def texture(self):
        return self.surface.texture


class NativeFrameSlotPool:
    def __init__(self, *, width: int, height: int, device, slots: int = 4):
        self.width = int(width)
        self.height = int(height)
        self.device = device
        self._lock = threading.Lock()
        self._slots = [
            NativeFrameSlot(slot_index=index, surface=RegionGpuSurface(width=self.width, height=self.height, device=device))
            for index in range(max(1, int(slots)))
        ]
        self._slot_index = -1

    def acquire_slot(self) -> NativeFrameSlot | None:
        with self._lock:
            for _ in range(len(self._slots)):
                self._slot_index = (self._slot_index + 1) % len(self._slots)
                slot = self._slots[self._slot_index]
                if not slot.in_use:
                    slot.in_use = True
                    return slot
        return None

    def create_native_frame(self, slot: NativeFrameSlot):
        return DXGINativeFrame(
            texture=slot.texture,
            texture_ptr=_texture_pointer(slot.texture),
            width=self.width,
            height=self.height,
            slot_index=slot.slot_index,
        )

    def release_native_frame(self, native_frame):
        slot_index = getattr(native_frame, "slot_index", None)
        if slot_index is None and isinstance(native_frame, dict):
            slot_index = native_frame.get("slot_index")
        if slot_index is None:
            return
        with self._lock:
            slot = self._slots[int(slot_index)]
            slot.in_use = False

    def release(self):
        for slot in self._slots:
            release = getattr(slot.surface, "release", None)
            if release is not None:
                release()


def _texture_pointer(texture) -> int | None:
    try:
        return int(ctypes.cast(texture, ctypes.c_void_p).value)
    except (TypeError, ValueError):
        return None


def _native_frame_texture(native_frame):
    texture = getattr(native_frame, "texture", None)
    if texture is None and isinstance(native_frame, dict):
        texture = native_frame.get("texture")
    return texture


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
        emit_native_frames: bool = False,
        native_slot_pool=None,
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
        self.emit_native_frames = bool(emit_native_frames)
        self.duplicator = duplicator or Duplicator(output=self.output, device=self.device)
        self.stage_surface = stage_surface or RegionStageSurface(
            width=self.width,
            height=self.height,
            device=self.device,
        )
        self.processor = processor or BGRARegionProcessor(output_color=output_color)
        self._copy_lock = threading.Lock()
        self.native_slot_pool = (
            native_slot_pool
            if self.emit_native_frames
            else None
        )
        if self.emit_native_frames and self.native_slot_pool is None:
            self.native_slot_pool = NativeFrameSlotPool(
                width=self.width,
                height=self.height,
                device=self.device,
            )

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

    def _full_surface_box(self):
        return D3D11_BOX(
            left=0,
            top=0,
            front=0,
            right=self.width,
            bottom=self.height,
            back=1,
        )

    def _handle_output_change(self):
        time.sleep(0.1)
        self.close()
        self.output.update_desc()
        if getattr(self.output, "rotation_angle", 0) not in (0,):
            raise NotImplementedError("Rotated outputs use the dxcam fallback backend.")
        self.stage_surface = RegionStageSurface(
            width=self.width,
            height=self.height,
            device=self.device,
        )
        self.duplicator = Duplicator(output=self.output, device=self.device)
        if self.emit_native_frames:
            self.native_slot_pool = NativeFrameSlotPool(
                width=self.width,
                height=self.height,
                device=self.device,
            )

    def _copy_to_stage_surface(self, source_texture, source_box):
        self.device.im_context.CopySubresourceRegion(
            self.stage_surface.texture,
            0,
            0,
            0,
            0,
            source_texture,
            0,
            ctypes.pointer(source_box),
        )

    def _read_stage_surface(self):
        rect = self.stage_surface.map()
        try:
            return self.processor.process(rect, self.width, self.height)
        finally:
            self.stage_surface.unmap()

    def _materialize_native_frame(self, native_frame):
        full_surface_box = self._full_surface_box()
        texture = _native_frame_texture(native_frame)
        if texture is None:
            raise RuntimeError("Native frame is missing texture handle.")
        with self._copy_lock:
            self._copy_to_stage_surface(texture, full_surface_box)
            return self._read_stage_surface()

    def _materialize_native_roi(self, native_frame, left: int, top: int, right: int, bottom: int):
        texture = _native_frame_texture(native_frame)
        if texture is None:
            raise RuntimeError("Native frame is missing texture handle.")
        roi_left = max(0, min(self.width, int(left)))
        roi_top = max(0, min(self.height, int(top)))
        roi_right = max(roi_left, min(self.width, int(right)))
        roi_bottom = max(roi_top, min(self.height, int(bottom)))
        roi_width = roi_right - roi_left
        roi_height = roi_bottom - roi_top
        if roi_width <= 0 or roi_height <= 0:
            return np.empty((0, 0, 3), dtype=np.uint8)
        roi_box = D3D11_BOX(
            left=roi_left,
            top=roi_top,
            front=0,
            right=roi_right,
            bottom=roi_bottom,
            back=1,
        )
        with self._copy_lock:
            self._copy_to_stage_surface(texture, roi_box)
            rect = self.stage_surface.map()
            try:
                return self.processor.process(rect, roi_width, roi_height)
            finally:
                self.stage_surface.unmap()

    def _release_native_frame(self, native_frame):
        if self.native_slot_pool is None:
            return
        self.native_slot_pool.release_native_frame(native_frame)

    def grab(self):
        with self._copy_lock:
            if not self.duplicator.update_frame():
                self._handle_output_change()
                return None
            if not self.duplicator.updated:
                return None

            source_box = self._source_box()
            if self.emit_native_frames and self.native_slot_pool is not None:
                native_slot = self.native_slot_pool.acquire_slot()
                if native_slot is not None:
                    self.device.im_context.CopySubresourceRegion(
                        native_slot.texture,
                        0,
                        0,
                        0,
                        0,
                        self.duplicator.texture,
                        0,
                        ctypes.pointer(source_box),
                    )
                    native_frame = self.native_slot_pool.create_native_frame(native_slot)
                    self.duplicator.release_frame()
                    return CaptureFrameData(
                        frame=None,
                        frame_shape=(self.height, self.width, 3),
                        native_frame=native_frame,
                        frame_loader=lambda nf=native_frame: self._materialize_native_frame(nf),
                        roi_loader=lambda left, top, right, bottom, nf=native_frame: self._materialize_native_roi(
                            nf,
                            left,
                            top,
                            right,
                            bottom,
                        ),
                        frame_release=lambda nf=native_frame: self._release_native_frame(nf),
                    )

            self._copy_to_stage_surface(self.duplicator.texture, source_box)
            self.duplicator.release_frame()
            return self._read_stage_surface()

    def close(self):
        if self.duplicator is not None:
            self.duplicator.release()
            self.duplicator = None
        if self.stage_surface is not None:
            release = getattr(self.stage_surface, "release", None)
            if release is not None:
                release()
            self.stage_surface = None
        if self.native_slot_pool is not None:
            release = getattr(self.native_slot_pool, "release", None)
            if release is not None:
                release()
            self.native_slot_pool = None


def create_capture_backend(
    *,
    region: tuple[int, int, int, int],
    output_color: str = "RGB",
    emit_native_frames: bool = False,
):
    try:
        return DXGIRegionCaptureBackend(
            region=region,
            output_color=output_color,
            emit_native_frames=emit_native_frames,
        )
    except NotImplementedError:
        return DXCamFallbackBackend(region=region, output_color=output_color)
