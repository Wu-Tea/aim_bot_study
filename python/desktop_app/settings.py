from __future__ import annotations

from copy import deepcopy
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import tempfile
import tomllib
import uuid


class UiPreferences:
    """UI selection is independent of native runtime configuration."""
    def __init__(self, root):
        self.path = Path(root) / 'runs/desktop/ui.json'

    def read(self):
        try:
            data = json.loads(self.path.read_text(encoding='utf-8-sig'))
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def save_game(self, game):
        data = self.read()
        data['game'] = game
        self.path.parent.mkdir(parents=True, exist_ok=True)
        handle, filename = tempfile.mkstemp(prefix='.ui-', suffix='.json', dir=self.path.parent)
        candidate = Path(filename)
        try:
            with os.fdopen(handle, 'w', encoding='utf-8') as stream:
                json.dump(data, stream, ensure_ascii=False, indent=2)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(candidate, self.path)
        finally:
            candidate.unlink(missing_ok=True)


def merge(base, overrides):
    result = deepcopy(base)
    for key, value in overrides.items():
        result[key] = merge(result.get(key, {}), value) if isinstance(value, dict) else value
    return result


def effective(document, game):
    common = {key: value for key, value in document.items() if key != 'games'}
    if game != 'default' and game not in document.get('games', {}):
        raise ValueError(f'游戏配置不存在：{game}')
    return merge(common, document.get('games', {}).get(game, {}))


def lookup(document, path, default=None):
    value = document
    for part in path.split('.'):
        if not isinstance(value, dict) or part not in value:
            return default
        value = value[part]
    return value


def literal(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, (int, float)):
        if not math.isfinite(value):
            raise ValueError('数值必须有限')
        return str(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    raise ValueError('此字段只接受文本、数字或开关')


def comment(line):
    quote = None
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
        elif quote == '"' and char == '\\':
            escaped = True
        elif quote:
            if char == quote:
                quote = None
        elif char in ('"', "'"):
            quote = char
        elif char == '#':
            return line[index:].rstrip('\r\n')
    return ''


def update_text(text, updates):
    """Explicit editor writes, preserving unrelated settings and comments."""
    tomllib.loads(text)
    lines = text.splitlines(keepends=True)
    for path, value in updates.items():
        section, key = path.rsplit('.', 1)
        if not re.fullmatch(r'[a-z0-9_.]+', path):
            raise ValueError(f'配置字段无效：{path}')
        begin = end = None
        for index, line in enumerate(lines):
            header = re.match(r'^\s*\[([^\]]+)\]\s*(?:#.*)?$', line.strip())
            if header:
                if begin is not None:
                    end = index
                    break
                if header[1] == section:
                    begin = index + 1
        if begin is None:
            if lines and not lines[-1].endswith('\n'):
                lines[-1] += '\n'
            lines.extend([f'\n[{section}]\n', f'{key} = {literal(value)}\n'])
            continue
        end = end if end is not None else len(lines)
        for index in range(begin, end):
            if re.match(r'^\s*' + re.escape(key) + r'\s*=', lines[index]):
                suffix = comment(lines[index])
                lines[index] = f'{key} = {literal(value)}' + (f' {suffix}' if suffix else '') + '\n'
                break
        else:
            lines.insert(begin, f'{key} = {literal(value)}\n')
    result = ''.join(lines)
    tomllib.loads(result)
    return result


class ConfigStore:
    def __init__(self, root):
        self.root = Path(root)
        self.path = self.root / 'config.toml'

    def read(self):
        raw = self.path.read_bytes()
        text = raw.decode('utf-8-sig')
        return text, tomllib.loads(text), hashlib.sha256(raw).hexdigest()

    def save(self, text, expected_hash, validate=None):
        tomllib.loads(text)
        if self.read()[2] != expected_hash:
            raise ValueError('配置已被其他程序修改，请重新载入后再保存。')
        handle, filename = tempfile.mkstemp(prefix='.config-', suffix='.toml', dir=self.root)
        candidate = Path(filename)
        try:
            with os.fdopen(handle, 'w', encoding='utf-8', newline='\n') as stream:
                stream.write(text)
                stream.flush()
                os.fsync(stream.fileno())
            if validate:
                validate(candidate)
            if self.read()[2] != expected_hash:
                raise ValueError('校验期间配置发生变化，请重新载入。')
            backup = self.root / 'runs/desktop/config-backups'
            backup.mkdir(parents=True, exist_ok=True)
            name = datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8] + '.toml'
            (backup / name).write_bytes(self.path.read_bytes())
            os.replace(candidate, self.path)
        finally:
            candidate.unlink(missing_ok=True)

    def migrate_games(self):
        text, data, digest = self.read()
        updates = {}
        if 'apex' not in data.get('games', {}):
            updates.update({
                'games.apex.runtime.vision.model_path': 'artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine',
                'games.apex.gamepad.aim_response_curve.algorithm': 'linear',
                'games.apex.gamepad.recoil.hipfire_multiplier': .5,
            })
        if 'bo3' not in data.get('games', {}):
            legacy_path = self.root / 'config.bo3.toml'
            legacy = tomllib.loads(legacy_path.read_text(encoding='utf-8-sig')) if legacy_path.exists() else {
                'gamepad': {'output_transfer': {'enabled': True, 'axial': False, 'deadzone': .16, 'game_exponent': 1.0},
                            'aim_response_curve': {'algorithm': 'linear'}}}
            def flatten(table, prefix):
                for key, value in table.items():
                    if isinstance(value, dict):
                        flatten(value, prefix + '.' + key)
                    else:
                        updates[prefix + '.' + key] = value
            flatten(legacy, 'games.bo3')
        if updates:
            self.save(update_text(text, updates), digest)
        return bool(updates)
