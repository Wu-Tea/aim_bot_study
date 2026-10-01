"""Shared form schema and configuration type/range checks."""
import math

# path, label, type, fallback, allowed values/range
GAME_FIELDS = [
    ('runtime.vision.model_path', '识别模型', str, '', None),
    ('runtime.vision.friendly_filter_enabled', '判断友方目标', bool, True, None),
    ('runtime.vision.target_height_ratio', '目标点高度比例', float, .35, (.01, .99)),
    ('gamepad.aim_response_curve.algorithm', '工具响应曲线', str, 'linear', ('linear', 'cod_dynamic_legacy_lut')),
    ('gamepad.ads.strength_scale', 'ADS 水平力度', float, 1.0, (0, 3)),
    ('gamepad.ads.vertical_strength_scale', 'ADS 垂直力度', float, 1.0, (0, 3)),
    ('gamepad.bodylock.strength', '持续跟随力度', float, 1.0, (0, 3)),
    ('gamepad.ai_aim.body_free_initial_scale', '跟随普通区响应初值（0=继承）', float, 0.0, (0, 4000)),
    ('gamepad.ai_aim.body_slow_initial_scale', '跟随减速区响应初值（0=继承）', float, 0.0, (0, 4000)),
    ('gamepad.ai_aim.ads_free_initial_scale', 'ADS 普通区响应初值（0=继承）', float, 0.0, (0, 4000)),
    ('gamepad.ai_aim.ads_slow_initial_scale', 'ADS 减速区响应初值（0=继承）', float, 0.0, (0, 4000)),
    ('gamepad.recoil.enabled', '固定力度压枪', bool, True, None),
    ('gamepad.recoil.feedback_amount', 'ADS 压枪力度', float, .2, (0, 1)),
    ('gamepad.recoil.hipfire_multiplier', '腰射压枪倍率', float, 1.0, (0, 1)),
    ('gamepad.output_transfer.enabled', '游戏死区补偿', bool, False, None),
    ('gamepad.output_transfer.deadzone', '游戏死区补偿量', float, .16, (0, .5)),
    ('gamepad.output_transfer.axial', '轴向死区补偿', bool, False, None),
    ('gamepad.output_transfer.game_exponent', '游戏响应幂指数', float, 1.0, (1, 3)),
    ('gamepad.auto_fire.fire_output', '自动开火输出', str, 'RB', ('RT', 'RB')),
    ('gamepad.auto_fire.manual_fire_input', '手动开火输入', str, 'both', ('both', 'RT', 'RB')),
]
COMMON_FIELDS = [
    ('runtime.profile', '性能档位', str, 'balanced', ('balanced', 'performance', 'low_latency', 'pascal_balanced')),
    ('runtime.vision.capture_fps', '检测帧率', int, 200, (1, 1000)),
    ('runtime.vision.idle_capture_fps', '空闲检测帧率', int, 60, (1, 1000)),
    ('runtime.vision.capture_width', '捕获宽度', int, 640, (1, 8192)),
    ('runtime.vision.capture_height', '捕获高度', int, 512, (1, 8192)),
    ('runtime.vision.tensor_width', '模型输入宽度', int, 480, (1, 8192)),
    ('runtime.vision.tensor_height', '模型输入高度', int, 384, (1, 8192)),
    ('runtime.input.auto_detect', '自动选择手柄', bool, True, None),
    ('runtime.input.controller_index', 'XInput 手柄编号', int, 0, (0, 3)),
    ('runtime.telemetry.enabled', '记录控制日志', bool, False, None),
    ('runtime.performance.enabled', '记录性能摘要', bool, False, None),
]


def field_value(field, raw):
    _, label, kind, _, limits = field
    try:
        value = bool(raw) if kind is bool else kind(raw)
    except (ValueError, TypeError):
        raise ValueError(f'“{label}”的输入格式不正确。') from None
    if kind in (float, int) and (not math.isfinite(value) or not limits[0] <= value <= limits[1]):
        raise ValueError(f'“{label}”应在 {limits[0]}～{limits[1]} 之间。')
    if kind is str and limits and value not in limits:
        raise ValueError(f'“{label}”的选项无效。')
    if field[0].endswith('_initial_scale') and value != 0 and value < 80:
        raise ValueError(f'“{label}”应为 0（继承）或 80～4000。')
    return value


def validate_fields(document, games):
    from .settings import effective, lookup
    for game in games:
        data = effective(document, game)
        for field in GAME_FIELDS + COMMON_FIELDS:
            value = lookup(data, field[0])
            if value is None:
                continue
            kind = field[2]
            valid_type = type(value) in (int, float) if kind is float else type(value) is kind
            if not valid_type:
                raise ValueError(f'{game} 的“{field[1]}”配置类型不正确。')
            field_value(field, value)
