#!/usr/bin/env python3
"""Small Tk front end for rt_render.

    python3 scripts/gui.py

Pick a scene, adjust resolution, field of view, camera and renderer options, and press Render.
The image appears in the window and is saved under out/gui/. The Video box renders an orbit or a
camera-path walk with scripts/make_video.sh (needs ffmpeg) and lets you scrub through the frames.
Everything runs through the rt_render CLI, so the options mean what USER_GUIDE.md says they mean.
Needs python3-tk and python3-pil.imagetk (sudo apt install python3-tk python3-pil.imagetk).

On X11 (including WSLg) the window draws its own dark title bar: drag it to move, double-click
it to maximize, and drag the right or bottom edge or the corner to resize. Set
RT_GUI_NATIVE_TITLEBAR=1 to keep the window manager's title bar instead.
"""
import ctypes
import json
import os
import queue
import re
import shlex
import shutil
import subprocess
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont

from PIL import Image, ImageTk

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "out" / "gui"
BUILD_DIRS = ["build", "build-cpu", "build-gen"]

# Dark palette
BG = "#1e1f22"        # window
PANEL = "#2b2d31"     # group boxes
FIELD = "#383a40"     # entries, comboboxes
BORDER = "#45474d"
FG = "#e6e6e6"
MUTED = "#9a9ca3"
ACCENT = "#4f8cff"
ACCENT_HI = "#6aa0ff"
GOOD = "#5fd38d"
BAD = "#ff6b6b"
TITLE = "#17181a"     # custom title bar
BAD_BG = "#c42b1c"    # close button hover, as on Windows


def pick_font(*families):
    available = set(tkfont.families())
    return next((f for f in families if f in available), "TkDefaultFont")


def parse_stats(lines):
    """Pulls the headline time, a summary line and the per-stage times out of rt_render output."""
    summary, headline, extra, stages = "", None, "", []
    for raw in lines:
        line = raw.strip()
        if line.startswith("rt_render:"):
            summary = line[len("rt_render:"):].split(" -> ")[0].strip()
        elif line.startswith("T_frame over the path"):
            m = re.search(r"median ([\d.]+) ms \(min ([\d.]+), max ([\d.]+)", line)
            if m:
                headline = ("T_frame median", float(m.group(1)))
                extra = f"min {float(m.group(2)):.2f} · max {float(m.group(3)):.2f} ms"
        elif line.startswith("T_app"):
            m = re.match(r"T_app ([\d.]+) ms", line)
            if m:
                headline = ("T_app", float(m.group(1)))
        elif line.startswith("bvh:"):
            extra = line[len("bvh:"):].strip()
        line = line.split("preparation:", 1)[-1]
        if not line.startswith("T_"):
            stages += [(k, float(v)) for k, v in re.findall(r"([A-Za-z_]+) ([\d.]+) ms", line)]
    return summary, headline, extra, stages


def remove_wm_decorations(root):
    """Asks the X11 window manager not to draw a title bar (_MOTIF_WM_HINTS, decorations = 0).

    WSLg draws a light title bar for X11 apps and ignores dark-theme hints, so the only way to get
    a dark one is to draw it ourselves. Call before the window is mapped. Returns False when not
    on X11 or libX11 is missing, in which case the native title bar stays.
    """
    if os.environ.get("RT_GUI_NATIVE_TITLEBAR") or root.tk.call("tk", "windowingsystem") != "x11":
        return False
    try:
        x = ctypes.cdll.LoadLibrary("libX11.so.6")
    except OSError:
        return False
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x.XInternAtom.restype = ctypes.c_ulong
    x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong] + [ctypes.c_void_p] * 4
    x.XChangeProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong,
                                  ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    x.XFree.argtypes = [ctypes.c_void_p]
    x.XFlush.argtypes = x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    display = x.XOpenDisplay(root.winfo_screen().encode())
    if not display:
        return False
    try:
        # The property goes on Tk's wrapper window, the parent of the toplevel's own window.
        root_w, parent, kids, n = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_void_p(), ctypes.c_uint()
        if not x.XQueryTree(display, root.winfo_id(), ctypes.byref(root_w), ctypes.byref(parent),
                            ctypes.byref(kids), ctypes.byref(n)):
            return False
        if kids:
            x.XFree(kids)
        atom = x.XInternAtom(display, b"_MOTIF_WM_HINTS", 0)
        hints = (ctypes.c_long * 5)(2, 0, 0, 0, 0)  # flags = MWM_HINTS_DECORATIONS, decorations = 0
        x.XChangeProperty(display, parent.value, atom, atom, 32, 0, hints, 5)
        x.XFlush(display)
        return True
    finally:
        x.XCloseDisplay(display)


def find_binaries():
    found = []
    for d in BUILD_DIRS:
        p = ROOT / d / "apps" / "rt_render" / "rt_render"
        if p.is_file() and os.access(p, os.X_OK):
            found.append(str(p.relative_to(ROOT)))
    return found


def has_cuda(binary):
    cache = (ROOT / binary).parents[2] / "CMakeCache.txt"
    try:
        return "RT_ENABLE_CUDA:BOOL=ON" in cache.read_text()
    except OSError:
        return False


def scan_json():
    """Returns (scenes, paths): scene files have a camera, camera paths have waypoints."""
    scenes, paths = [], []
    for p in sorted((ROOT / "scenes").rglob("*.json")):
        if p.parent.name in ("golden", "templates") or p.name == "manifest.json":
            continue
        try:
            with open(p) as f:
                d = json.load(f)
        except (OSError, ValueError):
            continue
        if not isinstance(d, dict):
            continue
        rel = str(p.relative_to(ROOT))
        if "camera" in d:
            scenes.append(rel)
        elif "waypoints" in d:
            paths.append(rel)
    return scenes, paths


def open_in_viewer(path):
    """Opens a file or folder with the desktop's default app (Windows Explorer under WSL)."""
    path = Path(path)
    if shutil.which("wslpath") and shutil.which("explorer.exe"):
        win = subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True).stdout.strip()
        subprocess.Popen(["explorer.exe", win])
    elif shutil.which("xdg-open"):
        subprocess.Popen(["xdg-open", str(path)])
    else:
        messagebox.showinfo("Open", str(path))


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Ray Tracer")
        self.geometry("1360x960")
        self.minsize(480, 320)
        self.configure(background=BG)
        self._apply_theme()
        self.withdraw()  # decorations must be changed before the window is first shown
        self.update_idletasks()
        self.custom_titlebar = remove_wm_decorations(self)
        self.proc = None
        self.run_lines = []
        self.lines = queue.Queue()
        self.photo = None
        self.image = None
        self.frames = []
        self.scenes, self.paths = scan_json()

        self.v = {k: tk.StringVar() for k in (
            "binary", "scene", "backend", "bvh", "build_threads", "render_threads", "shade",
            "lights", "width", "height", "vfov", "px", "py", "pz", "lx", "ly", "lz",
            "video_kind", "path", "frames", "fps", "status")}
        cpus = str(os.cpu_count() or 1)
        self.v["build_threads"].set(cpus)
        self.v["render_threads"].set(cpus)
        self.v["bvh"].set("lbvh")
        self.v["shade"].set("lit")
        self.v["video_kind"].set("orbit")
        self.v["frames"].set("120")
        self.v["fps"].set("30")
        self.v["status"].set("Ready")

        self._build_ui()
        bins = find_binaries()
        self.bin_box["values"] = bins
        if bins:
            self.v["binary"].set(bins[0])
        self._on_binary()
        if self.scenes:
            default = "scenes/known/known_showcase.json"
            self.v["scene"].set(default if default in self.scenes else self.scenes[0])
            self._load_scene_camera()
        if self.paths:
            self.v["path"].set(self.paths[0])
        self.deiconify()
        self.after(50, self._drain_log)

    # ---------------------------------------------------------------- theme
    def _apply_theme(self):
        family = self.family = pick_font("Ubuntu Sans", "Ubuntu", "DejaVu Sans")
        self.mono = pick_font("Ubuntu Sans Mono", "Ubuntu Mono", "DejaVu Sans Mono")
        for name in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont"):
            tkfont.nametofont(name).configure(family=family, size=10)

        s = ttk.Style(self)
        s.theme_use("clam")
        s.configure(".", background=BG, foreground=FG, fieldbackground=FIELD, bordercolor=BORDER,
                    lightcolor=BORDER, darkcolor=BORDER, troughcolor=FIELD, focuscolor=ACCENT,
                    selectbackground=ACCENT, selectforeground="white", insertcolor=FG,
                    arrowcolor=FG)
        s.map(".", foreground=[("disabled", MUTED)])
        s.configure("Side.TFrame", background=BG)
        s.configure("TLabelframe", background=PANEL, bordercolor=PANEL, relief="flat", padding=(8, 4, 8, 8))
        s.configure("TLabelframe.Label", background=PANEL, foreground=ACCENT_HI,
                    font=(family, 10, "bold"))
        s.configure("Panel.TFrame", background=PANEL)
        s.configure("TLabel", background=PANEL)
        s.configure("Muted.TLabel", background=PANEL, foreground=MUTED, font=(family, 9))
        s.configure("Status.TLabel", background=BG, foreground=MUTED)
        s.configure("Big.TLabel", background=PANEL, foreground=GOOD, font=(family, 18, "bold"))
        s.configure("Key.TLabel", background=PANEL, foreground=MUTED, font=(self.mono, 9))
        s.configure("Val.TLabel", background=PANEL, foreground=FG, font=(self.mono, 9))
        s.configure("Frame.TLabel", background=BG, foreground=MUTED)
        s.configure("TEntry", padding=(4, 2), relief="flat")
        s.map("TEntry", bordercolor=[("focus", ACCENT)], lightcolor=[("focus", ACCENT)])
        s.configure("TCombobox", padding=(4, 2), relief="flat", background=FIELD)
        s.map("TCombobox", fieldbackground=[("readonly", FIELD)], foreground=[("readonly", FG)],
              background=[("active", BORDER)], bordercolor=[("focus", ACCENT)],
              selectbackground=[("readonly", FIELD)], selectforeground=[("readonly", FG)])
        self.option_add("*TCombobox*Listbox.background", FIELD)
        self.option_add("*TCombobox*Listbox.foreground", FG)
        self.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
        self.option_add("*TCombobox*Listbox.font", (family, 10))
        s.configure("TButton", background=FIELD, foreground=FG, padding=(10, 4), relief="flat",
                    borderwidth=0)
        s.map("TButton", background=[("disabled", PANEL), ("pressed", BORDER), ("active", BORDER)])
        s.configure("Small.TButton", padding=(4, 2))
        s.configure("Accent.TButton", background=ACCENT, foreground="white",
                    font=(family, 10, "bold"), padding=(10, 6))
        s.map("Accent.TButton", background=[("disabled", PANEL), ("pressed", ACCENT),
                                            ("active", ACCENT_HI)])
        s.configure("TRadiobutton", background=PANEL, indicatorbackground=FIELD,
                    indicatorforeground=ACCENT)
        s.map("TRadiobutton", background=[("active", PANEL)],
              indicatorbackground=[("selected", ACCENT)])
        s.configure("Horizontal.TScale", background=FIELD, troughcolor=BG, borderwidth=0)
        s.map("Horizontal.TScale", background=[("active", ACCENT)])
        s.configure("Vertical.TScrollbar", background=FIELD, troughcolor=BG, borderwidth=0,
                    arrowsize=0)
        s.configure("TPanedwindow", background=BG)
        s.configure("Sash", sashthickness=6, gripcount=0, background=BG)

    # ---------------------------------------------------------------- layout
    # ---------------------------------------------------------------- window chrome
    def _build_chrome(self):
        """Dark title bar and resize grips; returns the frame the rest of the UI goes in."""
        outer = tk.Frame(self, background=BG, highlightthickness=1, highlightbackground=BORDER)
        outer.pack(fill=tk.BOTH, expand=True)
        bar = tk.Frame(outer, background=TITLE, height=34)
        bar.pack(side=tk.TOP, fill=tk.X)
        bar.pack_propagate(False)
        title = tk.Label(bar, text="  ●  Ray Tracer", background=TITLE, foreground=FG,
                         font=(self.family, 10, "bold"))
        title.pack(side=tk.LEFT, padx=6)
        for text, cmd, hover in (("✕", self.destroy, BAD_BG), ("☐", self._toggle_max, BORDER),
                                 ("—", self.iconify, BORDER)):
            b = tk.Label(bar, text=text, width=5, background=TITLE, foreground=FG,
                         font=(self.family, 11))
            b.pack(side=tk.RIGHT, fill=tk.Y)
            b.bind("<Enter>", lambda e, b=b, h=hover: b.configure(background=h))
            b.bind("<Leave>", lambda e, b=b: b.configure(background=TITLE))
            b.bind("<ButtonRelease-1>", lambda e, c=cmd: c())
        for w in (bar, title):
            w.bind("<ButtonPress-1>", self._move_start)
            w.bind("<B1-Motion>", self._move_drag)
            w.bind("<Double-Button-1>", lambda e: self._toggle_max())

        grip = 6
        bottom = tk.Frame(outer, background=BG, height=grip, cursor="sb_v_double_arrow")
        bottom.pack(side=tk.BOTTOM, fill=tk.X)
        corner = tk.Frame(bottom, background=BORDER, width=14, height=grip, cursor="bottom_right_corner")
        corner.pack(side=tk.RIGHT)
        right = tk.Frame(outer, background=BG, width=grip, cursor="sb_h_double_arrow")
        right.pack(side=tk.RIGHT, fill=tk.Y)
        for w, dx, dy in ((bottom, 0, 1), (corner, 1, 1), (right, 1, 0)):
            w.bind("<ButtonPress-1>", self._resize_start)
            w.bind("<B1-Motion>", lambda e, dx=dx, dy=dy: self._resize_drag(e, dx, dy))
        body = tk.Frame(outer, background=BG)
        body.pack(fill=tk.BOTH, expand=True)
        return body

    def _move_start(self, e):
        # winfo_root* is where the window really is; `wm geometry` can lag behind under WSLg.
        self.drag = (e.x_root, e.y_root, self.winfo_rootx(), self.winfo_rooty())

    def _move_drag(self, e):
        if self.attributes("-zoomed"):
            return
        sx, sy, wx, wy = self.drag
        self.geometry(f"+{wx + e.x_root - sx}+{wy + e.y_root - sy}")

    def _toggle_max(self):
        self.attributes("-zoomed", not self.attributes("-zoomed"))

    def _resize_start(self, e):
        if self.attributes("-zoomed"):
            self.attributes("-zoomed", False)
        self.drag = (e.x_root, e.y_root, self.winfo_width(), self.winfo_height())

    def _resize_drag(self, e, dx, dy):
        sx, sy, w, h = self.drag
        mw, mh = self.minsize()
        self.geometry(f"{max(mw, w + dx * (e.x_root - sx))}x{max(mh, h + dy * (e.y_root - sy))}")

    def _build_ui(self):
        root = self._build_chrome() if self.custom_titlebar else self
        side = ttk.Frame(root, padding=10, style="Side.TFrame")
        side.pack(side=tk.LEFT, fill=tk.Y)
        main = ttk.Frame(root, padding=(0, 10, 10, 10), style="Side.TFrame")
        main.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        def entry(parent, var, width=7):
            return ttk.Entry(parent, textvariable=self.v[var], width=width)

        def group(text):
            box = ttk.LabelFrame(side, text=text)
            box.pack(fill=tk.X, pady=(0, 6))
            return box

        def row(parent, r):
            f = ttk.Frame(parent, style="Panel.TFrame")
            f.grid(row=r, column=0, columnspan=4, sticky="ew", pady=2)
            return f

        # Scene and binary
        box = group("Scene")
        box.columnconfigure(1, weight=1)
        self.scene_box = ttk.Combobox(box, textvariable=self.v["scene"], values=self.scenes, width=38)
        self.scene_box.grid(row=0, column=0, columnspan=2, sticky="ew")
        self.scene_box.bind("<<ComboboxSelected>>", lambda e: self._load_scene_camera())
        ttk.Button(box, text="…", style="Small.TButton", width=3,
                   command=self._browse_scene).grid(row=0, column=2, padx=(4, 0))
        ttk.Label(box, text="Binary").grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.bin_box = ttk.Combobox(box, textvariable=self.v["binary"], width=28, state="readonly")
        self.bin_box.grid(row=1, column=1, columnspan=2, sticky="e", pady=(6, 0))
        self.bin_box.bind("<<ComboboxSelected>>", lambda e: self._on_binary())

        # Camera
        cam = group("Camera")
        res = row(cam, 0)
        ttk.Label(res, text="Resolution", width=10).pack(side=tk.LEFT)
        entry(res, "width", 6).pack(side=tk.LEFT)
        ttk.Label(res, text=" × ").pack(side=tk.LEFT)
        entry(res, "height", 6).pack(side=tk.LEFT)
        presets = row(cam, 1)
        for label, w, h in (("480p", 640, 480), ("720p", 1280, 720), ("1080p", 1920, 1080), ("4K", 3840, 2160)):
            ttk.Button(presets, text=label, style="Small.TButton", width=6,
                       command=lambda w=w, h=h: (self.v["width"].set(w), self.v["height"].set(h))
                       ).pack(side=tk.LEFT, padx=(0, 4))
        fov = row(cam, 2)
        ttk.Label(fov, text="FOV (deg)", width=10).pack(side=tk.LEFT)
        entry(fov, "vfov", 6).pack(side=tk.LEFT)
        self.fov_scale = ttk.Scale(fov, from_=10, to=120, orient=tk.HORIZONTAL, length=170,
                                   command=lambda x: self.v["vfov"].set(f"{float(x):.0f}"))
        self.fov_scale.pack(side=tk.LEFT, padx=8)
        for r, label, keys in ((3, "Position", ("px", "py", "pz")), (4, "Look at", ("lx", "ly", "lz"))):
            f = row(cam, r)
            ttk.Label(f, text=label, width=10).pack(side=tk.LEFT)
            for k in keys:
                entry(f, k, 7).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(f, text="Reset", style="Small.TButton",
                   command=self._load_scene_camera).pack(side=tk.RIGHT)

        # Renderer
        r = group("Renderer")
        r.columnconfigure(1, weight=1)
        r.columnconfigure(3, weight=1)
        ttk.Label(r, text="Backend").grid(row=0, column=0, sticky="w", pady=2)
        self.backend_box = ttk.Combobox(r, textvariable=self.v["backend"], width=8, state="readonly")
        self.backend_box.grid(row=0, column=1, sticky="w")
        ttk.Label(r, text="BVH").grid(row=0, column=2, sticky="w", padx=(10, 6))
        ttk.Combobox(r, textvariable=self.v["bvh"], values=["median", "lbvh", "sah", "none"],
                     width=8, state="readonly").grid(row=0, column=3, sticky="w")
        ttk.Label(r, text="Build thr.").grid(row=1, column=0, sticky="w", pady=2)
        entry(r, "build_threads", 6).grid(row=1, column=1, sticky="w")
        ttk.Label(r, text="Render thr.").grid(row=1, column=2, sticky="w", padx=(10, 6))
        entry(r, "render_threads", 6).grid(row=1, column=3, sticky="w")
        ttk.Label(r, text="Shading").grid(row=2, column=0, sticky="w", pady=2)
        ttk.Combobox(r, textvariable=self.v["shade"], values=["lit", "normal"],
                     width=8, state="readonly").grid(row=2, column=1, sticky="w")
        ttk.Label(r, text="Lights").grid(row=2, column=2, sticky="w", padx=(10, 6))
        entry(r, "lights", 6).grid(row=2, column=3, sticky="w")

        # Actions
        act = ttk.Frame(side, style="Side.TFrame")
        act.pack(fill=tk.X, pady=(0, 8))
        self.render_btn = ttk.Button(act, text="Render", style="Accent.TButton", command=self.render_image)
        self.render_btn.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.stop_btn = ttk.Button(act, text="Stop", command=self.stop, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=6)
        ttk.Button(act, text="Open folder", command=lambda: open_in_viewer(OUT_DIR)).pack(side=tk.LEFT)

        # Video
        vid = group("Video")
        kinds = row(vid, 0)
        ttk.Radiobutton(kinds, text="Orbit", variable=self.v["video_kind"], value="orbit").pack(side=tk.LEFT)
        ttk.Radiobutton(kinds, text="Camera path", variable=self.v["video_kind"], value="path").pack(side=tk.LEFT, padx=12)
        ttk.Label(kinds, text="needs ffmpeg", style="Muted.TLabel").pack(side=tk.RIGHT)
        pb = row(vid, 1)
        self.path_box = ttk.Combobox(pb, textvariable=self.v["path"], values=self.paths, width=34)
        self.path_box.pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(pb, text="…", style="Small.TButton", width=3, command=self._browse_path).pack(side=tk.LEFT, padx=(4, 0))
        fr = row(vid, 2)
        ttk.Label(fr, text="Frames").pack(side=tk.LEFT)
        entry(fr, "frames", 6).pack(side=tk.LEFT, padx=(6, 14))
        ttk.Label(fr, text="FPS").pack(side=tk.LEFT)
        entry(fr, "fps", 4).pack(side=tk.LEFT, padx=6)
        btns = row(vid, 3)
        self.video_btn = ttk.Button(btns, text="Render video", command=self.render_video)
        self.video_btn.pack(side=tk.LEFT, fill=tk.X, expand=True, pady=(4, 0))
        self.play_btn = ttk.Button(btns, text="Play video", command=self._play_video, state=tk.DISABLED)
        self.play_btn.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=(4, 0))

        # Benchmark numbers from the last successful run
        stats = group("Last run")
        stats.pack_configure(fill=tk.BOTH, expand=True)
        self.stat_summary = ttk.Label(stats, text="No run yet", style="Muted.TLabel", wraplength=330)
        self.stat_summary.pack(anchor="w")
        head = ttk.Frame(stats, style="Panel.TFrame")
        head.pack(fill=tk.X, pady=(4, 2))
        self.stat_big = ttk.Label(head, text="", style="Big.TLabel")
        self.stat_big.pack(side=tk.LEFT)
        self.stat_big_name = ttk.Label(head, text="", style="Muted.TLabel")
        self.stat_big_name.pack(side=tk.LEFT, padx=8, anchor="s", pady=(0, 4))
        self.stat_extra = ttk.Label(stats, text="", style="Muted.TLabel", wraplength=330)
        self.stat_extra.pack(anchor="w")
        self.stat_grid = ttk.Frame(stats, style="Panel.TFrame")
        self.stat_grid.pack(fill=tk.X, pady=(6, 0))


        # Image + frame scrubber + log
        ttk.Label(main, textvariable=self.v["status"], style="Status.TLabel").pack(side=tk.BOTTOM, fill=tk.X, pady=(6, 0))
        pane = ttk.PanedWindow(main, orient=tk.VERTICAL)
        pane.pack(fill=tk.BOTH, expand=True)
        top = ttk.Frame(pane, style="Side.TFrame")
        self.canvas = tk.Canvas(top, background="#111214", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind("<Configure>", lambda e: self._redraw())
        self.scrub = ttk.Scale(top, from_=0, to=0, orient=tk.HORIZONTAL, command=self._on_scrub)
        self.scrub_label = ttk.Label(top, text="", style="Frame.TLabel")
        pane.add(top, weight=4)
        logf = ttk.Frame(pane, style="Side.TFrame")
        self.log = tk.Text(logf, height=9, wrap=tk.NONE, font=(self.mono, 9), background=PANEL,
                           foreground="#c8c8c8", insertbackground=FG, relief="flat", borderwidth=0,
                           padx=8, pady=6, selectbackground=ACCENT, highlightthickness=0)
        self.log.tag_configure("cmd", foreground=ACCENT_HI)
        self.log.tag_configure("err", foreground=BAD)
        sb = ttk.Scrollbar(logf, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.log.pack(fill=tk.BOTH, expand=True)
        pane.add(logf, weight=1)

    def _show_stats(self):
        summary, headline, extra, stages = parse_stats(self.run_lines)
        self.stat_summary["text"] = summary or "No timing output"
        if headline:
            name, ms = headline
            self.stat_big["text"] = f"{ms:.1f} ms" if ms >= 10 else f"{ms:.2f} ms"
            self.stat_big_name["text"] = name
        else:
            self.stat_big["text"] = self.stat_big_name["text"] = ""
        self.stat_extra["text"] = extra
        for w in self.stat_grid.winfo_children():
            w.destroy()
        for i, (k, ms) in enumerate(stages):
            r, c = divmod(i, 2)
            ttk.Label(self.stat_grid, text=k, style="Key.TLabel").grid(row=r, column=2 * c, sticky="w", padx=(0 if c == 0 else 16, 6))
            ttk.Label(self.stat_grid, text=f"{ms:.3f} ms", style="Val.TLabel").grid(row=r, column=2 * c + 1, sticky="e")

    # ---------------------------------------------------------------- inputs
    def _on_binary(self):
        b = self.v["binary"].get()
        backends = ["cpu", "cuda"] if b and has_cuda(b) else ["cpu"]
        self.backend_box["values"] = backends
        self.v["backend"].set("cuda" if "cuda" in backends else "cpu")

    def _browse_scene(self):
        p = filedialog.askopenfilename(initialdir=ROOT / "scenes", filetypes=[("Scene JSON", "*.json")])
        if p:
            self.v["scene"].set(p)
            self._load_scene_camera()

    def _browse_path(self):
        p = filedialog.askopenfilename(initialdir=ROOT / "scenes", filetypes=[("Camera path JSON", "*.json")])
        if p:
            self.v["path"].set(p)
            self.v["video_kind"].set("path")

    def _load_scene_camera(self):
        try:
            with open(ROOT / self.v["scene"].get()) as f:
                cam = json.load(f)["camera"]
        except (OSError, ValueError, KeyError) as e:
            self.v["status"].set(f"Could not read scene camera: {e}")
            return
        self.v["width"].set(cam.get("width", ""))
        self.v["height"].set(cam.get("height", ""))
        if "vfov_deg" in cam:
            self.fov_scale.set(cam["vfov_deg"])  # before the field: moving the slider rounds it
        self.v["vfov"].set(cam.get("vfov_deg", ""))
        for keys, vec in ((("px", "py", "pz"), cam.get("position")), (("lx", "ly", "lz"), cam.get("look_at"))):
            for k, x in zip(keys, vec or ["", "", ""]):
                self.v[k].set(x)

    def _common_args(self, with_camera=True):
        """rt_render options shared by stills and videos. Raises ValueError on bad input."""
        def num(key, kind=float, name=None):
            s = self.v[key].get().strip()
            try:
                return str(kind(s))
            except ValueError:
                raise ValueError(f"{name or key}: '{s}' is not a number") from None

        a = ["--backend", self.v["backend"].get(), "--bvh", self.v["bvh"].get(),
             "--build-threads", num("build_threads", int, "Build threads"),
             "--render-threads", num("render_threads", int, "Render threads"),
             "--shade", self.v["shade"].get(),
             "--width", num("width", int, "Width"), "--height", num("height", int, "Height")]
        if self.v["lights"].get().strip():
            a += ["--lights", num("lights", int, "Lights")]
        if with_camera:
            a += ["--cam-vfov", num("vfov", name="FOV"),
                  "--cam-pos", num("px"), num("py"), num("pz"),
                  "--cam-look", num("lx"), num("ly"), num("lz")]
        return a

    # ---------------------------------------------------------------- actions
    def render_image(self):
        try:
            args = self._common_args()
        except ValueError as e:
            messagebox.showerror("Invalid input", str(e))
            return
        scene = self.v["scene"].get()
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        out = OUT_DIR / f"{Path(scene).stem}.png"
        cmd = [str(ROOT / self.v["binary"].get()), "--scene", scene] + args + ["--out", str(out)]
        self._run(cmd, lambda: self._show_still(out))

    def render_video(self):
        if not shutil.which("ffmpeg"):
            messagebox.showerror("ffmpeg missing", "Video needs ffmpeg: sudo apt install ffmpeg")
            return
        kind = self.v["video_kind"].get()
        try:
            int(self.v["frames"].get()), float(self.v["fps"].get())
            # A camera path sets position, look-at and FOV itself; an orbit starts from the fields.
            args = self._common_args(with_camera=(kind == "orbit"))
        except ValueError as e:
            messagebox.showerror("Invalid input", str(e))
            return
        scene = self.v["scene"].get()
        name = Path(scene).stem + ("_orbit" if kind == "orbit" else "_" + Path(self.v["path"].get()).stem)
        frames_dir = OUT_DIR / f"{name}_frames"
        if frames_dir.exists():
            shutil.rmtree(frames_dir)  # only ever a folder this GUI created
        frames_dir.mkdir(parents=True)
        video = OUT_DIR / f"{name}.mp4"
        cmd = ["bash", str(ROOT / "scripts" / "make_video.sh"), "--scene", scene, "--out", str(video),
               "--frames", self.v["frames"].get(), "--fps", self.v["fps"].get(),
               "--keep-frames", str(frames_dir), "--rt-render", self.v["binary"].get()]
        if kind == "path":
            if not self.v["path"].get():
                messagebox.showerror("No camera path", "Pick a camera path JSON")
                return
            cmd += ["--path", self.v["path"].get()]
        cmd += ["--"] + args
        self.video_path = video
        self._run(cmd, lambda: self._show_frames(frames_dir, video))

    def _run(self, cmd, on_success):
        if self.proc:
            return
        self.log.insert(tk.END, "$ " + shlex.join(cmd) + "\n", "cmd")
        self.log.see(tk.END)
        self.run_lines = []
        self.v["status"].set("Rendering…")
        self.render_btn["state"] = self.video_btn["state"] = tk.DISABLED
        self.stop_btn["state"] = tk.NORMAL
        self.proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, bufsize=1, start_new_session=True)

        def reader(proc):
            for line in proc.stdout:
                self.lines.put(line)
            self.lines.put((proc.wait(), on_success))

        threading.Thread(target=reader, args=(self.proc,), daemon=True).start()

    def stop(self):
        if self.proc:
            try:
                os.killpg(self.proc.pid, 15)  # also stops rt_render under make_video.sh
            except ProcessLookupError:
                pass

    def _drain_log(self):
        try:
            while True:
                item = self.lines.get_nowait()
                if isinstance(item, tuple):
                    code, on_success = item
                    self.proc = None
                    self.render_btn["state"] = self.video_btn["state"] = tk.NORMAL
                    self.stop_btn["state"] = tk.DISABLED
                    if code == 0:
                        on_success()
                        self._show_stats()
                    else:
                        self.v["status"].set(f"Failed (exit {code}), see the log")
                else:
                    self.run_lines.append(item)
                    bad = "error" in item.lower() or "failed" in item.lower()
                    self.log.insert(tk.END, item, "err" if bad else ())
                    self.log.see(tk.END)
        except queue.Empty:
            pass
        self.after(50, self._drain_log)

    # ---------------------------------------------------------------- display
    def _show_still(self, path):
        self.frames = []
        self.scrub.pack_forget()
        self.scrub_label.pack_forget()
        self._show_image(path)
        self.v["status"].set(f"Saved {path.relative_to(ROOT)}")

    def _show_frames(self, frames_dir, video):
        self.frames = sorted(frames_dir.glob("frame_*.png"))
        self.play_btn["state"] = tk.NORMAL
        self.v["status"].set(f"Saved {video.relative_to(ROOT)} ({len(self.frames)} frames)")
        if not self.frames:
            return
        self.scrub.configure(from_=0, to=len(self.frames) - 1)
        self.scrub.pack(fill=tk.X)
        self.scrub_label.pack()
        self.scrub.set(0)
        self._on_scrub(0)

    def _on_scrub(self, x):
        if self.frames:
            i = int(float(x))
            self.scrub_label["text"] = f"Frame {i + 1} / {len(self.frames)}"
            self._show_image(self.frames[i])

    def _play_video(self):
        open_in_viewer(self.video_path)

    def _show_image(self, path):
        with Image.open(path) as im:
            self.image = im.copy()
        self._redraw()

    def _redraw(self):
        if self.image is None:
            return
        cw, ch = self.canvas.winfo_width(), self.canvas.winfo_height()
        if cw < 2 or ch < 2:
            return
        scale = min(cw / self.image.width, ch / self.image.height)
        size = (max(1, int(self.image.width * scale)), max(1, int(self.image.height * scale)))
        resample = Image.NEAREST if scale >= 1 else Image.LANCZOS
        self.photo = ImageTk.PhotoImage(self.image.resize(size, resample))
        self.canvas.delete("all")
        self.canvas.create_image(cw // 2, ch // 2, image=self.photo)


if __name__ == "__main__":
    App().mainloop()
