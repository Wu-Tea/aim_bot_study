from __future__ import annotations
from project_paths import PROJECT_ROOT
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import queue
import tempfile
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont
import tomllib

from .components import ACCENT, INK, MUTED, RAIL, SURFACE, FORM_COLUMN_GAP, FORM_GROUP_GAP, FORM_LABEL_WIDTH, FORM_LABEL_GAP, FORM_ROW_PADDING, ChoiceInput, DisclosureButton, SegmentedInput, ScrollSurface, StrengthInput, UnitNumberInput, ToggleInput, configure_theme
from .curve_editor import CurveEditor, ProfileStrip
from .curve_model import CurveModel
from .curves import CurveLibrary, curve_document, read_curve, seed_points
from .runtime import RuntimeManager, tail
from .observation import RuntimeObserver
from .frame_rates import rate
from .settings import ConfigStore, UiPreferences, effective, lookup
from .fields import CHOICE_LABELS, COMMON_FIELDS, GAME_FIELDS, field_value, field_presentation
from .parameter_catalog import PARAMETERS, groups as catalog_groups
from .ads_preview import AdsEnvelopePreview
from .workspace import FIELDS, LEGACY_GAME_LABELS, ProfileRepository, configured_value, projection, put, snapshot, toml_text, export_filename

FIELD_MAP = {f[0]: f for f in FIELDS}
VIEW_ATTRIBUTES = ('surface','inputs','search_rows','field_widgets','page_traces','curve_editor',
    'curve_choice','preset_value','preset_entries','preset_button','point_title','point_entries',
    'precision_button','point_error','point_error_label','snap_value','lock_value','snap_toggle',
    'point_table','tableholder','undo_button','redo_button','add_point_button','remove_point_button',
    'device_text','fusion_button','learning_summary','learning_table',
    'frame_rate_values','frame_rate_detail',
    'advanced_parent','advanced_group','advanced_button','transfer_button','transfer_parent','transfer_group','ads_diagram','ads_groups')
PAGES = {'assist': ('参数调校', '辅助力度与识别目标'), 'curve': ('响应曲线', '直接编辑输入与响应'),
         'ads': ('范围与跟随', '开镜时找到目标，瞄上后持续跟随'),
         'device': ('模型与设备', '选择模型，设置捕获尺寸与手柄'), 'feedback': ('运行反馈', '设备状态、帧率与响应学习')}
ASSIST_GROUPS = [
    ('首次瞄准与腰射', ['gamepad.ads.output_limit_x','gamepad.ads.output_limit_y',
                   'gamepad.assist.hipfire_ratio','gamepad.ai_aim.aim_response_learning_enabled'],0),
    ('瞄点与识别', ['runtime.vision.target_height_ratio','runtime.vision.target_wide_low_height_ratio','runtime.vision.friendly_filter_enabled'],0),
    ('开火与压枪', ['gamepad.auto_fire.fire_output','gamepad.auto_fire.manual_fire_input','gamepad.recoil.enabled',
                   'gamepad.recoil.output_amount','gamepad.recoil.hipfire_multiplier'],1)]
for title, paths in catalog_groups(False):
    for group_title, group_paths, _ in ASSIST_GROUPS:
        if title == group_title:
            group_paths.extend(p for p in paths if p not in group_paths)
            break
    else:ASSIST_GROUPS.append((title,list(paths),1))
PRIOR_PATHS = [f[0] for f in FIELDS if f[0].endswith('_initial_scale')]
ADVANCED_PATHS = PRIOR_PATHS + [p['path'] for p in PARAMETERS.values() if p['advanced']]
SHORT_LABELS = {'gamepad.ai_aim.aim_response_learning_enabled':'自适应响应学习',
                'gamepad.ads.output_limit_x':'ADS 横向输出上限',
                'gamepad.ads.output_limit_y':'ADS 纵向输出上限',
                'gamepad.ads.pickup_base_radius_px':'ADS 拾取半径（px）',
                'gamepad.bodylock.activation_range_px':'跟随半径（px）',
                'gamepad.bodylock.response_time_x_ms':'横向响应时间',
                'gamepad.assist.hipfire_ratio':'腰射辅助保留比例', 'runtime.vision.target_height_ratio':'瞄点距顶部',
                'runtime.vision.friendly_filter_enabled':'过滤友方目标',
                'gamepad.ai_aim.body_free_initial_scale':'跟随 · 普通区', 'gamepad.ai_aim.body_slow_initial_scale':'跟随 · 减速区',
                'gamepad.ai_aim.ads_free_initial_scale':'ADS · 普通区', 'gamepad.ai_aim.ads_slow_initial_scale':'ADS · 减速区'}


def raw_value(field, value):
    return value if field[2] is bool else str(value)


class AssistantWindow:
    def __init__(self, root, project):
        self.root, self.project = root, Path(project).resolve()
        self.manager = RuntimeManager(self.project)
        self.repository = ProfileRepository(self.project)
        self.curve_library = CurveLibrary(self.project)
        self.preferences = UiPreferences(self.project)
        self.jobs = queue.Queue()
        self.busy, self.loading, self.closed = False, False, False
        self.states = {}
        self.variable_traces = []
        self.ui_traces = []
        self.page_traces = []
        self.views = {}
        self.current_view = None
        self.runtime_status = {'phase':'stopped','record':None}
        self.last_frame_rates=None
        self.frame_rate_record=None
        self.frame_rate_session=None
        self.input_devices=[]
        self.device_results=queue.Queue()
        self.device_scanning=False
        self.devices_scanned=False
        self.ads_policy=None
        self.ads_policy_loading=False
        self.ads_policy_results=queue.Queue()
        self.observation_after=0.
        self.disposed = False
        self.profile = None
        self.page = 'assist'
        self.inputs, self.variables, self.search_rows = [], {}, []
        self.pending_reload = None
        self.restart_required = False
        self.notice = tk.StringVar(value='新建配置，或导入已有 TOML 配置。')
        self.status_text = tk.StringVar(value='已停止')
        self.change_summary = tk.StringVar(value='')
        self.search = tk.StringVar()
        self.watch(self.search,lambda *_:self.filter_rows())
        self.advanced = tk.BooleanVar(value=False)
        self.manual_transfer = tk.BooleanVar(value=False)
        self.show_point_table = tk.BooleanVar(value=False)
        self.point_syncing = False
        self.point_dirty = False
        self.point_x, self.point_y = tk.StringVar(), tk.StringVar()
        self.watch(self.point_x,self.precision_dirty)
        self.watch(self.point_y,self.precision_dirty)
        self.build_shell()
        self.load_library()
        self.observer=RuntimeObserver(self.manager)
        self.next_observation=0.
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.root.bind('<Destroy>',self.dispose,add='+')
        self.root.bind('<Control-s>', lambda _:self.save())
        self.poll_id = self.root.after(150, self.poll)

    def build_shell(self):
        self.root.title('手柄助手')
        self.root.geometry('1120x840')
        self.root.minsize(840,680)
        configure_theme()
        self.root.configure(background=SURFACE)
        shell = ttk.Frame(self.root,padding=(24,16,24,12))
        shell.pack(fill='both',expand=True)
        header = ttk.Frame(shell)
        header.pack(fill='x',pady=(0,4))
        ttk.Label(header,text='手柄助手',style='Section.TLabel').pack(side='left')
        ttk.Label(header,text='  /  配置工作室',style='Muted.TLabel').pack(side='left')
        self.runtime_menu_value=tk.StringVar(value='tools')
        self.runtime_menu=ChoiceInput(header,self.runtime_menu_value,['restart','raw'],
            {'tools':'更多','restart':'保存并重启当前配置','raw':'编辑完整配置…'})
        self.runtime_menu.pack(side='right',padx=(8,0))
        self.watch(self.runtime_menu_value,self.runtime_menu_action)
        self.primary = ttk.Button(header,text='启动配置',style='Primary.TButton',command=self.primary_action)
        self.primary.pack(side='right')
        self.stop_button = ttk.Button(header,text='停止',command=self.stop_runtime)
        self.stop_button.pack(side='right',padx=(8,8))
        self.save_button = ttk.Button(header,text='保存修改',command=self.save)
        self.save_button.pack(side='right')
        ttk.Label(header,textvariable=self.status_text,style='Accent.TLabel').pack(side='right',padx=16)
        strip = ttk.Frame(shell)
        strip.pack(fill='x',pady=(0,4))
        self.new_button = ttk.Button(strip,text='＋ 新建',command=self.new_profile)
        self.new_button.pack(side='right',padx=(8,0))
        self.manage_value = tk.StringVar(value='manage')
        self.manage_button = ChoiceInput(strip,self.manage_value,['copy','rename','import','export','reload','reset','delete'],
            {'manage':'配置管理','copy':'复制当前配置','rename':'重命名','import':'导入配置…',
             'export':'导出配置…','reload':'重新载入','reset':'恢复创建时的参数','delete':'删除当前配置…'})
        self.manage_button.pack(side='right',padx=(8,0))
        self.watch(self.manage_value,self.manage_action)
        self.profile_strip = ProfileStrip(strip,self.select_profile)
        self.profile_strip.pack(side='left',fill='x',expand=True)
        nav = ttk.Frame(shell)
        nav.pack(fill='x')
        self.navigation = {}
        for key,(label,_) in PAGES.items():
            button = ttk.Button(nav,text=label,style='Nav.TButton',command=lambda k=key:self.show_page(k))
            button.pack(side='left',padx=(0,3))
            self.navigation[key]=button
        ttk.Separator(shell).pack(fill='x',pady=(4,8))
        page_header = ttk.Frame(shell)
        page_header.pack(fill='x',pady=(0,4))
        self.page_title = tk.StringVar()
        ttk.Label(page_header,textvariable=self.page_title,style='Section.TLabel').pack(side='left')
        ttk.Label(page_header,textvariable=self.change_summary,style='Muted.TLabel').pack(side='left',padx=12)
        self.search_entry = ttk.Entry(page_header,textvariable=self.search,width=20)
        self.search_entry.pack(side='right')
        self.search_label = ttk.Label(page_header,text='查找参数',style='Muted.TLabel')
        self.search_label.pack(side='right',padx=8)
        footer = ttk.Frame(shell)
        footer.pack(side='bottom',fill='x',pady=(8,0))
        ttk.Separator(footer).pack(fill='x',pady=(0,8))
        notice_row=ttk.Frame(footer,height=26);notice_row.pack(fill='x');notice_row.pack_propagate(False)
        self.notice_summary=tk.StringVar(master=self.root)
        self.notice_font=tkfont.Font(root=self.root,family='Microsoft YaHei UI',size=9)
        self.notice_detail=ttk.Button(notice_row,text='详情',style='Link.TButton',command=self.show_notice)
        self.notice_label=ttk.Label(notice_row,textvariable=self.notice_summary,style='Muted.TLabel')
        self.notice_label.pack(side='left',fill='x',expand=True)
        self.watch(self.notice,self.update_notice)
        self.root.bind('<Configure>',lambda e:self.update_notice() if e.widget==self.root else None,add='+')
        self.update_notice()
        self.content_host = ttk.Frame(shell)
        self.content_host.pack(fill='both',expand=True)

    def register(self, widget):
        self.inputs.append(widget)
        return widget

    def update_notice(self,*_):
        if self.disposed:return
        message=self.notice.get()
        first=message.splitlines()[0] if message else ''
        available=max(160,self.root.winfo_width()-110)
        preview=first
        if self.notice_font.measure(preview+'…')>available:
            low,high=0,len(preview)
            while low<high:
                middle=(low+high+1)//2
                if self.notice_font.measure(preview[:middle]+'…')<=available:low=middle
                else:high=middle-1
            preview=preview[:low]
        details=preview!=first or first!=message
        self.notice_summary.set(preview+('…' if details else ''))
        if details:self.notice_detail.pack(side='right',padx=(8,0))
        else:self.notice_detail.pack_forget()

    def show_notice(self):
        window=tk.Toplevel(self.root);window.title('操作详情');window.geometry('760x400');window.transient(self.root)
        editor=tk.Text(window,background=RAIL,foreground=INK,font=('Microsoft YaHei UI',10),wrap='word',padx=16,pady=14)
        editor.pack(fill='both',expand=True);editor.insert('1.0',self.notice.get());editor.configure(state='disabled')

    def watch(self,variable,callback,page=False):
        trace=variable.trace_add('write',callback)
        (self.page_traces if page else self.ui_traces).append((variable,trace))
        return trace

    def dispose(self,event):
        if event.widget!=self.root or self.disposed:return
        self.disposed=True
        if hasattr(self,'observer'):self.observer.close()
        page_traces=[pair for view in self.views.values() for pair in view['page_traces']]
        for variable,trace in self.variable_traces+self.ui_traces+page_traces:variable.trace_remove('write',trace)
        self.variable_traces,self.ui_traces,self.page_traces=[],[],[]
        self.variables={}
        self.views.clear()
        self.frame_rate_values=[]
        # Remove UI-owned variables while the interpreter is on its UI thread.
        # An in-flight worker may still own the window controller after an
        # external window destroy; it must not own Tcl variable finalizers.
        for name,value in list(vars(self).items()):
            if isinstance(value,tk.Variable):setattr(self,name,None)
        self.notice_font=None

    def load_library(self, select=None):
        self.profile_entries = self.repository.entries()
        for entry in self.profile_entries:
            if entry['id'] not in self.states:
                self.make_state(entry)
        identifiers = [p['id'] for p in self.profile_entries]
        active = self.manager.active()
        self.runtime_status['record']=active
        remembered = select or self.preferences.read().get('profile_id')
        if not select and active and active.get('profile_id') in identifiers:
            remembered = active['profile_id']
        if remembered not in identifiers:
            remembered = identifiers[0] if identifiers else None
        self.select_profile(remembered)
        if remembered and not self.repository.errors:self.notice.set('调整参数后保存；右键参数可恢复创建时的值或查看范围。')
        if self.repository.errors:
            self.notice.set('部分配置无法读取；原文件已保留。配置管理 → 重新载入可重试。')

    def make_state(self, entry):
        saved, expected = self.repository.read(self.repository.path(entry['id']))
        self.states[entry['id']] = {'saved':saved,'data':deepcopy(saved),'expected':expected,
            'raw':{f[0]:raw_value(f,configured_value(saved['config'],f[0],f[3])) for f in FIELDS},
            'model':self.model_for(saved)}

    def model_for(self,data):
        curve=data['curve']
        return CurveModel(curve['definition']['points'],{'algorithm':curve['algorithm'],'name':curve['definition']['name']})

    @property
    def state(self): return self.states[self.profile['id']] if self.profile else None

    def dirty(self,state=None):
        state = state or self.state
        if not state:return False
        baseline={f[0]:raw_value(f,configured_value(state['saved']['config'],f[0],f[3])) for f in FIELDS}
        return state['raw']!=baseline or state['data']!=state['saved']

    def select_profile(self,identifier):
        if self.busy:return
        if self.profile and self.point_dirty and not self.apply_precision():return
        self.profile = self.states[identifier]['data'] if identifier in self.states else None
        if self.profile:
            self.loading=True
            for field in FIELDS:
                path=field[0]
                if path not in self.variables:
                    variable=(tk.BooleanVar if field[2] is bool else tk.StringVar)(master=self.root,value=self.state['raw'][path])
                    trace=variable.trace_add('write',lambda *_,p=path,v=variable:self.field_changed(p,v.get()))
                    self.variable_traces.append((variable,trace))
                    self.variables[path]=variable
                elif self.variables[path].get()!=self.state['raw'][path]:
                    self.variables[path].set(self.state['raw'][path])
            self.loading=False
            self.preferences.save_profile(identifier,self.profile['game'])
        self.refresh_strip()
        self.show_page(self.page)
        self.refresh_actions()

    def refresh_strip(self):
        self.profile_strip.set_profiles([dict(p, name=self.states[p['id']]['data']['name'],dirty=self.dirty(self.states[p['id']]))
            for p in self.profile_entries],self.profile['id'] if self.profile else None)
        self.change_summary.set('有未保存修改' if self.dirty() else '已保存' if self.profile else '')

    def field_changed(self,path,value):
        if self.loading:return
        self.state['raw'][path]=value
        if path=='runtime.input.device_id':
            device=next((d for d in self.input_devices if d['id']==value),None)
            if device or not value:self.variables['runtime.input.device_name'].set(device['name'] if device else '')
            if not value:self.variables['runtime.input.auto_detect'].set(True)
            self.refresh_device_choices()
        self.refresh_strip()
        self.refresh_actions()
        self.validate_visible(path)

    def validate_visible(self,path):
        record=self.field_widgets.get(path) if hasattr(self,'field_widgets') else None
        if not record:return
        widget,error_label=record
        try:
            field_value(FIELD_MAP[path],self.state['raw'][path])
            if error_label.winfo_manager():
                error_label.configure(text='')
                error_label.grid_remove()
            target=getattr(widget,'entry',widget)
            target.state(['!invalid'])
        except ValueError as error:
            error_label.configure(text=str(error))
            error_label.grid()
            target=getattr(widget,'entry',widget)
            target.state(['invalid'])

    def show_page(self,page):
        if self.busy:return
        if self.point_dirty and not self.apply_precision():return
        key=page if self.profile else 'empty'
        if self.current_view is not None:
            self.views[self.current_view]={name:getattr(self,name,None) for name in VIEW_ATTRIBUTES}
            if self.current_view!=key:self.surface.pack_forget()
        self.page=page
        for nav_page,button in self.navigation.items():button.configure(style='Selected.Nav.TButton' if nav_page==page else 'Nav.TButton')
        self.page_title.set(PAGES[page][1])
        self.search_entry.state(['!disabled'] if page in ('assist','device','ads') and self.profile else ['disabled'])
        self.current_view=key
        if key in self.views:
            for name,value in self.views[key].items():setattr(self,name,value)
            self.surface.pack(fill='both',expand=True)
            if self.curve_editor:
                if self.curve_editor.model is not self.state['model']:
                    self.curve_editor.model=self.state['model']
                    self.curve_editor.view=[0.,0.,1.]
                self.curve_editor.baseline=deepcopy(self.state['saved']['curve']['definition']['points'])
                self.loading=True
                self.curve_choice.set(self.profile['curve']['algorithm'])
                self.loading=False
                self.curve_editor.draw()
                self.tableholder.pack(fill='x',pady=(8,0)) if self.show_point_table.get() else self.tableholder.pack_forget()
                self.point_selected(self.curve_editor.selected)
            if page=='feedback':self.show_frame_rates()
            if page=='device':self.refresh_device_choices()
            if page=='ads':
                self.ads_diagram.set_config(self.profile['config'])
                self.ads_diagram.set_policy(self.ads_policy)
            for path in self.field_widgets:self.validate_visible(path)
            self.update_advanced_groups()
            self.filter_rows()
            self.refresh_actions()
            return
        for name in VIEW_ATTRIBUTES:setattr(self,name,None)
        self.page_traces=[]
        self.inputs,self.search_rows,self.field_widgets=[],[],{}
        self.point_dirty=False
        self.surface=ScrollSurface(self.content_host,self.root)
        self.surface.pack(fill='both',expand=True)
        body=self.surface.content
        if not self.profile:
            empty=ttk.Frame(body,padding=(25,70))
            empty.pack(fill='x')
            ttk.Label(empty,text='为每一种手感，建立一份配置',style='Title.TLabel').pack(anchor='w')
            ttk.Label(empty,text='配置独立保存模型、辅助参数和响应曲线。\n填写名称即可从默认值开始调校，也可以导入已有配置。',
                      style='Muted.TLabel').pack(anchor='w',pady=(14,22))
            ttk.Button(empty,text='＋ 创建第一份配置',style='Primary.TButton',command=self.new_profile).pack(side='left')
            ttk.Button(empty,text='导入已有配置',command=self.import_profile).pack(side='left',padx=12)
        elif page=='assist':self.build_assist(body)
        elif page=='device':self.build_device(body)
        elif page=='curve':self.build_curve(body)
        elif page=='ads':self.build_ads(body)
        else:self.build_feedback(body)
        self.surface.install_wheel()
        self.views[key]={name:getattr(self,name,None) for name in VIEW_ATTRIBUTES}
        self.filter_rows()
        self.refresh_actions()

    def columns(self,parent):
        row=ttk.Frame(parent)
        row.pack(fill='x')
        row.columnconfigure((0,1),weight=1,uniform='columns')
        left,right=ttk.Frame(row),ttk.Frame(row)
        left.grid(row=0,column=0,sticky='new',padx=(0,FORM_COLUMN_GAP//2))
        right.grid(row=0,column=1,sticky='new',padx=(FORM_COLUMN_GAP//2,0))
        return left,right

    def group(self,parent,title,paths,slider=True,hint='',compact=False):
        group=ttk.Frame(parent)
        group.pack(fill='x',pady=(0,4 if compact else FORM_GROUP_GAP))
        ttk.Label(group,text=title,style='Section.TLabel').pack(anchor='w',pady=(0,4))
        if hint:ttk.Label(group,text=hint,style='Muted.TLabel',wraplength=380).pack(anchor='w',pady=(0,5))
        if not compact:ttk.Separator(group).pack(fill='x',pady=(0,4))
        for path in paths:self.field_row(group,path,slider,compact)
        return group

    def field_row(self,parent,path,slider=True,compact=False):
        field=FIELD_MAP[path]
        _,label,kind,_,limits=field
        row=ttk.Frame(parent,padding=(0,FORM_ROW_PADDING))
        row.pack(fill='x')
        row.columnconfigure(1,weight=1)
        presentation=field_presentation(path)
        label=presentation.get('label',SHORT_LABELS.get(path,label))
        ttk.Label(row,text=label,wraplength=FORM_LABEL_WIDTH-4).grid(row=0,column=0,sticky='w',padx=(0,FORM_LABEL_GAP))
        row.columnconfigure(0,minsize=FORM_LABEL_WIDTH+FORM_LABEL_GAP)
        variable=self.variables[path]
        if path=='runtime.input.device_id':
            widget=ChoiceInput(row,variable,[''],{'':'自动选择'},ellipsize=True,width=12)
        elif kind is bool:
            widget=ToggleInput(row,variable)
        elif kind is str and limits:
            widget=ChoiceInput(row,variable,limits,CHOICE_LABELS)
        elif presentation.get('display_scale'):
            if slider and kind is float and presentation.get('editor')!='number':
                widget=StrengthInput(row,variable,limits,unit=presentation['unit'],display_scale=presentation['display_scale'])
            else:widget=UnitNumberInput(row,variable,presentation['display_scale'],presentation['unit'])
        elif kind is float and slider and PARAMETERS.get(path,{}).get('editor') != 'number' and not path.endswith(('_initial_scale','_ms')):
            widget=StrengthInput(row,variable,limits,unit=presentation.get('unit',''))
        elif kind in (int,float):
            widget=UnitNumberInput(row,variable)
        else:
            widget=ttk.Entry(row,textvariable=variable,justify='right' if kind in (int,float) else 'left',width=12)
        widget.grid(row=0,column=1,sticky='ew')
        self.register(widget)
        error=ttk.Label(row,text='',foreground='#f49090',font=('Microsoft YaHei UI',8),wraplength=380)
        error.grid(row=1,column=0,columnspan=2,sticky='w',pady=(2,0))
        error.grid_remove()
        self.field_widgets[path]=(widget,error)
        self.search_rows.append((row,(label+' '+field[1]+' '+path+' '+parent.winfo_name()).lower()))
        targets=[widget,widget.entry,widget.scale] if isinstance(widget,StrengthInput) else [widget]
        if isinstance(widget,UnitNumberInput):targets.append(widget.entry)
        targets.extend(row.winfo_children())
        for target in targets:
            target.bind('<Button-3>',lambda e,p=path:self.field_menu(p,e),add='+')
            target.bind('<Shift-F10>',lambda e,p=path:self.field_menu(p,e),add='+')
        self.validate_visible(path)
        return row

    def field_menu(self,path,event):
        if self.busy:return 'break'
        if hasattr(self,'field_popup') and self.field_popup.winfo_exists():self.field_popup.destroy()
        value=tk.StringVar(value='actions')
        self.field_popup=ChoiceInput(self.root,value,['initial','help'],{'actions':'参数操作','initial':'恢复创建时的值','help':'查看范围与说明'})
        def action(*_):
            if self.field_popup.variable.get()=='initial':
                field=FIELD_MAP[path]
                self.variables[path].set(raw_value(field,configured_value(self.profile['initial']['config'],path,field[3])))
            elif self.field_popup.variable.get()=='help':
                field=FIELD_MAP[path]
                if path in ('gamepad.ads.output_limit_x','gamepad.ads.output_limit_y'):
                    self.notice.set('开镜时把准星拉向目标的力度倍率：1× 保留原力度上限，0.5× 将上限减半。范围 0～3×；保存后热加载。瞄上后的持续跟随力度在“范围与跟随”设置。')
                    return
                if path in PARAMETERS:
                    change='保存后热加载。' if PARAMETERS[path]['hot_reload'] else '保存后需要重启。'
                    scale=PARAMETERS[path].get('display_scale',1);unit=PARAMETERS[path].get('unit','')
                    self.notice.set(PARAMETERS[path]['help']+f' 范围 {field[4][0]*scale:g}～{field[4][1]*scale:g}{unit}。'+change)
                    return
                ranges='开关' if field[2] is bool else '、'.join(CHOICE_LABELS.get(v,v) for v in field[4]) if field[2] is str and field[4] else \
                       ('0 使用原生默认值；手动指定为 80～4000' if path.endswith('_initial_scale') else f'范围 {field[4][0]}～{field[4][1]}')
                self.notice.set(field[1]+'：'+ranges+'。恢复创建时的值只修改草稿。')
        value.trace_add('write',action)
        self.field_popup.open((event.x_root,event.y_root),event.widget)
        return 'break'

    def build_assist(self,parent):
        cols=self.columns(parent)
        for title,paths,column in ASSIST_GROUPS:
            group=self.group(cols[column],title,paths)
            if title=='首次瞄准与腰射':
                ttk.Button(group,text='瞄上后持续跟随 → 范围与跟随',style='Link.TButton',command=lambda:self.show_page('ads')).pack(anchor='w',pady=(4,0))
        self.advanced_button=self.register(DisclosureButton(parent,self.advanced,'手动介入与响应学习',self.update_advanced_groups))
        self.advanced_button.pack(fill='x',pady=(2,0))
        self.advanced_parent=ttk.Frame(parent)
        self.advanced_parent.pack(fill='x')
        self.transfer_button=self.register(DisclosureButton(parent,self.manual_transfer,'手动输出补偿',self.update_advanced_groups))
        self.transfer_button.pack(fill='x',pady=(8,0))
        self.transfer_parent=ttk.Frame(parent)
        self.transfer_parent.pack(fill='x')
        self.update_advanced_groups()

    def build_ads(self,parent):
        row=ttk.Frame(parent);row.pack(fill='x')
        row.columnconfigure(0,weight=1)
        self.ads_diagram=AdsEnvelopePreview(row,self.variables)
        self.ads_diagram.grid(row=0,column=0,sticky='new',padx=(0,FORM_COLUMN_GAP))
        diagram,surface=self.ads_diagram,self.surface
        surface.canvas.bind('<Configure>',lambda _:diagram.fit_height(surface.canvas.winfo_height()),add='+')
        settings=ttk.Frame(row,width=265)
        settings.grid(row=0,column=1,sticky='new')
        titles={'辅助输入':'L2 触发与 AI 输入',
                '目标范围':'① ADS Snap · 首次抓取','跟随响应':'② BodyLock · 持续跟随'}
        self.ads_groups={}
        for title,paths in catalog_groups(False,page='ads'):
            group=self.group(settings,titles.get(title,title),paths,False,compact=True)
            if title in ('目标范围','跟随响应'):
                self.ads_groups['acquire' if title=='目标范围' else 'follow']=group
        # Capture this retained page, not whichever page is later current.
        groups,preview=self.ads_groups,self.ads_diagram
        result=ttk.Frame(settings)
        result.pack(fill='x',pady=(4,0))
        def stage_changed(*_):
            for stage,group in groups.items():
                if stage==preview.active_mode:group.pack(fill='x',pady=(0,4),before=result)
                else:group.pack_forget()
        self.watch(preview.mode,stage_changed,page=True)
        stage_changed()
        ttk.Separator(result).pack(fill='x',pady=(0,6))
        ttk.Label(result,text='此位置的辅助示例',style='Section.TLabel').pack(anchor='w')
        ttk.Label(result,textvariable=preview.example_output,style='Muted.TLabel',wraplength=260).pack(anchor='w',pady=(4,4))
        ttk.Label(result,text='静止 / 线性示例，非实时输出',style='Muted.TLabel').pack(anchor='w')
        self.ads_diagram.set_config(self.profile['config'])
        self.ads_diagram.set_policy(self.ads_policy)
        if self.ads_policy is None and not self.ads_policy_loading:self.request_ads_policy()

    def request_ads_policy(self):
        if self.ads_policy_loading or self.closed:return
        self.ads_policy_loading=True
        manager,results=self.manager,self.ads_policy_results
        def work():
            try:results.put((manager.ads_geometry_policy(),None))
            except Exception as error:results.put((None,str(error)))
        threading.Thread(target=work,daemon=True,name='ads-geometry-policy').start()

    def build_device(self,parent):
        model=ttk.Frame(parent)
        model.pack(fill='x',pady=(0,16))
        ttk.Label(model,text='识别模型',style='Section.TLabel').pack(anchor='w',pady=(0,6))
        entry=self.register(ttk.Entry(model,textvariable=self.variables['runtime.vision.model_path']))
        entry.pack(side='left',fill='x',expand=True)
        self.register(ttk.Button(model,text='选择文件…',command=self.choose_model)).pack(side='left',padx=(8,0))
        cols=self.columns(parent)
        self.group(cols[0],'捕获与推理',['runtime.vision.'+key for key in
            ('capture_fps','idle_capture_fps','capture_width','capture_height','tensor_width','tensor_height')],False)
        devices=self.group(cols[1],'手柄与日志',[],False)
        row=self.field_row(devices,'runtime.input.device_id',False)
        self.register(ttk.Button(row,text='刷新',width=5,command=self.refresh_input_devices)).grid(row=0,column=2,padx=(6,0))
        ttk.Label(devices,text='按名称选择；保存后重启生效。',style='Muted.TLabel').pack(anchor='w',pady=(0,5))
        for path in ('runtime.telemetry.enabled','runtime.performance.enabled'):self.field_row(devices,path,False)
        self.refresh_device_choices()
        if not self.devices_scanned:self.refresh_input_devices()

    def refresh_input_devices(self):
        if self.device_scanning or self.closed:return
        self.device_scanning=True
        self.devices_scanned=True
        # The worker captures only the manager and result queue, never Tk or
        # this window controller. Scanning does not disable navigation/editing.
        manager,results=self.manager,self.device_results
        def work():
            try:results.put((manager.input_devices(),None))
            except Exception as error:results.put((None,str(error)))
        threading.Thread(target=work,daemon=True,name='input-device-scan').start()

    def refresh_device_choices(self):
        if self.page!='device' or not self.profile:return
        record=self.field_widgets.get('runtime.input.device_id')
        if not record:return
        choice,_=record
        selected=self.variables['runtime.input.device_id'].get()
        names={d['name']:sum(other['name']==d['name'] for other in self.input_devices) for d in self.input_devices}
        labels={'':'自动选择' if self.variables['runtime.input.auto_detect'].get() else '沿用已存手柄设置'}
        details={}
        for device in self.input_devices:
            if names[device['name']]<=1:continue
            try:detail=bytes.fromhex(device['id'].partition(':')[2]).decode('utf-8').split('\0')[-1]
            except (ValueError,UnicodeError):detail=device['id']
            # Windows HID paths share a final interface GUID. The instance
            # segment carries the useful distinction between equal names.
            if '#' in detail:detail=detail.split('#')[-2]
            details[device['id']]=detail
        for device in self.input_devices:
            name=device['name']
            if names[name]>1:
                peers=[details[d['id']] for d in self.input_devices if d['name']==name]
                prefix=os.path.commonprefix(peers)
                detail=details[device['id']][len(prefix):]
                length=8
                while length<len(detail) and sum(v[len(prefix):][:length]==detail[:length] for v in peers)>1:length+=1
                name+=f' · {detail[:length] or "末端"}'
            labels[device['id']]=name
        if selected and selected not in labels:
            labels[selected]='未连接 · '+(self.variables['runtime.input.device_name'].get() or '已选手柄')
        choice.choices=list(labels)
        choice.labels=labels
        if choice.popup:choice.dismiss()
        choice.refresh()

    def update_advanced_groups(self):
        if self.page=='assist' and self.profile:
            if self.advanced.get():
                self.advanced_parent.pack(fill='x',before=self.transfer_button)
                if self.advanced_group is None:
                    self.advanced_group=ttk.Frame(self.advanced_parent)
                    self.advanced_group.pack(fill='x',pady=(0,14))
                    left,right=self.columns(self.advanced_group)
                    for title,paths in catalog_groups(True):
                        self.group(left,title,paths,True,
                            '从开始介入到完全接管，手动控制权逐步增加。' if title=='手动意图' else '')
                    self.group(right,'响应学习起点',PRIOR_PATHS,False,
                               '0 使用原生默认值；手动指定范围为 80～4000。')
                    self.surface.install_wheel(self.advanced_group)
                else:self.advanced_group.pack(fill='x',pady=(0,14))
            else:
                if self.advanced_group is not None:self.advanced_group.pack_forget()
                self.advanced_parent.pack_forget()
        if self.page=='assist' and self.profile:
            if self.manual_transfer.get():
                self.transfer_parent.pack(fill='x')
                if self.transfer_group is None:
                    self.transfer_group=self.group(self.transfer_parent,'手动输出补偿',
                        [f[0] for f in GAME_FIELDS if f[0].startswith('gamepad.output_transfer.')],False,
                        '调整手动摇杆输出的映射；展开此处不会启用补偿。')
                    self.surface.install_wheel(self.transfer_group)
                else:self.transfer_group.pack(fill='x',pady=(0,14))
            else:
                if self.transfer_group is not None:self.transfer_group.pack_forget()
                self.transfer_parent.pack_forget()

    def build_curve(self,parent):
        toolbar=ttk.Frame(parent)
        toolbar.pack(fill='x',pady=(0,10))
        self.curve_choice=tk.StringVar(value=self.profile['curve']['algorithm'])
        choice=self.register(SegmentedInput(toolbar,self.curve_choice,['linear','cod_dynamic_legacy_lut','custom_lut'],
            {'linear':'线性','cod_dynamic_legacy_lut':'动态','custom_lut':'自定义'}))
        choice.pack(side='left')
        ttk.Label(toolbar,text='蓝：当前 / 灰：已保存',style='Muted.TLabel').pack(side='left',padx=12)
        self.watch(self.curve_choice,self.curve_choice_changed,page=True)
        files=ttk.Frame(parent)
        files.pack(fill='x',pady=(0,10))
        for label,command in [('导入曲线…',self.import_curve),('导出曲线…',self.export_curve),('存为预设…',self.save_curve_preset)]:
            self.register(ttk.Button(files,text=label,command=command)).pack(side='left',padx=(0,6))
        self.preset_value=tk.StringVar(value='presets')
        self.preset_button=self.register(ChoiceInput(files,self.preset_value,[],{'presets':'曲线预设'}))
        self.watch(self.preset_value,self.preset_changed,page=True)
        self.refresh_curve_presets()
        graphrow=ttk.Frame(parent)
        graphrow.pack(fill='x')
        graphrow.columnconfigure(0,weight=1)
        self.curve_editor=self.register(CurveEditor(graphrow,self.curve_changed,self.point_selected))
        self.curve_editor.grid(row=0,column=0,sticky='nsew',padx=(0,16))
        self.curve_editor.model=self.state['model']
        self.curve_editor.before_edit=self.apply_precision
        self.curve_editor.on_error=self.notice.set
        self.curve_editor.baseline=deepcopy(self.state['saved']['curve']['definition']['points'])
        side=ttk.Frame(graphrow,width=190)
        side.grid(row=0,column=1,sticky='ns')
        self.point_title=tk.StringVar()
        ttk.Label(side,textvariable=self.point_title,style='Section.TLabel').pack(anchor='w',pady=(0,10))
        self.point_entries=[]
        for label,variable in [('输入 %',self.point_x),('响应 %',self.point_y)]:
            ttk.Label(side,text=label,style='Muted.TLabel').pack(anchor='w')
            widget=self.register(ttk.Entry(side,textvariable=variable,width=17))
            widget.pack(fill='x',pady=(3,7))
            self.point_entries.append(widget)
            widget.bind('<Return>',lambda _:self.apply_precision())
            widget.bind('<Escape>',lambda _:self.point_selected(self.curve_editor.selected))
        self.curve_editor.bind('<<CurvePrecisionRequested>>',lambda _:self.focus_precision())
        self.precision_button=self.register(ttk.Button(side,text='应用精确坐标',command=self.apply_precision))
        self.precision_button.pack(fill='x')
        self.point_error=tk.StringVar()
        self.point_error_label=ttk.Label(side,textvariable=self.point_error,foreground='#f49090',wraplength=190,font=('Microsoft YaHei UI',8))
        self.snap_value=tk.BooleanVar()
        self.lock_value=tk.BooleanVar()
        self.snap_toggle=self.register(ttk.Checkbutton(side,variable=self.snap_value,text='吸附 1% 网格',command=lambda:setattr(self.curve_editor,'snap',self.snap_value.get())))
        self.snap_toggle.pack(anchor='w',pady=(10,0))
        self.register(ttk.Checkbutton(side,variable=self.lock_value,text='拖动时锁定输入',command=lambda:setattr(self.curve_editor,'lock_x',self.lock_value.get()))).pack(anchor='w')
        actions=ttk.Frame(parent)
        actions.pack(fill='x',pady=(10,8))
        self.undo_button=self.register(ttk.Button(actions,text='撤销',command=self.curve_editor.undo))
        self.undo_button.pack(side='left')
        self.redo_button=self.register(ttk.Button(actions,text='重做',command=self.curve_editor.redo))
        self.redo_button.pack(side='left',padx=6)
        self.add_point_button=self.register(ttk.Button(actions,text='插入点',command=self.add_point))
        self.add_point_button.pack(side='left')
        self.remove_point_button=self.register(ttk.Button(actions,text='删除点',command=self.curve_editor.remove))
        self.remove_point_button.pack(side='left',padx=6)
        for label,action in [('－',lambda:self.curve_editor.zoom(1.4)),('＋',lambda:self.curve_editor.zoom(.7)),('适合视图',self.curve_editor.fit)]:
            self.register(ttk.Button(actions,text=label,command=action)).pack(side='right',padx=(5,0))
        ttk.Label(parent,text='拖动点调整 · 双击空白插点 · ↑↓ 微调 · Ctrl+Z 撤销',
                  style='Muted.TLabel').pack(anchor='w',pady=(0,8))
        self.register(DisclosureButton(parent,self.show_point_table,'查看全部控制点',self.toggle_point_table)).pack(anchor='w')
        self.point_table=ttk.Treeview(parent,columns=('input','output'),show='headings',height=4,selectmode='browse')
        self.point_table.local_scroll=True
        self.point_table.heading('input',text='控制点输入 %')
        self.point_table.heading('output',text='响应 %')
        self.point_table.column('input',width=160,anchor='center')
        self.point_table.column('output',width=160,anchor='center')
        tableholder=self.tableholder=ttk.Frame(parent)
        if self.show_point_table.get():tableholder.pack(fill='x',pady=(8,0))
        self.point_table.pack(in_=tableholder,side='left',fill='x',expand=True)
        scrollbar=ttk.Scrollbar(tableholder,command=self.point_table.yview)
        scrollbar.pack(side='right',fill='y')
        self.point_table.configure(yscrollcommand=scrollbar.set)
        self.point_table.bind('<<TreeviewSelect>>',self.table_selected)
        self.point_table.bind('<Double-Button-1>',lambda _:self.precision_button.focus_set())
        self.point_selected(self.curve_editor.selected)

    def toggle_point_table(self):
        if self.show_point_table.get():
            self.tableholder.pack(fill='x',pady=(8,0))
            self.point_selected(self.curve_editor.selected)
        else:self.tableholder.pack_forget()

    def refresh_curve_presets(self):
        try:presets=self.curve_library.entries()
        except (OSError,ValueError,KeyError) as error:
            presets=[]
            self.notice.set('曲线预设读取失败：'+str(error))
        if self.curve_library.errors:self.notice.set('部分曲线预设无法读取，原文件已保留：'+'；'.join(self.curve_library.errors))
        self.preset_entries={str(p):d for p,d in presets}
        self.preset_button.choices=list(self.preset_entries)
        self.preset_button.labels={str(p):d['name'] for p,d in presets}|{'presets':'曲线预设'}
        self.preset_button.refresh()
        if presets:self.preset_button.pack(side='right',padx=8)
        else:self.preset_button.pack_forget()

    def precision_dirty(self,*_):
        if not self.point_syncing:self.point_dirty=True

    def focus_precision(self):
        if 0<self.curve_editor.selected<len(self.curve_editor.points)-1:
            self.point_entries[0].focus_set()
            self.point_entries[0].selection_range(0,'end')

    def point_selected(self,index):
        if not self.curve_editor or not hasattr(self,'point_title'):return
        self.point_syncing=True
        x,y=self.curve_editor.points[index]
        for variable,value in ((self.point_x,x),(self.point_y,y)):
            text=f'{value*100:.6f}'.rstrip('0').rstrip('.')
            if variable.get()!=text:variable.set(text)
        self.point_syncing=False
        self.point_dirty=False
        title=f'控制点 {index+1} / {len(self.curve_editor.points)}' + (' · 固定' if index in (0,len(self.curve_editor.points)-1) else '')
        if self.point_title.get()!=title:self.point_title.set(title)
        for widget in self.point_entries:
            widget.state(['disabled'] if self.busy or index in (0,len(self.curve_editor.points)-1) else ['!disabled'])
        if hasattr(self,'point_error'):
            self.point_error.set('')
            self.point_error_label.pack_forget()
        if self.show_point_table.get() and self.point_table.winfo_exists():
            existing=self.point_table.get_children()
            for i,p in enumerate(self.curve_editor.points):
                values=(f'{p[0]*100:.6f}'.rstrip('0').rstrip('.'),f'{p[1]*100:.6f}'.rstrip('0').rstrip('.'))
                if str(i) in existing:
                    if self.point_table.item(str(i),'values')!=values:self.point_table.item(str(i),values=values)
                else:self.point_table.insert('', 'end', iid=str(i),values=values)
            for iid in existing:
                if int(iid)>=len(self.curve_editor.points):self.point_table.delete(iid)
            if self.point_table.selection()!=(str(index),):self.point_table.selection_set(str(index))
        if hasattr(self,'undo_button') and self.undo_button.winfo_exists():
            self.undo_button.state(['!disabled'] if self.curve_editor.model.undo_stack and not self.busy else ['disabled'])
            self.redo_button.state(['!disabled'] if self.curve_editor.model.redo_stack and not self.busy else ['disabled'])
            self.remove_point_button.state(['!disabled'] if 0<index<len(self.curve_editor.points)-1 and not self.busy else ['disabled'])
            self.add_point_button.state(['!disabled'] if len(self.curve_editor.points)<32 and not self.busy else ['disabled'])
            self.precision_button.state(['!disabled'] if 0<index<len(self.curve_editor.points)-1 and not self.busy else ['disabled'])

    def table_selected(self,_):
        if not self.curve_editor or self.busy or not self.show_point_table.get():return
        selected=self.point_table.selection()
        if selected and int(selected[0])!=self.curve_editor.selected:
            self.curve_editor.select(int(selected[0]))
            self.point_table.selection_set(str(self.curve_editor.selected))

    def apply_precision(self):
        if not self.point_dirty:return True
        try:
            self.curve_editor.exact(float(self.point_x.get())/100,float(self.point_y.get())/100)
            return True
        except ValueError as error:
            self.point_error.set(str(error))
            self.point_error_label.pack(anchor='w',pady=8,before=self.snap_toggle)
            return False

    def curve_changed(self,points):
        metadata=self.curve_editor.model.metadata
        self.profile['curve']={'algorithm':metadata['algorithm'],'definition':curve_document(metadata['name'],points)}
        self.loading=True
        self.curve_choice.set(metadata['algorithm'])
        self.loading=False
        self.refresh_strip()
        self.refresh_actions()

    def curve_choice_changed(self,*_):
        if self.loading:return
        algorithm=self.curve_choice.get()
        if not self.apply_precision():
            self.loading=True
            self.curve_choice.set(self.profile['curve']['algorithm'])
            self.loading=False
            return
        if algorithm!='custom_lut':
            points=seed_points(algorithm,self.project)
            self.curve_editor.model.replace(points,{'algorithm':algorithm,'name':'响应曲线'})
        else:
            self.curve_editor.model.begin()
            self.curve_editor.model.metadata['algorithm']='custom_lut'
            self.curve_editor.model.commit()
        self.curve_editor.changed()

    def add_point(self):
        try:self.curve_editor.add_midpoint()
        except ValueError as error:self.notice.set(str(error))

    def use_curve(self,data):
        if not self.apply_precision():return False
        self.curve_editor.model.replace(data['points'],{'algorithm':'custom_lut','name':data['name']})
        self.curve_editor.changed()
        self.notice.set('曲线已载入当前草稿，可撤销；点击顶部保存后生效。')
        return True

    def preset_changed(self,*_):
        if self.preset_value.get() in self.preset_entries:self.use_curve(self.preset_entries[self.preset_value.get()])

    def import_curve(self):
        if self.busy or not self.profile or not self.apply_precision():return
        path=filedialog.askopenfilename(parent=self.root,title='导入响应曲线',filetypes=[('标准曲线 JSON','*.json')])
        if path:self.run_job('正在读取曲线…',lambda:read_curve(path),self.use_curve)

    def export_curve(self):
        if self.busy or not self.profile or not self.apply_precision():return
        path=filedialog.asksaveasfilename(parent=self.root,title='导出响应曲线',defaultextension='.json',
            initialfile=export_filename(self.profile['name'],'-curve.json'),filetypes=[('标准曲线 JSON','*.json')])
        if path:
            document=deepcopy(self.profile['curve']['definition'])
            def work():
                writer=UiPreferences(self.project);writer.path=Path(path);writer.write(document)
            self.run_job('正在导出曲线…',work,lambda _:self.notice.set('曲线已导出：'+str(path)))

    def save_curve_preset(self):
        if self.busy or not self.profile or not self.apply_precision():return
        name=self.name_dialog('保存曲线预设',(self.profile['name']+' 响应曲线')[:80])
        if name:
            points=deepcopy(self.curve_editor.points)
            def done(_):
                self.refresh_curve_presets()
                self.notice.set('曲线预设已保存，可在其他配置中选择使用。')
            self.run_job('正在保存曲线预设…',lambda:self.curve_library.create(name,points),done)

    def filter_rows(self):
        query=self.search.get().strip().lower()
        transfer=[f[0] for f in GAME_FIELDS if f[0].startswith('gamepad.output_transfer.')]
        if self.profile and self.page=='assist' and query and not self.manual_transfer.get() and \
            any(query in ('手动输出补偿 '+FIELD_MAP[p][1]+' '+p).lower() for p in transfer):
            self.manual_transfer.set(True)
            self.show_page('assist')
            return
        if self.profile and self.page=='ads' and query:
            matches={AdsEnvelopePreview.context_for(p) for p in self.field_widgets
                     if query in (SHORT_LABELS.get(p,'')+' '+FIELD_MAP[p][1]+' '+p).lower()}
            matches.discard(None)
            if len(matches)==1:self.ads_diagram.mode.set(matches.pop())
        if self.profile and self.page=='assist' and query and not self.advanced.get() and \
            any(query in (SHORT_LABELS.get(p,'')+' '+FIELD_MAP[p][1]+' '+p).lower() for p in ADVANCED_PATHS):
            self.advanced.set(True)
            self.show_page('assist')
            return
        for row,text in self.search_rows:
            if query and query not in text:row.pack_forget()
            else:row.pack(fill='x')

    def name_dialog(self,title,initial='',branches=None):
        window=tk.Toplevel(self.root)
        window.title(title)
        window.configure(background=SURFACE)
        window.resizable(False,False)
        window.transient(self.root)
        frame=ttk.Frame(window,padding=22)
        frame.pack(fill='both',expand=True)
        ttk.Label(frame,text=title,style='Section.TLabel').pack(anchor='w',pady=(0,14))
        name=tk.StringVar(value=initial)
        ttk.Label(frame,text='名称',style='Muted.TLabel').pack(anchor='w')
        entry=ttk.Entry(frame,textvariable=name,width=35)
        entry.pack(fill='x',pady=(4,12))
        selected_game=tk.StringVar(value=branches[0] if branches else '')
        if branches:
            ttk.Label(frame,text='旧文件中的配置分支',style='Muted.TLabel').pack(anchor='w')
            ChoiceInput(frame,selected_game,branches,{key:key for key in branches}).pack(fill='x',pady=(4,12))
        error=tk.StringVar()
        ttk.Label(frame,textvariable=error,foreground='#f49090').pack(anchor='w')
        result=[]
        def confirm():
            if not 1<=len(name.get().strip())<=80:error.set('名称应为 1～80 个字符。');return
            result.append((name.get().strip(),selected_game.get()) if branches else name.get().strip())
            window.destroy()
        buttons=ttk.Frame(frame);buttons.pack(fill='x',pady=(12,0))
        ttk.Button(buttons,text='确认',style='Primary.TButton',command=confirm).pack(side='right')
        ttk.Button(buttons,text='取消',command=window.destroy).pack(side='right',padx=8)
        window.bind('<Return>',lambda _:confirm())
        window.bind('<Escape>',lambda _:window.destroy())
        window.update_idletasks()
        window.geometry(f'+{self.root.winfo_rootx()+90}+{self.root.winfo_rooty()+100}')
        window.grab_set();entry.focus_set();entry.selection_range(0,'end')
        self.root.wait_window(window)
        return result[0] if result else None

    def new_profile(self):
        name=self.name_dialog('新建配置')
        if not name:return
        game='custom'
        def work():
            defaults=self.manager.inspect_defaults(game,f'[runtime]\ngame="{game}"\n')
            defaults['runtime.vision.model_path']=''
            return self.repository.create(name,game,defaults,validate=self.native_validator(game))
        self.run_job('正在创建独立配置…',work,lambda p:self.profile_created(p))

    def profile_created(self,profile):
        self.load_library(profile['id'])
        if not self.has_model_file():self.show_page('device')
        self.notice.set('配置已创建。选择识别模型后即可启动；调整参数后点击保存。')

    def native_validator(self,game):
        if not self.manager.executable.is_file():return None
        return lambda path:self.manager.validate(path,[game])

    def manage_action(self,*_):
        action=self.manage_value.get()
        if action=='manage':return
        self.manage_value.set('manage')
        if self.busy:return
        if action=='import':self.import_profile();return
        if action=='reload':self.reload_library();return
        if not self.profile:self.notice.set('先创建或导入一份配置。');return
        if action=='copy':
            name=self.name_dialog('复制当前配置',self.profile['name']+' 副本')
            if name:
                try:candidate=self.collect()
                except ValueError as error:self.notice.set(str(error));return
                self.run_job('正在复制配置…',lambda:self.repository.duplicate(candidate,name,self.native_validator(candidate['game'])),self.profile_created)
        elif action=='rename':
            name=self.name_dialog('重命名配置',self.profile['name'])
            if name:
                state=self.state
                candidate=deepcopy(state['saved']);candidate['name']=name
                def done(expected):
                    state['expected']=expected
                    state['saved']['name']=state['data']['name']=name
                    self.refresh_strip();self.notice.set('配置已重命名。')
                self.run_job('正在重命名…',lambda:self.repository.save(candidate,state['expected'],self.native_validator(candidate['game'])),done)
        elif action=='delete':
            if self.owns_active_config(self.manager.active()):
                self.notice.set('当前配置正在运行，请先停止运行再删除。');return
            profile=deepcopy(self.profile)
            expected=self.state['expected']
            if not messagebox.askyesno('删除配置',f"删除“{profile['name']}”？未保存修改会丢弃，磁盘配置将保留在删除备份中。",parent=self.root):return
            def done(archive):
                self.states.pop(profile['id'],None)
                self.profile=None;self.point_dirty=False
                self.load_library()
                self.notice.set('配置已删除。可从删除备份中的 JSON 重新导入：'+str(archive))
            self.run_job('正在删除配置…',lambda:self.repository.delete(profile,expected),done)
        elif action=='export':self.export_profile()
        elif action=='reset':
            if messagebox.askyesno('恢复创建时的参数','把当前草稿恢复为配置创建时的参数和曲线？保存后才会写入文件。',parent=self.root):
                initial=deepcopy(self.profile['initial'])
                self.profile.update(initial)
                self.state['raw']={f[0]:raw_value(f,configured_value(self.profile['config'],f[0],f[3])) for f in FIELDS}
                self.state['model']=self.model_for(self.profile)
                self.select_profile(self.profile['id'])

    def import_profile(self):
        path=filedialog.askopenfilename(parent=self.root,title='导入为独立配置',filetypes=[('配置文件','*.toml *.json'),('所有文件','*.*')])
        if not path:return
        try:choices=self.repository.import_choices(path)
        except (OSError,ValueError,KeyError,TypeError) as error:
            self.notice.set('导入失败：'+str(error));return
        result=self.name_dialog('导入配置',Path(path).stem,choices if len(choices)>1 else None)
        if not result:return
        if len(choices)>1:name,game=result
        else:name,game=result,choices[0]
        self.run_job('正在读取并校验导入配置…',lambda:self.repository.import_file(path,name,game,
            self.manager.inspect_defaults(game,self.repository.import_text(path)),self.native_validator(game)),self.profile_created)

    def export_profile(self):
        try:candidate=self.collect()
        except ValueError as error:self.notice.set(str(error));return
        path=filedialog.asksaveasfilename(parent=self.root,title='导出配置',initialfile=export_filename(self.profile['name'],'.json'),
            defaultextension='.json',filetypes=[('完整配置 JSON','*.json'),('原生配置 TOML','*.toml')])
        if not path:return
        def work():
            if Path(path).suffix.lower()=='.toml':
                target=Path(path)
                if target.exists():
                    ConfigStore(self.project,target).save(toml_text(projection(candidate)),target.read_bytes())
                else:
                    # The save dialog already confirmed the export destination.
                    with target.open('x',encoding='utf-8',newline='\n') as stream:stream.write(toml_text(projection(candidate)))
            else:
                writer=UiPreferences(self.project);writer.path=Path(path);writer.write(candidate)
        self.run_job('正在导出配置…',work,lambda _:self.notice.set('配置已导出，包括当前有效草稿。'))

    def reload_library(self):
        if any(self.dirty(s) for s in self.states.values()) and not messagebox.askyesno('重新载入','丢弃未保存草稿，重新读取磁盘配置？',parent=self.root):return
        identifier=self.profile['id'] if self.profile else None
        self.states={};self.profile=None;self.point_dirty=False
        self.load_library(identifier)
        self.notice.set('已重新载入。'+(' 无法读取：'+'；'.join(self.repository.errors) if self.repository.errors else ''))

    def choose_model(self):
        path=filedialog.askopenfilename(parent=self.root,title='选择 TensorRT 识别模型',filetypes=[('TensorRT 模型','*.engine'),('所有文件','*.*')])
        if path:
            value=Path(path).resolve()
            try:value=value.relative_to(self.project)
            except ValueError:pass
            self.variables['runtime.vision.model_path'].set(value.as_posix())
            self.notice.set('模型已选择。请核对模型输入尺寸，再点击启动配置；启动时会保存当前配置。')
            return True
        return False

    def has_model_file(self):
        if not self.profile:return False
        value=self.state['raw'].get('runtime.vision.model_path','').strip()
        return bool(value) and (self.project/value).is_file()

    def collect(self):
        if self.point_dirty and not self.apply_precision():raise ValueError('请修正选中控制点的坐标。')
        data=deepcopy(self.profile)
        for field in FIELDS:put(data['config'],field[0],field_value(field,self.state['raw'][field[0]]))
        self.repository.check(data)
        return data

    def owns_active_config(self,record):
        return bool(record and self.profile and record['game']==self.profile['game'] and
            Path(record.get('config_path',self.project/'config.toml')).resolve()==self.repository.runtime_path(self.profile).resolve())

    def save(self):
        if self.busy or not self.profile:return
        try:candidate=self.collect()
        except ValueError as error:self.notice.set(str(error));return
        state=self.state
        def work():
            expected=self.repository.save(candidate,state['expected'],self.native_validator(candidate['game']))
            result={'expected':expected}
            if self.owns_active_config(self.manager.active()):
                try:result['reload']=self.manager.reload_config()
                except Exception as error:result['apply_error']=str(error)
            return result
        def done(result):
            state['saved']=deepcopy(candidate)
            state['data'].update(deepcopy(candidate))
            state['expected']=result['expected']
            state['raw']={f[0]:raw_value(f,configured_value(candidate['config'],f[0],f[3])) for f in FIELDS}
            self.loading=True
            for path,variable in self.variables.items():variable.set(state['raw'][path])
            self.loading=False
            if self.curve_editor:
                self.curve_editor.baseline=deepcopy(candidate['curve']['definition']['points']);self.curve_editor.draw()
            self.refresh_strip()
            self.notice.set('配置已保存，将在下次启动时生效。')
            if 'reload' in result:self.applied(result['reload'])
            if 'apply_error' in result:
                self.restart_required=True
                self.notice.set('配置已保存，尚未应用：'+result['apply_error'])
        self.run_job('正在校验并保存…',work,done)

    def applied(self,result):
        status=result.get('status')
        self.restart_required=status==3
        if status!=1:self.pending_reload=None
        if status==2:
            self.pending_reload=None
            action='保留' if 'learning preserved' in result.get('message','') else '重置'
            self.notice.set(f'已热重载，配置版本 {result.get("revision", "—")}；响应学习数据已{action}。')
        elif status==1:
            active=self.manager.active()
            self.pending_reload=(active['process_id'],result['request_id']) if active else None
            self.notice.set('配置已保存；重载已受理，等待新视觉帧，尚未全部生效。')
        elif status==3:
            self.notice.set('配置已保存，需要重启：'+result.get('message','').removeprefix('restart required: '))
        else:
            self.restart_required=True
            self.notice.set('配置已保存，尚未应用：'+result.get('message','未知错误'))

    def apply_saved_config(self):
        if self.dirty():self.save()
        elif self.owns_active_config(self.manager.active()):self.run_job('正在热重载…',self.manager.reload_config,self.applied)
        else:self.notice.set('当前配置未运行；保存内容将在启动时生效。')

    def primary_action(self,force_restart=False):
        if self.busy or not self.profile:return
        if not force_restart and self.owns_active_config(self.runtime_status.get('record')) and not self.restart_required:
            self.apply_saved_config();return
        # A fresh profile deliberately has no model. Complete this dependency
        # before saving or stopping another profile, on the UI thread where
        # the user can act on it rather than a transient worker status line.
        if not self.has_model_file():
            self.show_page('device')
            if not self.choose_model():
                self.notice.set('未选择识别模型，配置尚未启动。请点击“选择模型”完成设置。')
            self.refresh_actions()
            return
        try:candidate=self.collect()
        except ValueError as error:self.notice.set(str(error));return
        def checked(shape):
            config=candidate['config']
            w,h=shape['input_width'],shape['input_height']
            proposed={'tensor_width':w,'tensor_height':h}
            cw,ch=(lookup(config,'runtime.vision.'+key) for key in ('capture_width','capture_height'))
            if lookup(config,'runtime.vision.require_isotropic_resize',True) and cw*h!=ch*w:
                divisor=math.gcd(w,h);ux,uy=w//divisor,h//divisor
                scale=max(math.ceil(32/min(ux,uy)),min(round(cw/ux),8192//max(ux,uy)))
                proposed.update(capture_width=ux*scale,capture_height=uy*scale)
            changed={key:value for key,value in proposed.items() if lookup(config,'runtime.vision.'+key)!=value}
            if changed:
                text=f'模型实际输入为 {w}×{h}，当前配置与模型不匹配。\n'
                names={'tensor_width':'模型输入宽度','tensor_height':'模型输入高度',
                       'capture_width':'捕获宽度','capture_height':'捕获高度'}
                text+='\n'.join(f'{names[key]}：{lookup(config,"runtime.vision."+key)} → {value}' for key,value in changed.items())
                if not messagebox.askyesno('同步模型规格后启动',text+'\n\n应用这些规格并启动？其他参数保持当前草稿。',parent=self.root):
                    self.show_page('device');self.notice.set('启动已取消：模型规格尚未匹配，配置和运行实例未改变。');return
                for key,value in changed.items():self.variables['runtime.vision.'+key].set(str(value))
                candidate.update(self.collect())
            self.start_prepared_profile(candidate,force_restart)
        self.run_job('正在读取模型真实输入规格…',lambda:self.manager.inspect_model(
            lookup(candidate['config'],'runtime.vision.model_path')),checked)

    def start_prepared_profile(self,candidate,force_restart=False):
        state=self.state
        def work():
            expected=self.repository.save(candidate,state['expected'],self.native_validator(candidate['game']))
            try:
                model=self.project/lookup(candidate['config'],'runtime.vision.model_path','')
                if not model.is_file():raise ValueError('识别模型文件不存在，请在设备与运行中选择模型。')
                active=self.manager.active()
                if active:
                    self.manager.stop()
                    deadline=time.monotonic()+8
                    while self.manager.active():
                        if time.monotonic()>deadline:raise ValueError('退出请求已发送，但旧程序尚未退出；请查看运行日志。')
                        time.sleep(.05)
                record=self.manager.start(candidate['game'],projection(candidate),self.repository.runtime_path(candidate),candidate['id'])
                return {'expected':expected,'record':record}
            except Exception as error:
                return {'expected':expected,'start_error':str(error)}
        def done(result):
            state['saved']=deepcopy(candidate);state['data'].update(deepcopy(candidate));state['expected']=result['expected']
            state['raw']={f[0]:raw_value(f,configured_value(candidate['config'],f[0],f[3])) for f in FIELDS}
            self.restart_required=False;self.pending_reload=None
            self.refresh_strip()
            self.notice.set('启动失败：'+result['start_error'] if 'start_error' in result else '启动请求已提交，正在等待原生程序与手柄就绪。')
        self.run_job('正在保存并重启…' if force_restart or self.manager.active() else '正在保存并启动…',work,done)

    def stop_runtime(self):self.run_job('正在请求正常退出…',self.manager.stop,lambda _:self.notice.set('已发送正常退出请求。'))

    def run_job(self,label,work,done=None):
        if self.busy:return
        self.busy=True
        self.notice.set(label)
        self.refresh_actions()
        for widget in self.inputs:
            if widget.winfo_exists():widget.state(['disabled'])
        def worker():
            try:self.jobs.put((done,work(),None))
            except Exception as error:self.jobs.put((done,None,str(error)))
        threading.Thread(target=worker,daemon=True).start()

    def runtime_menu_action(self,*_):
        action=self.runtime_menu_value.get()
        if action=='tools':return
        self.runtime_menu_value.set('tools')
        if self.busy or not self.profile:return
        if action=='restart':self.primary_action(force_restart=True)
        elif action=='raw':self.raw_editor()

    def refresh_actions(self):
        for widget in [self.primary,self.runtime_menu,self.stop_button,self.save_button,self.new_button,self.manage_button,*self.navigation.values()]:
            widget.state(['disabled'] if self.busy else ['!disabled'])
        if self.busy:return
        active=self.runtime_status.get('record')
        self.stop_button.state(['!disabled'] if active else ['disabled'])
        self.primary.state(['!disabled'] if self.profile else ['disabled'])
        self.save_button.state(['!disabled'] if self.profile and self.dirty() else ['disabled'])
        owned=self.owns_active_config(active)
        self.runtime_menu.state(['!disabled'] if self.profile else ['disabled'])
        if owned:self.save_button.pack_forget()
        elif not self.save_button.winfo_manager():self.save_button.pack(side='right',after=self.stop_button)
        self.primary.configure(text=('保存并重启' if self.restart_required else '保存并应用') if owned else
            '选择模型' if self.profile and not self.has_model_file() else '切换并启动' if active else '启动配置')

    def build_feedback(self,parent):
        self.device_text=tk.StringVar(value='未运行')
        ttk.Label(parent,textvariable=self.device_text,style='Accent.TLabel').pack(anchor='w',pady=(0,12))
        tools=ttk.Frame(parent);tools.pack(fill='x',pady=(0,18))
        self.fusion_button=self.register(ttk.Button(tools,text='开启 Fusion',command=self.toggle_fusion))
        self.fusion_button.pack(side='left')
        self.register(ttk.Button(tools,text='查看日志…',command=self.show_logs)).pack(side='left',padx=8)
        self.register(ttk.Button(tools,text='导出响应学习…',command=self.export_learning)).pack(side='left')
        rates=ttk.Frame(parent)
        rates.pack(fill='x',pady=(0,12))
        rates.columnconfigure((0,1,2),weight=1,uniform='fps')
        self.frame_rate_values=[tk.StringVar(master=self.root,value='—') for _ in range(3)]
        for column,(title,variable) in enumerate(zip(('应用平均 FPS','Aim 平均 FPS','近 5 秒 Aim FPS'),self.frame_rate_values)):
            group=ttk.Frame(rates)
            group.grid(row=0,column=column,sticky='ew')
            ttk.Label(group,text=title,style='Muted.TLabel').pack(anchor='w')
            ttk.Label(group,textvariable=variable,style='Title.TLabel').pack(anchor='w')
        self.frame_rate_detail=tk.StringVar(master=self.root)
        ttk.Label(parent,textvariable=self.frame_rate_detail,style='Muted.TLabel',wraplength=760).pack(anchor='w',pady=(0,6))
        ttk.Label(parent,text='FPS 为新视觉结果的消费帧率；Aim 按实体开镜计时。统计只驻留内存，无需开启日志。',
                  style='Muted.TLabel',wraplength=760).pack(anchor='w',pady=(0,14))
        self.show_frame_rates()
        ttk.Label(parent,text='响应学习',style='Section.TLabel').pack(anchor='w',pady=(0,8))
        self.learning_summary=tk.StringVar(value='尚无运行数据。')
        ttk.Label(parent,textvariable=self.learning_summary,style='Muted.TLabel',wraplength=760).pack(anchor='w',pady=(0,10))
        self.learning_table=ttk.Treeview(parent,columns=('region','effective','learned','confidence','samples'),show='headings',height=4)
        for key,label in [('region','区域'),('effective','当前响应'),('learned','学习响应'),('confidence','置信度'),('samples','样本数')]:
            self.learning_table.heading(key,text=label);self.learning_table.column(key,width=100,anchor='center')
        for i,label in enumerate(['跟随 · 普通区','跟随 · 减速区','ADS · 普通区','ADS · 减速区']):
            self.learning_table.insert('', 'end',iid=str(i),values=(label,'—','—','—','—'))
        self.learning_table.pack(fill='x')
        ttk.Label(parent,text='响应单位：px / (有效摇杆 × 秒)。这是控制器内部响应估计，不能代替独立游戏曲线校准。',
            style='Muted.TLabel',wraplength=760).pack(anchor='w',pady=(12,0))

    def show_frame_rates(self):
        record=self.runtime_status.get('record')
        data=self.last_frame_rates
        values=[None,None,None]
        if data:
            values=[rate(data['vision_frames'],data['elapsed_ns']),rate(data['aim_frames'],data['aim_ns']),
                    rate(data['recent_aim_frames'],data['recent_aim_ns'])]
            owner=self.states.get(self.frame_rate_record.get('profile_id'))
            name=owner['saved']['name'] if owner else LEGACY_GAME_LABELS.get(self.frame_rate_record['game'],self.frame_rate_record['game'])
            mode='已停止' if not record or data['state']==2 else 'Aim' if data['aiming'] else '空闲'
            detail=f'{name} · {mode} · Aim {data["aim_ns"]/1_000_000_000:.1f} 秒 / {data["aim_frames"]:,} 帧'
            if record and data['state']==0:detail+=' · 等待首个控制周期'
        elif record:detail='当前运行实例尚未提供内存帧率；旧版本需重启更新后的原生程序。'
        else:detail='启动配置后自动统计；每次启动从零开始，结束后在本窗口保留最近收到的数据。'
        for variable,value in zip(self.frame_rate_values,values):
            text='—' if value is None else f'{value:.1f}'
            if variable.get()!=text:variable.set(text)
        if self.frame_rate_detail.get()!=detail:self.frame_rate_detail.set(detail)

    def toggle_fusion(self):
        enabled=not bool(self.manager.fusion_state())
        self.run_job('正在打开 Fusion…' if enabled else '正在关闭 Fusion…',lambda:self.manager.set_fusion(enabled),
                     lambda _:self.notice.set('Fusion 已打开。' if enabled else 'Fusion 已关闭。'))

    def export_learning(self):
        data=self.manager.learning()
        if not data:self.notice.set('当前没有可导出的原生学习数据。');return
        folder=self.project/'runs/desktop/learning';folder.mkdir(parents=True,exist_ok=True)
        path=folder/(datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.json')
        value={'exported_at_utc':datetime.now(timezone.utc).isoformat(),'runtime':self.manager.active(),
            'units':'px / (effective_stick * second)','region_order':['body_free','body_slow','ads_free','ads_slow'],
            'measurement_kind':'controller response estimate, not independent game calibration','learning':data}
        writer=UiPreferences(self.project);writer.path=path
        try:writer.write(value);self.notice.set('已导出响应学习：'+path.name)
        except OSError as error:self.notice.set(str(error))

    def raw_editor(self):
        try:text=toml_text(projection(self.collect()))
        except ValueError as error:self.notice.set(str(error));return
        window=tk.Toplevel(self.root);window.title('完整配置 · 当前草稿');window.geometry('850x600');window.transient(self.root)
        frame=ttk.Frame(window,padding=16);frame.pack(fill='both',expand=True)
        ttk.Label(frame,text='编辑后先校验，再写入当前草稿。保存修改时才写入配置库。',style='Muted.TLabel').pack(anchor='w',pady=(0,10))
        editor=tk.Text(frame,background=RAIL,foreground=INK,insertbackground=INK,font=('Consolas',10),undo=True,wrap='none',padx=12,pady=10)
        editor.pack(fill='both',expand=True);editor.insert('1.0',text)
        error=tk.StringVar();ttk.Label(frame,textvariable=error,foreground='#f49090',wraplength=790).pack(anchor='w',pady=8)
        def apply():
            if self.busy:return
            try:
                text=editor.get('1.0','end-1c')
                data=tomllib.loads(text)
                if 'games' in data or lookup(data,'runtime.game')!=self.profile['game']:
                    raise ValueError('完整配置必须保持当前运行标识，且不包含 games 继承表。')
            except (ValueError,KeyError,TypeError) as problem:error.set(str(problem));return
            profile=self.profile
            button.state(['disabled']);error.set('正在校验…')
            def work():
                try:
                    defaults=self.manager.inspect_defaults(profile['game'],text)
                    candidate=snapshot(data,profile['game'],self.project,defaults)
                    native=self.native_validator(profile['game'])
                    if native:
                        handle,filename=tempfile.mkstemp(prefix='.editor-',suffix='.toml',dir=self.project)
                        temporary=Path(filename)
                        try:
                            with os.fdopen(handle,'w',encoding='utf-8',newline='\n') as stream:stream.write(toml_text(data))
                            native(temporary)
                        finally:temporary.unlink(missing_ok=True)
                    return candidate,None
                except (ValueError,KeyError,TypeError,OSError) as problem:return None,str(problem)
            def done(result):
                if not window.winfo_exists():return
                candidate,problem=result
                button.state(['!disabled'])
                if problem:error.set(problem);return
                profile.update(candidate)
                state=self.states[profile['id']]
                state['raw']={f[0]:raw_value(f,configured_value(candidate['config'],f[0],f[3])) for f in FIELDS}
                state['model']=self.model_for(profile)
                window.destroy();self.select_profile(profile['id']);self.notice.set('完整配置已进入草稿；点击顶部保存后生效。')
            self.run_job('正在校验完整配置…',work,done)
        button=ttk.Button(frame,text='校验并写入草稿',style='Primary.TButton',command=apply)
        button.pack(anchor='e')
        window.bind('<Escape>',lambda _:window.destroy());window.grab_set();editor.focus_set()

    def show_logs(self):
        status=self.manager.status();record=status.get('record') or status.get('last_record')
        window=tk.Toplevel(self.root);window.title('运行日志');window.geometry('900x580');window.configure(background=SURFACE)
        editor=tk.Text(window,background=RAIL,foreground=INK,insertbackground=INK,font=('Consolas',10),wrap='word',padx=12,pady=12)
        editor.pack(fill='both',expand=True)
        value='尚无运行日志。'
        if record:value=tail(record.get('stdout_path',''))+'\n'+tail(record.get('stderr_path',''))
        editor.insert('1.0',value);editor.configure(state='disabled')

    def process_jobs(self):
        while True:
            try:done,result,error=self.jobs.get_nowait()
            except queue.Empty:break
            self.busy=False
            for widget in self.inputs:
                if widget.winfo_exists():widget.state(['!disabled'])
            if error:self.notice.set('操作未完成：'+error)
            elif done:done(result)
            self.observation_after=time.monotonic()
            self.next_observation=0.
            self.refresh_actions()
            if self.curve_editor:self.point_selected(self.curve_editor.selected)

    def poll(self):
        if self.closed:return
        self.process_jobs()
        try:policy,error=self.ads_policy_results.get_nowait()
        except queue.Empty:pass
        else:
            self.ads_policy_loading=False
            if error:self.notice.set(error)
            else:
                self.ads_policy=policy
                if self.page=='ads' and self.ads_diagram:self.ads_diagram.set_policy(policy)
        try:devices,error=self.device_results.get_nowait()
        except queue.Empty:pass
        else:
            self.device_scanning=False
            if error:self.notice.set('手柄识别失败：'+error)
            else:
                self.input_devices=devices
                self.refresh_device_choices()
                if not devices:self.notice.set('未识别到可选择的手柄；连接后点击刷新。')
        if not self.busy:
            if time.monotonic()>=self.next_observation:
                self.observer.request(learning=self.page=='feedback' or bool(self.pending_reload),fusion=self.page=='feedback',
                                      performance=self.page=='feedback')
                self.next_observation=time.monotonic()+.5
            observation=self.observer.drain()
            if observation:
                if 'error' in observation:self.notice.set('运行状态读取失败：'+observation['error'])
                elif observation['sampled_at']>=self.observation_after:self.apply_observation(observation)
        self.poll_id=self.root.after(50,self.poll)

    def apply_observation(self,observation):
        status=observation['status'];self.runtime_status=status
        record=status.get('record');phase=status['phase']
        if record:
            session=(record['process_id'],record.get('process_created'))
            if session!=self.frame_rate_session:
                self.frame_rate_session=session
                self.last_frame_rates=None
            self.frame_rate_record=dict(record)
            performance=observation.get('performance')
            if performance is not None:self.last_frame_rates=performance
        description={'stopped':'已停止','starting':'正在启动','running':'运行中','waiting_device':'等待手柄','stopping':'正在停止','failed':'运行失败'}.get(phase,phase)
        if record:
            state=self.states.get(record.get('profile_id'))
            name=state['saved']['name'] if state else LEGACY_GAME_LABELS.get(record['game'],record['game'])
            description+=' · '+(name if len(name)<=10 else name[:9]+'…')
        self.status_text.set(description)
        if phase=='failed' and status.get('error')!=getattr(self,'last_runtime_failure',None):
            self.last_runtime_failure=status.get('error')
            self.notice.set('运行失败：'+status['error'].strip().splitlines()[-1])
        learning=observation['learning']
        if self.pending_reload:
            pid,request=self.pending_reload
            if not record or record.get('process_id')!=pid:self.pending_reload=None
            elif learning and learning.get('completed_id')==request and learning.get('status')!=1:self.applied(learning)
        if self.page=='feedback' and self.profile:
            self.show_frame_rates()
            owner=' · '+LEGACY_GAME_LABELS.get(record['game'],record['game']) if record else ''
            self.device_text.set(status.get('device','未运行')+owner+(' · 虚拟输出已连接' if status.get('virtual_connected') else ''))
            fusion=observation['fusion']
            if fusion is not None:
                self.fusion_button.configure(text='关闭 Fusion' if fusion else '开启 Fusion')
                self.fusion_button.state(['!disabled'] if fusion or status.get('initialized') else ['disabled'])
            if learning:
                self.learning_summary.set(f'配置版本 {learning["revision"]} · 手动输入 {learning["manual_fire_input"]} · 自动输出 {learning["fire_output"]}')
                for i,values in enumerate(learning['regions']):
                    self.learning_table.item(str(i),values=(['跟随 · 普通区','跟随 · 减速区','ADS · 普通区','ADS · 减速区'][i],
                        f'{values["effective"]:.2f}',f'{values["learned"]:.2f}' if values['samples'] else '未学习',f'{values["confidence"]:.1%}',values['samples']))
            else:
                self.learning_summary.set('应用未运行，暂无学习数据。' if not record else '原生程序尚未提供学习数据。')
                for i in range(4):self.learning_table.item(str(i),values=(['跟随 · 普通区','跟随 · 减速区','ADS · 普通区','ADS · 减速区'][i],'—','—','—','—'))
        self.refresh_actions()

    def close(self):
        if self.busy:self.notice.set('请等待当前保存或运行操作完成后关闭。');return
        if (self.point_dirty or any(self.dirty(s) for s in self.states.values())) and not messagebox.askyesno('关闭配置工作室','有未保存的修改，确定丢弃草稿并关闭？',parent=self.root):return
        self.closed=True
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
    try:
        from .web_host import launch
        launch(project)
    except Exception as error:
        # pythonw has no console: startup failures must stay visible.
        ctypes.windll.user32.MessageBoxW(None, str(error), '手柄助手无法打开', 0x10)
        raise
    finally:
        kernel.CloseHandle(ui_lock)



if __name__ == '__main__':
    main()
