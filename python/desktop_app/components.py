"""Small native widgets for the desktop assistant. No extra UI dependency."""
from __future__ import annotations

import math
import tkinter as tk
from tkinter import ttk

SURFACE = '#171a20'
BACKGROUND = '#292e37'
INK = '#eef0f4'
MUTED = '#9ca5b4'
ACCENT = '#89baff'
RAIL = '#20242b'

# Dense desktop forms use one grid and one spacing ramp. Values are Tk pixels
# at the app's normal desktop scale; font growth may increase row height.
FORM_VALUE_WIDTH = 88
FORM_UNIT_WIDTH = 24
FORM_LABEL_WIDTH = 144
FORM_LABEL_GAP = 12
FORM_ROW_PADDING = 2
FORM_GROUP_GAP = 8
FORM_COLUMN_GAP = 24


def configure_theme():
    style = ttk.Style()
    style.theme_use('clam')
    style.configure('.', font=('Microsoft YaHei UI', 10), foreground=INK, background=SURFACE)
    style.configure('TFrame', background=SURFACE)
    style.configure('TLabel', background=SURFACE, foreground=INK)
    style.configure('Title.TLabel', font=('Microsoft YaHei UI', 18, 'bold'))
    style.configure('Section.TLabel', font=('Microsoft YaHei UI', 11, 'bold'))
    style.configure('Muted.TLabel', foreground=MUTED, font=('Microsoft YaHei UI', 9))
    style.configure('Accent.TLabel', foreground=ACCENT, font=('Microsoft YaHei UI', 11, 'bold'))
    style.configure('TButton', padding=(10, 5), borderwidth=0, background=BACKGROUND)
    style.configure('Disclosure.TButton',padding=(10,3),anchor='w')
    style.map('TButton', background=[('active', '#394351'), ('disabled', RAIL)], foreground=[('disabled', '#626b78')])
    style.configure('Primary.TButton', background=ACCENT, foreground=SURFACE, padding=(16, 6), font=('Microsoft YaHei UI', 10, 'bold'))
    style.map('Primary.TButton', background=[('disabled', '#526178'), ('active', ACCENT)], foreground=[('disabled', '#bdc8d8')])
    style.configure('Nav.TButton', background=SURFACE, foreground=MUTED, padding=(13, 8))
    style.configure('Selected.Nav.TButton', background=RAIL, foreground=INK)
    style.configure('Link.TButton', background=SURFACE, foreground=ACCENT, padding=(6, 2), font=('Microsoft YaHei UI', 9))
    style.configure('TEntry', padding=(8, 3), fieldbackground=BACKGROUND, bordercolor='#343a45', lightcolor='#343a45', darkcolor='#343a45', insertcolor=INK)
    style.map('TEntry', bordercolor=[('invalid', '#f49090'), ('focus', ACCENT)])
    style.configure('TCombobox', padding=(7, 4), fieldbackground=BACKGROUND, background=BACKGROUND, bordercolor='#343a45', arrowsize=13)
    style.map('TCombobox', fieldbackground=[('readonly', SURFACE)], foreground=[('readonly', INK)])
    style.configure('TCheckbutton', background=SURFACE, padding=(0, 2))
    style.configure('Horizontal.TScale', background=SURFACE, troughcolor='#343a45')
    style.configure('Treeview', rowheight=26, fieldbackground=SURFACE, background=SURFACE, borderwidth=0)
    style.map('Treeview', background=[('selected', '#31465f')])
    style.configure('Treeview.Heading', background=BACKGROUND, padding=(6, 4), font=('Microsoft YaHei UI', 9, 'bold'))
    style.configure('Vertical.TScrollbar', background='#343a45', troughcolor=SURFACE, arrowsize=9, borderwidth=0)
    return style


class ScrollSurface(ttk.Frame):
    """A local scrolling surface that reveals focused controls."""
    def __init__(self, parent, root):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, background=SURFACE, highlightthickness=0, borderwidth=0)
        # Reserve the gutter even when content fits: showing the thumb must
        # never resize the form or trigger a wrap/scrollbar feedback loop.
        gutter = ttk.Frame(self)
        gutter.pack(side='right', fill='y')
        gutter.pack_propagate(False)
        scrollbar = ttk.Scrollbar(gutter, orient='vertical', command=self.canvas.yview)
        gutter.configure(width=scrollbar.winfo_reqwidth())
        def scroll_extent(start,end):
            scrollbar.set(start,end)
            if float(start)<=0 and float(end)>=1:
                scrollbar.pack_forget()
            elif not scrollbar.winfo_manager():
                scrollbar.pack(fill='both',expand=True)
        self.canvas.configure(yscrollcommand=scroll_extent)
        scrollbar.pack(fill='both',expand=True)
        self.canvas.pack(side='left', fill='both', expand=True)
        self.content = ttk.Frame(self.canvas, padding=(0, 8, 0, 8))
        self.window = self.canvas.create_window((0, 0), window=self.content, anchor='nw')
        self.content.bind('<Configure>', self.update_extent)
        self.canvas.bind('<Configure>', self.resize)
        self.bind_ids = [(event, root.bind(event, callback, add='+')) for event, callback in
                         (('<FocusIn>', self.reveal),)]
        self.root = root
        self.wheel_tag = 'PageWheel' + str(self).replace('.', '_')
        # Class bindings belong to the interpreter's Tk root, including when
        # this surface lives in a Toplevel. Release through the same owner.
        self.wheel_owner = root._root()
        self.wheel_binding = self.wheel_owner.bind_class(self.wheel_tag, '<MouseWheel>', self.scroll)
        self.bind('<Destroy>', self.dispose, add='+')

    def resize(self,event):
        self.canvas.itemconfigure(self.window,width=event.width)
        self.update_extent()

    def update_extent(self,_=None):
        # Tk can scroll a short region above zero to align its lower edge.
        # Own a top-anchored region at least as tall as the viewport instead.
        self.canvas.configure(scrollregion=(0,0,self.canvas.winfo_width(),
            max(self.canvas.winfo_height(),self.content.winfo_reqheight())))

    def on_map(self, event):
        if self.owns(event.widget):
            self.tag_wheel(event.widget)

    def tag_wheel(self,widget):
        tags=widget.bindtags()
        if self.wheel_tag not in tags and not getattr(widget,'local_scroll',False):
            widget.bindtags((self.wheel_tag,)+tags)

    def install_wheel(self, widget=None):
        widget = widget or self
        self.tag_wheel(widget)
        for child in widget.winfo_children():
            self.install_wheel(child)

    def owns(self, widget):
        while widget is not None:
            if widget == self:
                return True
            widget = getattr(widget, 'master', None)
        return False

    def scroll(self, event):
        if event.delta:
            self.canvas.yview_scroll(-int(event.delta / 120) or (-1 if event.delta > 0 else 1), 'units')
        return 'break'

    def reveal(self, event):
        widget = event.widget
        if not self.owns(widget) or not widget.winfo_ismapped():
            return
        top = widget.winfo_rooty() - self.content.winfo_rooty()
        visible = self.canvas.canvasy(0)
        if top < visible or top + widget.winfo_height() > visible + self.canvas.winfo_height():
            self.canvas.yview_moveto(max(0, top - 10) / max(1, self.content.winfo_height()))

    def dispose(self, event):
        if event.widget == self:
            for sequence, binding in self.bind_ids:
                self.root.unbind(sequence, binding)
            self.wheel_owner.unbind_class(self.wheel_tag, '<MouseWheel>')
            self.wheel_owner.deletecommand(self.wheel_binding)


class SegmentedInput(ttk.Frame):
    """Visible, keyboard-accessible choices for a small mutually exclusive set."""
    def __init__(self,parent,variable,choices,labels):
        super().__init__(parent)
        self.variable=variable
        self.buttons={}
        for i,value in enumerate(choices):
            button=ttk.Button(self,text=labels[value],command=lambda v=value:variable.set(v))
            button.grid(row=0,column=i,sticky='ew',padx=(0,2))
            self.columnconfigure(i,weight=1)
            self.buttons[value]=button
        self.trace=variable.trace_add('write',self.refresh)
        self.bind('<Destroy>',self.dispose,add='+')
        self.refresh()

    def refresh(self,*_):
        for value,button in self.buttons.items():
            button.configure(style='Primary.TButton' if value==self.variable.get() else 'TButton')

    def state(self,spec=None):
        if spec is not None:
            for button in self.buttons.values():button.state(spec)
        return super().state(spec)

    def dispose(self,event):
        if event.widget==self:
            self.variable.trace_remove('write',self.trace)
            self.variable=None


class DisclosureButton(ttk.Button):
    """Expand retained content; the arrow describes visibility, not a setting."""
    def __init__(self,parent,variable,label,command=None,**kwargs):
        self.variable,self.label,self.on_change=variable,label,command
        kwargs.setdefault('style','Disclosure.TButton')
        super().__init__(parent,command=self.toggle,**kwargs)
        self.trace=variable.trace_add('write',self.refresh)
        self.bind('<Destroy>',self.dispose,add='+')
        self.refresh()

    def refresh(self,*_):
        self.configure(text=('▾ ' if self.variable.get() else '▸ ')+self.label)

    def toggle(self):
        self.variable.set(not self.variable.get())
        if self.on_change:self.on_change()

    def dispose(self,event):
        if event.widget==self:
            self.variable.trace_remove('write',self.trace)
            self.variable=self.on_change=None


class NumberRail(ttk.Frame):
    """Fixed value column and reserved unit slot, shared with slider inputs."""
    def __init__(self,parent,variable,unit=''):
        super().__init__(parent)
        self.columnconfigure(0,minsize=FORM_VALUE_WIDTH)
        self.columnconfigure(1,minsize=FORM_UNIT_WIDTH+4)
        self.entry=ttk.Entry(self,textvariable=variable,justify='right',width=1)
        self.entry.grid(row=0,column=0,sticky='ew')
        self.unit=ttk.Label(self,text=unit,style='Muted.TLabel')
        self.unit.grid(row=0,column=1,sticky='w',padx=(4,0))


class UnitNumberInput(ttk.Frame):
    """Display a unit while keeping the profile's canonical numeric variable."""
    def __init__(self,parent,variable,scale=1,unit='',**kwargs):
        super().__init__(parent,**kwargs)
        self.variable,self.scale,self.syncing=variable,scale,False
        self.display=tk.StringVar(master=self)
        self.columnconfigure(0,weight=1)
        self.rail=NumberRail(self,self.display,unit)
        self.rail.grid(row=0,column=1,sticky='e')
        self.entry=self.rail.entry
        self.raw_trace=variable.trace_add('write',self.sync)
        self.display_trace=self.display.trace_add('write',self.edit)
        self.bind('<Destroy>',self.dispose,add='+')
        self.sync()

    def sync(self,*_):
        if self.syncing:return
        value=self.variable.get()
        try:
            number=float(value)
            if math.isfinite(number):value=format(number*self.scale,'.15g')
        except ValueError:pass
        self.syncing=True
        try:
            if self.display.get()!=value:self.display.set(value)
        finally:self.syncing=False

    def edit(self,*_):
        if self.syncing:return
        value=self.display.get()
        try:
            number=float(value)
            if math.isfinite(number):value=format(number/self.scale,'.15g')
        except ValueError:pass
        self.syncing=True
        try:
            if self.variable.get()!=value:self.variable.set(value)
        finally:self.syncing=False

    def state(self,statespec=None):
        if statespec is not None:self.entry.state(statespec)
        return super().state(statespec)

    def dispose(self,event):
        if event.widget==self:
            self.variable.trace_remove('write',self.raw_trace)
            self.display.trace_remove('write',self.display_trace)
            self.variable=self.display=None


class StrengthInput(ttk.Frame):
    """A precise entry with a slider. Invalid text stays visible for validation."""
    def __init__(self, parent, variable, limits, unit='', display_scale=1):
        super().__init__(parent)
        self.variable = variable
        self.syncing = False
        try:
            number = float(variable.get())
            number = number if math.isfinite(number) else limits[0]
        except ValueError:
            number = limits[0]
        self.knob = tk.DoubleVar(value=number)
        self.columnconfigure(0,weight=1)
        self.scale = ValueSlider(self, limits, self.knob, self.drag)
        self.scale.grid(row=0,column=0,sticky='ew',padx=(0,8))
        self.rail=UnitNumberInput(self,variable,display_scale,unit)
        self.rail.grid(row=0,column=1,sticky='e')
        self.entry=self.rail.entry
        self.trace_id = variable.trace_add('write', self.sync)
        self.bind('<Destroy>', self.dispose, add='+')

    @property
    def display(self):return self.rail.display

    def drag(self, value):
        if not self.syncing:
            text=f'{float(value):.2f}'
            if self.variable.get()!=text:self.variable.set(text)

    def sync(self, *_):
        try:
            number = float(self.variable.get())
        except ValueError:
            return
        if math.isfinite(number):
            self.syncing = True
            if self.knob.get()!=number:self.knob.set(number)
            self.syncing = False

    def state(self, statespec=None):
        if statespec is not None:
            self.scale.state(statespec)
            self.entry.state(statespec)
        return super().state(statespec)

    def dispose(self, event):
        if event.widget == self:
            self.variable.trace_remove('write', self.trace_id)
            # Tk variables must be released on the UI thread, before a worker
            # can collect a destroyed widget's master/command reference cycle.
            self.variable = None
            self.knob = None


def section(parent, title, description):
    frame = ttk.Frame(parent)
    frame.pack(fill='x', pady=(0, 20))
    ttk.Label(frame, text=title, style='Section.TLabel').pack(anchor='w')
    ttk.Label(frame, text=description, style='Muted.TLabel', wraplength=480).pack(anchor='w', pady=(5, 12))
    return frame


class ChoiceInput(ttk.Button):
    """A deliberate selection menu: hover never edits or focuses the field."""
    def __init__(self, parent, variable, choices, labels=None, ellipsize=False, **kwargs):
        self.variable = variable
        self.choices = list(choices)
        self.labels = labels or {}
        self.ellipsize = ellipsize
        self.popup = None
        super().__init__(parent, command=self.open, **kwargs)
        self.trace = variable.trace_add('write', self.refresh)
        self.refresh()
        self.bind('<Destroy>', self.dispose, add='+')
        if ellipsize:self.bind('<Configure>',self.refresh,add='+')

    def refresh(self, *_):
        value = self.variable.get()
        text=self.labels.get(value, value)
        suffix='  ▾'
        if self.ellipsize and self.winfo_width()>1:
            font=ttk.Style(self).lookup(self.cget('style') or 'TButton','font') or 'TkDefaultFont'
            available=max(0,self.winfo_width()-24)
            measure=lambda value:int(self.tk.call('font','measure',font,value))
            if measure(text+suffix)>available:
                lo,hi=0,len(text)
                while lo<hi:
                    mid=(lo+hi+1)//2
                    if measure(text[:mid]+'…'+suffix)<=available:lo=mid
                    else:hi=mid-1
                text=text[:lo]+'…'
        if self.cget('text')!=text+suffix:self.configure(text=text+suffix)

    def open(self, position=None, return_focus=None):
        if self.instate(['disabled']) or self.popup:
            return
        self.return_focus = return_focus or self
        self.return_focus.focus_set()
        menu = self.popup = tk.Toplevel(self)
        menu.overrideredirect(True)
        menu.configure(background='#343a45')
        self.menu_index = self.choices.index(self.variable.get()) if self.variable.get() in self.choices else 0
        self.menu_rows = []
        for index, value in enumerate(self.choices):
            row = tk.Label(menu, text=('✓  ' if value == self.variable.get() else '    ') + self.labels.get(value, value),
                           bg=RAIL, fg=INK, font=('Microsoft YaHei UI', 10), anchor='w', padx=12, pady=7)
            row.pack(fill='x', padx=1, pady=(1, 0))
            row.bind('<Enter>', lambda _, i=index: self.highlight(i))
            row.bind('<ButtonRelease-1>', lambda _, v=value: self.choose(v))
            self.menu_rows.append(row)
        menu.update_idletasks()
        width = max(self.winfo_width(), menu.winfo_reqwidth())
        height = menu.winfo_reqheight()
        x = min(self.winfo_rootx(), menu.winfo_screenwidth() - width - 8)
        y = self.winfo_rooty() + self.winfo_height() + 3
        if position:
            x, y = position
            x = min(x, menu.winfo_screenwidth() - width - 8)
        if y + height > menu.winfo_screenheight():
            y = self.winfo_rooty() - height - 3
        menu.geometry(f'{width}x{height}+{max(0,x)}+{max(0,y)}')
        menu.bind('<Escape>', lambda _: self.dismiss())
        menu.bind('<Tab>', self.next_focus)
        menu.bind('<Shift-Tab>', lambda _:self.next_focus(backward=True))
        menu.bind('<Up>', lambda _: self.highlight((self.menu_index - 1) % len(self.choices)))
        menu.bind('<Down>', lambda _: self.highlight((self.menu_index + 1) % len(self.choices)))
        menu.bind('<Return>', lambda _: self.choose(self.choices[self.menu_index]))
        menu.bind('<MouseWheel>', lambda _: 'break')
        menu.bind('<ButtonPress-1>', self.outside, add='+')
        menu.bind('<FocusOut>', lambda _: self.dismiss())
        menu.grab_set()
        menu.focus_set()
        self.highlight(self.menu_index)

    def highlight(self, index):
        self.menu_index = index
        for i, row in enumerate(self.menu_rows):
            row.configure(background='#31465f' if i == index else RAIL)

    def outside(self, event):
        menu = self.popup
        if menu and not (menu.winfo_rootx() <= event.x_root < menu.winfo_rootx() + menu.winfo_width() and
                         menu.winfo_rooty() <= event.y_root < menu.winfo_rooty() + menu.winfo_height()):
            self.dismiss()

    def choose(self, value):
        self.dismiss()
        self.variable.set(value)

    def next_focus(self,_=None,backward=False):
        target=self.return_focus
        self.dismiss()
        (target.tk_focusPrev() if backward else target.tk_focusNext()).focus_set()
        return 'break'

    def dismiss(self):
        menu, self.popup = self.popup, None
        if menu:
            menu.grab_release()
            menu.destroy()
        target = getattr(self,'return_focus',self)
        if target.winfo_exists():
            target.focus_set()

    def dispose(self, event):
        if event.widget == self:
            if self.popup:
                self.dismiss()
            self.variable.trace_remove('write', self.trace)
            self.variable = None


class ValueSlider(tk.Canvas):
    def __init__(self,parent,limits,variable,command):
        super().__init__(parent,height=26,width=72,background=SURFACE,highlightthickness=0,takefocus=True)
        self.limits,self.variable,self.command=limits,variable,command
        self.enabled=True
        self.trace=variable.trace_add('write',lambda *_:self.draw())
        self.bind('<Configure>',lambda _:self.draw())
        self.bind('<Button-1>',self.drag)
        self.bind('<B1-Motion>',self.drag)
        self.bind('<Left>',lambda _:self.step(-.01))
        self.bind('<Right>',lambda _:self.step(.01))
        self.bind('<FocusIn>',lambda _:self.draw())
        self.bind('<FocusOut>',lambda _:self.draw())
        self.bind('<Destroy>',self.dispose,add='+')

    def state(self,spec=None):
        if spec is not None and self.enabled!=('disabled' not in spec):
            self.enabled='disabled' not in spec;self.draw()
        return () if self.enabled else ('disabled',)

    def draw(self):
        self.delete('all')
        low,high=self.limits
        width=max(25,self.winfo_width())
        amount=min(1,max(0,(self.variable.get()-low)/(high-low)))
        x=9+amount*(width-18)
        colour=ACCENT if self.enabled else '#626b78'
        self.create_line(9,13,width-9,13,fill='#424a56',width=3,capstyle='round')
        if x>9:self.create_line(9,13,x,13,fill=colour,width=3,capstyle='round')
        self.create_oval(x-5,8,x+5,18,fill=INK if self.enabled else '#626b78',outline='')
        if self.focus_get()==self:self.create_rectangle(1,1,width-1,25,outline=ACCENT,dash=(2,3))

    def drag(self,event):
        if not self.enabled:return
        self.focus_set()
        amount=min(1,max(0,(event.x-9)/max(1,self.winfo_width()-18)))
        self.command(self.limits[0]+amount*(self.limits[1]-self.limits[0]))

    def step(self,delta):
        if self.enabled:self.command(min(self.limits[1],max(self.limits[0],self.variable.get()+delta)))
        return 'break'

    def dispose(self,event):
        if event.widget==self:
            self.variable.trace_remove('write',self.trace)
            self.variable=None
            self.command=None


class ToggleInput(tk.Canvas):
    def __init__(self,parent,variable,label='',command=None,width=78):
        super().__init__(parent,width=width,height=26,background=SURFACE,highlightthickness=0,takefocus=True)
        self.variable,self.label,self.command=variable,label,command
        self.enabled=True
        self.trace=variable.trace_add('write',lambda *_:self.draw())
        self.bind('<Button-1>',self.toggle)
        self.bind('<space>',self.toggle)
        self.bind('<Return>',self.toggle)
        self.bind('<Configure>',lambda _:self.draw())
        self.bind('<FocusIn>',lambda _:self.draw())
        self.bind('<FocusOut>',lambda _:self.draw())
        self.bind('<Destroy>',self.dispose,add='+')

    def state(self,spec=None):
        if spec is not None and self.enabled!=('disabled' not in spec):
            self.enabled='disabled' not in spec;self.draw()
        return () if self.enabled else ('disabled',)

    def draw(self):
        self.delete('all')
        on=self.variable.get()
        self.create_line(10,13,27,13,width=17,capstyle='round',fill=ACCENT if on and self.enabled else '#424a56')
        x=27 if on else 10
        self.create_oval(x-6,7,x+6,19,fill=SURFACE if on and self.enabled else INK if self.enabled else '#7b8593',outline='')
        self.create_text(45,13,anchor='w',text=self.label or ('开' if on else '关'),fill=INK if self.enabled else MUTED,font=('Microsoft YaHei UI',9))
        if self.focus_get()==self:self.create_rectangle(1,1,self.winfo_width()-1,25,outline=ACCENT,dash=(2,3))

    def toggle(self,_=None):
        if self.enabled:
            self.focus_set();self.variable.set(not self.variable.get())
            if self.command:self.command()
        return 'break'

    def dispose(self,event):
        if event.widget==self:
            self.variable.trace_remove('write',self.trace)
            self.variable=None
            self.command=None
