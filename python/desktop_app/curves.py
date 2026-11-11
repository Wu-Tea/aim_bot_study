"""Versioned response curves with strictly monotonic active-segment LUTs.

Version 1 stores normalized shape only. Version 2 adds optional deadzone
(input start/end fractions) and compensation (output start/end fractions).
For x > input_start, evaluate output_start + output_span * shape(
(x-input_start)/input_span), clamping the shape domain. At/below input_start
return exact zero. This retains physical deadzones and compensation jumps
without approximating either with near-duplicate knots.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
import uuid
import struct
from .settings import UiPreferences

KIND = 'normalized_stick_response'
MAX_POINTS = 128


def curve_window(data, key):
    window = data.get(key, {})
    if not isinstance(window, dict) or set(window) - {'start', 'end'}:
        raise ValueError('曲线死区格式无效。')
    start, end = window.get('start', 0.), window.get('end', 0.)
    if any(type(v) not in (int, float) or not math.isfinite(v) or v < 0 or v >= 1 for v in (start, end)) or start + end >= 1:
        raise ValueError('起点与终点之和必须小于 100%，请保留有效区间。')
    f32 = lambda v: struct.unpack('f', struct.pack('f', v))[0]
    if f32(f32(start) + f32(end)) >= 1:
        raise ValueError('有效区间过小，无法用手柄精度表示。')
    return float(start), float(end)


def evaluate_curve(curve, value, inverse=False):
    start, end = curve_window(curve, 'deadzone')
    lower, upper = curve_window(curve, 'compensation')
    if inverse:
        if value <= 0: return 0.
        return start + (1-start-end) * interpolate(curve['points'], (value-lower)/(1-lower-upper), True)
    if value <= start: return 0.
    return lower + (1-lower-upper) * interpolate(curve['points'], (value-start)/(1-start-end))


def curve_runtime_mapping(curve):
    start, end = curve_window(curve, 'deadzone')
    lower, upper = curve_window(curve, 'compensation')
    return dict(input_start=start, input_end=end, output_start=lower, output_end=upper)


def validate_source(source):
    if not isinstance(source, dict): raise ValueError('曲线来源格式无效。')
    result = {}
    for key in ('kind', 'url', 'description', 'base_name', 'target_name'):
        if key in source:
            if not isinstance(source[key], str) or len(source[key]) > 2000:
                raise ValueError('曲线来源文本过长或无效。')
            result[key] = source[key]
    for key in ('base_points', 'target_points'):
        if key in source: result[key] = validate_points(source[key])
    for prefix in ('base_', 'target_'):
        for key in ('deadzone', 'compensation'):
            if prefix + key in source:
                start, end = curve_window({key: source[prefix + key]}, key)
                result[prefix + key] = dict(start=start, end=end)
    return result


def interpolate(points, value, inverse=False):
    a, b = (1, 0) if inverse else (0, 1)
    if value <= 0: return 0.
    if value >= 1: return 1.
    for lo, hi in zip(points, points[1:]):
        if value <= hi[a]:
            return lo[b] + (hi[b]-lo[b]) * (value-lo[a]) / (hi[a]-lo[a])
    return 1.


def compose_curves(base, target, name):
    a, b = validate_points(base['points']), validate_points(target['points'])
    ai, ae = curve_window(base, 'deadzone')
    ao, ax = curve_window(base, 'compensation')
    bi, be = curve_window(target, 'deadzone')
    bo, bx = curve_window(target, 'compensation')
    if bo < ao or bx < ax:
        raise ValueError('目标响应超出基准曲线可达到的范围，请选择完整响应曲线作为基准。')
    # Solve only the active segments; keep the exact flat intervals and the
    # right-hand jump as metadata rather than epsilon-spaced LUT points.
    def target_y(y): return (bo + (1-bo-bx)*y - ao)/(1-ao-ax)
    low = interpolate(a, target_y(0), True)
    high = interpolate(a, target_y(1), True)
    if high <= low:
        raise ValueError('目标响应范围过小，无法生成有效转译曲线。')
    # All breakpoints of A^-1(B(x)); do not fit away sharp turns or silently
    # squeeze a generated result back into a smaller user point budget.
    crossings = [(ao + (1-ao-ax)*p[1] - bo)/(1-bo-bx) for p in a]
    xs = sorted(set([p[0] for p in b] + [interpolate(b, y, True) for y in crossings if 0 < y < 1]))
    points = [[x, (interpolate(a, target_y(interpolate(b, x)), True)-low)/(high-low)] for x in xs]
    points[0], points[-1] = [0., 0.], [1., 1.]
    # Remove only numerically collinear knots; no approximation tolerance.
    reduced = []
    for p in points:
        while len(reduced) >= 2:
            l, m = reduced[-2:]
            if abs((m[1]-l[1])*(p[0]-m[0])-(p[1]-m[1])*(m[0]-l[0])) > 1e-14: break
            reduced.pop()
        reduced.append(p)
    result = curve_document(name, reduced, dict(deadzone=dict(start=bi, end=be),
        compensation=dict(start=ai+(1-ai-ae)*low, end=ae+(1-ai-ae)*(1-high))))
    result['source'] = dict(kind='calculated', base_name=base['name'], base_points=a,
                            target_name=target['name'], target_points=b)
    for prefix, curve in (('base_', base), ('target_', target)):
        for key in ('deadzone', 'compensation'):
            if key in curve: result['source'][prefix + key] = dict(zip(('start', 'end'), curve_window(curve, key)))
    return result


def validate_points(points):
    if not isinstance(points, list) or not 2 <= len(points) <= MAX_POINTS:
        raise ValueError('曲线需要 2～128 个控制点。')
    result = []
    for point in points:
        if not isinstance(point, (list, tuple)) or len(point) != 2 or any(type(v) not in (float, int) for v in point):
            raise ValueError('控制点必须是 [输入, 响应] 数值对。')
        x, y = map(float, point)
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (x, y)):
            raise ValueError('控制点必须在 0～1 之间。')
        f32 = lambda value: struct.unpack('f', struct.pack('f', value))[0]
        if result and (f32(x) <= f32(result[-1][0]) or f32(y) <= f32(result[-1][1])):
            raise ValueError('输入与响应必须严格递增，才能进行反向映射。')
        result.append([x, y])
    if result[0] != [0., 0.] or result[-1] != [1., 1.]:
        raise ValueError('曲线端点必须是 [0, 0] 和 [1, 1]。')
    return result


def encode_points(points):
    # Preserve the validated double values across text serialization. Rounding
    # to nine digits can shrink a valid near-neighbor gap below the contract.
    return ';'.join(f'{x:.17g}:{y:.17g}' for x, y in validate_points(points))


def decode_points(text):
    try:
        points = [[float(v) for v in point.split(':')] for point in text.split(';')]
    except (ValueError, AttributeError):
        raise ValueError('自定义曲线格式不正确。') from None
    return validate_points(points)


def seed_points(algorithm, project):
    names = {'linear':'linear', 'cod_dynamic_legacy_lut':'cod_dynamic_legacy', 'cod_linear_legacy_lut':'cod_linear_legacy'}
    if algorithm not in names:
        raise ValueError('未知的曲线模板。')
    return read_curve(Path(project) / 'resources/curves' / (names[algorithm] + '.json'))['points']


def curve_document(name, points, metadata=None):
    if not isinstance(name, str) or not name.strip() or len(name.strip()) > 80:
        raise ValueError('曲线名称应为 1～80 个字符。')
    result = {'schema_version': 1, 'kind': KIND, 'name': name.strip(),
            'interpolation': 'piecewise_linear', 'points': validate_points(points)}
    for key in ('deadzone', 'compensation'):
        start, end = curve_window(metadata or {}, key)
        if start or end:
            result[key] = dict(start=start, end=end)
            result['schema_version'] = 2
    return result


def read_curve(path):
    data = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or type(data.get('schema_version')) is not int or data.get('schema_version') not in (1, 2) or data.get('kind') != KIND or data.get('interpolation') != 'piecewise_linear':
        raise ValueError('不支持的曲线格式或版本。')
    result = curve_document(data.get('name'), data.get('points'), data)
    if 'source' in data:
        result['source'] = validate_source(data['source'])
    return result


class CurveLibrary:
    def __init__(self, root, project=None):
        self.builtins = Path(project or root) / 'resources/curves'
        self.folder = Path(root) / 'profiles/curves'
        self.errors = []

    def entries(self):
        entries, self.errors = [], []
        for path in sorted(self.folder.glob('*.json')):
            try:entries.append((path, read_curve(path)))
            except (OSError,ValueError,KeyError,TypeError) as error:self.errors.append(f'{path.name}：{error}')
        return entries

    def create(self, name, points, source=None, metadata=None):
        data = curve_document(name, points, metadata)
        if source is not None: data['source'] = validate_source(source)
        self.folder.mkdir(parents=True, exist_ok=True)
        path = self.folder / (uuid.uuid4().hex + '.json')
        writer = UiPreferences(self.folder)
        writer.path = path
        writer.write(data)
        return path

    def catalog(self):
        entries = [dict(id=path.name, readonly=False, **data) for path, data in self.entries()]
        for path in sorted(self.builtins.glob('*.json')):
            try: entries.append(dict(id='builtin:' + path.name, readonly=True, **read_curve(path)))
            except (OSError, ValueError, TypeError, KeyError) as error: self.errors.append(f'{path.name}：{error}')
        return {'entries':entries, 'errors':self.errors}

    def path(self, identifier):
        import re
        if not isinstance(identifier, str) or not re.fullmatch(r'[a-f0-9]{32}\.json', identifier):
            raise ValueError('请选择可编辑的用户曲线。')
        return self.folder / identifier

    def update(self, identifier, data):
        path = self.path(identifier)
        if not path.is_file(): raise ValueError('曲线已不存在，请刷新曲线库。')
        result = curve_document(data['name'], data['points'], data)
        if 'source' in data: result['source'] = validate_source(data['source'])
        writer = UiPreferences(self.folder); writer.path = path; writer.write(result)
        return result

    def archive(self, identifier):
        path = self.path(identifier)
        archive = self.folder / 'archive'
        archive.mkdir(parents=True, exist_ok=True)
        path.replace(archive / (uuid.uuid4().hex + '.json'))
