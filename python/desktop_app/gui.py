from __future__ import annotations

from project_paths import PROJECT_ROOT

import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk
import tomllib

from .components import ACCENT, INK, RAIL, ScrollSurface, StrengthInput, configure_theme, section
from .curve_editor import CurveEditor, ProfileStrip
from .curves import CurveLibrary, curve_document, decode_points, encode_points, read_curve, seed_points
from .profiles import ProfileLibrary
from .runtime import RuntimeManager, tail
from .settings import ConfigStore, UiPreferences, effective, lookup, update_text

GAME_LABELS = {'default': '通用 / COD', 'apex': 'Apex Legends', 'bo3': 'COD：Black Ops III'}
from .fields import CHOICE_LABELS, COMMON_FIELDS, GAME_FIELDS, field_value


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
        self.profile_library = ProfileLibrary(self.project)
        self.curve_library = CurveLibrary(self.project)
        self.profile_entries = self.profile_library.entries(self.document, GAME_LABELS)
        self.profile_drafts = {}
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
        profile_id = ui_settings.get('profile_id', selected)
        if active:
            profile_id = next((entry['id'] for entry in self.profile_entries if
                              entry['path'].resolve() == Path(active.get('config_path', self.store.path)).resolve() and
                              entry['game'] == selected), selected)
        self.profile = next((entry for entry in self.profile_entries if entry['id'] == profile_id),
                            next(entry for entry in self.profile_entries if entry['id'] == selected))
        if self.profile.get('legacy'):
            self.profile = next(entry for entry in self.profile_entries if entry['id'] == selected)
        if not self.profile.get('legacy'):
            self.store = ConfigStore(self.project, self.profile['path'])
            self.text, self.document, self.digest = self.store.read()
            self.games = ['default'] + list(self.document.get('games', {}))
            self.game_labels = {key: GAME_LABELS.get(key, key) for key in self.games}
            selected = self.profile['game']
        self.game = tk.StringVar(value=self.game_labels[selected])
        self.status_text = tk.StringVar(value='已停止')
        self.device_text = tk.StringVar(value='启动后自动识别手柄')
        self.model_text = tk.StringVar()
        self.config_summary = tk.StringVar()
        self.change_summary = tk.StringVar(value='没有待保存的修改')
        self.show_details = tk.BooleanVar(value=False)
        self.notice = tk.StringVar(value='核对模型与曲线后，点击“启动配置”。')
        self.inputs = []
        self.build_shell()
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.build_forms()
        self.show_page('prepare')
        if not self.manager.executable.is_file():
            self.notice.set('原生程序尚未就绪；请先构建主程序，再重新载入。')
        self.poll_id = self.root.after(100, self.poll)

    def build_shell(self):
        self.root.title('手柄助手')
        self.root.geometry('1120x840')
        self.root.minsize(840, 680)
        self.style = configure_theme()
        workspace = ttk.Frame(self.root, padding=(28, 20, 28, 18))
        workspace.pack(fill='both', expand=True)
        brand = ttk.Frame(workspace)
        brand.pack(fill='x', pady=(0, 16))
        ttk.Label(brand, text='手柄助手', style='Section.TLabel').pack(side='left')
        ttk.Label(brand, text=' /  配置库', style='Muted.TLabel').pack(side='left', padx=10)
        ttk.Label(brand, textvariable=self.status_text, style='Accent.TLabel').pack(side='right')
        strip_row = ttk.Frame(workspace)
        strip_row.pack(fill='x', pady=(0, 18))
        profile_tools = ttk.Frame(strip_row)
        profile_tools.pack(side='right', padx=(16, 0))
        self.new_profile_button = ttk.Button(profile_tools, text='＋ 新建配置', command=self.new_profile)
        self.new_profile_button.pack(fill='x')
        self.copy_profile_button = ttk.Button(profile_tools, text='复制当前配置', command=lambda:self.new_profile(duplicate=True))
        self.copy_profile_button.pack(fill='x', pady=(6, 0))
        self.profile_strip = ProfileStrip(strip_row, self.select_profile)
        self.profile_strip.pack(side='left',fill='x',expand=True)
        self.profile_strip.set_profiles(self.profile_entries,self.profile['id'])
        title_row = ttk.Frame(workspace)
        title_row.pack(fill='x', pady=(0, 5))
        self.page_title = tk.StringVar()
        self.page_description = tk.StringVar()
        ttk.Label(title_row, textvariable=self.page_title, style='Title.TLabel').pack(side='left')
        ttk.Button(title_row, text='重命名', style='Link.TButton', command=self.rename_profile).pack(side='right')
        ttk.Label(workspace, textvariable=self.page_description, style='Muted.TLabel', wraplength=720).pack(anchor='w', pady=(0, 15))
        navigation = ttk.Frame(workspace)
        navigation.pack(fill='x', pady=(0, 12))
        self.navigation = {}
        for key, label in [('prepare', '配置概览'), ('assist', '辅助参数'), ('curve', '响应曲线'), ('common', '运行设置'), ('learning', '学习与诊断')]:
            button = ttk.Button(navigation, text=label, style='Nav.TButton', command=lambda k=key:self.show_page(k))
            button.pack(side='left',padx=(0,5))
            self.navigation[key]=button
        self.content_host = ttk.Frame(workspace)
        footer = ttk.Frame(workspace)
        footer.pack(side='bottom',fill='x',pady=(12,0))
        ttk.Separator(footer).pack(fill='x',pady=(0,12))
        actions=ttk.Frame(footer)
        actions.pack(fill='x')
        self.primary=ttk.Button(actions,text='启动配置',style='Primary.TButton',command=self.primary_action)
        self.primary.pack(side='right')
        self.save_button=ttk.Button(actions,text='保存为配置' if self.profile.get('legacy') else '保存修改',command=self.save)
        self.save_button.pack(side='right',padx=10)
        self.stop_button=ttk.Button(actions,text='停止',command=lambda:self.run_job('正在停止…',self.manager.stop))
        self.stop_button.pack(side='right')
        self.stop_button.state(['disabled'])
        ttk.Label(actions,textvariable=self.change_summary,style='Muted.TLabel',wraplength=360).pack(side='left',fill='x',expand=True)
        ttk.Label(footer,textvariable=self.notice,style='Muted.TLabel',wraplength=760).pack(fill='x',pady=(10,0))
        self.content_host.pack(fill='both',expand=True)
        self.page_frames={}
        self.pages={}
        for key in ('prepare','assist','common'):
            page=ttk.Frame(self.content_host)
            self.page_frames[key]=page
            if key=='prepare':
                toolbar=ttk.Frame(page)
                toolbar.pack(fill='x',pady=(0,12))
                ttk.Label(toolbar,text='游戏场景',style='Muted.TLabel').pack(side='left')
                self.selector=ttk.Combobox(toolbar,textvariable=self.game,values=list(self.game_labels.values()),state='readonly',width=26)
                self.selector.pack(side='left',padx=10)
                self.selector.bind('<<ComboboxSelected>>',lambda _:self.change_game())
                ttk.Label(toolbar,textvariable=self.device_text,style='Muted.TLabel',wraplength=350).pack(side='right')
            if key in ('assist','common'):
                toolbar=ttk.Frame(page)
                toolbar.pack(fill='x',pady=(0,12))
                ttk.Checkbutton(toolbar,text='展开详细参数',variable=self.show_details,command=self.toggle_details).pack(side='right')
                ttk.Label(toolbar,text='此配置的辅助参数' if key=='assist' else '此配置的运行条件',style='Muted.TLabel').pack(side='left')
            if key=='common':
                tools=ttk.Frame(page)
                tools.pack(side='bottom',fill='x',pady=(12,0))
                self.raw_button=ttk.Button(tools,text='配置源文件',command=self.open_editor)
                self.raw_button.pack(side='left')
                self.reload_button=ttk.Button(tools,text='重新载入',command=self.reload)
                self.reload_button.pack(side='left',padx=6)
                self.apply_button=ttk.Button(tools,text='重新应用',command=self.apply_saved_config)
                self.apply_button.pack(side='left')
            surface=ScrollSurface(page,self.root)
            surface.pack(fill='both',expand=True)
            self.pages[key]=surface.content
        curve_page=ttk.Frame(self.content_host)
        self.page_frames['curve']=curve_page
        curve_page.bind('<Configure>', self.resize_curve)
        curve_surface=ScrollSurface(curve_page,self.root)
        curve_surface.pack(fill='both',expand=True)
        self.curve_content=curve_surface.content
        learning_page=ttk.Frame(self.content_host)
        self.page_frames['learning']=learning_page
        self.build_learning(learning_page)

    def show_page(self, key):
        self.current_page = key
        titles = {
            'prepare': ('准备本次游戏', '确认识别模型与响应曲线，准备好后启动。'),
            'assist': ('调整辅助力度', '按开镜、持续跟随与开火分别调整；滑杆与数值输入同步。'),
            'common': ('连接与运行设置', '配置手柄、捕获与性能。这些设置会被所有游戏使用。'),
            'curve': ('编辑响应曲线', '直接拖动控制点；横轴为摇杆幅度，纵轴为归一化响应。'),
            'learning': ('查看响应学习', '查看控制器在本次运行中积累的响应估计。'),
        }
        self.page_title.set(self.profile['name'] if key == 'prepare' else titles[key][0])
        self.page_description.set(titles[key][1])
        for page_key, page in self.page_frames.items():
            page.pack_forget()
            self.navigation[page_key].configure(style='Selected.Nav.TButton' if page_key == key else 'Nav.TButton')
        self.page_frames[key].pack(fill='both', expand=True)

    def build_learning(self, page):
        toolbar = ttk.Frame(page)
        toolbar.pack(fill='x', pady=(0, 16))
        self.fusion_text = tk.StringVar(value='Fusion：未开启')
        ttk.Label(toolbar, textvariable=self.fusion_text, style='Muted.TLabel').pack(side='left')
        self.fusion_button = ttk.Button(toolbar, text='开启 Fusion', command=self.toggle_fusion)
        self.fusion_button.pack(side='right')
        self.learning_summary = tk.StringVar(value='启动辅助后，学习数据会显示在这里。')
        surface = ScrollSurface(page, self.root)
        surface.pack(fill='both', expand=True)
        content = surface.content
        ttk.Label(content, textvariable=self.learning_summary, wraplength=480).pack(anchor='w', pady=(0, 16))
        self.learning_table = ttk.Treeview(content, columns=('effective', 'learned', 'confidence', 'samples'), height=4)
        self.learning_table.heading('#0', text='响应区域')
        self.learning_table.column('#0', width=155, minwidth=125)
        for name, label in [('effective', '生效系数'), ('learned', '学习系数'), ('confidence', '置信度'), ('samples', '样本')]:
            self.learning_table.heading(name, text=label)
            self.learning_table.column(name, width=85, minwidth=65, anchor='e')
        for index, name in enumerate(('跟随 · 普通区', '跟随 · 减速区', '开镜 · 普通区', '开镜 · 减速区')):
            self.learning_table.insert('', 'end', iid=str(index), text=name, values=('--', '--', '--', '--'))
        self.learning_table.pack(fill='x')
        ttk.Label(content, text='这些系数是控制器的响应估计，单位为像素 /（有效摇杆 × 秒），并非独立测得的游戏灵敏度。\n\n'
                  '关闭学习会暂停更新，保留本次运行的估计；重启后使用配置初值。重载后是否保留数据，以原生确认为准。',
                  style='Muted.TLabel', wraplength=480).pack(anchor='w', pady=18)
        tools = ttk.Frame(content)
        tools.pack(fill='x')
        ttk.Button(tools, text='导出学习数据', command=self.export_learning).pack(side='left')
        ttk.Button(tools, text='查看运行日志', command=self.show_logs).pack(side='left', padx=8)

    def selected_game(self):
        return next(key for key, label in self.game_labels.items() if label == self.game.get())

    def refresh_profiles(self):
        legacy = ConfigStore(self.project).read()[1]
        self.profile_entries = self.profile_library.entries(legacy, GAME_LABELS)
        self.profile_strip.set_profiles(self.profile_entries, self.profile['id'])

    def select_profile(self, identifier):
        if self.busy or identifier == self.profile['id']:
            return
        candidate = next(entry for entry in self.profile_entries if entry['id'] == identifier)
        store = ConfigStore(self.project, candidate['path'])
        try:
            text, document, digest = store.read()
            self.manager.inspect_defaults(candidate['game'], text)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            messagebox.showerror('无法打开配置', str(error), parent=self.root)
            return
        self.profile_drafts[self.profile['id']] = (self.drafts, self.pinned)
        self.profile = candidate
        self.store = store
        self.text, self.document, self.digest = text, document, digest
        self.drafts, self.pinned = self.profile_drafts.pop(identifier, ({}, set()))
        self.games = ['default'] + list(document.get('games', {}))
        self.game_labels = {key: GAME_LABELS.get(key, key) for key in self.games}
        self.selector.configure(values=list(self.game_labels.values()))
        self.game.set(self.game_labels[candidate['game']])
        self.build_forms()
        self.profile_strip.set_profiles(self.profile_entries, identifier)
        self.show_page('prepare')
        try:
            self.preferences.save_profile(identifier, self.selected_game())
        except OSError as error:
            self.notice.set('选择记忆未保存：' + str(error))

    def new_profile(self, duplicate=False):
        if self.busy:
            return
        window = tk.Toplevel(self.root)
        window.title('复制配置' if duplicate else '新建配置')
        window.geometry('460x270')
        window.transient(self.root)
        body = ttk.Frame(window, padding=24)
        body.pack(fill='both', expand=True)
        ttk.Label(body, text='配置名称', style='Section.TLabel').pack(anchor='w')
        name = tk.StringVar(value=self.profile['name'] + ' 副本' if duplicate else '')
        entry = ttk.Entry(body, textvariable=name)
        entry.pack(fill='x', pady=(8, 14))
        ttk.Label(body, text='游戏场景', style='Muted.TLabel').pack(anchor='w')
        game = tk.StringVar(value=self.game.get())
        ttk.Combobox(body, textvariable=game, values=list(self.game_labels.values()), state='readonly').pack(fill='x', pady=(6, 14))
        def create():
            chosen = next(key for key, label in self.game_labels.items() if label == game.get())
            try:
                base = update_text(self.text, self.collect_changes()) if duplicate else ConfigStore(self.project).read()[0]
            except ValueError as error:
                messagebox.showerror('无法创建配置', str(error), parent=window)
                return
            # Tk values must be captured on the UI thread before the worker.
            profile_name = name.get()
            def create_profile():
                return self.profile_library.create(profile_name, chosen, base,
                    lambda path: self.manager.validate(path, ['default'] + list(tomllib.loads(path.read_text(encoding='utf-8')).get('games', {}))))
            def finished(profile):
                if window.winfo_exists():
                    window.destroy()
                self.refresh_profiles()
                self.select_profile(profile['id'])
                self.notice.set('独立配置已创建，可继续调整参数与曲线。')
            self.run_job('正在创建独立配置…', create_profile, finished)
        ttk.Button(body, text='创建配置', style='Primary.TButton', command=create).pack(anchor='e')
        entry.focus_set()

    def rename_profile(self):
        name = simpledialog.askstring('重命名配置', '配置名称', initialvalue=self.profile['name'], parent=self.root)
        if name is None:
            return
        try:
            self.profile_library.rename(self.profile, name)
            self.profile['name'] = name.strip()
            self.refresh_profiles()
            self.show_page(self.current_page)
        except (OSError, ValueError) as error:
            messagebox.showerror('无法重命名', str(error), parent=self.root)

    def owns_active_config(self, active):
        return bool(active) and Path(active.get('config_path', self.project / 'config.toml')).resolve() == self.store.path.resolve()

    def toggle_details(self):
        for frame in self.detail_sections:
            if self.show_details.get():
                frame.pack(fill='x', pady=(0, 20))
            else:
                frame.pack_forget()

    def build_forms(self):
        game = self.selected_game()
        native_defaults = self.manager.inspect_defaults(game, self.text)
        common_defaults = self.manager.inspect_defaults('default', self.text) if game != 'default' else native_defaults
        self.loading = True
        self.inputs = []
        self.variables = {}
        self.field_specs = {}
        self.sources = {}
        self.detail_sections = []
        for form in self.pages.values():
            for child in form.winfo_children():
                child.destroy()
        self.readiness_text = tk.StringVar()
        intro = ttk.Frame(self.pages['prepare'])
        intro.pack(fill='x', pady=(0, 18))
        ttk.Label(intro, textvariable=self.config_summary, style='Accent.TLabel', wraplength=480).pack(anchor='w', pady=(0, 8))
        ttk.Label(intro, textvariable=self.model_text, style='Muted.TLabel', wraplength=480).pack(anchor='w')
        sections = [
            ('prepare', '模型与游戏响应', '模型用于识别目标；工具曲线与游戏内的响应设置配合使用。', False,
             ['runtime.vision.model_path', 'gamepad.aim_response_curve.algorithm']),
            ('assist', '开镜与持续跟随', '1.0 为标准倍率；先调整一个参数，再观察游戏中的变化。', False,
             ['gamepad.ads.strength_scale', 'gamepad.ads.vertical_strength_scale', 'gamepad.bodylock.strength',
              'gamepad.ai_aim.hipfire_multiplier', 'gamepad.ai_aim.aim_response_learning_enabled']),
            ('assist', '目标识别', '决定识别哪些目标，以及在目标身体的哪个位置瞄准。', False,
             ['runtime.vision.friendly_filter_enabled', 'runtime.vision.target_height_ratio']),
            ('assist', '开火与压枪', '手动输入和自动输出分别设置；实体按键仍按原样透传。', False,
             ['gamepad.auto_fire.manual_fire_input', 'gamepad.auto_fire.fire_output', 'gamepad.recoil.enabled',
              'gamepad.recoil.feedback_amount', 'gamepad.recoil.hipfire_multiplier']),
            ('assist', '响应学习初值', '0 使用基础初值；自定义范围为 80～4000。', True,
             ['gamepad.ai_aim.body_free_initial_scale', 'gamepad.ai_aim.body_slow_initial_scale',
              'gamepad.ai_aim.ads_free_initial_scale', 'gamepad.ai_aim.ads_slow_initial_scale']),
            ('assist', '游戏输入补偿', '仅在了解游戏死区与响应设置时调整；修改后需要重启。', True,
             ['gamepad.output_transfer.enabled', 'gamepad.output_transfer.deadzone', 'gamepad.output_transfer.axial',
              'gamepad.output_transfer.game_exponent']),
            ('common', '手柄连接', '自动识别适合常规使用；手柄编号仅用于 XInput。', False,
             ['runtime.input.auto_detect', 'runtime.input.controller_index']),
            ('common', '运行记录', '有问题需要排查时开启日志；记录会占用磁盘并增加运行开销。', False,
             ['runtime.telemetry.enabled', 'runtime.performance.enabled']),
            ('common', '捕获与性能', '模型输入尺寸必须与模型匹配；修改后需要重启。', True,
             ['runtime.profile', 'runtime.vision.capture_fps', 'runtime.vision.idle_capture_fps',
              'runtime.vision.capture_width', 'runtime.vision.capture_height', 'runtime.vision.tensor_width', 'runtime.vision.tensor_height']),
        ]
        game_fields = {field[0]: field for field in GAME_FIELDS}
        all_fields = {field[0]: field for field in GAME_FIELDS + COMMON_FIELDS}
        for page, title, description, detailed, paths in sections:
            body = section(self.pages[page], title, description)
            if detailed:
                self.detail_sections.append(body)
            for path in paths:
                field = all_fields[path]
                _, label, kind, fallback, limits = field
                is_game = path in game_fields
                data = effective(self.document, game) if is_game else self.document
                defaults = native_defaults if is_game else common_defaults
                target = f'games.{game}.{path}' if is_game and game != 'default' else path
                original = lookup(data, path, defaults.get(path, fallback))
                raw = self.drafts[target][1] if target in self.drafts else original
                value = lookup(self.document, path, common_defaults.get(path, fallback)) if raw is None else raw
                variable = tk.BooleanVar(value=value) if kind is bool else tk.StringVar(value=str(value))
                self.variables[target] = variable
                self.field_specs[target] = field
                row = ttk.Frame(body)
                row.pack(fill='x', pady=(0, 14))
                heading = ttk.Frame(row)
                heading.pack(fill='x', pady=(0, 6))
                ttk.Label(heading, text=label).pack(side='left')
                if is_game and game != 'default':
                    inherited = lookup(self.document, target) is None
                    source = tk.StringVar(value='待保存 · 继承通用' if raw is None else '待保存 · 游戏专属' if target in self.drafts else '继承通用' if inherited else '游戏专属')
                    self.sources[target] = source
                    if inherited:
                        pin = ttk.Button(heading, text='设为专属', style='Link.TButton', command=lambda p=target: self.pin_field(p))
                        pin.pack(side='right')
                        self.inputs.append(pin)
                    else:
                        reset = ttk.Button(heading, text='使用通用', style='Link.TButton', command=lambda p=target: self.inherit_field(p))
                        reset.pack(side='right')
                        self.inputs.append(reset)
                    ttk.Label(heading, textvariable=source, style='Muted.TLabel').pack(side='right', padx=(8, 0))
                elif is_game:
                    ttk.Label(heading, text='通用值', style='Muted.TLabel').pack(side='right')
                else:
                    ttk.Label(heading, text='所有游戏', style='Muted.TLabel').pack(side='right')
                control = ttk.Frame(row)
                control.pack(fill='x')
                if kind is bool:
                    widget = ttk.Checkbutton(control, text='启用', variable=variable)
                elif kind is str and limits:
                    display = tk.StringVar(value=CHOICE_LABELS.get(value, value))
                    widget = ttk.Combobox(control, textvariable=display, values=[CHOICE_LABELS.get(v, v) for v in limits], state='readonly')
                    widget.bind('<<ComboboxSelected>>', lambda _, d=display, v=variable, options=limits:
                                v.set(next(x for x in options if CHOICE_LABELS.get(x, x) == d.get())))
                    variable.trace_add('write', lambda *_args, d=display, v=variable: d.set(CHOICE_LABELS.get(v.get(), v.get())))
                elif kind is float and path.endswith(('strength_scale', '.strength', 'hipfire_multiplier', 'feedback_amount')):
                    widget = StrengthInput(control, variable, limits)
                else:
                    widget = ttk.Entry(control, textvariable=variable)
                if path.endswith('model_path'):
                    button = ttk.Button(control, text='浏览…', command=lambda v=variable: self.choose_model(v))
                    button.pack(side='right', padx=(8, 0))
                    self.inputs.append(button)
                widget.pack(side='left', fill='x', expand=True)
                self.inputs.append(widget)
                variable.trace_add('write', lambda *_args, p=target, f=field, v=variable, original=original:
                    self.edit_field(p, f, v.get(), original))
        ready = ttk.Frame(self.pages['prepare'])
        ready.pack(fill='x', pady=(0, 10))
        ttk.Label(ready, textvariable=self.readiness_text, style='Muted.TLabel', wraplength=480).pack(anchor='w')
        self.loading = False
        self.rendered_game = game
        self.build_curve_editor()
        self.toggle_details()
        self.update_model()

    def build_curve_editor(self):
        retired = getattr(self, 'curve_inputs', [])
        self.inputs = [widget for widget in self.inputs if widget not in retired]
        if hasattr(self, 'curve_algorithm_trace'):
            old_variable, trace = self.curve_algorithm_trace
            old_variable.trace_remove('write', trace)
        self.curve_inputs = []
        for child in self.curve_content.winfo_children():
            child.destroy()
        game = self.selected_game()
        path = 'gamepad.aim_response_curve.custom_points'
        target = f'games.{game}.{path}' if game != 'default' else path
        field = next(field for field in GAME_FIELDS if field[0] == path)
        original = lookup(effective(self.document, game), path, '')
        raw = self.drafts[target][1] if target in self.drafts else original
        value = lookup(self.document, path, '') if raw is None else raw
        variable = tk.StringVar(value=value)
        self.variables[target] = variable
        self.field_specs[target] = field
        variable.trace_add('write', lambda *_: self.edit_field(target, field, variable.get(), original))
        toolbar = ttk.Frame(self.curve_content)
        toolbar.pack(fill='x', pady=(0, 12))
        self.curve_presets = {f'{data["name"]} · {file.stem[:6]}': data for file, data in self.curve_library.entries()}
        preset = tk.StringVar(value='选择已保存的曲线…')
        chooser = ttk.Combobox(toolbar, textvariable=preset, values=list(self.curve_presets), state='readonly', width=28)
        chooser.pack(side='left')
        chooser.bind('<<ComboboxSelected>>', lambda _: self.use_curve(self.curve_presets[preset.get()]['points']))
        self.curve_inputs.append(chooser)
        for label, command in [('保存预设', self.save_curve_preset), ('导出', self.export_curve), ('导入', self.import_curve)]:
            button = ttk.Button(toolbar, text=label, command=command)
            button.pack(side='right', padx=6)
            self.curve_inputs.append(button)
        self.curve_editor = CurveEditor(self.curve_content, self.use_curve, height=250)
        self.curve_editor.pack(fill='x')
        self.curve_inputs.append(self.curve_editor)
        ttk.Label(self.curve_content, text='拖动控制点调整输入与响应。双击添加点，Delete 删除选中点；左右键选点，上下键微调。\n'
                  '端点固定为 0% 与 100%，曲线保持严格单调。虚线表示线性响应；修改曲线后需要保存并重启。',
                  style='Muted.TLabel', wraplength=720).pack(anchor='w', pady=(12, 8))
        tools = ttk.Frame(self.curve_content)
        tools.pack(fill='x')
        for label, seed in [('线性模板', 'linear'), ('COD 动态模板', 'cod_dynamic_legacy_lut')]:
            button = ttk.Button(tools, text=label, command=lambda s=seed: self.use_curve(seed_points(s, PROJECT_ROOT)))
            button.pack(side='left', padx=(0, 8))
            self.curve_inputs.append(button)
        self.inputs.extend(self.curve_inputs)
        algorithm = self.curve_algorithm_variable()
        self.curve_algorithm_trace = (algorithm, algorithm.trace_add('write', lambda *_: self.refresh_curve()))
        self.refresh_curve()

    def curve_algorithm_variable(self):
        game = self.selected_game()
        path = f'games.{game}.gamepad.aim_response_curve.algorithm' if game != 'default' else 'gamepad.aim_response_curve.algorithm'
        return self.variables[path]

    def resize_curve(self, event):
        if hasattr(self, 'curve_editor'):
            self.curve_editor.configure(height=max(150, min(260, event.height - 110)))

    def curve_points_variable(self):
        game = self.selected_game()
        path = f'games.{game}.gamepad.aim_response_curve.custom_points' if game != 'default' else 'gamepad.aim_response_curve.custom_points'
        return self.variables[path]

    def refresh_curve(self):
        algorithm = self.curve_algorithm_variable().get()
        if algorithm == 'custom_lut':
            raw = self.curve_points_variable().get()
            if not raw:
                # Selecting Custom creates an explicit editable linear seed.
                self.curve_points_variable().set(encode_points(seed_points('linear', PROJECT_ROOT)))
                raw = self.curve_points_variable().get()
            points = decode_points(raw)
        else:
            points = seed_points(algorithm, PROJECT_ROOT)
        self.curve_editor.set_points(points)

    def use_curve(self, points):
        self.curve_points_variable().set(encode_points(points))
        self.curve_algorithm_variable().set('custom_lut')
        self.notice.set('曲线已修改；保存后需要重启当前配置，才会用于控制器输出。')

    def save_curve_preset(self):
        name = simpledialog.askstring('保存曲线', '曲线名称', parent=self.root)
        if name is None:
            return
        try:
            path = self.curve_library.create(name, self.curve_editor.points)
            self.notice.set('曲线已保存：' + path.name)
            self.build_curve_editor()
        except (OSError, ValueError) as error:
            messagebox.showerror('曲线未保存', str(error), parent=self.root)

    def import_curve(self):
        path = filedialog.askopenfilename(parent=self.root, title='导入曲线', filetypes=[('曲线 JSON', '*.json')])
        if not path:
            return
        try:
            data = read_curve(path)
            self.curve_library.create(data['name'], data['points'])
            self.use_curve(data['points'])
            self.build_curve_editor()
        except (OSError, ValueError) as error:
            messagebox.showerror('曲线无法导入', str(error), parent=self.root)

    def export_curve(self):
        path = filedialog.asksaveasfilename(parent=self.root, title='导出曲线', defaultextension='.json',
                                           initialfile='response-curve.json', filetypes=[('曲线 JSON', '*.json')])
        if not path:
            return
        try:
            data = curve_document(self.profile['name'] + ' 曲线', self.curve_editor.points)
            writer = UiPreferences(self.project)
            writer.path = Path(path)
            writer.write(data)
            self.notice.set('曲线已导出：' + Path(path).name)
        except (OSError, ValueError) as error:
            messagebox.showerror('曲线未导出', str(error), parent=self.root)

    def pin_field(self, path):
        self.pinned.add(path)
        field = self.field_specs[path]
        self.edit_field(path, field, self.variables[path].get(), None)

    def inherit_field(self, path):
        if not path.startswith('games.'):
            raise ValueError('只有游戏专属参数可以恢复继承。')
        self.pinned.discard(path)
        self.drafts[path] = (self.field_specs[path], None)
        self.build_forms()
        self.notice.set('此参数将恢复继承通用值；保存后生效。')

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
        self.notice.set('修改尚未保存；保存会应用所有游戏的草稿，需要重启的参数会单独提示。' if self.drafts else '所有修改已保存，可启动辅助。')
        self.update_model()

    def change_game(self):
        if not self.busy:
            try:
                self.build_forms()
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                self.game.set(self.game_labels[self.rendered_game])
                messagebox.showerror('无法读取游戏配置', str(error), parent=self.root)
                return
            try:
                if not self.profile.get('legacy'):
                    field = ('runtime.game', '游戏场景', str, 'default', None)
                    self.drafts['runtime.game'] = (field, self.selected_game())
                else:
                    self.profile = next(entry for entry in self.profile_entries if entry['id'] == self.selected_game())
                    self.profile_strip.set_profiles(self.profile_entries, self.profile['id'])
                    self.show_page(self.current_page)
                self.preferences.save_profile(self.profile['id'], self.selected_game())
            except OSError as error:
                self.notice.set('游戏选择未能记住：' + str(error))

    def update_model(self):
        game = self.selected_game()
        path = f'games.{game}.runtime.vision.model_path' if game != 'default' else 'runtime.vision.model_path'
        model = self.variables[path].get()
        self.model_text.set('模型：' + (Path(model).name if model else '尚未设置') +
                            (' · 文件未找到，请选择模型' if model and not (self.project / model).is_file() else ''))
        curve_path = f'games.{game}.gamepad.aim_response_curve.algorithm' if game != 'default' else 'gamepad.aim_response_curve.algorithm'
        curve = self.variables[curve_path].get()

        self.readiness_text.set(('主程序已就绪' if self.manager.executable.is_file() else '主程序未构建') + '\n' +
                                ('识别模型已就绪' if model and (self.project / model).is_file() else '识别模型未找到，请先选择模型'))
        self.config_summary.set(f'{self.game.get()} · {CHOICE_LABELS.get(curve, curve)}' +
                                (' · 待保存' if path in self.drafts or curve_path in self.drafts else ' · 已保存'))
        scopes = {}
        for target in self.drafts:
            key = target.split('.')[1] if target.startswith('games.') else 'default'
            label = self.game_labels.get(key, key) if target.startswith('games.') else '通用配置'
            scopes[label] = scopes.get(label, 0) + 1
        self.change_summary.set('待保存：' + '；'.join(f'{label} {count} 项' for label, count in scopes.items()) if scopes else '所有修改已保存')
        entries = [dict(entry, dirty=bool(self.drafts) if entry['id'] == self.profile['id'] else
                        bool(self.profile_drafts.get(entry['id'], ({}, set()))[0])) for entry in self.profile_entries]
        self.profile_strip.set_profiles(entries, self.profile['id'])

    def choose_model(self, variable):
        filename = filedialog.askopenfilename(parent=self.root, title='选择识别模型', filetypes=[('TensorRT 模型', '*.engine'), ('所有文件', '*.*')])
        if filename:
            try:
                filename = Path(filename).resolve().relative_to(self.project).as_posix()
            except ValueError:
                filename = Path(filename).as_posix()
            variable.set(str(filename))

    def collect_changes(self):
        return {path: None if value is None else field_value(field, value) for path, (field, value) in self.drafts.items()}

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
        for widget in self.inputs + [self.primary, self.stop_button, self.selector, self.save_button, self.raw_button, self.apply_button, self.fusion_button, self.reload_button, self.new_profile_button, self.copy_profile_button]:
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
        self.refresh_profiles()
        self.notice.set('配置已保存；运行中的参数以热重载结果为准。' if self.manager.active() else '已保存，下次启动生效。')

    def started(self, result):
        identifier = result.get('profile_id')
        if identifier and identifier != self.profile['id']:
            self.drafts.clear()
            self.pinned.clear()
            self.refresh_profiles()
            self.select_profile(identifier)
        self.saved(result)
        self.restart_required = False
        self.notice.set('已启动。关闭窗口后继续运行；结束时请点击“停止”。')

    def save(self):
        try:
            changes = self.collect_changes()
        except ValueError as error:
            messagebox.showerror('设置未保存', str(error), parent=self.root)
            return
        if changes:
            if self.profile.get('legacy'):
                game = self.selected_game()
                snapshot = update_text(self.text, changes)
                name = self.game.get() + ' 自定义'
                def create():
                    return self.profile_library.create(name, game, snapshot,
                        lambda path: self.manager.validate(path, ['default'] + list(tomllib.loads(snapshot).get('games', {}))))
                def created(profile):
                    self.drafts.clear()
                    self.pinned.clear()
                    self.refresh_profiles()
                    self.select_profile(profile['id'])
                    self.notice.set('已保存为独立配置；启动此配置后生效，原配置保留。')
                self.run_job('正在保存独立配置…', create, created)
                return
            def work():
                self.commit(changes)
                active = self.manager.active()
                affects_active = self.owns_active_config(active) and any(not path.startswith('games.') or path.startswith(f'games.{active["game"]}.') for path in changes)
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
        elif self.owns_active_config(self.manager.active()):
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
        same = self.owns_active_config(active) and active['game'] == game
        if same and self.drafts:
            self.save()
            return
        if same and not self.restart_required:
            self.run_job('正在停止…', self.manager.stop)
            return
        try:
            changes = self.collect_changes()
        except ValueError as error:
            messagebox.showerror('无法启动', str(error), parent=self.root)
            return
        def work():
            profile_id = self.profile['id']
            config_path = self.store.path
            if changes and self.profile.get('legacy'):
                snapshot = update_text(self.text, changes)
                profile = self.profile_library.create(self.profile['name'] + ' 自定义', game, snapshot,
                    lambda path: self.manager.validate(path, ['default'] + list(tomllib.loads(snapshot).get('games', {}))))
                config_path, profile_id = profile['path'], profile['id']
            else:
                self.commit(changes)
            if self.manager.active():
                self.manager.stop()
                deadline = time.monotonic() + 8
                while self.manager.active():
                    if time.monotonic() >= deadline:
                        raise ValueError('程序尚未退出，请查看日志；不会同时启动第二个控制实例。')
                    time.sleep(.05)
            _, data, _ = ConfigStore(self.project, config_path).read()
            record = self.manager.start(game, data, config_path, profile_id)
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
        if self.profile.get('legacy'):
            messagebox.showinfo('复制配置', '请先复制当前模板，再编辑独立配置的源文件。', parent=self.root)
            return
        if self.drafts:
            messagebox.showinfo('高级配置', '请先保存当前表单修改，再打开高级配置。', parent=self.root)
            return
        original, _, digest = self.store.read()
        window = tk.Toplevel(self.root)
        window.title('配置源文件 · ' + self.store.path.name)
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
                for widget in self.inputs + [self.primary, self.selector, self.save_button, self.raw_button, self.apply_button, self.fusion_button, self.reload_button, self.new_profile_button, self.copy_profile_button]:
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
                if not self.owns_active_config(record):
                    text = '切换并启动'
                elif self.profile.get('legacy') and self.drafts:
                    text = '保存为配置'
                self.primary.configure(text=text)
                self.stop_button.state(['!disabled'])
            else:
                self.primary.configure(text='保存并启动' if self.drafts else '启动配置')
                self.stop_button.state(['disabled'])
            self.save_button.configure(text='保存为配置' if self.profile.get('legacy') else '保存并应用' if self.owns_active_config(record) else '保存修改')
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
        unsaved = self.drafts or any(drafts for drafts, _ in self.profile_drafts.values())
        if unsaved and not messagebox.askyesno('关闭窗口', '配置库中还有未保存的修改，是否放弃这些修改并关闭？', parent=self.root):
            return
        try:
            self.preferences.save_profile(self.profile['id'], self.selected_game())
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
