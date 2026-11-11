"""Curve settings captured at process launch; not inferred from a mutable file."""
from .settings import effective, lookup


def runtime_curve_binding(document, game=None):
    data = effective(document, game or lookup(document, 'runtime.game', 'custom'))
    algorithm = lookup(data, 'gamepad.aim_response_curve.algorithm', 'linear')
    return {
        'ai_algorithm': algorithm,
        'ai_points': lookup(data, 'gamepad.aim_response_curve.custom_points', '') if algorithm == 'custom_lut' else '',
        'response_units': 'full_scale_px_per_second',
        'response_measurement': 'passive_v1',
        'manual_translation': lookup(data, 'gamepad.follow_response_curve', False),
        'output': {key: lookup(data, 'gamepad.output_transfer.' + key, default) for key, default in (
            ('enabled', False), ('response_model', 'power'), ('custom_points', ''),
            ('curve_name', ''), ('deadzone', .16), ('axial', False), ('game_exponent', 1.))},
    }
