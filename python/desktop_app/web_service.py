"""Desktop bridge: UI drafts are proposals; repository/native validation owns writes."""
from copy import deepcopy
import math
from pathlib import Path
import threading
import time
import tomllib
import uuid

from .workspace import FIELDS, ProfileRepository, projection, snapshot, toml_text, put, export_filename
from .fields import field_presentation, field_value, CHOICE_LABELS
from .parameter_catalog import configured_value
from .settings import lookup, UiPreferences
from .curves import CurveLibrary, curve_document, read_curve, seed_points


def schema():
    result = []
    for path, label, kind, default, limits in FIELDS:
        meta = field_presentation(path)
        if path.startswith('runtime.vision.'):
            group = '目标与识别' if path.endswith(('friendly_filter_enabled', 'target_height_ratio', 'target_wide_low_height_ratio')) else '捕获与模型'
        elif path.startswith('runtime.input.'): group = '输入设备'
        elif path.startswith('runtime.'): group = '诊断记录'
        elif path.startswith('gamepad.output_transfer.'): group = '手动输出补偿'
        elif path.startswith(('gamepad.recoil.', 'gamepad.auto_fire.')): group = '开火与压枪'
        elif path.startswith('gamepad.bodylock.'): group = '持续跟随'
        elif path.startswith('gamepad.ads.'): group = '首次瞄准'
        elif path.startswith('gamepad.assist.'): group = '辅助输入'
        else: group = '响应学习与手动介入'
        advanced = meta.get('advanced', False) or path in ('gamepad.ads.activation_trigger','gamepad.ads.scope_ready_trigger')
        result.append(dict(path=path, label=meta.get('label', label), kind=kind.__name__,
            limits=limits, scale=meta.get('display_scale', 1), unit=meta.get('unit', ''),
            help=meta.get('help', ''), advanced=advanced, group=group,
            choices={value: CHOICE_LABELS.get(value, value) for value in limits} if kind is str and limits else None,
            hot_reload=meta.get('hot_reload'), readonly=path in ('runtime.vision.tensor_width','runtime.vision.tensor_height','runtime.input.device_name')))
    order = ['gamepad.ads.output_limit_x','gamepad.ads.output_limit_y','gamepad.ads.response_time_ms',
             'gamepad.bodylock.output_limit_x','gamepad.bodylock.output_limit_y',
             'gamepad.bodylock.response_time_x_ms','gamepad.bodylock.response_time_y_ms']
    return sorted(result, key=lambda f: order.index(f['path']) if f['path'] in order else len(order))


class WorkspaceService:
    def __init__(self, root, manager=None, dialogs=None, curve_source=None):
        from .runtime import RuntimeManager
        self.root = Path(root).resolve()
        self.manager = manager or RuntimeManager(self.root)
        self.repo = ProfileRepository(self.root, curve_source)
        self.curves = CurveLibrary(self.root)
        self.preferences = UiPreferences(self.root)
        self.dialogs = dialogs
        self.lock = threading.RLock()
        self.states = {}
        self.imports = {}
        self.policy = None

    def _validator(self, game):
        return lambda path: self.manager.validate(path, [game])

    def _open(self, identifier):
        doc, raw = self.repo.read(self.repo.path(identifier))
        revision = uuid.uuid4().hex
        self.states[identifier] = (doc, raw, revision)
        return self._view(doc, revision)

    def _view(self, doc, revision):
        return dict(id=doc['id'], name=doc['name'], game=doc['game'], revision=revision,
            config=deepcopy(doc['config']),
            values={f[0]: configured_value(doc['config'], f[0], f[3]) for f in FIELDS},
            curve=deepcopy(doc['curve']), initial=deepcopy(doc['initial']))

    def _saved(self, payload):
        identifier = payload['id']
        self.repo.path(identifier)
        if identifier not in self.states or self.states[identifier][2] != payload.get('revision'):
            raise ValueError('配置版本已变化，请重新载入后再操作。')
        return self.states[identifier]

    def _candidate(self, payload):
        saved, expected, revision = self._saved(payload)
        doc = deepcopy(saved)
        if not isinstance(payload.get('config'), dict) or 'games' in payload['config']:
            raise ValueError('配置草稿必须是独立配置对象。')
        doc['config'] = deepcopy(payload['config'])
        values = payload.get('values', {})
        if not isinstance(values, dict) or set(values) != {f[0] for f in FIELDS}:
            raise ValueError('参数集合不完整，请重新载入界面。')
        for field in FIELDS:
            raw = values[field[0]]
            if field[2] is bool and type(raw) is not bool:
                raise ValueError(field[1] + '必须是开关值。')
            if field[2] in (float, int) and (type(raw) not in (int, float) or not math.isfinite(raw)):
                raise ValueError(field[1] + '必须是有限数值。')
            if field[2] is int and raw != int(raw):
                raise ValueError(field[1] + '必须是整数。')
            put(doc['config'], field[0], field_value(field, raw))
        doc['curve'] = deepcopy(payload['curve'])
        self.repo.check(doc)
        return doc, expected

    def _save(self, doc, expected):
        raw = self.repo.save(doc, expected, self._validator(doc['game']))
        revision = uuid.uuid4().hex
        self.states[doc['id']] = (deepcopy(doc), raw, revision)
        return self._view(doc, revision)

    def _owns(self, doc, record):
        return bool(record and record.get('game') == doc['game'] and
            Path(record.get('config_path', self.root / 'config.toml')).resolve() == self.repo.runtime_path(doc).resolve())

    def _choose(self, mode, name='', filters=()):
        if self.dialogs is None: raise ValueError('文件选择器尚未就绪。')
        return self.dialogs(mode, name, filters)

    def _export(self, data, name):
        selected = self._choose('save', name, ('JSON 文件 (*.json)',))
        if not selected: return None
        path = Path(selected).resolve()
        # Export cannot bypass repository optimistic-write or active-config ownership.
        active = self.manager.active()
        active_path = Path(active['config_path']).resolve() if active and active.get('config_path') else None
        if path.is_relative_to(self.root / 'profiles') or path == self.root / 'config.toml' or path == active_path:
            raise ValueError('请导出到配置库以外的位置，避免覆盖受管理的配置。')
        writer = UiPreferences(self.root)
        writer.path = path
        writer.write(data)
        return str(path)

    def dispatch(self, command, payload=None):
        """One explicit RPC boundary. Errors remain visible; no implicit fallback runtime."""
        payload = payload or {}
        try:
            if command == 'status': return {'ok': True, 'data': self._status()}
            with self.lock:
                return {'ok': True, 'data': self._dispatch(command, payload)}
        except Exception as error:
            return {'ok': False, 'error': str(error) or type(error).__name__}

    def _status(self):
        status = self.manager.status()
        record = status.get('record')
        status['rates'] = self.manager.frame_rates(record) if record else None
        status['learning'] = self.manager.learning() if record else None
        status['fusion'] = bool(self.manager.fusion_state())
        status.pop('output', None)
        return status

    def _dispatch(self, command, p):
        if command == 'bootstrap':
            entries = self.repo.entries()
            return dict(profiles=[self._open(doc['id']) for doc in entries], errors=self.repo.errors,
                selected=self.preferences.read().get('profile_id'), fields=schema(),
                seeds={key: seed_points(key, self.repo.curve_source) for key in ('linear','cod_dynamic_legacy_lut')})
        if command == 'select':
            doc = self.states[p['id']][0]
            self.preferences.save_profile(doc['id'], doc['game'])
            return True
        if command == 'reload': return self._open(p['id'])
        if command == 'reset':
            saved, _, revision = self._saved(p)
            doc = deepcopy(saved); doc.update(deepcopy(doc['initial']))
            return self._view(doc, revision)
        if command == 'create':
            if not self.manager.executable.is_file(): raise ValueError('请先构建原生程序，才能读取新配置的默认参数。')
            defaults = self.manager.inspect_defaults('custom', '[runtime]\ngame = "custom"\n')
            # A native built-in model path is not a user choice for a new profile.
            defaults['runtime.vision.model_path'] = ''
            doc = self.repo.create(p['name'], defaults=defaults, validate=self._validator('custom'))
            return self._open(doc['id'])
        if command in ('save','start','copy','export_profile','raw','raw_apply'):
            doc, expected = self._candidate(p)
            if command == 'copy':
                new = self.repo.duplicate(doc, p['name'], self._validator(doc['game']))
                return self._open(new['id'])
            if command == 'export_profile': return self._export(doc, export_filename(doc['name'], '.json'))
            if command == 'raw': return toml_text(projection(doc))
            if command == 'raw_apply':
                text = p['text']
                source = tomllib.loads(text)
                if 'games' in source: raise ValueError('完整配置编辑只接受当前独立配置；多分支文件请使用导入。')
                defaults = self.manager.inspect_defaults(doc['game'], text)
                doc.update(snapshot(source, doc['game'], self.repo.curve_source, defaults))
                self.repo.check(doc)
                # Validation without persistence, then return a draft with the same revision.
                import tempfile
                with tempfile.TemporaryDirectory() as folder:
                    path = Path(folder) / 'draft.toml'; path.write_text(toml_text(projection(doc)), encoding='utf-8')
                    self.manager.validate(path, [doc['game']])
                return self._view(doc, p['revision'])
            if command == 'start':
                model = lookup(doc['config'],'runtime.vision.model_path','')
                if not model: raise ValueError('请先在“模型与设备”选择识别模型。')
                changes = self._model_changes(doc['config'], self.manager.inspect_model(model))
                if changes: return {'needs_model_sync': changes}
                active = self.manager.active()
                identity = [active.get('process_id'), str(active.get('process_created'))] if active else None
                if active and p.get('confirmed_runtime') != identity:
                    return {'needs_restart': identity, 'name': active.get('game')}
                saved = self._save(doc, expected)
                try:
                    current = self.manager.active()
                    current_identity = [current.get('process_id'), str(current.get('process_created'))] if current else None
                    if current_identity != identity:
                        raise ValueError('运行实例在校验期间发生变化，请重新确认启动。')
                    if active:
                        self.manager.stop()
                        deadline = time.monotonic() + 8
                        while self.manager.active():
                            if time.monotonic() > deadline: raise ValueError('已请求正常退出，但旧程序尚未退出。请查看日志。')
                            time.sleep(.05)
                    record = self.manager.start(doc['game'], projection(doc), self.repo.runtime_path(doc), doc['id'])
                    return {'profile': saved, 'status': {'phase': 'starting', 'record': record},
                        'message': '启动请求已提交，正在等待程序和手柄就绪。'}
                except Exception as error:
                    return {'profile': saved, 'error': '配置已保存，启动失败：' + str(error)}
            saved = self._save(doc, expected)
            if p.get('apply') and self._owns(doc, self.manager.active()):
                try: return {'profile': saved, 'reload': self.manager.reload_config()}
                except Exception as error: return {'profile': saved, 'error': '配置已保存，尚未应用：' + str(error)}
            return {'profile': saved, 'message': '配置已保存，将在下次启动时生效。'}
        if command == 'rename':
            saved, expected, _ = self._saved(p)
            # Rename saved metadata only; preserve the front-end parameter draft.
            doc = deepcopy(saved); doc['name'] = p['name'].strip()
            return self._save(doc, expected)
        if command == 'delete':
            doc, expected, _ = self._saved(p)
            if self._owns(doc, self.manager.active()): raise ValueError('请先停止此配置，再删除。')
            archive = self.repo.delete(doc, expected)
            del self.states[doc['id']]
            return str(archive)
        if command == 'import_choose':
            path = self._choose('open', '', ('配置文件 (*.json;*.toml)',))
            if not path: return None
            choices = self.repo.import_choices(path)
            token = uuid.uuid4().hex
            self.imports[token] = (path, Path(path).read_bytes(), self.repo.import_text(path))
            return dict(token=token, choices=choices, name=Path(path).stem)
        if command == 'import':
            path, raw, text = self.imports[p['token']]
            if Path(path).read_bytes()!=raw or self.repo.import_text(path)!=text: raise ValueError('导入文件已变化，请重新选择。')
            if p['game'] not in self.repo.import_choices(path): raise ValueError('请选择文件中存在的配置分支。')
            defaults = self.manager.inspect_defaults(p['game'], text)
            doc = self.repo.import_file(path, p['name'], p['game'], defaults, self._validator(p['game']))
            del self.imports[p['token']]
            return self._open(doc['id'])
        if command == 'model':
            path = self._choose('open', '', ('TensorRT 模型 (*.engine)',))
            if not path: return None
            shape = self.manager.inspect_model(path)
            return dict(path=path, shape=shape, changes=self._model_changes(p['config'], shape))
        if command == 'devices': return self.manager.input_devices()
        if command == 'stop': self.manager.stop(); return '已请求正常退出。'
        if command == 'fusion': self.manager.set_fusion(bool(p['enabled'])); return True
        if command == 'logs':
            from .runtime import tail
            status = self.manager.status(); record = status.get('record') or status.get('last_record')
            return {'stdout': tail(record.get('stdout_path','')) if record else '', 'stderr': tail(record.get('stderr_path','')) if record else ''}
        if command == 'learning_export':
            data = self.manager.learning()
            if not data: raise ValueError('当前没有可导出的响应学习数据。')
            return self._export({'learning': data, 'units': 'px / (effective_stick * second)',
                'measurement_kind':'controller response estimate, not independent game calibration'}, '响应学习.json')
        if command == 'curve_import':
            path = self._choose('open','',('响应曲线 (*.json)',))
            return read_curve(path) if path else None
        if command == 'curve_export': return self._export(curve_document(p['name'],p['points']), export_filename(p['name'],'.json'))
        if command == 'curve_presets':
            return {'entries':[dict(id=path.name, **data) for path,data in self.curves.entries()], 'errors':self.curves.errors}
        if command == 'curve_preset_save':
            self.curves.create(p['name'],p['points']); return True
        if command == 'geometry': return self._geometry(p)
        raise ValueError('不支持的界面操作：' + str(command))

    @staticmethod
    def _model_changes(config, shape):
        w,h = shape['input_width'],shape['input_height']
        proposed = {'tensor_width':w,'tensor_height':h}
        cw,ch = (lookup(config,'runtime.vision.'+key) for key in ('capture_width','capture_height'))
        if lookup(config,'runtime.vision.require_isotropic_resize',True) and cw*h != ch*w:
            divisor=math.gcd(w,h); ux,uy=w//divisor,h//divisor
            factor=max(math.ceil(32/min(ux,uy)),min(round(cw/ux),8192//max(ux,uy)))
            proposed.update(capture_width=ux*factor,capture_height=uy*factor)
        return {'runtime.vision.'+key:value for key,value in proposed.items() if lookup(config,'runtime.vision.'+key)!=value}

    def _geometry(self, p):
        from .ads_geometry import envelope, admission, acquisition_example, follow_response_example, filter_ai_input
        if self.policy is None: self.policy = self.manager.ads_geometry_policy()
        if self.policy.get('parameter_semantics') != 'range-response-v4': raise ValueError('原生预览版本不匹配，请更新原生程序。')
        v=p['values']; fields={f[0]:f for f in FIELDS}
        def n(path): return field_value(fields[path],v[path])
        width,height=n('runtime.vision.capture_width'),n('runtime.vision.capture_height')
        fraction=float(p['height'])/100
        x,y=map(float,p['offset'])
        if not all(math.isfinite(z) for z in (fraction,x,y)) or not .01<=fraction<=.9: raise ValueError('预览尺寸或位置无效。')
        bh=height*fraction; bw=bh*{'standing':.35,'crouching':.8,'wide':2.4}[p['pose']]
        args=(self.policy,bw,bh,height,n('runtime.vision.target_height_ratio'),n('runtime.vision.target_wide_low_height_ratio'))
        g=envelope(*args,n('gamepad.ads.pickup_base_radius_px'))
        g['follow_radius']=envelope(*args,n('gamepad.bodylock.activation_range_px'))['radius']
        extra=dict(radius_px=g['radius'] if p['mode']=='acquire' else g['follow_radius'],
            minimum_stick=n('gamepad.assist.minimum_position_stick'),arrival_radius_px=n('gamepad.assist.arrival_radius_px'))
        if p['mode']=='acquire':
            example=acquisition_example(self.policy,g['normalized_height'],n('gamepad.ads.output_limit_x'),n('gamepad.ads.output_limit_y'),x,y,
                nominal_horizon=n('gamepad.ads.response_time_ms')/1000,output_limits=True,**extra)
        else:
            example=follow_response_example(self.policy,n('gamepad.bodylock.response_time_x_ms'),n('gamepad.bodylock.response_time_y_ms'),
                n('gamepad.bodylock.output_limit_x'),n('gamepad.bodylock.output_limit_y'),x,y,**extra)
        g.update(width=width,height=height,body_width=bw,body_height=bh,admission=admission(self.policy,bw,bh,width,height),
            stick=filter_ai_input(example['stick'],n('gamepad.assist.input_deadzone')))
        return g
