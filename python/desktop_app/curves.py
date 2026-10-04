"""Versioned, normalized, strictly monotonic response curves."""
from __future__ import annotations
import json
import math
from pathlib import Path
import uuid
from .settings import UiPreferences

KIND = 'normalized_stick_response'
MAX_POINTS = 32


def validate_points(points):
    if not isinstance(points, list) or not 2 <= len(points) <= MAX_POINTS:
        raise ValueError('曲线需要 2～32 个控制点。')
    result = []
    for point in points:
        if not isinstance(point, (list, tuple)) or len(point) != 2 or any(type(v) not in (float, int) for v in point):
            raise ValueError('控制点必须是 [输入, 响应] 数值对。')
        x, y = map(float, point)
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (x, y)):
            raise ValueError('控制点必须在 0～1 之间。')
        if result and (x - result[-1][0] < 1.1e-5 - 1e-12 or y - result[-1][1] < 1.1e-5 - 1e-12):
            raise ValueError('输入与响应必须严格递增，才能进行反向映射。')
        result.append([x, y])
    if result[0] != [0., 0.] or result[-1] != [1., 1.]:
        raise ValueError('曲线端点必须是 [0, 0] 和 [1, 1]。')
    return result


def encode_points(points):
    return ';'.join(f'{x:.9g}:{y:.9g}' for x, y in validate_points(points))


def decode_points(text):
    try:
        points = [[float(v) for v in point.split(':')] for point in text.split(';')]
    except (ValueError, AttributeError):
        raise ValueError('自定义曲线格式不正确。') from None
    return validate_points(points)


def seed_points(algorithm, project):
    if algorithm == 'linear':
        return [[i / 10, i / 10] for i in range(11)]
    if algorithm != 'cod_dynamic_legacy_lut':
        raise ValueError('未知的曲线模板。')
    # Read the versioned native seed rather than inventing a similar shape.
    import re
    text = (Path(project) / 'native/controller_native/aim_response_curve_plugin.h').read_text(encoding='utf-8')
    arrays = []
    for name in ('kCodDynamicLegacyStick', 'kCodDynamicLegacyResponse'):
        match = re.search(name + r'\s*\{([^}]+)\}', text)
        if not match:
            raise ValueError('原生动态曲线模板不存在。')
        arrays.append([float(value.strip().removesuffix('f')) for value in match[1].split(',') if value.strip()])
    return validate_points([list(pair) for pair in zip(*arrays)])


def curve_document(name, points):
    if not isinstance(name, str) or not name.strip() or len(name.strip()) > 80:
        raise ValueError('曲线名称应为 1～80 个字符。')
    return {'schema_version': 1, 'kind': KIND, 'name': name.strip(),
            'interpolation': 'piecewise_linear', 'points': validate_points(points)}


def read_curve(path):
    data = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or type(data.get('schema_version')) is not int or data.get('schema_version') != 1 or data.get('kind') != KIND or data.get('interpolation') != 'piecewise_linear':
        raise ValueError('不支持的曲线格式或版本。')
    return curve_document(data.get('name'), data.get('points'))


class CurveLibrary:
    def __init__(self, root):
        self.folder = Path(root) / 'profiles/curves'

    def entries(self):
        return [(path, read_curve(path)) for path in sorted(self.folder.glob('*.json'))]

    def create(self, name, points):
        data = curve_document(name, points)
        self.folder.mkdir(parents=True, exist_ok=True)
        path = self.folder / (uuid.uuid4().hex + '.json')
        writer = UiPreferences(self.folder)
        writer.path = path
        writer.write(data)
        return path
