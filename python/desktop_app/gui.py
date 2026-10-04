from __future__ import annotations

from project_paths import PROJECT_ROOT

import argparse
import json
import os
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import tomllib

from .runtime import RuntimeManager, tail
from .settings import ConfigStore, UiPreferences, effective, lookup, update_text

GAME_LABELS = {'default': '通用 / COD', 'apex': 'Apex Legends', 'bo3': 'COD：Black Ops III'}
from .fields import CHOICE_LABELS, COMMON_FIELDS, FIELD_GROUPS, GAME_FIELDS, field_value


class AssistantWindow:
    def __init__(self, root, project):
        self.root = root
        self.project = Path(project).resolve()
        self.store = ConfigStore(self.project)
        self.manager = RuntimeManager(self.project)
        self.jobs = queue.Queue()
        self.busy = False
        self.loading = False
        self.drafts = {}
        self.pinned = set()
        self.restart_required = False
        self.pending_request_id = None
        self.preferences = UiPreferences(self.project)
        self.ui_state = self.preferences.path
        self.text, self.document, self.digest = self.store.read()
        self.games = ['default'] + list(self.document.get('games', {}))
        self.game_labels = {key: GAME_LABELS.get(key, key) for key in self.games}
        selected = lookup(self.document, 'runtime.game', 'default')
        ui_settings = self.preferences.read()
        selected = ui_settings.get('game', selected)
        active = self.manager.active()
        if active:
            selected = active['game']
        if selected not in self.games:
            selected = 'default'
        self.game = tk.StringVar(value=self.game_labels[selected])
        self.status_text = tk.StringVar(value='已停止')
        self.device_text = tk.StringVar(value='启动后自动识别手柄')
        self.model_text = tk.StringVar()
        self.config_summary = tk.StringVar()
        self.scope_text = tk.StringVar()
        self.change_summary = tk.StringVar(value='没有待保存的修改')
        self.show_details = tk.BooleanVar(value=False)
        self.notice = tk.StringVar(value='设置保存到 config.toml；支持的参数可直接热重载。')
        self.inputs = []
        self.root.title('手柄助手')
        self.root.geometry('1000x820')
        self.root.minsize(760, 610)
        self.root.configure(bg='#f4f6f9')
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('.', font=('Microsoft YaHei UI', 10))
        style.configure('TFrame', background='#f4f6f9')
        style.configure('TLabel', background='#f4f6f9', foreground='#243149')
        style.configure('Title.TLabel', font=('Microsoft YaHei UI', 21, 'bold'))
        style.configure('Muted.TLabel', foreground='#66758a', font=('Microsoft YaHei UI', 9))
        style.configure('Status.TLabel', font=('Microsoft YaHei UI', 12, 'bold'))
        style.configure('Section.TLabel', font=('Microsoft YaHei UI', 12, 'bold'), foreground='#245bd6')
        style.configure('TButton', padding=(13, 8))
        style.configure('TCheckbutton', background='#f4f6f9')
        style.configure('Primary.TButton', background='#245bd6', foreground='white', padding=(22, 11))
        style.map('Primary.TButton', background=[('active', '#1a4ab4'), ('disabled', '#93a8cf')])
        style.configure('TNotebook.Tab', padding=(16, 8))
        outer = ttk.Frame(root, padding=16)
        outer.pack(fill='both', expand=True)
        ttk.Label(outer, text='手柄助手', style='Title.TLabel').pack(anchor='w')
        ttk.Label(outer, text='选择游戏，核对识别模型与响应曲线，再启动辅助。', style='Muted.TLabel').pack(anchor='w', pady=(0, 10))
        row = ttk.Frame(outer)
        row.pack(fill='x')
        ttk.Label(row, text='游戏').pack(side='left', padx=(0, 12))
        self.selector = ttk.Combobox(row, textvariable=self.game, values=list(self.game_labels.values()), state='readonly', width=27)
        self.selector.pack(side='left')
        self.selector.bind('<<ComboboxSelected>>', lambda _: self.change_game())
        self.primary = ttk.Button(row, text='启动', style='Primary.TButton', command=self.primary_action)
        self.primary.pack(side='right')
        self.stop_button = ttk.Button(row, text='停止主程序', command=lambda: self.run_job('正在停止主程序…', self.manager.stop))
        self.stop_button.pack(side='right', padx=8)
        self.stop_button.state(['disabled'])
        ttk.Label(outer, textvariable=self.config_summary, wraplength=690).pack(anchor='w', pady=(8, 0))
        status = ttk.Frame(outer, padding=(0, 8, 0, 8))
        status.pack(fill='x')
        status_details = ttk.Frame(status)
        status_details.pack(side='left', fill='x', expand=True)
        status_line = ttk.Frame(status_details)
        status_line.pack(fill='x')
        ttk.Label(status_line, textvariable=self.status_text, style='Status.TLabel').pack(side='left')
        ttk.Label(status_line, textvariable=self.device_text, style='Muted.TLabel').pack(side='left', padx=(14, 0))
        ttk.Label(status_details, textvariable=self.model_text, style='Muted.TLabel', wraplength=500).pack(anchor='w', pady=(3, 0))
        fusion_row = ttk.Frame(status)
        fusion_row.pack(side='right')
        self.fusion_text = tk.StringVar(value='Fusion：未开启')
        ttk.Label(fusion_row, textvariable=self.fusion_text, style='Muted.TLabel').pack(anchor='e')
        self.fusion_button = ttk.Button(fusion_row, text='开启 Fusion', command=self.toggle_fusion)
        self.fusion_button.pack(side='right')
        self.notebook = ttk.Notebook(outer)
        self.pages = {}
        for key, label in [('game', '游戏设置'), ('common', '设备与性能')]:
            page = ttk.Frame(self.notebook)
            self.notebook.add(page, text=label)
            canvas = tk.Canvas(page, background='#f4f6f9', highlightthickness=0)
            scrollbar = ttk.Scrollbar(page, orient='vertical', command=canvas.yview)
            canvas.configure(yscrollcommand=scrollbar.set)
            scrollbar.pack(side='right', fill='y')
            canvas.pack(side='left', fill='both', expand=True)
            form = ttk.Frame(canvas, padding=(16, 14))
            window = canvas.create_window((0, 0), window=form, anchor='nw')
            form.bind('<Configure>', lambda _, c=canvas: c.configure(scrollregion=c.bbox('all')))
            canvas.bind('<Configure>', lambda event, c=canvas, win=window: c.itemconfigure(win, width=event.width))
            self.bind_scroll(page, canvas)
            self.pages[key] = form
        settings_context = ttk.Frame(outer)
        settings_context.pack(fill='x', pady=(0, 6))
        ttk.Label(settings_context, textvariable=self.scope_text, style='Muted.TLabel', wraplength=550).pack(side='left', fill='x', expand=True)
        ttk.Checkbutton(settings_context, text='显示详细参数', variable=self.show_details, command=self.toggle_details).pack(side='right')
        learning_page = ttk.Frame(self.notebook, padding=16)
        self.notebook.add(learning_page, text='学习参数')
        self.learning_summary = tk.StringVar(value='应用未运行，暂无学习数据。')
        learning_toolbar = ttk.Frame(learning_page)
        learning_toolbar.pack(fill='x', pady=(0, 12))
        ttk.Button(learning_toolbar, text='导出学习数据', command=self.export_learning).pack(side='right', padx=(12, 0))
        ttk.Label(learning_toolbar, textvariable=self.learning_summary, wraplength=465).pack(side='left', fill='x', expand=True)
        learning_canvas = tk.Canvas(learning_page, background='#f4f6f9', highlightthickness=0)
        learning_scroll = ttk.Scrollbar(learning_page, orient='vertical', command=learning_canvas.yview)
        learning_canvas.configure(yscrollcommand=learning_scroll.set)
        learning_scroll.pack(side='right', fill='y')
        learning_canvas.pack(side='left', fill='both', expand=True)
        learning_content = ttk.Frame(learning_canvas)
        learning_window = learning_canvas.create_window((0, 0), window=learning_content, anchor='nw')
        learning_content.bind('<Configure>', lambda _: learning_canvas.configure(scrollregion=learning_canvas.bbox('all')))
        learning_canvas.bind('<Configure>', lambda event: learning_canvas.itemconfigure(learning_window, width=event.width))
        self.bind_scroll(learning_page, learning_canvas)
        self.learning_table = ttk.Treeview(learning_content, columns=('effective', 'learned', 'confidence', 'samples'), height=4)
        self.learning_table.heading('#0', text='响应区域')
        self.learning_table.column('#0', width=185, minwidth=145)
        for name, label in [('effective', '生效系数'), ('learned', '学习系数'), ('confidence', '置信度'), ('samples', '有效样本')]:
            self.learning_table.heading(name, text=label)
            self.learning_table.column(name, width=100, minwidth=80, anchor='e')
        for index, name in enumerate(('持续跟随 · 普通区', '持续跟随 · 减速区', 'ADS · 普通区', 'ADS · 减速区')):
            self.learning_table.insert('', 'end', iid=str(index), text=name, values=('--', '--', '--', '--'))
        self.learning_table.pack(fill='x')
        ttk.Label(learning_content, text='系数单位：像素 /（有效摇杆 × 秒）。生效系数结合了初值与置信度；学习系数是样本估计。\n'
                  '这是控制器对响应的估计，并非独立测得的游戏灵敏度。没有有效样本时显示“未学习”。\n'
                  '关闭学习会暂停更新，保留当前运行中的估计；重启后使用配置初值。\n'
                  '仅修改学习开关或腰射 AI 倍率会保留学习数据；其他热重载仍会清理。模型等设置需重启。',
                  style='Muted.TLabel', wraplength=660).pack(anchor='w', pady=14)
        footer = ttk.Frame(outer)
        ttk.Label(outer, textvariable=self.change_summary, wraplength=680).pack(side='bottom', anchor='w', pady=(8, 0))
        self.save_button = ttk.Button(footer, text='保存并应用', command=self.save)
        self.save_button.pack(side='left')
        self.raw_button = ttk.Button(footer, text='编辑配置文件', command=self.open_editor)
        self.raw_button.pack(side='left', padx=8)
        ttk.Button(footer, text='查看日志', command=self.show_logs).pack(side='right')
        ttk.Button(footer, text='重新载入', command=self.reload).pack(side='right', padx=8)
        self.apply_button = ttk.Button(footer, text='应用已保存配置', command=self.apply_saved_config)
        self.apply_button.pack(side='left', padx=(0, 8))
        ttk.Label(outer, textvariable=self.notice, style='Muted.TLabel', wraplength=680).pack(side='bottom', anchor='w', pady=(9, 0))
        footer.pack(side='bottom', fill='x', pady=(14, 0))
        self.notebook.pack(fill='both', expand=True)
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.build_forms()
        self.poll_id = self.root.after(100, self.poll)

    def selected_game(self):
        return next(key for key, label in self.game_labels.items() if label == self.game.get())

    def bind_scroll(self, page, canvas):
        # Bind to this window only; child widgets keep their own wheel behavior.
        def scroll(event):
            widget = event.widget
            while widget is not None:
                if widget == page:
                    if event.widget.winfo_class() != 'TCombobox':
                        canvas.yview_scroll(-int(event.delta / 120), 'units')
                    return
                widget = getattr(widget, 'master', None)
        self.root.bind('<MouseWheel>', scroll, add='+')

    def toggle_details(self):
        for section in self.detail_sections:
            if self.show_details.get():
                section.pack(fill='x', pady=(0, 20))
            else:
                section.pack_forget()

    def build_forms(self):
        self.loading = True
        self.inputs = []
        self.variables = {}
        self.field_specs = {}
        self.sources = {}
        self.detail_sections = []
        game = self.selected_game()
        native_defaults = self.manager.inspect_defaults(game)
        common_defaults = self.manager.inspect_defaults('default') if game != 'default' else native_defaults
        for scope, fields in [('game', GAME_FIELDS), ('common', COMMON_FIELDS)]:
            form = self.pages[scope]
            for child in form.winfo_children():
                child.destroy()
            data = effective(self.document, game) if scope == 'game' else self.document
            defaults = native_defaults if scope == 'game' else common_defaults
            groups = []
            available = {field[0]: field for field in fields}
            for title, description, detailed, paths in FIELD_GROUPS:
                members = [available[path] for path in paths if path in available]
                if not members:
                    continue
                section = ttk.Frame(form)
                section.pack(fill='x', pady=(0, 20))
                ttk.Label(section, text=title, style='Section.TLabel').pack(anchor='w')
                ttk.Label(section, text=description, style='Muted.TLabel', wraplength=580).pack(anchor='w', pady=(4, 8))
                body = ttk.Frame(section)
                body.pack(fill='x')
                body.columnconfigure(1, weight=1)
                groups.extend((body, row, field) for row, field in enumerate(members))
                if detailed:
                    self.detail_sections.append(section)
            for body, row, field in groups:
                path, label, kind, fallback, limits = field
                target = f'games.{game}.{path}' if scope == 'game' and game != 'default' else path
                original = lookup(data, path, defaults.get(path, fallback))
                value = self.drafts[target][1] if target in self.drafts else original
                variable = tk.BooleanVar(value=value) if kind is bool else tk.StringVar(value=str(value))
                self.variables[target] = variable
                self.field_specs[target] = field
                ttk.Label(body, text=label).grid(row=row, column=0, sticky='w', padx=(0, 16), pady=7)
                if kind is bool:
                    widget = ttk.Checkbutton(body, text='启用', variable=variable)
                elif kind is str and limits:
                    # Canonical values remain in variables and persisted TOML.
                    display = tk.StringVar(value=CHOICE_LABELS.get(value, value))
                    widget = ttk.Combobox(body, textvariable=display, values=[CHOICE_LABELS.get(v, v) for v in limits], state='readonly', width=20)
                    widget.bind('<<ComboboxSelected>>', lambda _, d=display, v=variable, options=limits:
                                v.set(next(x for x in options if CHOICE_LABELS.get(x, x) == d.get())))
                    variable.trace_add('write', lambda *_args, d=display, v=variable: d.set(CHOICE_LABELS.get(v.get(), v.get())))
                else:
                    widget = ttk.Entry(body, textvariable=variable)
                widget.grid(row=row, column=1, sticky='ew', pady=7)
                self.inputs.append(widget)
                if path.endswith('model_path'):
                    button = ttk.Button(body, text='选择文件', command=lambda v=variable: self.choose_model(v))
                    button.grid(row=row, column=2, padx=(8, 0))
                    self.inputs.append(button)
                if scope == 'game' and game != 'default':
                    inherited = lookup(self.document, target) is None
                    source = tk.StringVar(value='待保存 · 游戏专属' if target in self.drafts else '继承通用' if inherited else '游戏专属')
                    self.sources[target] = source
                    ttk.Label(body, textvariable=source, style='Muted.TLabel').grid(row=row, column=3, padx=(12, 0))
                    if inherited:
                        button = ttk.Button(body, text='设为专属', command=lambda p=target: self.pin_field(p))
                        button.grid(row=row, column=4, padx=(8, 0))
                        self.inputs.append(button)
                variable.trace_add('write', lambda *_args, p=target, f=field, v=variable, original=original:
                    self.edit_field(p, f, v.get(), original))
        self.loading = False
        self.toggle_details()
        self.update_model()

    def pin_field(self, path):
        self.pinned.add(path)
        field = self.field_specs[path]
        self.edit_field(path, field, self.variables[path].get(), None)

    def edit_field(self, path, field, value, original):
        if self.loading:
            return
        try:
            changed = path in self.pinned or field_value(field, value) != original
        except ValueError:
            changed = True
        if changed:
            self.drafts[path] = (field, value)
        else:
            self.drafts.pop(path, None)
        if path in self.sources:
            self.sources[path].set('待保存 · 游戏专属' if changed else '继承通用' if lookup(self.document, path) is None else '游戏专属')
        self.notice.set('有未保存的修改；保存时尝试热重载，不支持的参数会提示重启。' if self.drafts else '设置保存到 config.toml；支持的参数可直接热重载。')
        self.update_model()

    def change_game(self):
        if not self.busy:
            self.build_forms()
            try:
                self.preferences.save_game(self.selected_game())
            except OSError as error:
                self.notice.set('游戏选择未能记住：' + str(error))

    def update_model(self):
        game = self.selected_game()
        path = f'games.{game}.runtime.vision.model_path' if game != 'default' else 'runtime.vision.model_path'
        model = self.drafts[path][1] if path in self.drafts else lookup(effective(self.document, game), 'runtime.vision.model_path', '')
        self.model_text.set('模型：' + (Path(model).name if model else '尚未设置'))
        curve_path = f'games.{game}.gamepad.aim_response_curve.algorithm' if game != 'default' else 'gamepad.aim_response_curve.algorithm'
        curve = self.variables[curve_path].get()
        self.config_summary.set(f'正在编辑：{self.game.get()}  ·  工具曲线：{CHOICE_LABELS.get(curve, curve)}' +
                                ('  ·  含未保存修改' if path in self.drafts or curve_path in self.drafts else '  ·  已保存配置'))
        self.scope_text.set('通用 / COD 的游戏参数也会被其他游戏继承；需要不同值时，在对应游戏中单独保存。' if game == 'default' else
                            '当前页只修改此游戏；“设备与性能”中的参数由所有游戏共用。')
        scopes = {}
        for target in self.drafts:
            key = target.split('.')[1] if target.startswith('games.') else 'default'
            label = self.game_labels.get(key, key) if target.startswith('games.') else '通用配置'
            scopes[label] = scopes.get(label, 0) + 1
        self.change_summary.set('待保存：' + '；'.join(f'{label} {count} 项' for label, count in scopes.items()) + '。保存会写入以上所有修改。' if scopes else '没有待保存的修改')

    def choose_model(self, variable):
        filename = filedialog.askopenfilename(parent=self.root, title='选择识别模型', filetypes=[('TensorRT 模型', '*.engine'), ('所有文件', '*.*')])
        if filename:
            try:
                filename = Path(filename).resolve().relative_to(self.project).as_posix()
            except ValueError:
                filename = Path(filename).as_posix()
            variable.set(str(filename))

    def collect_changes(self):
        return {path: field_value(field, value) for path, (field, value) in self.drafts.items()}

    def commit(self, changes):
        if changes:
            candidate = update_text(self.text, changes)
            data = tomllib.loads(candidate)
            games = ['default'] + list(data.get('games', {}))
            self.store.save(candidate, self.digest, lambda path: self.manager.validate(path, games))

    def run_job(self, label, work, done=None):
        if self.busy:
            return
        self.busy = True
        self.notice.set(label)
        for widget in self.inputs + [self.primary, self.stop_button, self.selector, self.save_button, self.raw_button, self.apply_button, self.fusion_button]:
            widget.state(['disabled'])
        def worker():
            try:
                result = work()
                self.jobs.put((done, result, None))
            except Exception as error:
                self.jobs.put((None, None, str(error)))
        threading.Thread(target=worker, daemon=True).start()

    def saved(self, _result=None):
        self.text, self.document, self.digest = self.store.read()
        self.drafts.clear()
        self.pinned.clear()
        self.build_forms()
        self.notice.set('配置已保存；运行中的参数以热重载结果为准。' if self.manager.active() else '已保存，下次启动生效。')

    def started(self, result):
        self.saved(result)
        self.restart_required = False
        self.notice.set('已启动。关闭窗口后应用会继续运行；停止请使用上方按钮。')

    def save(self):
        try:
            changes = self.collect_changes()
        except ValueError as error:
            messagebox.showerror('设置未保存', str(error), parent=self.root)
            return
        if changes:
            def work():
                self.commit(changes)
                active = self.manager.active()
                affects_active = active and any(not path.startswith('games.') or path.startswith(f'games.{active["game"]}.') for path in changes)
                if affects_active:
                    try:
                        return self.manager.reload_config()
                    except Exception as error:
                        return {'apply_error': str(error)}
            self.run_job('正在校验、保存并应用…', work, self.applied)

    def applied(self, result):
        self.saved()
        self.pending_request_id = None
        if not result:
            return
        if 'apply_error' in result:
            self.notice.set('配置已保存，尚未应用：' + result['apply_error'])
            return
        status = result['status']
        self.restart_required = status == 3
        if status == 2:
            learning_action = '保留' if 'learning preserved' in result.get('message', '') else '清理'
            self.notice.set(f'已热重载，配置版本 {result["revision"]}；响应学习数据已{learning_action}。')
        elif status == 1:
            self.pending_request_id = result['request_id']
            self.notice.set('重载已受理，等待新视觉帧；当前尚未全部生效。')
        elif status == 3:
            self.notice.set('配置已保存，这些修改需要重启：' + result['message'].removeprefix('restart required: '))
        else:
            self.notice.set('配置未应用：' + result['message'])

    def apply_saved_config(self):
        if self.drafts:
            self.save()
        elif self.manager.active():
            self.run_job('正在热重载已保存配置…', self.manager.reload_config, self.applied)
        else:
            self.notice.set('应用未运行，保存的配置将在启动时生效。')

    def toggle_fusion(self):
        enabled = not bool(self.manager.fusion_state())
        self.run_job('正在打开 Fusion…' if enabled else '正在关闭 Fusion…', lambda: self.manager.set_fusion(enabled),
                     lambda _: self.notice.set('Fusion 已打开。' if enabled else 'Fusion 已关闭。'))

    def export_learning(self):
        data = self.manager.learning()
        if not data:
            self.notice.set('当前没有可导出的原生学习数据。')
            return
        from datetime import datetime, timezone
        folder = self.project / 'runs/desktop/learning'
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / (datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
        value = {'exported_at_utc': datetime.now(timezone.utc).isoformat(), 'runtime': self.manager.active(),
                 'units': 'px / (effective_stick * second)', 'region_order': ['body_free', 'body_slow', 'ads_free', 'ads_slow'],
                 'measurement_kind': 'controller response estimate, not independent game calibration', 'learning': data}
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
        self.notice.set('已导出学习数据：' + str(path))

    def primary_action(self):
        active = self.manager.active()
        game = self.selected_game()
        if active and active['game'] == game and self.drafts:
            self.save()
            return
        if active and active['game'] == game and not self.restart_required:
            self.run_job('正在停止…', self.manager.stop)
            return
        try:
            changes = self.collect_changes()
        except ValueError as error:
            messagebox.showerror('无法启动', str(error), parent=self.root)
            return
        def work():
            self.commit(changes)
            if self.manager.active():
                self.manager.stop()
                deadline = time.monotonic() + 8
                while self.manager.active():
                    if time.monotonic() >= deadline:
                        raise ValueError('程序尚未退出，请查看日志；不会同时启动第二个控制实例。')
                    time.sleep(.05)
            _, data, _ = self.store.read()
            record = self.manager.start(game, data)
            return record
        self.run_job('正在切换并启动…' if active else '正在启动…', work, self.started)

    def reload(self):
        if self.busy:
            return
        if self.drafts and not messagebox.askyesno('重新载入', '重新载入会放弃未保存的修改，是否继续？', parent=self.root):
            return
        try:
            self.saved()
            self.notice.set('已重新载入配置。')
        except Exception as error:
            messagebox.showerror('读取配置失败', str(error), parent=self.root)

    def open_editor(self):
        if self.busy:
            return
        if self.drafts:
            messagebox.showinfo('高级配置', '请先保存当前表单修改，再打开高级配置。', parent=self.root)
            return
        original, _, digest = self.store.read()
        window = tk.Toplevel(self.root)
        window.title('高级配置 · config.toml')
        window.geometry('930x690')
        frame = ttk.Frame(window, padding=16)
        frame.pack(fill='both', expand=True)
        ttk.Label(frame, text='共用配置与所有游戏分块；保存前会校验，原文件自动备份。').pack(anchor='w', pady=(0, 10))
        editor = tk.Text(frame, font=('Consolas', 10), wrap='none', undo=True)
        editor.pack(fill='both', expand=True)
        editor.insert('1.0', original)
        def save_raw():
            candidate = editor.get('1.0', 'end-1c')
            try:
                data = tomllib.loads(candidate)
                games = ['default'] + list(data.get('games', {}))
            except Exception as error:
                messagebox.showerror('配置格式有误', str(error), parent=window)
                return
            def finished(_):
                if window.winfo_exists():
                    window.destroy()
                self.applied(_)
            def work_raw():
                self.store.save(candidate, digest, lambda path: self.manager.validate(path, games))
                if self.manager.active():
                    try:
                        return self.manager.reload_config()
                    except Exception as error:
                        return {'apply_error': str(error)}
            self.run_job('正在校验并应用高级配置…', work_raw, finished)
        ttk.Button(frame, text='校验并保存', command=save_raw).pack(anchor='e', pady=(12, 0))

    def show_logs(self):
        status = self.manager.status()
        record = status.get('record') or status.get('last_record')
        window = tk.Toplevel(self.root)
        window.title('运行日志')
        window.geometry('900x580')
        editor = tk.Text(window, font=('Consolas', 10), wrap='word', padx=12, pady=12)
        editor.pack(fill='both', expand=True)
        value = '尚无运行日志。'
        if record:
            value = tail(record['stdout_path']) + '\n' + tail(record['stderr_path'])
        editor.insert('1.0', value)
        editor.configure(state='disabled')

    def poll(self):
        try:
            while True:
                done, result, error = self.jobs.get_nowait()
                self.busy = False
                for widget in self.inputs + [self.primary, self.selector, self.save_button, self.raw_button, self.apply_button, self.fusion_button]:
                    widget.state(['!disabled'])
                if error:
                    self.notice.set('操作未完成，请检查提示。')
                    messagebox.showerror('操作未完成', error, parent=self.root)
                elif done:
                    done(result)
                else:
                    self.notice.set('已发送正常退出请求。')
        except queue.Empty:
            pass
        if not self.busy:
            status = self.manager.status()
            phase = status['phase']
            record = status.get('record')
            fusion = self.manager.fusion_state()
            self.fusion_text.set('Fusion：已开启' + ('，等待主程序' if not record else '') if fusion else
                                 'Fusion：未开启' + ('（主程序启动后可开启）' if not record else ''))
            self.fusion_button.configure(text='关闭 Fusion' if fusion else '开启 Fusion')
            self.fusion_button.state(['!disabled'] if fusion or status.get('initialized') else ['disabled'])
            learning = self.manager.learning() if record else None
            if learning:
                if self.pending_request_id is not None and learning['completed_id'] == self.pending_request_id and learning['status'] == 2:
                    learning_action = '保留' if 'learning preserved' in learning.get('message', '') else '清理'
                    self.notice.set(f'已热重载，配置版本 {learning["revision"]}；响应学习数据已{learning_action}。')
                    self.pending_request_id = None
                self.restart_required = learning['status'] == 3
                self.learning_summary.set(f'当前配置版本：{learning["revision"]}  ·  手动输入：{learning["manual_fire_input"]}  ·  自动输出：{learning["fire_output"]}' +
                                          ('  ·  等待配置提交' if learning['status'] == 1 else ''))
                for index, values in enumerate(learning['regions']):
                    self.learning_table.item(str(index), values=(f'{values["effective"]:.2f}',
                        f'{values["learned"]:.2f}' if values['samples'] else '未学习',
                        f'{values["confidence"]:.1%}', values['samples']))
            else:
                self.learning_summary.set('当前程序尚未提供学习数据。' if record else '应用未运行，暂无学习数据。')
                for index in range(4):
                    self.learning_table.item(str(index), values=('--', '--', '--', '--'))
            label = self.game_labels.get(record['game'], record['game']) if record else ''
            descriptions = {'stopped': '已停止', 'starting': '正在启动…', 'running': '运行中',
                            'waiting_device': '等待手柄', 'stopping': '正在停止…', 'failed': '启动失败'}
            self.status_text.set(descriptions[phase] + (' · ' + label if label else ''))
            if phase == 'failed':
                error_lines = status['error'].strip().splitlines()
                self.notice.set(error_lines[-1] if error_lines else '启动失败，请查看日志。')
            self.device_text.set(('启动识别：' + status['device'] if record else '启动后自动识别手柄') +
                                 ('  ·  虚拟输出已连接' if status.get('virtual_connected') else '') +
                                 ('。请连接手柄后停止并重启。' if phase == 'waiting_device' else ''))
            if record:
                text = '切换并启动' if record['game'] != self.selected_game() else '保存并应用' if self.drafts else '重启应用' if self.restart_required else '停止运行'
                self.primary.configure(text=text)
                self.stop_button.state(['!disabled'])
            else:
                self.primary.configure(text='保存并启动' if self.drafts else '启动 ' + self.game.get())
                self.stop_button.state(['disabled'])
            if phase == 'stopping':
                self.primary.state(['disabled'])
                self.stop_button.state(['disabled'])
            else:
                self.primary.state(['!disabled'])
        self.poll_id = self.root.after(500, self.poll)

    def close(self):
        if self.busy:
            messagebox.showinfo('操作进行中', '请等待当前操作完成后再关闭窗口。', parent=self.root)
            return
        if self.drafts and not messagebox.askyesno('关闭窗口', '还有未保存的修改，是否放弃这些修改并关闭？', parent=self.root):
            return
        try:
            self.preferences.save_game(self.selected_game())
        except OSError as error:
            messagebox.showerror('游戏选择未保存', str(error), parent=self.root)
            return
        self.root.after_cancel(self.poll_id)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=str(PROJECT_ROOT))
    parser.add_argument('--action', choices=['start', 'stop', 'status', 'migrate', 'preview-start', 'preview-stop'])
    parser.add_argument('--game', default='default')
    args = parser.parse_args()
    project = Path(args.root).resolve()
    if args.action:
        store = ConfigStore(project)
        if args.action == 'migrate':
            print(json.dumps({'migrated': store.migrate_games()}))
            return
        manager = RuntimeManager(project)
        if args.action == 'start':
            active = manager.active()
            print(json.dumps(active if active and active['game'] == args.game else manager.start(args.game, store.read()[1])))
        elif args.action == 'stop':
            manager.stop()
            deadline = time.monotonic() + 8
            while manager.active():
                if time.monotonic() >= deadline:
                    raise RuntimeError('退出请求已发送，但程序尚未退出。')
                time.sleep(.05)
        elif args.action.startswith('preview-'):
            data = effective(store.read()[1], args.game)
            model = project / lookup(data, 'runtime.vision.model_path', '')
            print(json.dumps({'action': args.action.replace('-', '_'), 'game': args.game,
                'executable_path': str(manager.executable), 'config_path': str(store.path),
                'state_path': str(manager.state_path), 'model_path': str(model), 'model_exists': model.is_file(),
                'arguments': ['--config', str(store.path), '--game', args.game],
                'fusion_channel_enabled': True, 'fusion_session': os.environ.get('FUSION_SESSION', 'dev')}))
        else:
            print(json.dumps(manager.status(), ensure_ascii=False))
        return
    # One settings writer/window per project; the native runtime has its own
    # process and remains alive when the window closes.
    import ctypes
    import hashlib
    from ctypes import wintypes
    from .runtime import kernel
    lock_name = 'Local\\gamepad_assistant_' + hashlib.sha256(os.path.normcase(str(project)).encode()).hexdigest()[:16]
    ui_lock = kernel.CreateMutexW(None, False, lock_name)
    if not ui_lock:
        raise OSError('无法创建界面实例锁')
    if ctypes.get_last_error() == 183:
        user32 = ctypes.WinDLL('user32')
        user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
        user32.FindWindowW.restype = wintypes.HWND
        user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        user32.SetForegroundWindow.argtypes = [wintypes.HWND]
        window = user32.FindWindowW(None, '手柄助手')
        if window:
            user32.ShowWindow(window, 9)
            user32.SetForegroundWindow(window)
        kernel.CloseHandle(ui_lock)
        return
    root = tk.Tk()
    try:
        AssistantWindow(root, project)
    except Exception as error:
        root.withdraw()
        messagebox.showerror('手柄助手无法打开', str(error), parent=root)
        root.destroy()
        raise
    try:
        root.mainloop()
    finally:
        kernel.CloseHandle(ui_lock)


if __name__ == '__main__':
    main()
