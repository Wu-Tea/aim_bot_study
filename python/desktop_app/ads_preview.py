"""Retained stage previews: first ADS acquisition and selected-target follow."""
import math
import tkinter as tk
from tkinter import ttk

from .ads_geometry import admission, envelope, follow_response_example, acquisition_example, filter_ai_input, f32
from .components import ACCENT, INK, MUTED, SURFACE, ChoiceInput, SegmentedInput
from .fields import GAME_FIELDS, COMMON_FIELDS, field_value
from .settings import lookup

PREVIEW_FIELDS={field[0]:field for field in GAME_FIELDS+COMMON_FIELDS}


class AdsEnvelopePreview(ttk.Frame):
    PATHS=('runtime.vision.capture_width','runtime.vision.capture_height','runtime.vision.target_height_ratio',
           'runtime.vision.target_wide_low_height_ratio','gamepad.ads.pickup_base_radius_px',
           'gamepad.ads.output_limit_x','gamepad.ads.output_limit_y',
           'gamepad.bodylock.output_limit_x','gamepad.bodylock.output_limit_y',
           'gamepad.bodylock.activation_range_px','gamepad.bodylock.response_time_x_ms',
           'gamepad.bodylock.response_time_y_ms','gamepad.ads.response_time_ms','gamepad.assist.input_deadzone','gamepad.assist.minimum_position_stick','gamepad.assist.arrival_radius_px')

    def __init__(self,parent,variables):
        super().__init__(parent,width=400,height=380)
        # The page owns our viewport; pack allocates the remaining space to
        # the canvas. Deriving canvas height from our own requested height
        # creates a feedback loop when wrapped text toggles the scrollbar.
        self.pack_propagate(False)
        self.variables,self.policy,self.geometry=variables,None,None
        self.profile_config={}
        self.available_height=0
        self.offset=[25.,0.]
        self.mode=tk.StringVar(value='acquire')
        self.compare=tk.BooleanVar(value=False)
        self.pose=tk.StringVar(value='standing')
        self.height=tk.StringVar(value='25')
        self.summary=tk.StringVar(value='正在读取原生触发规则…')
        self.feedback_summary=tk.StringVar()
        self.example_output=tk.StringVar()
        self.traces=[]
        self.mode_button=SegmentedInput(self,self.mode,['acquire','follow'],
            {'acquire':'① ADS Snap · 抓取','follow':'② BodyLock · 跟随'})
        self.mode_button.pack(fill='x',pady=(0,8))
        toolbar=ttk.Frame(self);toolbar.pack(fill='x',pady=(0,6))
        ChoiceInput(toolbar,self.pose,['standing','crouching','wide'],
            {'standing':'直立','crouching':'蹲姿','wide':'宽矮'},width=4).pack(side='left',padx=(0,6))
        ttk.Label(toolbar,text='高度').pack(side='left')
        ttk.Entry(toolbar,textvariable=self.height,width=4,justify='right').pack(side='left',padx=3)
        ttk.Label(toolbar,text='%').pack(side='left',padx=(0,6))
        for title,value in (('小',10),('中',25),('大',50)):
            ttk.Button(toolbar,text=title,width=2,command=lambda v=value:self.height.set(str(v))).pack(side='left',padx=1)
        positions=ttk.Frame(self);positions.pack(fill='x',pady=(0,4))
        ttk.Label(positions,text='示例位置',style='Muted.TLabel').pack(side='left',padx=(0,6))
        for title,value in (('居中',0),('圈内',.8),('圈外',1.15)):
            ttk.Button(positions,text=title,width=4,command=lambda v=value:self.place_example(v)).pack(side='left',padx=2)
        ttk.Checkbutton(positions,text='对比两圈',variable=self.compare).pack(side='left',padx=(8,0))
        self.legend=ttk.Label(self,text='蓝圈 ADS 拾取 · 紫圈 BodyLock 跟随 · 拖动蓝点比较范围',
                              style='Muted.TLabel',wraplength=400)
        self.legend.pack(anchor='w',pady=(0,4))
        self.canvas=tk.Canvas(self,height=280,background=SURFACE,highlightthickness=0,cursor='hand2')
        self.items={}
        for key,kind,style in (
            ('radius','oval',{'outline':ACCENT,'width':2}),
            ('follow_radius','oval',{'outline':'#b5a2de','width':2,'dash':(5,3)}),
            ('body','rectangle',{'outline':'#8b98a7','fill':'#26303b','width':1}),
            ('region','rectangle',{'outline':'#99bf9c','fill':'#334337','width':1}),
            ('aim','oval',{'outline':ACCENT,'fill':ACCENT}),
            ('horizontal','line',{'fill':INK,'width':1}),('vertical','line',{'fill':INK,'width':1}),
            ('distance','line',{'fill':'#8b98a7','dash':(3,3)}),
            ('frame','rectangle',{'outline':'#596473','width':1}),
            ('caption','text',{'fill':MUTED,'anchor':'nw','font':('Microsoft YaHei UI',9)}),
            ('center_label','text',{'fill':INK,'anchor':'ne','font':('Microsoft YaHei UI',9)}),
            ('aim_label','text',{'fill':ACCENT,'anchor':'nw','font':('Microsoft YaHei UI',9)}),
        ):
            self.items[key]=getattr(self.canvas,'create_'+kind)(0,0,0,0,**style) if kind!='text' else self.canvas.create_text(0,0,**style)
        self.canvas.bind('<Configure>',self.resized)
        self.canvas.bind('<Button-1>',self.drag)
        self.canvas.bind('<B1-Motion>',self.drag)
        self.summary_label=ttk.Label(self,textvariable=self.summary,style='Muted.TLabel',wraplength=400)
        self.feedback_label=ttk.Label(self,textvariable=self.feedback_summary,style='Muted.TLabel',wraplength=400)
        # Reserve captions first; the canvas consumes the remaining height.
        self.feedback_label.pack(side='bottom',anchor='w')
        self.summary_label.pack(side='bottom',anchor='w',pady=(4,8))
        self.canvas.pack(fill='both',expand=True)
        for variable in [variables[p] for p in self.PATHS]+[self.pose,self.height,self.mode,self.compare]:
            self.traces.append((variable,variable.trace_add('write',lambda *_:self.redraw())))
        self.bind('<Destroy>',self.destroyed,add='+')
        self.bind('<Configure>',lambda _:self.fit_height(self.available_height),add='+')

    def destroyed(self,event):
        if event.widget is not self:return
        for variable,trace in self.traces:variable.trace_remove('write',trace)
        self.traces.clear()
        self.pose=self.height=self.mode=self.compare=None
        self.summary=self.feedback_summary=self.example_output=None
        self.variables=None

    @staticmethod
    def context_for(path):
        if path.startswith('gamepad.bodylock.'):return 'follow'
        if path in ('gamepad.ads.output_limit_x','gamepad.ads.output_limit_y','gamepad.ads.pickup_base_radius_px',
                    'runtime.vision.target_height_ratio','runtime.vision.target_wide_low_height_ratio'):return 'acquire'
        return None

    @property
    def active_mode(self):
        return self.mode.get()

    def place_example(self,fraction):
        if self.geometry is None:return
        radius=self.geometry['radius' if self.active_mode=='acquire' else 'follow_radius']
        self.offset=[radius*fraction,0.]
        self.redraw()

    def set_policy(self,policy):
        self.policy=policy
        self.redraw()

    def set_config(self,config):
        self.profile_config=config
        self.redraw()

    def resized(self,event):
        self.legend.configure(wraplength=max(180,event.width))
        self.summary_label.configure(wraplength=max(180,event.width))
        self.feedback_label.configure(wraplength=max(180,event.width))
        self.redraw()

    def fit_height(self,available):
        self.available_height=available
        if available<100 or not self.winfo_exists():return
        desired=max(240,min(650,available-16))
        if int(self.cget('height'))!=desired:self.configure(height=desired)

    def number(self,path):
        return field_value(PREVIEW_FIELDS[path],self.variables[path].get())

    def redraw(self):
        if not self.winfo_exists():return
        if self.policy is None:
            self.summary.set('构建更新的原生程序后可读取实际触发规则。')
            return
        if self.policy.get('parameter_semantics')!='range-response-v3':
            self.geometry=None
            for item in self.items.values():self.canvas.itemconfigure(item,state='hidden')
            self.summary.set('请构建支持独立输出上限与响应时间的原生程序。')
            self.example_output.set('')
            return
        try:
            width=self.number('runtime.vision.capture_width');height=self.number('runtime.vision.capture_height')
            size=float(self.height.get())/100
            if not math.isfinite(size) or not .01<=size<=.9:raise ValueError('示例高度应为 1%～90%。')
            body_height=height*size
            body_width=body_height*{'standing':.35,'crouching':.8,'wide':2.4}[self.pose.get()]
            ordinary=self.number('runtime.vision.target_height_ratio');wide=self.number('runtime.vision.target_wide_low_height_ratio')
            if not 0<ordinary<1 or not 0<wide<1:raise ValueError('请修正瞄点比例。')
            geometry=envelope(self.policy,body_width,body_height,height,ordinary,wide,0)
            self.number('gamepad.assist.input_deadzone')
            geometry.update(admission(self.policy,body_width,body_height,width,height))
            # Independent drawings: an invalid inactive draft suppresses its
            # comparison ring, never invents a replacement radius or blocks
            # the other stage's valid calculation.
            try:
                geometry['radius']=envelope(self.policy,body_width,body_height,height,ordinary,wide,
                    self.number('gamepad.ads.pickup_base_radius_px'))['radius']
            except ValueError:
                if self.active_mode=='acquire':raise
                geometry['radius']=None
            try:
                follow_base=self.number('gamepad.bodylock.activation_range_px')
                completion=self.number('gamepad.assist.arrival_radius_px')
                if not math.isfinite(completion) or not 1<=completion<=64:raise ValueError('近点收尾半径无效。')
                if follow_base<completion:raise ValueError('基础跟随半径不能小于 近点收尾半径。')
                follow=envelope(self.policy,body_width,body_height,height,ordinary,wide,follow_base)
                geometry['follow_radius']=follow['radius']
                geometry['cue_follow_radius']=follow_base
            except ValueError:
                if self.active_mode=='follow':raise
                geometry['follow_radius']=None
            if self.active_mode=='follow':
                time_x=self.number('gamepad.bodylock.response_time_x_ms')
                time_y=self.number('gamepad.bodylock.response_time_y_ms')
                force=self.number('gamepad.bodylock.output_limit_x')
                vertical_force=self.number('gamepad.bodylock.output_limit_y')
                geometry['response_time_ms']=(time_x,time_y)
                geometry['example']=follow_response_example(self.policy,time_x,time_y,force,vertical_force,*self.offset,radius_px=geometry['follow_radius'],minimum_stick=self.number('gamepad.assist.minimum_position_stick'),arrival_radius_px=self.number('gamepad.assist.arrival_radius_px'))
                geometry['caps']=(min(1,force),min(1,vertical_force))
            else:
                force=self.number('gamepad.ads.output_limit_x')
                vertical_force=self.number('gamepad.ads.output_limit_y')
                nominal=self.number('gamepad.ads.response_time_ms')/1000
                geometry['example']=acquisition_example(self.policy,geometry['normalized_height'],
                    force,vertical_force,*self.offset,nominal_horizon=nominal,output_limits=True,radius_px=geometry['radius'],minimum_stick=self.number('gamepad.assist.minimum_position_stick'),arrival_radius_px=self.number('gamepad.assist.arrival_radius_px'))
                geometry['caps']=geometry['example']['caps']
        except (ValueError,KeyError) as error:
            self.geometry=None
            for item in self.items.values():self.canvas.itemconfigure(item,state='hidden')
            self.summary.set(str(error));self.feedback_summary.set('请先修正参数。');self.example_output.set('')
            return
        self.geometry=geometry
        for item in self.items.values():self.canvas.itemconfigure(item,state='normal')
        self.canvas.itemconfigure(self.items['radius'],outline=ACCENT if self.active_mode=='acquire' else '#53677e',
                                  width=2 if self.active_mode=='acquire' else 1)
        self.canvas.itemconfigure(self.items['follow_radius'],outline='#b5a2de' if self.active_mode=='follow' else '#645e72',
                                  width=2 if self.active_mode=='follow' else 1)
        cw,ch=max(1,self.canvas.winfo_width()),max(1,self.canvas.winfo_height())
        if cw<=32 or ch<=32:return
        spatial_height=ch
        scale=max(.001,min((cw-32)/width,(spatial_height-36)/height))
        self.transform=((cw-width*scale)/2,(spatial_height-height*scale)/2,scale,width,height)
        x0,y0,_,_,_=self.transform
        def point(x,y):return (x0+x*scale,y0+y*scale)
        center=(width/2,height/2)
        ax,ay=center[0]+self.offset[0],center[1]+self.offset[1]
        bx,by=ax-geometry['aim'][0],ay-geometry['aim'][1]
        radius=geometry['radius']
        if radius is None:self.canvas.itemconfigure(self.items['radius'],state='hidden')
        else:self.canvas.coords(self.items['radius'],*point(center[0]-radius,center[1]-radius),*point(center[0]+radius,center[1]+radius))
        follow_radius=geometry['follow_radius']
        if follow_radius is None:self.canvas.itemconfigure(self.items['follow_radius'],state='hidden')
        else:self.canvas.coords(self.items['follow_radius'],*point(center[0]-follow_radius,center[1]-follow_radius),*point(center[0]+follow_radius,center[1]+follow_radius))
        self.canvas.coords(self.items['body'],*point(bx,by),*point(bx+body_width,by+body_height))
        left,top,right,bottom=geometry['region']
        self.canvas.coords(self.items['region'],*point(bx+left,by+top),*point(bx+right,by+bottom))
        px,py=point(ax,ay)
        self.canvas.coords(self.items['aim'],px-3,py-3,px+3,py+3)
        self.canvas.coords(self.items['aim_label'],px+7,py-16)
        self.canvas.itemconfigure(self.items['aim_label'],text='目标瞄点')
        px,py=point(*center)
        self.canvas.coords(self.items['horizontal'],px-7,py,px+7,py)
        self.canvas.coords(self.items['vertical'],px,py-7,px,py+7)
        self.canvas.coords(self.items['center_label'],px-9,py+8)
        self.canvas.itemconfigure(self.items['center_label'],text='准星')
        self.canvas.coords(self.items['distance'],*point(*center),*point(ax,ay))
        self.canvas.tag_lower(self.items['distance'],self.items['aim'])
        self.canvas.coords(self.items['frame'],*point(0,0),*point(width,height))
        self.canvas.coords(self.items['caption'],x0,y0-16)
        self.canvas.itemconfigure(self.items['caption'],text=f'{int(width)} × {int(height)} 捕获画面示意')
        distance=math.hypot(*self.offset)
        inside=radius is not None and distance<=radius
        following=follow_radius is not None and distance<=follow_radius
        shape='宽矮瞄点' if geometry['wide_low'] else '普通瞄点'
        ads_text=f'{radius:.1f} px' if radius is not None else '待修正'
        follow_text=f'{follow_radius:.1f} px' if follow_radius is not None else '待修正'
        label=f'蓝圈 ADS {ads_text} · 紫圈跟随 {follow_text}' if self.compare.get() else \
            f'ADS 抓取半径 {ads_text}' if self.active_mode=='acquire' else f'BodyLock 跟随半径 {follow_text}'
        self.legend.configure(text=label+' · 点击或拖动目标')
        if not self.compare.get():
            self.canvas.itemconfigure(self.items['follow_radius' if self.active_mode=='acquire' else 'radius'],state='hidden')
        state=('ADS 半径待修正' if radius is None else 'ADS 圈内，可拾取' if inside and geometry['pickup'] else 'ADS 圈外，不能新拾取' if not inside else '尺寸 / 外形不满足新拾取')
        if self.active_mode=='follow':
            state='跟随半径待修正' if follow_radius is None else '跟随圈内，可辅助已选目标' if following and geometry['tracking'] else '跟随圈外，停止辅助' if not following else '尺寸 / 外形不满足跟踪'
        self.summary.set(f'{shape} · 距准星 {distance:.1f} px · '+state)
        if not geometry['pickup'] and not geometry['tracking']:self.summary.set('尺寸 / 外形不满足拾取和跟踪\n'+state)
        self.describe_example(geometry,following)

    def describe_example(self,geometry,following):
        # One concrete position -> action. Directions and numeric units carry
        # meaning without requiring the reader to decode plotted coordinates.
        acquire=self.active_mode=='acquire'
        if acquire:
            stopped='目标在 ADS 圈外，不能新拾取。' if math.hypot(*self.offset)>geometry['radius'] else \
                    '目标尺寸 / 外形不足，不能新拾取。' if not geometry['pickup'] else ''
        else:
            stopped='目标在跟随圈外，停止跟随辅助。' if not following else \
                    '目标尺寸 / 外形不足，停止跟随辅助。' if not geometry['tracking'] else ''
        raw_output=(0.,0.) if stopped else geometry['example']['stick']
        deadzone=self.number('gamepad.assist.input_deadzone')
        output=filter_ai_input(raw_output,deadzone)
        def amount(value):return f'{abs(value):.1f}' if abs(value)>=.1 else '<0.1'
        lines=[]
        for axis,(value,force) in enumerate(zip(output,geometry['caps'])):
            direction=('右' if value>0 else '左') if axis==0 else ('上' if value>0 else '下')
            action=f'向{direction} {amount(value*100)}%' if value!=0 else '死区内，已过滤' if raw_output[axis]!=0 else '无需位置纠偏' if not stopped and force>0 else '不输出辅助'
            text=f'{"横向" if axis==0 else "纵向"}：{action} · 上限 {min(1,force)*100:g}%'
            lines.append(text)
        self.example_output.set('\n'.join(lines))
        stage='首次瞄准' if acquire else '持续跟随'
        explanation=stopped or f"{stage} · 低速参考 {geometry['example']['minimum_stick']*100:g}% · 瞄点收尾 {geometry['example']['arrival_radius_px']:g}px"
        if not stopped and geometry['example'].get('arrived'):
            explanation=f"{stage} · 已到瞄点，停止位置纠偏"
        self.feedback_summary.set(explanation)

    def drag(self,event):
        if self.geometry is None:return
        x0,y0,scale,width,height=self.transform
        self.offset=[max(-width/2,min(width/2,(event.x-x0)/scale-width/2)),
                     max(-height/2,min(height/2,(event.y-y0)/scale-height/2))]
        self.redraw()
