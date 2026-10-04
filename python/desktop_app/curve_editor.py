"""Direct manipulation of normalized, invertible response curves."""
import tkinter as tk
from .components import ACCENT, INK, MUTED, SURFACE
from .curves import validate_points


class CurveEditor(tk.Canvas):
    def __init__(self, parent, on_change, height=250):
        super().__init__(parent, height=height, background=SURFACE, highlightthickness=1,
                         highlightbackground='#334664', highlightcolor=ACCENT, takefocus=True)
        self.points = [[i/10, i/10] for i in range(11)]
        self.selected = 1
        self.enabled = True
        self.on_change = on_change
        self.bind('<Configure>', lambda _: self.draw())
        self.bind('<Button-1>', self.pick)
        self.bind('<B1-Motion>', self.drag)
        self.bind('<Double-Button-1>', self.add_point)
        self.bind('<Button-3>', self.remove_point)
        self.bind('<Delete>', self.remove_point)
        self.bind('<Left>', lambda _: self.select(-1))
        self.bind('<Right>', lambda _: self.select(1))
        self.bind('<Up>', lambda _: self.nudge(.01))
        self.bind('<Down>', lambda _: self.nudge(-.01))
        self.bind('<FocusIn>', lambda _: self.draw())
        self.bind('<FocusOut>', lambda _: self.draw())

    def bounds(self):
        return 44, 18, max(90, self.winfo_width()-22), max(70, self.winfo_height()-36)

    def pixel(self, point):
        left, top, right, bottom = self.bounds()
        return left + point[0]*(right-left), bottom-point[1]*(bottom-top)

    def value(self, x, y):
        left, top, right, bottom = self.bounds()
        return (x-left)/(right-left), (bottom-y)/(bottom-top)

    def set_points(self, points):
        self.points = validate_points(points)
        self.selected = min(self.selected, len(self.points)-2)
        self.draw()

    def draw(self):
        self.delete('all')
        left, top, right, bottom = self.bounds()
        for i in range(5):
            t = i/4
            x, y = left+t*(right-left), bottom-t*(bottom-top)
            self.create_line(x, top, x, bottom, fill='#25354d')
            self.create_line(left, y, right, y, fill='#25354d')
            self.create_text(x, bottom+16, text=f'{t:.0%}', fill=MUTED, font=('Segoe UI', 8))
            self.create_text(left-8, y, text=f'{t:.0%}', fill=MUTED, anchor='e', font=('Segoe UI', 8))
        self.create_line(left, bottom, right, top, fill='#52617a', dash=(4, 4))
        coordinates = [v for point in self.points for v in self.pixel(point)]
        self.create_line(*coordinates, fill=ACCENT, width=3)
        for index, point in enumerate(self.points):
            x, y = self.pixel(point)
            radius = 6 if index == self.selected else 4
            self.create_oval(x-radius,y-radius,x+radius,y+radius, fill=INK if index == self.selected else ACCENT, outline=SURFACE, width=2)
        x, y = self.points[self.selected]
        self.create_text(right-4, bottom-12, text=f'输入 {x:.1%}  /  响应 {y:.1%}', anchor='se', fill=INK, font=('Microsoft YaHei UI', 9))

    def pick(self, event):
        if not self.enabled:
            return
        self.focus_set()
        self.selected = min(range(len(self.points)), key=lambda i: (self.pixel(self.points[i])[0]-event.x)**2 + (self.pixel(self.points[i])[1]-event.y)**2)
        self.draw()

    def move_point(self, index, x, y):
        if not self.enabled or index <= 0 or index >= len(self.points)-1:
            return
        before, after = self.points[index-1], self.points[index+1]
        result = []
        for axis, desired in enumerate((x, y)):
            span = after[axis] - before[axis]
            if span <= 5e-5:
                result.append(self.points[index][axis])
            else:
                margin = min(.001, span / 4)
                result.append(round(max(before[axis]+margin,min(after[axis]-margin,desired)),6))
        self.points[index] = result
        self.points = validate_points(self.points)
        self.on_change([point[:] for point in self.points])
        self.draw()

    def drag(self, event):
        self.move_point(self.selected, *self.value(event.x,event.y))

    def select(self, direction):
        self.selected = max(0, min(len(self.points)-1, self.selected+direction))
        self.draw()

    def nudge(self, delta):
        x, y = self.points[self.selected]
        self.move_point(self.selected, x, y+delta)

    def add_point(self, event):
        if not self.enabled or len(self.points) >= 32:
            return
        x, y = self.value(event.x,event.y)
        for i in range(1,len(self.points)):
            a, b = self.points[i-1], self.points[i]
            if a[0]+.002 < x < b[0]-.002 and b[1]-a[1] > .004:
                self.points.insert(i,[round(x,6),round(max(a[1]+.001,min(b[1]-.001,y)),6)])
                self.selected=i
                self.on_change([point[:] for point in self.points])
                self.draw()
                break

    def remove_point(self, event=None):
        if self.enabled and 0 < self.selected < len(self.points)-1:
            self.points.pop(self.selected)
            self.selected=min(self.selected,len(self.points)-2)
            self.on_change([point[:] for point in self.points])
            self.draw()

    def state(self, statespec=None):
        if statespec:
            self.enabled = 'disabled' not in statespec
        return () if self.enabled else ('disabled',)


class ProfileStrip(tk.Canvas):
    def __init__(self,parent,on_select):
        super().__init__(parent,height=90,background=SURFACE,highlightthickness=0,takefocus=True)
        self.entries=[]
        self.current=None
        self.on_select=on_select
        self.bind('<Configure>',lambda _:self.draw())
        self.bind('<Button-1>',self.pick)
        self.bind('<Left>',lambda _:self.step(-1))
        self.bind('<Right>',lambda _:self.step(1))
        self.bind('<Return>',lambda _:self.on_select(self.current))
        self.bind('<MouseWheel>',lambda event:self.xview_scroll(-int(event.delta/120),'units'))
        self.bind('<FocusIn>',lambda _:self.draw())
        self.bind('<FocusOut>',lambda _:self.draw())

    def set_profiles(self,entries,current):
        self.entries=entries
        self.current=current
        self.draw()
        index=next((i for i,e in enumerate(entries) if e['id']==current),0)
        total=max(1,len(entries)*166)
        start,end=self.xview()
        if index*166 < start*total or (index+1)*166 > end*total:
            self.xview_moveto(max(0,index*166-20)/total)

    def draw(self):
        self.delete('all')
        for i,entry in enumerate(self.entries):
            chosen=entry['id']==self.current
            left=i*166+3
            top=3 if chosen else 11
            bottom=83 if chosen else 75
            self.create_rectangle(left,top,left+150,bottom,fill='#28446c' if chosen else '#1c2b43',outline=INK if chosen else '#344664',width=2 if chosen else 1)
            name=entry['name']
            self.create_text(left+13,top+25,text=name if len(name)<=11 else name[:10]+'…',anchor='w',fill=INK,font=('Microsoft YaHei UI',11,'bold' if chosen else 'normal'))
            caption=('模板 · ' if entry.get('legacy') else '') + entry['game'].upper()
            self.create_text(left+13,bottom-17,text=caption,anchor='w',fill=ACCENT,font=('Microsoft YaHei UI',8))
            if entry.get('dirty'):
                self.create_oval(left+131,top+12,left+139,top+20,fill=ACCENT,outline='')
        self.configure(scrollregion=(0,0,max(self.winfo_width(),len(self.entries)*166),90))

    def pick(self,event):
        self.focus_set()
        index=int(self.canvasx(event.x)//166)
        if 0<=index<len(self.entries):
            self.on_select(self.entries[index]['id'])

    def step(self,direction):
        if not self.entries:
            return
        index=next((i for i,e in enumerate(self.entries) if e['id']==self.current),0)
        self.on_select(self.entries[max(0,min(len(self.entries)-1,index+direction))]['id'])
