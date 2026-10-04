"""Small native widgets for the desktop assistant. No extra UI dependency."""
from __future__ import annotations

import math
import tkinter as tk
from tkinter import ttk

SURFACE = '#111b2d'
BACKGROUND = '#1d2b43'
INK = '#edf3ff'
MUTED = '#9eafc8'
ACCENT = '#a8ceff'
RAIL = '#090f1d'


def configure_theme():
    style = ttk.Style()
    style.theme_use('clam')
    style.configure('.', font=('Microsoft YaHei UI', 10), foreground=INK, background=SURFACE)
    style.configure('TFrame', background=SURFACE)
    style.configure('TLabel', background=SURFACE, foreground=INK)
    style.configure('Title.TLabel', font=('Microsoft YaHei UI', 23, 'bold'))
    style.configure('Section.TLabel', font=('Microsoft YaHei UI', 12, 'bold'))
    style.configure('Muted.TLabel', foreground=MUTED, font=('Microsoft YaHei UI', 9))
    style.configure('Accent.TLabel', foreground=ACCENT, font=('Microsoft YaHei UI', 11, 'bold'))
    style.configure('TButton', padding=(12, 8), borderwidth=0, background=BACKGROUND)
    style.map('TButton', background=[('active', '#2b4062')])
    style.configure('Primary.TButton', background=INK, foreground=RAIL, padding=(18, 11), font=('Microsoft YaHei UI', 11, 'bold'))
    style.map('Primary.TButton', background=[('disabled', '#526178'), ('active', ACCENT)], foreground=[('disabled', '#bdc8d8')])
    style.configure('Nav.TButton', background=SURFACE, foreground=MUTED, padding=(10, 8))
    style.configure('Selected.Nav.TButton', background='#243a5d', foreground=INK)
    style.configure('Link.TButton', background=SURFACE, foreground=ACCENT, padding=(6, 2), font=('Microsoft YaHei UI', 9))
    style.configure('TEntry', padding=(9, 7), fieldbackground=BACKGROUND, bordercolor='#364968', lightcolor='#364968', darkcolor='#364968', insertcolor=INK)
    style.configure('TCombobox', padding=(9, 7), fieldbackground=BACKGROUND, background=BACKGROUND, bordercolor='#364968', arrowsize=13)
    style.map('TCombobox', fieldbackground=[('readonly', SURFACE)], foreground=[('readonly', INK)])
    style.configure('TCheckbutton', background=SURFACE, padding=(0, 4))
    style.configure('Horizontal.TScale', background=SURFACE, troughcolor='#2a3d59')
    style.configure('Treeview', rowheight=32, fieldbackground=SURFACE, background=SURFACE, borderwidth=0)
    style.configure('Treeview.Heading', background=BACKGROUND, padding=(8, 7), font=('Microsoft YaHei UI', 9, 'bold'))
    style.configure('Vertical.TScrollbar', background='#3b506c', troughcolor=SURFACE, arrowsize=10, borderwidth=0)
    return style


class ScrollSurface(ttk.Frame):
    """A local scrolling surface that reveals focused controls."""
    def __init__(self, parent, root):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, background=SURFACE, highlightthickness=0, borderwidth=0)
        scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side='right', fill='y')
        self.canvas.pack(side='left', fill='both', expand=True)
        self.content = ttk.Frame(self.canvas, padding=(0, 4, 14, 16))
        window = self.canvas.create_window((0, 0), window=self.content, anchor='nw')
        self.content.bind('<Configure>', lambda _: self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        self.canvas.bind('<Configure>', lambda event: self.canvas.itemconfigure(window, width=event.width))
        self.bind_ids = [(event, root.bind(event, callback, add='+')) for event, callback in
                         (('<MouseWheel>', self.scroll), ('<FocusIn>', self.reveal))]
        self.root = root
        self.bind('<Destroy>', self.dispose, add='+')

    def owns(self, widget):
        while widget is not None:
            if widget == self:
                return True
            widget = getattr(widget, 'master', None)
        return False

    def scroll(self, event):
        if self.owns(event.widget) and event.widget.winfo_class() not in ('TCombobox', 'TScale'):
            self.canvas.yview_scroll(-int(event.delta / 120), 'units')

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
        self.scale = ttk.Scale(self, from_=limits[0], to=limits[1], variable=self.knob, command=self.drag)
        self.scale.pack(side='left', fill='x', expand=True, padx=(0, 16))
        self.entry = ttk.Entry(self, textvariable=variable, width=8, justify='right')
        self.entry.pack(side='right')
        self.trace_id = variable.trace_add('write', self.sync)
        self.bind('<Destroy>', self.dispose, add='+')

    def drag(self, value):
        if not self.syncing:
            self.variable.set(f'{float(value):.2f}')

    def sync(self, *_):
        try:
            number = float(self.variable.get())
        except ValueError:
            return
        if math.isfinite(number):
            self.syncing = True
            self.knob.set(number)
            self.syncing = False

    def state(self, statespec=None):
        if statespec is not None:
            self.scale.state(statespec)
            self.entry.state(statespec)
        return super().state(statespec)

    def dispose(self, event):
        if event.widget == self:
            self.variable.trace_remove('write', self.trace_id)


def section(parent, title, description):
    frame = ttk.Frame(parent)
    frame.pack(fill='x', pady=(0, 20))
    ttk.Label(frame, text=title, style='Section.TLabel').pack(anchor='w')
    ttk.Label(frame, text=description, style='Muted.TLabel', wraplength=480).pack(anchor='w', pady=(5, 12))
    return frame
