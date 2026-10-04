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
    style.map('TButton', background=[('active', '#394351'), ('disabled', RAIL)], foreground=[('disabled', '#626b78')])
    style.configure('Primary.TButton', background=ACCENT, foreground=SURFACE, padding=(16, 6), font=('Microsoft YaHei UI', 10, 'bold'))
    style.map('Primary.TButton', background=[('disabled', '#526178'), ('active', ACCENT)], foreground=[('disabled', '#bdc8d8')])
    style.configure('Nav.TButton', background=SURFACE, foreground=MUTED, padding=(13, 8))
    style.configure('Selected.Nav.TButton', background=RAIL, foreground=INK)
    style.configure('Link.TButton', background=SURFACE, foreground=ACCENT, padding=(6, 2), font=('Microsoft YaHei UI', 9))
    style.configure('TEntry', padding=(7, 4), fieldbackground=BACKGROUND, bordercolor='#343a45', lightcolor='#343a45', darkcolor='#343a45', insertcolor=INK)
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
        scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.canvas.yview)
        def scroll_extent(start,end):
            scrollbar.set(start,end)
            if float(start)<=0 and float(end)>=1:
                scrollbar.pack_forget()
            elif not scrollbar.winfo_manager():
                scrollbar.pack(side='right',fill='y',before=self.canvas)
        self.canvas.configure(yscrollcommand=scroll_extent)
        scrollbar.pack(side='right', fill='y')
        self.canvas.pack(side='left', fill='both', expand=True)
        self.content = ttk.Frame(self.canvas, padding=(0, 4, 14, 8))
        window = self.canvas.create_window((0, 0), window=self.content, anchor='nw')
        self.content.bind('<Configure>', lambda _: self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        self.canvas.bind('<Configure>', lambda event: self.canvas.itemconfigure(window, width=event.width))
        self.bind_ids = [(event, root.bind(event, callback, add='+')) for event, callback in
                         (('<FocusIn>', self.reveal),)]
        self.root = root
        self.wheel_tag = 'PageWheel' + str(self).replace('.', '_')
        # Class bindings belong to the interpreter's Tk root, including when
        # this surface lives in a Toplevel. Release through the same owner.
        self.wheel_owner = root._root()
        self.wheel_binding = self.wheel_owner.bind_class(self.wheel_tag, '<MouseWheel>', self.scroll)
        self.content.bind('<Map>', lambda _: self.install_wheel(), add='+')
        self.bind_ids.append(('<Map>', root.bind('<Map>', self.on_map, add='+')))
        self.bind('<Destroy>', self.dispose, add='+')

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


class StrengthInput(ttk.Frame):
    """A precise entry with a slider. Invalid text stays visible for validation."""
    def __init__(self, parent, variable, limits):
        super().__init__(parent)
        self.variable = variable
        self.syncing = False
        try:
            number = float(variable.get())
            number = number if math.isfinite(number) else limits[0]
        except ValueError:
            number = limits[0]
        self.knob = tk.DoubleVar(value=number)
        self.scale = ValueSlider(self, limits, self.knob, self.drag)
        self.scale.pack(side='left', fill='x', expand=True, padx=(0, 8))
        self.entry = ttk.Entry(self, textvariable=variable, width=7, justify='right')
        self.entry.pack(side='right')
        self.trace_id = variable.trace_add('write', self.sync)
        self.bind('<Destroy>', self.dispose, add='+')

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
    def __init__(self, parent, variable, choices, labels=None, **kwargs):
        self.variable = variable
        self.choices = list(choices)
        self.labels = labels or {}
        self.popup = None
        super().__init__(parent, command=self.open, **kwargs)
        self.trace = variable.trace_add('write', self.refresh)
        self.refresh()
        self.bind('<Destroy>', self.dispose, add='+')

    def refresh(self, *_):
        value = self.variable.get()
        self.configure(text=self.labels.get(value, value) + '  ▾')

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
        super().__init__(parent,height=26,width=110,background=SURFACE,highlightthickness=0,takefocus=True)
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
