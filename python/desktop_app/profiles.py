"""Named, independent runtime configurations; legacy config stays readable."""
from __future__ import annotations
import json
from pathlib import Path
import re
import uuid

from .settings import ConfigStore, UiPreferences, lookup, update_text


class ProfileLibrary:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.folder = self.root / 'profiles'

    def entries(self, legacy_document, labels):
        profiles = [{'id': game, 'name': labels.get(game, game), 'game': game, 'legacy': True,
                     'path': self.root / 'config.toml'} for game in ['default'] + list(legacy_document.get('games', {}))]
        for path in sorted(self.folder.glob('*.json')):
            data = json.loads(path.read_text(encoding='utf-8'))
            if (not isinstance(data, dict) or type(data.get('schema_version')) is not int or data.get('schema_version') != 1 or
                not isinstance(data.get('id'), str) or
                not re.fullmatch(r'p_[a-f0-9]{32}', data.get('id', '')) or
                not isinstance(data.get('game'), str) or
                not re.fullmatch(r'[a-z0-9_]+', data.get('game', '')) or
                not isinstance(data.get('name'), str) or not data['name'].strip()):
                raise ValueError(f'配置资料格式错误：{path.name}')
            data['path'] = self.folder / (data['id'] + '.toml')
            if not data['path'].is_file():
                raise ValueError(f'配置文件不存在：{data["name"]}')
            import tomllib
            document = tomllib.loads(data['path'].read_text(encoding='utf-8-sig'))
            data['game'] = lookup(document, 'runtime.game', data['game'])
            if not isinstance(data['game'], str) or not re.fullmatch(r'[a-z0-9_]+', data['game']):
                raise ValueError(f'配置的游戏场景无效：{data["name"]}')
            profiles.append(data)
        return profiles

    def create(self, name, game, text, validate=None):
        if not name.strip() or len(name.strip()) > 80:
            raise ValueError('配置名称应为 1～80 个字符。')
        if not re.fullmatch(r'[a-z0-9_]+', game):
            raise ValueError('游戏标识只能使用小写字母、数字与下划线。')
        import tomllib
        document = tomllib.loads(text)
        changes = {'runtime.game': game}
        if game != 'default' and game not in document.get('games', {}):
            changes[f'games.{game}.runtime.vision.model_path'] = lookup(document, 'runtime.vision.model_path', '')
            changes[f'games.{game}.gamepad.aim_response_curve.algorithm'] = 'linear'
        candidate = update_text(text, changes)
        self.folder.mkdir(parents=True, exist_ok=True)
        identifier = 'p_' + uuid.uuid4().hex
        path = self.folder / (identifier + '.toml')
        with path.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(candidate)
        try:
            if validate:
                validate(path)
            data = {'schema_version': 1, 'id': identifier, 'name': name.strip(), 'game': game}
            writer = UiPreferences(self.root)
            writer.path = path.with_suffix('.json')
            writer.write(data)
        except Exception:
            path.unlink(missing_ok=True)
            raise
        return dict(data, path=path)

    def rename(self, profile, name):
        if profile.get('legacy'):
            raise ValueError('请先复制现有配置，再命名你的独立配置。')
        if not name.strip() or len(name.strip()) > 80:
            raise ValueError('配置名称应为 1～80 个字符。')
        data = {key: profile[key] for key in ('schema_version', 'id', 'game')}
        data['name'] = name.strip()
        path = self.folder / (profile['id'] + '.json')
        # Reuse the atomic preferences writer, with an explicit target file.
        preferences = UiPreferences(self.root)
        preferences.path = path
        preferences.write(data)
