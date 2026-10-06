"""Shared form schema and configuration type/range checks."""
import math
from .parameter_catalog import PARAMETERS, CATALOG_FIELDS, configured_value, validate_relationships

# path, label, type, fallback, allowed values/range
GAME_FIELDS = [
    ('runtime.vision.model_path', '识别模型', str, '', None),
    ('runtime.vision.friendly_filter_enabled', '判断友方目标', bool, True, None),
    ('runtime.vision.target_height_ratio', '目标点高度比例', float, .35, (.01, .99)),
    ('runtime.vision.target_wide_low_height_ratio', '宽矮目标瞄点比例', float, .65, (.01, .99)),
    ('gamepad.aim_response_curve.algorithm', '工具响应曲线', str, 'linear', ('linear', 'cod_dynamic_legacy_lut', 'custom_lut')),
    ('gamepad.aim_response_curve.custom_points', '自定义曲线控制点', str, '', None),
    ('gamepad.ads.strength_scale', '首次瞄准横向倍率', float, 1.0, (0, 3)),
    ('gamepad.ads.vertical_strength_scale', '首次瞄准纵向倍率', float, 1.0, (0, 3)),
    ('gamepad.ai_aim.hipfire_multiplier', '腰射 AI 力度倍率', float, 1.0, (0, 3)),
    ('gamepad.ai_aim.aim_response_learning_enabled', '学习游戏响应曲线', bool, True, None),
    ('gamepad.ai_aim.body_free_initial_scale', '跟随普通区响应初值（0=默认）', float, 0.0, (0, 4000)),
    ('gamepad.ai_aim.body_slow_initial_scale', '跟随减速区响应初值（0=默认）', float, 0.0, (0, 4000)),
    ('gamepad.ai_aim.ads_free_initial_scale', 'ADS 普通区响应初值（0=默认）', float, 0.0, (0, 4000)),
    ('gamepad.ai_aim.ads_slow_initial_scale', 'ADS 减速区响应初值（0=默认）', float, 0.0, (0, 4000)),
    ('gamepad.recoil.enabled', '固定力度压枪', bool, True, None),
    ('gamepad.recoil.feedback_amount', '开镜压枪力度', float, .2, (0, 1)),
    ('gamepad.recoil.hipfire_multiplier', '腰射压枪倍率', float, 1.0, (0, 1)),
    ('gamepad.output_transfer.enabled', '游戏死区补偿', bool, False, None),
    ('gamepad.output_transfer.deadzone', '游戏死区补偿量', float, .16, (0, .5)),
    ('gamepad.output_transfer.axial', '轴向死区补偿', bool, False, None),
    ('gamepad.output_transfer.game_exponent', '游戏响应幂指数', float, 1.0, (1, 3)),
    ('gamepad.auto_fire.fire_output', '自动开火输出', str, 'RB', ('RT', 'RB')),
    ('gamepad.auto_fire.manual_fire_input', '手动开火输入', str, 'both', ('both', 'RT', 'RB')),
]
GAME_FIELDS += CATALOG_FIELDS
COMMON_FIELDS = [
    ('runtime.profile', '性能档位', str, 'balanced', ('legacy', 'balanced', 'performance', 'low_latency')),
    ('runtime.vision.capture_fps', '检测帧率', int, 200, (1, 1000)),
    ('runtime.vision.idle_capture_fps', '空闲检测帧率', int, 60, (1, 240)),
    ('runtime.vision.capture_width', '捕获宽度', int, 640, (32, 8192)),
    ('runtime.vision.capture_height', '捕获高度', int, 512, (32, 8192)),
    ('runtime.vision.tensor_width', '模型输入宽度', int, 480, (32, 8192)),
    ('runtime.vision.tensor_height', '模型输入高度', int, 384, (32, 8192)),
    ('runtime.input.auto_detect', '自动选择手柄', bool, True, None),
    ('runtime.input.controller_index', 'XInput 手柄编号', int, 0, (0, 3)),
    ('runtime.input.device_id', '输入手柄', str, '', None),
    ('runtime.input.device_name', '手柄名称', str, '', None),
    ('runtime.telemetry.enabled', '记录控制日志', bool, False, None),
    ('runtime.performance.enabled', '记录性能摘要', bool, False, None),
]

CHOICE_LABELS = {
    'legacy': '原生默认',
    'custom_lut': '自定义曲线',
    'linear': '线性（Linear）', 'cod_dynamic_legacy_lut': 'COD 动态曲线',
    'both': 'RT 或 RB', 'balanced': '均衡', 'performance': '性能优先',
    'low_latency': '低延迟', 'pascal_balanced': 'Pascal 显卡均衡',
}

# Display semantics shared by labels, entries, units and validation. Native
# storage keeps normalized fractions; users never enter ambiguous 0..1 values.
DISPLAY_FIELDS = {
    'runtime.vision.target_height_ratio': {'label':'瞄点距顶部','display_scale':100,'unit':'%'},
    'runtime.vision.target_wide_low_height_ratio': {'label':'宽矮目标瞄点距顶部','display_scale':100,'unit':'%'},
    'gamepad.recoil.feedback_amount': {'label':'开镜压枪输出','display_scale':100,'unit':'%'},
    'gamepad.recoil.hipfire_multiplier': {'label':'腰射压枪保留比例','display_scale':100,'unit':'%'},
    'gamepad.output_transfer.deadzone': {'label':'游戏死区补偿量','display_scale':100,'unit':'%'},
    'gamepad.ai_aim.hipfire_multiplier': {'label':'腰射辅助增益','unit':'×'},
    'gamepad.output_transfer.game_exponent': {'label':'游戏响应指数'},
}


def field_presentation(path):
    return DISPLAY_FIELDS.get(path,PARAMETERS.get(path,{}))


def field_value(field, raw):
    _, label, kind, _, limits = field
    try:
        value = bool(raw) if kind is bool else kind(raw)
    except (ValueError, TypeError):
        raise ValueError(f'“{label}”的输入格式不正确。') from None
    if kind in (float, int) and (not math.isfinite(value) or not limits[0] <= value <= limits[1]):
        scale=field_presentation(field[0]).get('display_scale',1)
        unit=field_presentation(field[0]).get('unit','')
        raise ValueError(f'“{label}”应在 {limits[0]*scale:g}～{limits[1]*scale:g}{unit} 之间。')
    if kind is str and limits and value not in limits:
        raise ValueError(f'“{label}”的选项无效。')
    if field[0].endswith('_initial_scale') and value != 0 and value < 80:
        raise ValueError(f'“{label}”应为 0（继承）或 80～4000。')
    if field[0].endswith('custom_points') and value:
        from .curves import decode_points
        decode_points(value)
    return value


def validate_fields(document, games):
    from .settings import effective, lookup
    for game in games:
        data = effective(document, game)
        if lookup(data, 'gamepad.aim_response_curve.algorithm') == 'custom_lut':
            from .curves import decode_points
            decode_points(lookup(data, 'gamepad.aim_response_curve.custom_points', ''))
        for field in GAME_FIELDS + COMMON_FIELDS:
            value = configured_value(data, field[0])
            if value is None:
                continue
            kind = field[2]
            valid_type = type(value) in (int, float) if kind is float else type(value) is kind
            if not valid_type:
                raise ValueError(f'{game} 的“{field[1]}”配置类型不正确。')
            field_value(field, value)
        validate_relationships(data)
