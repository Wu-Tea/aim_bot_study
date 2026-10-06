"""Shared scalar metadata; native CMake consumes the same catalog at build time."""
import json
from project_paths import PROJECT_ROOT

CATALOG = json.loads((PROJECT_ROOT / 'native/controller_native/editable_parameters.json').read_text(encoding='utf-8'))
if CATALOG['schema_version'] != 1:
    raise ValueError('Unsupported editable parameter catalog.')
PARAMETERS = {p['path']: p for p in CATALOG['parameters']}
CATALOG_FIELDS = [(p['path'], p['label'], float, float(p['default']), (p['min'], p['max']))
                  for p in PARAMETERS.values()]

LEGACY_PATHS = {'gamepad.ads.strength_scale','gamepad.ads.vertical_strength_scale',
                'gamepad.ai_aim.hipfire_multiplier','gamepad.recoil.feedback_amount'} | {
    p['path'] for p in PARAMETERS.values() if p.get('legacy')}


def configured_value(document,path,default=None):
    """Read canonical fields without rewriting historical profile documents."""
    from .settings import lookup
    value=lookup(document,path)
    if value is not None:return value
    from .ads_geometry import f32
    if path=='gamepad.assist.arrival_radius_px':return lookup(document,'gamepad.ads.completion_radius_px',lookup(document,'gamepad.ai_aim.ads_completion_radius_px',2.))
    if path=='gamepad.assist.hipfire_ratio':return min(1.,max(0.,lookup(document,'gamepad.ai_aim.hipfire_multiplier',1.)))
    if path=='gamepad.recoil.output_amount':
        return lookup(document,'gamepad.recoil.feedback_amount',.2)
    if path.startswith('gamepad.ads.output_limit_'):
        vertical=path.endswith('_y')
        base=lookup(document,'gamepad.ai_aim.ads_snap_max_ai_force'+('_y' if vertical else ''),1.)
        gain=lookup(document,'gamepad.ads.'+('vertical_strength_scale' if vertical else 'strength_scale'),1.)
        return min(1.,max(0.,f32(f32(f32(base)*f32(gain))*f32(2**.5))))
    if path=='gamepad.ads.response_time_ms':
        return min(350.,max(60.,lookup(document,'gamepad.ads.snap_duration_ms',
            lookup(document,'gamepad.ai_aim.ads_snap_window_ms',135.))))
    if path.startswith(('gamepad.bodylock.output_limit_','gamepad.bodylock.response_time_')):
        vertical=path.endswith('_y') or path.endswith('_y_ms')
        force=lookup(document,'gamepad.bodylock.'+('vertical_strength' if vertical else 'strength'),
            lookup(document,'gamepad.ai_aim.body_lock_max_ai_force'+('_y' if vertical else ''),.42 if vertical else .3))
        if 'output_limit' in path:return min(1.,max(0.,force))
        legacy=('gamepad.bodylock.strength','gamepad.bodylock.vertical_strength',
                'gamepad.bodylock.feedback_distance_px','gamepad.bodylock.tolerance_px',
                'gamepad.ai_aim.body_lock_max_ai_force','gamepad.ai_aim.body_lock_max_ai_force_y',
                'gamepad.ai_aim.body_lock_box_tolerance_px')
        if not any(lookup(document,p) is not None for p in legacy):return float(PARAMETERS[path]['default'])
        distance=configured_value(document,'gamepad.bodylock.feedback_distance_px',27.)
        return f32(min(1000.,max(5.,f32(f32(1000*f32(distance))/max(1.,f32(f32(force)*500.))))))
    if path=='gamepad.bodylock.feedback_distance_px':
        legacy=lookup(document,'gamepad.bodylock.tolerance_px',
                      lookup(document,'gamepad.ai_aim.body_lock_box_tolerance_px'))
        if legacy is not None:
            import math
            if type(legacy) not in (int,float) or not math.isfinite(legacy) or not 0<=legacy<=2000:
                raise ValueError('旧版跟随柔和度应为 0～2000 的有限数值。')
            # Match the native float32 legacy conversion at the persistence boundary.
            from .ads_geometry import f32
            return max(18.,f32(f32(legacy)*1.5))
    return default


def validate_relationships(document):
    from .settings import lookup
    radius=lookup(document,'gamepad.bodylock.activation_range_px',150)
    completion=configured_value(document,'gamepad.assist.arrival_radius_px')
    if radius<completion:raise ValueError('基础跟随半径不能小于 近点收尾半径。')
    for relation in CATALOG['relations']:
        left = lookup(document, relation['left'], PARAMETERS[relation['left']]['default'])
        right = lookup(document, relation['right'], PARAMETERS[relation['right']]['default'])
        valid = left < right if relation['operator'] == 'lt' else left <= right
        if not valid:raise ValueError(relation['message'])


def groups(advanced, page='assist'):
    result = {}
    for parameter in PARAMETERS.values():
        if not parameter.get('legacy') and parameter['advanced'] == advanced and parameter.get('page','assist') == page:
            result.setdefault(parameter['group'], []).append(parameter['path'])
    return result.items()
