"""Direct manipulation of the same piecewise-linear curve executed natively."""
import math
import tkinter as tk
from copy import deepcopy
from .components import SURFACE, RAIL, INK, MUTED, ACCENT
from .curve_model import CurveModel


class CurveEditor(tk.Canvas):
    def __init__(self, parent, on_change=None, on_select=None):
        super().__init__(parent, background=RAIL, highlightthickness=1, highlightbackground='#343a45', height=260, takefocus=True)
        self.model = CurveModel([[i/10,i/10] for i in range(11)])
        self.on_change = on_change or (lambda _: None)
        self.on_select = on_select or (lambda _: None)
        self.on_error = lambda _: self.bell()
        self.before_edit = lambda: True
        self.baseline = deepcopy(self.points)
        self.enabled, self.snap, self.lock_x = True, False, False
        self.view = [0.,0.,1.]
        self.probe_x = None
        self.drag_origin = self.pan_origin = None
        self.paint_items = {}
        for sequence, callback in [('<Configure>',lambda _:self.draw()),('<Button-1>',self.pick),('<B1-Motion>',self.drag),
            ('<ButtonRelease-1>',self.release),('<Double-Button-1>',self.add),('<Button-2>',self.pan_start),('<B2-Motion>',self.pan),
            ('<Delete>',lambda _:self.remove()),('<Left>',lambda _:self.select(max(0,self.selected-1))),
            ('<Right>',lambda _:self.select(min(len(self.points)-1,self.selected+1))),
            ('<Up>',lambda e:self.nudge(.01 if e.state&1 else .001)),('<Down>',lambda e:self.nudge(-.01 if e.state&1 else -.001)),
            ('<Control-z>',lambda _:self.undo()),('<Control-y>',lambda _:self.redo()),('<Escape>',self.cancel)]:
            self.bind(sequence,callback)
        self.bind('<Destroy>',self.dispose,add='+')
        self.bind('<Motion>',self.probe)
        self.bind('<Leave>',self.clear_probe)

    def dispose(self,event):
        if event.widget==self:
            self.on_change=self.on_select=self.on_error=self.before_edit=None

    @property
    def points(self): return self.model.points
    @property
    def selected(self): return self.model.selected

    def set_points(self, points, baseline=None):
        self.model = CurveModel(points)
        self.baseline = deepcopy(baseline if baseline is not None else points)
        self.view = [0.,0.,1.]
        self.draw()
        self.on_select(self.selected)

    def state(self, spec=None):
        if spec is not None: self.enabled = 'disabled' not in spec
        return () if self.enabled else ('disabled',)

    def plot(self): return (48,24,max(80,self.winfo_width()-24),max(70,self.winfo_height()-42))

    def pixel(self,x,y,plot=None):
        left,top,right,bottom = self.plot() if plot is None else plot
        vx,vy,span = self.view
        return left+(x-vx)/span*(right-left),bottom-(y-vy)/span*(bottom-top)

    def value(self,x,y):
        left,top,right,bottom = self.plot()
        vx,vy,span = self.view
        return vx+(x-left)/(right-left)*span,vy+(bottom-y)/(bottom-top)*span

    def paint(self,key,kind,*coordinates,**options):
        options['state']='normal'
        if isinstance(key,tuple) and key[0]=='line':options['tags']=('series'+str(key[1]),)
        elif isinstance(key,tuple) and key[0]=='point':options['tags']=('points',)
        elif key=='selection':options['tags']=('selection',)
        previous=self.paint_items.get(key)
        if previous is None:
            item=getattr(self,'create_'+kind)(*coordinates,**options)
            self.layers_changed=True
        else:
            item,old_coordinates,old_options=previous
            if old_coordinates!=coordinates:self.coords(item,*coordinates)
            if old_options!=options:self.itemconfigure(item,**options)
        self.paint_items[key]=(item,coordinates,options)
        self.painted.add(key)

    def draw(self):
        self.painted=set()
        self.layers_changed=False
        plot=self.plot()
        left,top,right,bottom = plot
        vx,vy,span = self.view
        for i in range(5):
            x,y = left+i/4*(right-left),bottom-i/4*(bottom-top)
            self.paint(('grid-x',i),'line',x,top,x,bottom,fill='#343a45')
            self.paint(('grid-y',i),'line',left,y,right,y,fill='#343a45')
            self.paint(('label-x',i),'text',x,bottom+15,text=f'{(vx+i/4*span)*100:g}',fill=MUTED,font=('Segoe UI',8))
            self.paint(('label-y',i),'text',left-10,y,text=f'{(vy+i/4*span)*100:g}',anchor='e',fill=MUTED,font=('Segoe UI',8))
        self.paint('title-y','text',left,10,text='响应 %',anchor='w',fill=MUTED,font=('Microsoft YaHei UI',8))
        self.paint('title-x','text',right,bottom+30,text='输入 %',anchor='e',fill=MUTED,font=('Microsoft YaHei UI',8))
        readout='移动鼠标预览输入 → 响应'
        if self.probe_x is not None:
            x=self.probe_x
            a,b=next((a,b) for a,b in zip(self.points,self.points[1:]) if a[0]<=x<=b[0])
            y=a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0])
            readout=f'输入 {x*100:.1f}% → 响应 {y*100:.1f}%'
        self.paint('readout','text',right,10,text=readout,anchor='e',fill=ACCENT,font=('Microsoft YaHei UI',8))
        # Canvas clips to a tagged plot region by drawing only visible segments.
        for series,(points,colour,dash) in enumerate([([[0,0],[1,1]],'#48515f',(3,5)),(self.baseline,'#747e8c',(5,4)),(self.points,ACCENT,())]):
            for segment,(a,b) in enumerate(zip(points,points[1:])):
                t0,t1 = 0.,1.
                for axis,start in enumerate((vx,vy)):
                    delta = b[axis]-a[axis]
                    if delta:
                        t0=max(t0,(start-a[axis])/delta)
                        t1=min(t1,(start+span-a[axis])/delta)
                if t0<=t1:
                    p=[a[j]+(b[j]-a[j])*t0 for j in range(2)]
                    q=[a[j]+(b[j]-a[j])*t1 for j in range(2)]
                    self.paint(('line',series,segment),'line',*self.pixel(*p,plot=plot),*self.pixel(*q,plot=plot),fill=colour,width=2 if not dash else 1,dash=dash)
        for i,p in enumerate(self.points):
            x,y=self.pixel(*p,plot=plot)
            if left<=x<=right and top<=y<=bottom:
                radius=6 if i==self.selected else 4
                self.paint(('point',i),'oval',x-radius,y-radius,x+radius,y+radius,fill=INK if i==self.selected else ACCENT,outline=RAIL,width=2)
                if i==self.selected:
                    self.paint('selection','text',min(right-45,max(left+45,x)),max(top+12,y-18),text=f'{p[0]*100:.3f} / {p[1]*100:.3f}',fill=INK,font=('Segoe UI',8))
        for key,(item,coordinates,options) in self.paint_items.items():
            if key not in self.painted and options['state']!='hidden':
                self.itemconfigure(item,state='hidden')
                options['state']='hidden'
        if self.layers_changed:
            for tag in ('series0','series1','series2','points','selection'):self.tag_raise(tag)

    def hit(self,event):
        plot=self.plot()
        distances=[math.hypot(event.x-x,event.y-y) for x,y in (self.pixel(*p,plot=plot) for p in self.points)]
        index=min(range(len(distances)),key=distances.__getitem__)
        return index if distances[index]<=13 else None

    def probe(self,event):
        left,top,right,bottom=self.plot()
        if left<=event.x<=right and top<=event.y<=bottom:
            self.probe_x=max(0.,min(1.,self.value(event.x,event.y)[0]))
            self.configure(cursor='hand2' if self.hit(event) is not None else 'crosshair')
        else:
            self.probe_x=None
            self.configure(cursor='')
        self.draw()

    def clear_probe(self,_=None):
        self.probe_x=None
        self.draw()

    def select(self,index):
        if not self.enabled or not self.before_edit():return 'break'
        self.model.selected=index
        self.draw()
        self.on_select(index)
        return 'break'

    def pick(self,event):
        if not self.enabled or not self.before_edit():return
        self.focus_set()
        index=self.hit(event)
        self.drag_origin=None
        if index is not None:
            self.select(index)
            if self.selected!=index:return
            self.model.begin()
            self.drag_origin=(event.x,event.y,deepcopy(self.points[index]))

    def drag(self,event):
        if not self.enabled or not self.drag_origin:return
        x,y=self.value(event.x,event.y)
        px,py,original=self.drag_origin
        if event.state&8:
            ox,oy=self.value(px,py)
            x,y=original[0]+(x-ox)*.1,original[1]+(y-oy)*.1
        if self.lock_x:x=original[0]
        if event.state&1:
            if abs(event.x-px)>abs(event.y-py):y=original[1]
            else:x=original[0]
        if self.snap:x,y=round(x,2),round(y,2)
        self.model.drag(self.selected,x,y)
        self.changed()

    def release(self,_=None):
        if self.drag_origin:
            self.model.commit()
            self.drag_origin=None
            self.on_select(self.selected)

    def cancel(self,_=None):
        if self.drag_origin:
            self.model.cancel()
            self.drag_origin=None
            self.changed()
        return 'break'

    def changed(self):
        self.draw()
        self.on_change(deepcopy(self.points))
        self.on_select(self.selected)

    def move_point(self,index,x,y):
        if self.enabled:
            self.model.begin()
            self.model.drag(index,x,y)
            self.model.commit()
            self.changed()

    def exact(self,x,y):
        if self.enabled:
            self.model.exact(self.selected,x,y)
            self.changed()

    def add(self,event):
        if not self.enabled or not self.before_edit():return
        self.release()
        if self.hit(event) is not None:
            self.on_select(self.selected)
            self.event_generate('<<CurvePrecisionRequested>>')
            return 'break'
        try:
            self.model.add(*self.value(event.x,event.y))
            self.changed()
        except ValueError as error: self.on_error(str(error))
        return 'break'

    def add_midpoint(self):
        if self.enabled and self.before_edit():
            i=min(self.selected,len(self.points)-2)
            a,b=self.points[i:i+2]
            self.model.add((a[0]+b[0])/2,(a[1]+b[1])/2)
            self.changed()

    def remove(self):
        if self.enabled and self.before_edit() and 0<self.selected<len(self.points)-1:
            self.model.remove()
            self.changed()
        return 'break'

    def nudge(self,amount):
        if self.enabled and self.before_edit():self.move_point(self.selected,self.points[self.selected][0],self.points[self.selected][1]+amount)
        return 'break'

    def undo(self):
        if self.enabled and self.before_edit() and self.model.undo_stack:
            self.model.undo();self.changed()
        return 'break'

    def redo(self):
        if self.enabled and self.before_edit() and self.model.redo_stack:
            self.model.redo();self.changed()
        return 'break'

    def zoom(self,factor):
        vx,vy,span=self.view
        new=min(1.,max(.125,span*factor))
        self.view=[min(1-new,max(0,vx+(span-new)/2)),min(1-new,max(0,vy+(span-new)/2)),new]
        self.draw()

    def fit(self):self.view=[0.,0.,1.];self.draw()

    def pan_start(self,event):self.pan_origin=(event.x,event.y,self.view[:])

    def pan(self,event):
        if self.pan_origin:
            x,y,view=self.pan_origin
            left,top,right,bottom=self.plot()
            self.view=[min(1-view[2],max(0,view[0]-(event.x-x)/(right-left)*view[2])),
                       min(1-view[2],max(0,view[1]+(event.y-y)/(bottom-top)*view[2])),view[2]]
            self.draw()


class ProfileStrip(tk.Canvas):
    def __init__(self,parent,on_select):
        super().__init__(parent,background=SURFACE,height=56,highlightthickness=0,takefocus=True)
        self.on_select=on_select
        self.entries=[]
        self.current=None
        self.bind('<Configure>',lambda _:self.draw())
        self.bind('<Button-1>',self.pick)
        self.bind('<Left>',lambda _:self.step(-1))
        self.bind('<Right>',lambda _:self.step(1))
        self.bind('<MouseWheel>',lambda e:self.xview_scroll(-int(e.delta/120),'units'))
        self.bind('<Destroy>',self.dispose,add='+')

    def dispose(self,event):
        if event.widget==self:self.on_select=None

    def set_profiles(self,entries,current):
        if self.entries==entries and self.current==current:return
        self.entries,self.current=entries,current
        self.draw()
        index=next((i for i,e in enumerate(entries) if e['id']==current),0)
        total=max(self.winfo_width(),len(entries)*174)
        start,end=self.xview()
        if index*174<start*total or (index+1)*174>end*total:self.xview_moveto(max(0,index*174-8)/total)

    def draw(self):
        self.delete('all')
        for i,entry in enumerate(self.entries):
            chosen=entry['id']==self.current
            left=i*174+1
            self.create_rectangle(left,3,left+164,52,fill=RAIL if chosen else SURFACE,outline=ACCENT if chosen else SURFACE,width=1)
            name=entry['name']
            self.create_text(left+12,19,text=name if len(name)<=13 else name[:12]+'…',anchor='w',fill=INK if chosen else MUTED,
                             font=('Microsoft YaHei UI',10,'bold' if chosen else 'normal'))
            self.create_text(left+12,39,text=entry['game'].upper(),anchor='w',fill=MUTED,font=('Segoe UI',8))
            if entry.get('dirty'):self.create_oval(left+148,12,left+154,18,fill=ACCENT,outline='')
        self.configure(scrollregion=(0,0,max(self.winfo_width(),len(self.entries)*174),56))

    def pick(self,event):
        self.focus_set()
        index=int(self.canvasx(event.x)//174)
        if 0<=index<len(self.entries):self.on_select(self.entries[index]['id'])

    def step(self,direction):
        if self.entries:
            index=next((i for i,e in enumerate(self.entries) if e['id']==self.current),0)
            self.on_select(self.entries[max(0,min(len(self.entries)-1,index+direction))]['id'])
