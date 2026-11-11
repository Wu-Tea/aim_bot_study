"""Shared form schema and configuration type/range checks."""
import math
from .parameter_catalog import PARAMETERS, CATALOG_FIELDS, configured_value, validate_relationships

# path, label, type, fallback, allowed values/range
GAME_FIELDS = [
    ('gamepad.follow_response_curve', '跟随当前曲线转译', bool, False, None),
    ('runtime.vision.model_path', '识别模型', str, '', None),
    ('runtime.vision.enemy_marker_color', '敌人标识颜色', str, '#FF00FF', None),
    ('runtime.vision.friendly_marker_color', '友军标识颜色', str, '#00FFFF', None),
    ('runtime.vision.friendly_filter_enabled', '判断友方目标', bool, True, None),
    ('gamepad.enemy_mark.enabled', '自动标记敌人', bool, True, None),
    ('runtime.vision.target_height_ratio', '目标点高度比例', float, .35, (.01, .99)),
    ('runtime.vision.target_wide_low_height_ratio', '宽矮目标瞄点比例', float, .65, (.01, .99)),
    ('runtime.vision.cue_pickup_offset_y_px', '仅标识时向下偏移', float, 40.0, (10, 512)),
    ('gamepad.aim_response_curve.algorithm', '工具响应曲线', str, 'linear', ('linear', 'cod_dynamic_legacy_lut', 'custom_lut')),
    ('gamepad.aim_response_curve.custom_points', '自定义曲线控制点', str, '', None),
    ('gamepad.ads.strength_scale', '首次瞄准横向倍率', float, 1.0, (0, 3)),
    ('gamepad.ads.vertical_strength_scale', '首次瞄准纵向倍率', float, 1.0, (0, 3)),
    ('gamepad.ai_aim.hipfire_multiplier', '腰射 AI 力度倍率', float, 1.0, (0, 3)),
    ('gamepad.ai_aim.body_free_initial_scale', '腰射（Hipfire）', float, 0.0, (0, None)),
    ('gamepad.ai_aim.ads_free_initial_scale', '开镜（ADS）', float, 0.0, (0, None)),
    ('gamepad.recoil.enabled', '固定力度压枪', bool, True, None),
    ('gamepad.recoil.feedback_amount', '开镜压枪力度', float, .2, (0, 1)),
    ('gamepad.recoil.hipfire_multiplier', '腰射压枪倍率', float, 1.0, (0, 1)),
    ('gamepad.output_transfer.enabled', '游戏响应补偿', bool, False, None),
    ('gamepad.output_transfer.response_model', '游戏响应模型', str, 'power', ('power', 'cod_linear_legacy_lut', 'custom_lut', 'output_lut')),
    ('gamepad.output_transfer.custom_points', '基准曲线数据', str, '', None),
    ('gamepad.output_transfer.curve_name', '基准曲线名称', str, '', None),
    ('gamepad.output_transfer.deadzone', '游戏死区补偿量', float, .16, (0, .5)),
    ('gamepad.output_transfer.outer_deadzone', '终点死区', float, 0., (0, .5)),
    ('gamepad.output_transfer.axial', '轴向死区补偿', bool, False, None),
    ('gamepad.output_transfer.game_exponent', '游戏响应幂指数', float, 1.0, (1, 3)),
    ('gamepad.auto_fire.fire_output', '自动开火输出', str, 'RB', ('RT', 'RB')),
    ('gamepad.auto_fire.manual_fire_input', '手动开火输入', str, 'both', ('both', 'RT', 'RB')),
]
GAME_FIELDS += CATALOG_FIELDS
GAME_FIELDS.append(('gamepad.detection_haptics.enabled', '发现人物时震动', bool, False, None))
COMMON_FIELDS = [
    ('runtime.profile', '性能档位', str, 'legacy', ('legacy', 'balanced', 'performance', 'low_latency')),
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
    'power': '死区 / 幂函数', 'cod_linear_legacy_lut': 'COD Linear（旧版参考）',
    'legacy': '原生默认',
    'custom_lut': '自定义曲线',
    'linear': '线性（Linear）', 'cod_dynamic_legacy_lut': 'COD 动态曲线',
    'both': 'RT 或 RB', 'balanced': '均衡', 'performance': '性能优先',
    'low_latency': '低延迟', 'pascal_balanced': 'Pascal 显卡均衡',
}

# Display semantics shared by labels, entries, units and validation. Native
# storage keeps normalized fractions; users never enter ambiguous 0..1 values.
DISPLAY_FIELDS = {
    'gamepad.follow_response_curve': {'hot_reload':False},
    'runtime.vision.enemy_marker_color': {'hot_reload':False, 'help':'填写与游戏一致的 HEX，例如 #FF00FF。修改后需重启助手。'},
    'runtime.vision.friendly_marker_color': {'hot_reload':False, 'help':'填写与游戏一致的 HEX，例如 #00FFFF。敌我颜色需明显区分；修改后需重启助手。'},
    'gamepad.enemy_mark.enabled': {'hot_reload':False, 'help':'开启后，按左摇杆或开镜时会在确认目标后自动标记敌人。关闭后不再自动发送标记按键；手动标记仍可使用。修改后需重启助手，游戏里已有的标记不会立即消失。'},
    'gamepad.detection_haptics.enabled': {'hot_reload':True, 'help':'通过 USB 连接 DSE 手柄。与瞄准共用锁敌规则和准入门槛；选中目标时按左、中、右震动一次。同一目标不重复，策略切换或重新确认新目标时再次提醒。不开镜也可提醒，仅覆盖捕获区域。'},
    'runtime.vision.target_height_ratio': {'label':'瞄点距顶部','display_scale':100,'unit':'%'},
    'runtime.vision.cue_pickup_offset_y_px': {'unit':'px', 'hot_reload':False, 'help':'首次只有敌军标识时，定位点在标识下方的距离，单位为采集画面像素。看到人框后改用实际身体瞄点；仅标识定位不授予开火权限。修改后需重启助手。'},
    'runtime.vision.target_wide_low_height_ratio': {'label':'宽矮目标瞄点距顶部','display_scale':100,'unit':'%'},
    'gamepad.recoil.feedback_amount': {'label':'开镜压枪输出','display_scale':100,'unit':'%'},
    'gamepad.recoil.hipfire_multiplier': {'label':'腰射压枪保留比例','display_scale':100,'unit':'%'},
    'gamepad.output_transfer.deadzone': {'label':'起点死区','display_scale':100,'unit':'%','hot_reload':False},
    'gamepad.output_transfer.outer_deadzone': {'label':'终点死区','display_scale':100,'unit':'%','hot_reload':False},
    'gamepad.ai_aim.hipfire_multiplier': {'label':'腰射辅助增益','unit':'×'},
    'gamepad.output_transfer.game_exponent': {'label':'游戏响应指数'},
}

for _prior in ('body_free_initial_scale', 'ads_free_initial_scale'):
    DISPLAY_FIELDS['gamepad.ai_aim.' + _prior] = {'unit':'px/s', 'hot_reload':True,
        'help':'所选 AI 曲线达到 100% 响应时的相机速度。运行始终使用此值。'}


def field_presentation(path):
    return DISPLAY_FIELDS.get(path,PARAMETERS.get(path,{}))


def field_value(field, raw):
    _, label, kind, _, limits = field
    try:
        value = bool(raw) if kind is bool else kind(raw)
    except (ValueError, TypeError):
        raise ValueError(f'“{label}”的输入格式不正确。') from None
    if kind in (float, int) and (not math.isfinite(value) or (value < limits[0] or (limits[1] is not None and value > limits[1]))):
        scale=field_presentation(field[0]).get('display_scale',1)
        unit=field_presentation(field[0]).get('unit','')
        raise ValueError(f'“{label}”应为不小于 {limits[0]*scale:g} 的有限数值。' if limits[1] is None else f'“{label}”应在 {limits[0]*scale:g}～{limits[1]*scale:g}{unit} 之间。')
    if kind is str and limits and value not in limits:
        raise ValueError(f'“{label}”的选项无效。')
    if field[0].endswith('_marker_color'):
        import re
        if not re.fullmatch(r'#?[0-9a-fA-F]{6}', value):
            raise ValueError(f'“{label}”应为六位 HEX，例如 #FF00FF。')
        value = '#' + value.lstrip('#').upper()
    if field[0].endswith('_initial_scale') and value != 0 and value < 80:
        raise ValueError(f'“{label}”应为 0（继承）或不小于 80 的有限数值。')
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
        if lookup(data, 'gamepad.output_transfer.deadzone', .16) + lookup(data, 'gamepad.output_transfer.outer_deadzone', 0.) >= 1:
            raise ValueError('起点死区与终点死区之和必须小于 100%，请保留有效输入区间。')
