"""Independent profile documents; TOML is a projection for the native loader."""
from copy import deepcopy
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import tempfile
import tomllib
import uuid

from .curves import curve_document, decode_points, encode_points, seed_points
from .fields import COMMON_FIELDS, GAME_FIELDS, field_value, validate_fields
from .parameter_catalog import configured_value, LEGACY_PATHS
from .settings import effective, literal, lookup, UiPreferences

FIELDS = [f for f in GAME_FIELDS + COMMON_FIELDS if not f[0].startswith('gamepad.aim_response_curve.') and f[0] not in LEGACY_PATHS and f[0] != 'runtime.profile']
LEGACY_GAME_LABELS = {'default': '通用 / COD', 'apex': 'Apex Legends', 'bo3': 'COD · Black Ops III'}


def export_filename(name, suffix):
    stem = re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', name).strip(' .') or '配置'
    if stem.split('.')[0].upper() in {'CON','PRN','AUX','NUL',*[f'{prefix}{i}' for prefix in ('COM','LPT') for i in range(1,10)]}:
        stem = '_' + stem
    return stem + suffix


def put(document, path, value):
    parts = path.split('.')
    target = document
    for part in parts[:-1]:
        target = target.setdefault(part, {})
    target[parts[-1]] = value


def toml_text(document):
    """Serialize scalar native settings without dropping unexposed fields."""
    lines = []
    def table(data, parts):
        for key in data:
            if not re.fullmatch(r'[a-z0-9_]+', key):
                raise ValueError('配置字段名称无效：' + key)
        scalars = [(k, v) for k, v in data.items() if not isinstance(v, dict)]
        if parts:
            lines.extend(['', '[' + '.'.join(parts) + ']'])
        lines.extend(k + ' = ' + literal(v) for k, v in scalars)
        for key, value in data.items():
            if isinstance(value, dict):
                table(value, parts + [key])
    table(document, [])
    return '\n'.join(lines).lstrip() + '\n'


def projection(profile):
    data = deepcopy(profile['config'])
    put(data, 'runtime.game', profile['game'])
    algorithm = profile['curve']['algorithm']
    put(data, 'gamepad.aim_response_curve.algorithm', algorithm)
    if algorithm == 'custom_lut':
        put(data, 'gamepad.aim_response_curve.custom_points', encode_points(profile['curve']['definition']['points']))
    validate_fields(data, [profile['game']])
    return data


def snapshot(config, game, project, defaults=None):
    """Resolve inheritance once on explicit import; never inherit while editing."""
    # An explicit import chooses which game's overrides to flatten. A common
    # config may be imported for any supported game; this choice is never made
    # implicitly when opening an existing profile.
    common = deepcopy({key:value for key,value in config.items() if key != 'games'})
    put(common, 'runtime.game', game)
    data = deepcopy(effective(dict(common, games=config.get('games', {})), game))
    put(data, 'runtime.game', game)
    validate_fields(data, [game])
    for field in FIELDS:
        value = configured_value(data, field[0], (defaults or {}).get(field[0], field[3]))
        put(data, field[0], field_value(field, value))
    algorithm = lookup(data, 'gamepad.aim_response_curve.algorithm', (defaults or {}).get('gamepad.aim_response_curve.algorithm', 'linear'))
    if algorithm == 'custom_lut':
        points = decode_points(lookup(data, 'gamepad.aim_response_curve.custom_points', ''))
    else:
        points = seed_points(algorithm, project)
    data.get('gamepad', {}).pop('aim_response_curve', None)
    return {'config': data, 'curve': {'algorithm': algorithm, 'definition': curve_document('响应曲线', points)}}


class ProfileRepository:
    def __init__(self, root, curve_source=None):
        self.root = Path(root)
        self.folder = self.root / 'profiles/library'
        self.curve_source = Path(curve_source or root)
        self.errors = []

    def path(self, identifier):
        if not isinstance(identifier, str) or not re.fullmatch(r'p_[a-f0-9]{32}', identifier):
            raise ValueError('配置标识无效。')
        return self.folder / (identifier + '.json')

    def runtime_path(self, profile):
        return self.path(profile['id']).with_suffix('.toml')

    def read(self, path):
        raw = Path(path).read_bytes()
        data = json.loads(raw.decode('utf-8-sig'))
        self.check(data)
        if Path(path).stem != data['id']:
            raise ValueError('配置文件名与标识不一致。')
        return data, raw

    def entries(self):
        entries, self.errors = [], []
        for path in sorted(self.folder.glob('p_*.json')):
            try:
                entries.append(self.read(path)[0])
            except (OSError, ValueError, TypeError, KeyError) as error:
                self.errors.append(f'{path.name}：{error}')
        return sorted(entries,key=lambda entry:(entry.get('created_at_utc',''),entry['id']))

    def check(self, data):
        if not isinstance(data, dict) or type(data.get('schema_version')) is not int or data.get('schema_version') != 2:
            raise ValueError('不支持的配置格式。')
        self.path(data.get('id'))
        if 'created_at_utc' in data and not isinstance(data['created_at_utc'],str):
            raise ValueError('配置创建时间格式无效。')
        if not isinstance(data.get('game'), str) or not re.fullmatch(r'[a-z0-9_]+', data['game']):
            raise ValueError('运行配置标识无效。')
        if not isinstance(data.get('name'), str) or not 1 <= len(data['name'].strip()) <= 80:
            raise ValueError('配置名称应为 1～80 个字符。')
        if not isinstance(data.get('config'), dict) or 'games' in data['config']:
            raise ValueError('配置必须是独立快照。')
        for state in (data, data['initial']):
            if not isinstance(state,dict) or not isinstance(state.get('config'),dict) or not isinstance(state.get('curve'),dict):
                raise ValueError('配置快照格式无效。')
            if 'games' in state['config']:raise ValueError('配置快照不能包含游戏继承表。')
            curve = state['curve']
            if curve['algorithm'] not in ('linear', 'cod_dynamic_legacy_lut', 'custom_lut'):
                raise ValueError('曲线类型无效。')
            definition = curve['definition']
            if not isinstance(definition,dict) or type(definition.get('schema_version')) is not int or definition.get('schema_version') != 1 or definition.get('kind') != 'normalized_stick_response' or definition.get('interpolation') != 'piecewise_linear':
                raise ValueError('曲线格式无效。')
            curve_document(definition['name'], definition['points'])
            if curve['algorithm'] != 'custom_lut' and definition['points'] != seed_points(curve['algorithm'], self.curve_source):
                raise ValueError('预设曲线的点位与原生算法不一致。')
            toml_text(projection(dict(data, config=state['config'], curve=curve)))

    def create(self, name, game='custom', defaults=None, source=None, validate=None):
        if not isinstance(game, str) or not re.fullmatch(r'[a-z0-9_]+', game):
            raise ValueError('运行配置标识无效。')
        base = {'runtime': {'game': game}} if source is None else source
        state = snapshot(base, game, self.curve_source, defaults)
        data = {'schema_version': 2, 'id': 'p_' + uuid.uuid4().hex, 'name': name.strip(),
                'game': game, 'created_at_utc':datetime.now(timezone.utc).isoformat(), **state, 'initial': deepcopy(state)}
        self.save(data, None, validate)
        return data

    def duplicate(self, profile, name, validate=None):
        data = deepcopy(profile)
        data.update(id='p_' + uuid.uuid4().hex, name=name.strip(), created_at_utc=datetime.now(timezone.utc).isoformat(),
                    initial=deepcopy({'config': data['config'], 'curve': data['curve']}))
        self.save(data, None, validate)
        return data

    def save(self, data, expected, validate=None):
        self.check(data)
        path = self.path(data['id'])
        runtime_path = self.runtime_path(data)
        self.folder.mkdir(parents=True, exist_ok=True)
        def unchanged():
            if (path.read_bytes() if path.exists() else None) != expected:
                raise ValueError('配置已被外部修改，请重新载入后再保存。')
            if expected is not None and runtime_path.exists():
                old = json.loads(expected.decode('utf-8-sig'))
                if runtime_path.read_text(encoding='utf-8-sig') != toml_text(projection(old)):
                    raise ValueError('运行配置 TOML 被外部修改；请先将它导入为新配置。')
        unchanged()
        handle, filename = tempfile.mkstemp(prefix='.candidate-', suffix='.toml', dir=self.folder)
        candidate = Path(filename)
        try:
            with os.fdopen(handle, 'w', encoding='utf-8', newline='\n') as stream:
                stream.write(toml_text(projection(data)))
                stream.flush()
                os.fsync(stream.fileno())
            if validate:
                validate(candidate)
            unchanged()
            if expected is not None:
                backups = self.root / 'runs/desktop/profile-backups'
                backups.mkdir(parents=True, exist_ok=True)
                backup = backups / (path.stem + '-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
                backup.write_bytes(expected)
            writer = UiPreferences(self.root)
            writer.path = path
            writer.write(data)
            os.replace(candidate, runtime_path)
            return path.read_bytes()
        finally:
            candidate.unlink(missing_ok=True)

    def delete(self, profile, expected):
        """Remove from the library while retaining a recoverable JSON/TOML pair."""
        path = self.path(profile['id'])
        runtime = self.runtime_path(profile)
        if path.read_bytes() != expected:
            raise ValueError('配置已被外部修改，请重新载入后再删除。')
        saved = json.loads(expected.decode('utf-8-sig'))
        if runtime.exists() and runtime.read_text(encoding='utf-8-sig') != toml_text(projection(saved)):
            raise ValueError('运行配置 TOML 被外部修改；请先导入后再删除。')
        archive = self.root / 'runs/desktop/deleted-profiles' / (profile['id'] + '-' + uuid.uuid4().hex)
        archive.mkdir(parents=True)
        moved = []
        try:
            for source in (runtime, path):
                if source.exists():
                    destination = archive / source.name
                    source.rename(destination)
                    moved.append((source, destination))
        except OSError:
            for source, destination in reversed(moved):
                destination.rename(source)
            raise
        return archive

    def import_text(self, path):
        path = Path(path)
        if path.suffix.lower() == '.json':
            data = json.loads(path.read_text(encoding='utf-8-sig'))
            if data.get('schema_version') == 2:
                self.check(data)
                return toml_text(projection(data))
            path = path.with_suffix('.toml')
        return path.read_text(encoding='utf-8-sig')

    def import_choices(self, path):
        """Read actual branches from an import; never invent game templates."""
        path = Path(path)
        if path.suffix.lower() == '.json':
            data = json.loads(path.read_text(encoding='utf-8-sig'))
            if not isinstance(data, dict):raise ValueError('配置 JSON 必须是对象。')
            if data.get('schema_version') == 2:
                self.check(data)
                return [data['game']]
            if data.get('kind') == 'normalized_stick_response':
                raise ValueError('这是响应曲线文件，请从响应曲线工作区导入。')
            if data.get('schema_version') != 1 or not all(k in data for k in ('id','name','game')):
                raise ValueError('不支持的配置 JSON 格式或版本。')
            path = path.with_suffix('.toml')
        document = tomllib.loads(path.read_text(encoding='utf-8-sig'))
        branches = document.get('games', {})
        if not isinstance(branches, dict):raise ValueError('配置分支格式无效。')
        return list(dict.fromkeys([lookup(document, 'runtime.game', 'default'), *branches]))

    def import_file(self, path, name, game, defaults=None, validate=None):
        path = Path(path)
        if path.suffix.lower() == '.json':
            data = json.loads(path.read_text(encoding='utf-8-sig'))
            if not isinstance(data,dict):raise ValueError('配置 JSON 必须是对象。')
            if data.get('schema_version') == 2:
                self.check(data)
                return self.duplicate(data, name, validate)
            if data.get('kind') == 'normalized_stick_response':
                raise ValueError('这是响应曲线文件，请从响应曲线工作区导入。')
            if type(data.get('schema_version')) is not int or data.get('schema_version')!=1 or not all(k in data for k in ('id','name','game')):
                raise ValueError('不支持的配置 JSON 格式或版本。')
            # Explicit import of a historical metadata/TOML pair.
            path = path.with_suffix('.toml')
        source = tomllib.loads(path.read_text(encoding='utf-8-sig'))
        return self.create(name, game, defaults, source, validate)
