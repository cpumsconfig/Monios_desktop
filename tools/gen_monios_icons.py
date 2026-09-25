"""
Monios icon generator - Windows 11 Fluent Icons style.

Programmatically draws 128 64x64 RGBA PNG icons in the Fluent style and
overwrites assets/icons/individual/<name>.png. The original file names are kept
so that tools/gen_icons_data.py's enum mapping stays intact.

Design language (Windows 11 Fluent / Segoe Fluent Icons):
  - Flat, line+fill hybrid; monochrome primary glyphs.
  - Soft (very low-alpha) drop shadow under card-like shapes.
  - Small rounded corners (2-4px at 64px -> 8-16px at 4x supersample).
  - Palette:
      Accent blue   #0078D4  (0,120,212)
      Dark          #1C1C1C  (28,28,28)
      Mid grey      #616161  (97,97,97)
      Light grey    #F3F3F3  (243,243,243)
      White         #FFFFFF
      Folder amber  #FFB900  (255,185,0)
  - Body glyphs in Dark; interactive / connected / active states in Accent.
  - File = white rounded card with folded corner; Folder = amber Manila folder.
  - App/Start tile = Accent rounded square with White glyph.

Run:  python tools/gen_monios_icons.py
Then: python tools/gen_icons_data.py
"""
import os
import math
from PIL import Image, ImageDraw, ImageFont

# ----------------------------------------------------------------------------
# Paths / constants
# ----------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
INDIVIDUAL_DIR = os.path.join(ROOT, "assets", "icons", "individual")

SIZE = 64
SS = 4                 # supersample factor
CANVAS = SIZE * SS     # 256
CX = CY = CANVAS // 2  # 128

# Fluent palette (RGB)
ACCENT = (0, 120, 212)      # #0078D4
DARK   = (28, 28, 28)       # #1C1C1C
MID    = (97, 97, 97)       # #616161
LIGHT  = (243, 243, 243)    # #F3F3F3
WHITE  = (255, 255, 255)
FOLDER = (255, 185, 0)      # #FFB900

# ----------------------------------------------------------------------------
# Fonts
# ----------------------------------------------------------------------------
def _load_font(size, bold=True):
    candidates = []
    if bold:
        candidates = [r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf"]
    else:
        candidates = [r"C:\Windows\Fonts\segoeui.ttf", r"C:\Windows\Fonts\arial.ttf"]
    for c in candidates:
        if os.path.exists(c):
            try:
                return ImageFont.truetype(c, size)
            except Exception:
                pass
    return ImageFont.load_default()

# ----------------------------------------------------------------------------
# Primitive helpers (all operate on the 256x256 supersampled draw)
# ----------------------------------------------------------------------------
def new_canvas():
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)

def rr(d, box, r, fill):
    d.rounded_rectangle(box, radius=r, fill=fill)

def circle(d, cx, cy, rad, fill):
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=fill)

def line(d, p1, p2, fill, w):
    d.line([p1, p2], fill=fill, width=w)

def poly(d, pts, fill):
    d.polygon(pts, fill=fill)

def text_center(d, cx, cy, s, font, fill):
    bb = d.textbbox((0, 0), s, font=font)
    w = bb[2] - bb[0]
    h = bb[3] - bb[1]
    d.text((cx - w / 2 - bb[0], cy - h / 2 - bb[1]), s, font=font, fill=fill)

def soft_shadow_rr(d, box, r, dy=5, alpha=26):
    """Very low-alpha rounded shadow offset downward."""
    d.rounded_rectangle([box[0], box[1] + dy, box[2], box[3] + dy],
                        radius=r, fill=(0, 0, 0, alpha))

def gear(d, cx, cy, r_out, r_in, teeth, fill, hole=None, hole_col=None):
    pts = []
    steps = teeth * 2
    for i in range(steps):
        ang = (math.pi * 2 * i / steps) - math.pi / 2
        rad = r_out if i % 2 == 0 else r_in
        pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
    d.polygon(pts, fill=fill)
    if hole:
        circle(d, cx, cy, hole, hole_col if hole_col else (0, 0, 0, 0))

def fluent_folder(d, x, y, w, h, fill=FOLDER, shadow=True):
    """Windows 11 Manilla folder: main rounded body + top-left tab."""
    if shadow:
        soft_shadow_rr(d, [x, y, x + w, y + h], 14, dy=4, alpha=22)
    tab_h = int(h * 0.30)
    # main body
    rr(d, [x, y + tab_h - 8, x + w, y + h], 14, fill)
    # tab
    rr(d, [x, y, x + int(w * 0.58), y + tab_h + 8], 10, fill)

def fluent_file(d, x, y, w, h, body=WHITE):
    """White rounded document card with folded top-right corner."""
    soft_shadow_rr(d, [x, y, x + w, y + h], 12, dy=5, alpha=24)
    rr(d, [x, y, x + w, y + h], 12, body)
    fs = 32  # fold size
    poly(d, [(x + w, y + fs), (x + w, y), (x + w - fs, y)], LIGHT)

def cloud(d, x, y, w, h, fill):
    """Puffy cloud inside bounding box."""
    base = y + h * 0.55
    circle(d, x + w * 0.28, base, h * 0.28, fill)
    circle(d, x + w * 0.50, y + h * 0.42, h * 0.34, fill)
    circle(d, x + w * 0.74, base, h * 0.26, fill)
    rr(d, [x + w * 0.16, base, x + w * 0.86, y + h * 0.92], 18, fill)

def wifi_fan(d, cx, cy, levels, fill, w=10):
    """Concentric arcs pointing up; levels=1..3. cy = bottom pivot."""
    base_r = 26
    gap = 26
    for i in range(levels):
        r = base_r + i * gap
        d.arc([cx - r, cy - r, cx + r, cy + r], start=210, end=330, fill=fill, width=w)
    circle(d, cx, cy, 9, fill)

# ----------------------------------------------------------------------------
# FILE icons (file_*) : white document card
# ----------------------------------------------------------------------------
def draw_file_symbol(d, kind):
    """Symbol drawn centered on a file body at ~(128,150)."""
    cx, cy = 128, 152
    if kind == "archive":
        for yy in [120, 142, 164]:
            line(d, (cx - 28, yy), (cx + 28, yy), MID, 9)
        rr(d, [cx - 12, 180, cx + 12, 196], 3, ACCENT)
    elif kind == "audio":
        circle(d, cx - 16, 184, 15, ACCENT)
        line(d, (cx, 184), (cx, 116), ACCENT, 8)
        poly(d, [(cx, 116), (cx + 32, 126), (cx, 140)], ACCENT)
    elif kind == "code":
        line(d, (cx - 16, cy - 20), (cx - 38, cy), ACCENT, 9)
        line(d, (cx - 38, cy), (cx - 16, cy + 20), ACCENT, 9)
        line(d, (cx + 16, cy - 20), (cx + 38, cy), ACCENT, 9)
        line(d, (cx + 38, cy), (cx + 16, cy + 20), ACCENT, 9)
        line(d, (cx + 5, cy - 26), (cx - 5, cy + 26), MID, 8)
    elif kind == "executable":
        gear(d, cx, cy, 34, 22, 8, ACCENT, hole=13, hole_col=WHITE)
    elif kind == "folder":
        fluent_folder(d, cx - 44, cy - 30, 88, 60, FOLDER)
    elif kind == "image":
        circle(d, cx + 22, cy - 18, 10, FOLDER)
        poly(d, [(cx - 38, cy + 26), (cx - 12, cy - 8), (cx + 6, cy + 12),
                 (cx + 18, cy - 2), (cx + 38, cy + 26)], ACCENT)
    elif kind == "pdf":
        f = _load_font(38, bold=True)
        text_center(d, cx, cy, "PDF", f, ACCENT)
    elif kind == "presentation":
        for i, h in enumerate([24, 42, 32]):
            bx = cx - 30 + i * 26
            rr(d, [bx, cy + 24 - h, bx + 16, cy + 24], 3, ACCENT)
        line(d, (cx - 42, cy + 28), (cx + 42, cy + 28), MID, 7)
    elif kind == "spreadsheet":
        rr(d, [cx - 38, cy - 28, cx + 38, cy + 28], 4, ACCENT)
        line(d, (cx, cy - 28), (cx, cy + 28), WHITE, 5)
        line(d, (cx - 38, cy), (cx + 38, cy), WHITE, 5)
    elif kind == "text":
        for yy in [cy - 22, cy - 2, cy + 18]:
            line(d, (cx - 34, yy), (cx + 34, yy), MID, 9)
    elif kind == "unknown":
        f = _load_font(56, bold=True)
        text_center(d, cx, cy, "?", f, MID)
    elif kind == "video":
        poly(d, [(cx - 18, cy - 26), (cx - 18, cy + 26), (cx + 30, cy)], ACCENT)

def make_file(kind):
    def draw(d, img):
        if kind == "folder":
            # file_folder is a folder glyph, not a document card
            fluent_folder(d, 78, 78, 100, 100, FOLDER)
            return
        fluent_file(d, 80, 48, 96, 172, WHITE)
        draw_file_symbol(d, kind)
    return draw

# ----------------------------------------------------------------------------
# NET icons (net_*) : Dark monochrome glyphs, Accent for active/connected
# ----------------------------------------------------------------------------
def net_airplane(d, img):
    poly(d, [(40, 128), (188, 116), (150, 128), (188, 140), (150, 140),
             (120, 178), (108, 178), (118, 140), (80, 150)], DARK)

def net_bluetooth(d, img):
    cx = 128
    line(d, (cx, 60), (cx, 196), DARK, 12)
    line(d, (cx, 60), (cx + 40, 100), DARK, 12)
    line(d, (cx + 40, 100), (cx, 140), DARK, 12)
    line(d, (cx, 140), (cx + 40, 180), DARK, 12)
    line(d, (cx + 40, 180), (cx, 196), DARK, 12)

def net_cloud_fn(d, img, slash=False, down=False, up=False):
    cloud(d, 56, 78, 144, 96, DARK)
    if slash:
        line(d, (70, 196), (186, 60), DARK, 12)
    if down:
        line(d, (128, 150), (128, 200), ACCENT, 12)
        poly(d, [(108, 190), (148, 190), (128, 214)], ACCENT)
    if up:
        line(d, (128, 206), (128, 156), ACCENT, 12)
        poly(d, [(108, 166), (148, 166), (128, 142)], ACCENT)

def net_connected(d, img):
    rr(d, [44, 92, 104, 164], 8, DARK)
    rr(d, [152, 92, 212, 164], 8, DARK)
    line(d, (108, 128), (148, 128), ACCENT, 10)
    circle(d, 128, 128, 12, ACCENT)

def net_disconnected(d, img):
    rr(d, [44, 92, 104, 164], 8, DARK)
    rr(d, [152, 92, 212, 164], 8, DARK)
    line(d, (108, 118), (148, 138), DARK, 10)
    line(d, (108, 138), (148, 118), DARK, 10)

def net_dialup(d, img):
    rr(d, [64, 100, 192, 176], 10, DARK)
    poly(d, [(128, 60), (168, 100), (142, 100), (142, 128), (114, 128),
             (114, 100), (88, 100)], ACCENT)

def net_ethernet(d, img):
    rr(d, [84, 78, 172, 150], 8, DARK)
    rr(d, [104, 150, 152, 184], 6, DARK)
    line(d, (116, 158), (116, 178), LIGHT, 6)
    line(d, (140, 158), (140, 178), LIGHT, 6)
    circle(d, 128, 114, 10, ACCENT)

def net_firewall(d, img):
    for r in range(2):
        for c in range(3):
            x = 78 + c * 38
            y = 96 + r * 40
            rr(d, [x, y, x + 34, y + 34], 3, DARK)
    poly(d, [(118, 176), (138, 176), (128, 200)], ACCENT)

def net_hotspot(d, img):
    wifi_fan(d, 128, 170, 3, ACCENT, 10)
    circle(d, 128, 196, 12, DARK)

def net_mobile_data(d, img):
    rr(d, [88, 64, 168, 196], 12, DARK)
    rr(d, [104, 84, 152, 168], 4, LIGHT)
    for i, h in enumerate([16, 28, 40, 52]):
        bx = 108 + i * 12
        col = ACCENT if i >= 2 else MID
        rr(d, [bx, 168 - h, bx + 8, 168], 2, col)

def net_modem(d, img):
    rr(d, [64, 120, 192, 184], 10, DARK)
    for i in range(4):
        circle(d, 88 + i * 28, 152, 7, ACCENT if i == 0 else MID)
    line(d, (96, 120), (96, 84), DARK, 8)
    line(d, (160, 120), (160, 84), DARK, 8)

def net_network_settings(d, img):
    circle(d, 128, 110, 46, DARK)
    line(d, (84, 110), (172, 110), LIGHT, 5)
    d.arc([92, 74, 164, 146], 0, 360, fill=LIGHT, width=5)
    gear(d, 168, 168, 26, 16, 8, ACCENT, hole=9, hole_col=(0, 0, 0, 0))

def net_nfc(d, img):
    cx = 80
    for i in range(3):
        r = 20 + i * 22
        d.arc([cx - r, 128 - r, cx + r, 128 + r], start=-60, end=60, fill=DARK, width=9)
    circle(d, 80, 128, 10, ACCENT)

def net_no_signal(d, img):
    for i, h in enumerate([14, 26, 38]):
        bx = 92 + i * 22
        rr(d, [bx, 168 - h, bx + 14, 168], 3, MID)
    line(d, (78, 184), (178, 72), DARK, 12)

def net_proxy(d, img):
    rr(d, [64, 84, 192, 150], 8, DARK)
    rr(d, [64, 158, 192, 190], 8, MID)
    poly(d, [(128, 56), (146, 78), (110, 78)], ACCENT)
    line(d, (128, 78), (128, 110), ACCENT, 8)

def net_router(d, img):
    rr(d, [64, 130, 192, 186], 10, DARK)
    line(d, (92, 130), (78, 78), DARK, 9)
    line(d, (164, 130), (178, 78), DARK, 9)
    for i in range(3):
        circle(d, 92 + i * 22, 158, 6, ACCENT if i == 0 else MID)

def net_server(d, img):
    for i in range(3):
        y = 78 + i * 40
        rr(d, [74, y, 182, y + 30], 4, DARK)
        circle(d, 92, y + 15, 6, ACCENT if i == 0 else MID)

def net_share(d, img):
    circle(d, 128, 70, 16, DARK)
    circle(d, 84, 178, 16, DARK)
    circle(d, 172, 178, 16, DARK)
    line(d, (120, 84), (92, 164), MID, 8)
    line(d, (136, 84), (164, 164), MID, 8)
    line(d, (100, 178), (156, 178), MID, 8)

def net_switch(d, img):
    rr(d, [56, 104, 200, 168], 10, DARK)
    for i in range(5):
        x = 72 + i * 24
        rr(d, [x, 120, x + 16, 152], 2, LIGHT)
    circle(d, 184, 136, 6, ACCENT)

def net_vpn(d, img):
    poly(d, [(128, 56), (184, 78), (184, 132), (128, 196), (72, 132), (72, 78)], DARK)
    rr(d, [110, 116, 146, 150], 4, LIGHT)
    line(d, (118, 116), (118, 100), LIGHT, 8)
    line(d, (138, 116), (138, 100), LIGHT, 8)

# ----------------------------------------------------------------------------
# START tiles (start_*) : Accent rounded square + White glyph
# ----------------------------------------------------------------------------
def start_base(d, fill=ACCENT):
    soft_shadow_rr(d, [36, 36, 220, 220], 36, dy=5, alpha=24)
    rr(d, [36, 36, 220, 220], 36, fill)

def s_person(d, col=WHITE, cy=110):
    circle(d, 128, cy - 18, 22, col)
    d.pieslice([96, cy + 6, 160, cy + 70], start=180, end=360, fill=col)

def s_gear(d, col=WHITE, cx=128, cy=128, r=34, hole_col=(0, 0, 0, 0)):
    gear(d, cx, cy, r, r - 12, 8, col, hole=r - 20, hole_col=hole_col)

def s_folder(d, col=WHITE):
    fluent_folder(d, 84, 104, 88, 60, col, shadow=False)

def s_doc(d, col=WHITE):
    rr(d, [96, 78, 160, 182], 6, col)
    for yy in [104, 124, 144]:
        line(d, (108, yy), (148, yy), ACCENT, 6)

def s_music(d, col=WHITE):
    circle(d, 116, 162, 14, col)
    line(d, (130, 162), (130, 96), col, 8)
    poly(d, [(130, 96), (160, 106), (130, 118)], col)

def s_house(d, col=WHITE):
    poly(d, [(128, 78), (180, 124), (164, 124), (164, 178), (92, 178), (92, 124), (76, 124)], col)

def s_arrow_down(d, col=WHITE):
    line(d, (128, 84), (128, 150), col, 12)
    poly(d, [(104, 140), (152, 140), (128, 172)], col)

def s_clock(d, col=WHITE):
    d.ellipse([84, 84, 172, 172], outline=col, width=12)
    line(d, (128, 128), (128, 100), col, 8)
    line(d, (128, 128), (148, 140), col, 8)

def s_power(d, col=WHITE):
    d.arc([92, 92, 164, 164], start=-60, end=240, fill=col, width=12)
    line(d, (128, 78), (128, 124), col, 12)

def s_bolt(d, col=WHITE):
    poly(d, [(138, 78), (108, 138), (126, 138), (118, 182), (152, 120), (134, 120)], col)

def s_trash(d, col=WHITE):
    rr(d, [100, 104, 156, 180], 4, col)
    rr(d, [94, 92, 162, 104], 3, col)
    line(d, (116, 116), (116, 168), ACCENT, 5)
    line(d, (140, 116), (140, 168), ACCENT, 5)

def s_restart(d, col=WHITE):
    d.arc([92, 92, 164, 164], start=-30, end=230, fill=col, width=12)
    poly(d, [(150, 86), (176, 96), (158, 118)], col)

def s_logout(d, col=WHITE):
    rr(d, [80, 84, 120, 172], 6, col)
    line(d, (128, 128), (176, 128), col, 12)
    poly(d, [(156, 106), (184, 128), (156, 150)], col)

def s_search(d, col=WHITE):
    d.ellipse([88, 88, 156, 156], outline=col, width=12)
    line(d, (150, 150), (184, 184), col, 12)

def s_moon(d, col=WHITE):
    d.pieslice([88, 84, 168, 164], 90, 360, fill=col)

def s_videos(d, col=WHITE):
    rr(d, [80, 96, 176, 160], 8, col)
    poly(d, [(120, 112), (120, 144), (150, 128)], ACCENT)

def s_pictures(d, col=WHITE):
    rr(d, [84, 92, 172, 168], 6, col)
    circle(d, 146, 116, 8, FOLDER)
    poly(d, [(92, 158), (114, 128), (128, 146), (138, 134), (164, 158)], ACCENT)

def s_pin(d, col=WHITE):
    poly(d, [(128, 70), (146, 104), (134, 104), (134, 150), (122, 150), (122, 104), (110, 104)], col)
    line(d, (128, 150), (128, 186), col, 8)

def s_run(d, col=WHITE):
    f = _load_font(64, bold=True)
    text_center(d, 128, 128, ">_", f, col)

def s_recent(d, col=WHITE):
    d.ellipse([84, 84, 172, 172], outline=col, width=11)
    poly(d, [(150, 96), (176, 104), (160, 124)], col)
    line(d, (128, 128), (128, 104), col, 7)
    line(d, (128, 128), (146, 138), col, 7)

def s_switch_user(d, col=WHITE):
    circle(d, 112, 108, 18, col)
    d.pieslice([86, 128, 138, 180], 180, 360, fill=col)
    circle(d, 152, 116, 14, col)
    d.pieslice([132, 132, 172, 178], 180, 360, fill=col)

def s_all_apps(d, col=WHITE):
    for r in range(3):
        for c in range(3):
            circle(d, 100 + c * 28, 100 + r * 28, 9, col)

# ----------------------------------------------------------------------------
# STATUS icons (status_*) : Dark glyphs, Accent for level/connected
# ----------------------------------------------------------------------------
def status_battery(d, img, level=1.0, charging=False):
    rr(d, [70, 96, 180, 168], 8, LIGHT)
    rr(d, [180, 118, 196, 146], 3, MID)
    inner_w = 100
    fill_w = int(inner_w * level)
    if fill_w > 0:
        col = ACCENT
        rr(d, [76, 102, 76 + fill_w, 162], 4, col)
    if charging:
        s_bolt(d, col=ACCENT)

def status_volume(d, img, level=2, mute=False):
    poly(d, [(70, 116), (96, 116), (124, 88), (124, 168), (96, 140), (70, 140)], DARK)
    if mute:
        line(d, (140, 104), (184, 152), DARK, 10)
        line(d, (184, 104), (140, 152), DARK, 10)
    else:
        for i in range(level):
            r = 30 + i * 22
            d.arc([124 - r, 128 - r, 124 + r, 128 + r], start=-50, end=50, fill=ACCENT, width=9)

def status_wifi(d, img, level=2, off=False):
    if off:
        wifi_fan(d, 128, 150, 3, MID, 9)
        line(d, (84, 176), (172, 84), DARK, 12)
    else:
        wifi_fan(d, 128, 150, level, ACCENT, 10)

# ----------------------------------------------------------------------------
# UI icons (ui_*) : Dark monochrome, Accent for check/plus/active
# ----------------------------------------------------------------------------
def ui_arrow(d, dir, col=DARK):
    cx, cy = 128, 128
    if dir == "down":
        line(d, (cx, 96), (cx, 158), col, 12); poly(d, [(104,146),(152,146),(128,178)], col)
    elif dir == "up":
        line(d, (cx, 160), (cx, 98), col, 12); poly(d, [(104,110),(152,110),(128,78)], col)
    elif dir == "left":
        line(d, (160, cy), (98, cy), col, 12); poly(d, [(110,104),(110,152),(78,128)], col)
    elif dir == "right":
        line(d, (96, cy), (158, cy), col, 12); poly(d, [(146,104),(146,152),(178,128)], col)

def ui_check(d, img):
    line(d, (88, 128), (116, 156), ACCENT, 14)
    line(d, (116, 156), (172, 96), ACCENT, 14)

def ui_close(d, img):
    line(d, (98, 98), (158, 158), DARK, 14)
    line(d, (158, 98), (98, 158), DARK, 14)

def ui_plus(d, img):
    line(d, (128, 92), (128, 164), ACCENT, 14)
    line(d, (92, 128), (164, 128), ACCENT, 14)

def ui_minus(d, img):
    line(d, (92, 128), (164, 128), DARK, 14)

def ui_home(d, img):
    s_house(d, DARK)

def ui_search(d, img):
    s_search(d, DARK)

def ui_folder(d, img):
    fluent_folder(d, 80, 100, 96, 64, FOLDER)

def ui_file(d, img):
    fluent_file(d, 100, 78, 56, 100, WHITE)
    for yy in [116, 134, 152]:
        line(d, (112, yy), (144, yy), MID, 5)

def ui_settings(d, img):
    s_gear(d, DARK, hole_col=ACCENT)

def ui_lock(d, img):
    rr(d, [96, 116, 160, 172], 8, DARK)
    d.arc([106, 84, 150, 130], start=180, end=360, fill=DARK, width=11)
    circle(d, 128, 144, 8, ACCENT)

def ui_user(d, img):
    s_person(d, DARK, cy=128)

def ui_clock(d, img):
    s_clock(d, DARK)

def ui_calendar(d, img):
    rr(d, [86, 90, 170, 170], 6, WHITE)
    rr(d, [86, 90, 170, 114], 6, ACCENT)
    line(d, (104, 80), (104, 100), DARK, 7)
    line(d, (152, 80), (152, 100), DARK, 7)

def ui_maximize(d, img):
    rr(d, [92, 92, 164, 164], 4, DARK)

def ui_restore(d, img):
    rr(d, [104, 104, 170, 170], 4, DARK)
    rr(d, [86, 86, 148, 148], 4, LIGHT)

def ui_minimize(d, img):
    line(d, (92, 156), (164, 156), DARK, 12)

def ui_delete(d, img):
    s_trash(d, DARK)

def ui_edit(d, img):
    poly(d, [(96, 168), (90, 150), (150, 90), (168, 108), (108, 168)], DARK)
    line(d, (150, 90), (168, 108), ACCENT, 6)

def ui_copy(d, img):
    rr(d, [104, 74, 168, 146], 4, ACCENT)
    rr(d, [86, 100, 150, 172], 4, DARK)

def ui_paste(d, img):
    rr(d, [96, 96, 160, 170], 4, LIGHT)
    rr(d, [116, 84, 140, 100], 3, ACCENT)

def ui_save(d, img):
    rr(d, [88, 84, 168, 172], 6, ACCENT)
    rr(d, [112, 84, 144, 116], 2, DARK)
    rr(d, [104, 138, 152, 172], 2, LIGHT)

def ui_power(d, img):
    s_power(d, DARK)

def ui_shutdown(d, img):
    circle(d, 128, 128, 52, DARK)
    d.arc([104, 104, 152, 152], start=-60, end=240, fill=WHITE, width=10)
    line(d, (128, 98), (128, 132), WHITE, 10)

def ui_restart(d, img):
    s_restart(d, DARK)

def ui_refresh(d, img):
    d.arc([92, 92, 164, 164], start=20, end=300, fill=ACCENT, width=11)
    poly(d, [(92, 100), (74, 122), (104, 124)], ACCENT)

def ui_undo(d, img):
    d.arc([104, 96, 172, 164], start=-40, end=200, fill=DARK, width=10)
    poly(d, [(104, 96), (84, 118), (114, 122)], DARK)

def ui_redo(d, img):
    d.arc([84, 96, 152, 164], start=-20, end=220, fill=DARK, width=10)
    poly(d, [(152, 96), (172, 118), (142, 122)], DARK)

def ui_info(d, img):
    circle(d, 128, 128, 46, ACCENT)
    circle(d, 128, 106, 6, WHITE)
    rr(d, [122, 122, 134, 162], 3, WHITE)

def ui_error(d, img):
    circle(d, 128, 128, 46, DARK)
    line(d, (112, 112), (144, 144), WHITE, 11)
    line(d, (144, 112), (112, 144), WHITE, 11)

def ui_warning(d, img):
    poly(d, [(128, 82), (178, 170), (78, 170)], FOLDER)
    line(d, (128, 116), (128, 146), DARK, 9)
    circle(d, 128, 160, 5, DARK)

def ui_favorite(d, img):
    d.pieslice([96, 100, 128, 132], 180, 360, fill=FOLDER)
    d.pieslice([128, 100, 160, 132], 180, 360, fill=FOLDER)
    poly(d, [(96, 116), (160, 116), (128, 168)], FOLDER)

def ui_sun(d, img):
    circle(d, 128, 128, 22, FOLDER)
    for i in range(8):
        a = math.pi * 2 * i / 8
        x1 = 128 + 34 * math.cos(a); y1 = 128 + 34 * math.sin(a)
        x2 = 128 + 48 * math.cos(a); y2 = 128 + 48 * math.sin(a)
        line(d, (x1, y1), (x2, y2), FOLDER, 6)

def ui_moon(d, img):
    s_moon(d, DARK)

def ui_brightness(d, img):
    circle(d, 128, 128, 18, ACCENT)
    for i in range(8):
        a = math.pi * 2 * i / 8
        x1 = 128 + 28 * math.cos(a); y1 = 128 + 28 * math.sin(a)
        x2 = 128 + 44 * math.cos(a); y2 = 128 + 44 * math.sin(a)
        line(d, (x1, y1), (x2, y2), ACCENT, 6)

def ui_sleep(d, img):
    s_moon(d, DARK)
    f = _load_font(28, bold=True)
    d.text((146, 70), "Z", font=f, fill=ACCENT)
    d.text((168, 52), "z", font=f, fill=ACCENT)

def ui_volume(d, img):
    poly(d, [(80, 116), (104, 116), (128, 92), (128, 164), (104, 140), (80, 140)], DARK)
    for i in range(2):
        r = 26 + i * 20
        d.arc([128 - r, 128 - r, 128 + r, 128 + r], start=-50, end=50, fill=ACCENT, width=8)

def ui_mute(d, img):
    poly(d, [(80, 116), (104, 116), (128, 92), (128, 164), (104, 140), (80, 140)], DARK)
    line(d, (138, 108), (174, 148), DARK, 10)
    line(d, (174, 108), (138, 148), DARK, 10)

def ui_battery(d, img):
    rr(d, [78, 102, 170, 154], 4, LIGHT)
    rr(d, [170, 118, 184, 138], 2, MID)
    rr(d, [84, 108, 138, 148], 3, ACCENT)

def ui_charging(d, img):
    s_bolt(d, ACCENT)

def ui_bluetooth(d, img):
    net_bluetooth(d, img)

def ui_wifi(d, img):
    wifi_fan(d, 128, 140, 3, ACCENT, 9)

def ui_network(d, img):
    circle(d, 128, 128, 46, DARK)
    line(d, (82, 128), (174, 128), LIGHT, 5)
    d.arc([94, 90, 162, 166], 0, 360, fill=LIGHT, width=5)

def ui_new(d, img):
    poly(d, [(128, 82), (140, 116), (176, 116), (148, 138), (158, 174), (128, 152),
             (98, 174), (108, 138), (80, 116), (116, 116)], ACCENT)

def ui_notification(d, img):
    poly(d, [(128, 84), (166, 150), (90, 150)], DARK)
    rr(d, [90, 150, 166, 162], 3, DARK)
    circle(d, 128, 172, 7, ACCENT)

def ui_open(d, img):
    rr(d, [86, 96, 150, 160], 4, LIGHT)
    line(d, (150, 116), (178, 116), DARK, 9)
    line(d, (178, 116), (162, 100), DARK, 9)
    line(d, (178, 116), (162, 132), DARK, 9)

def ui_palette(d, img):
    circle(d, 128, 128, 48, DARK)
    circle(d, 112, 112, 7, FOLDER)
    circle(d, 146, 108, 7, WHITE)
    circle(d, 150, 142, 7, ACCENT)
    circle(d, 112, 146, 7, MID)

def ui_print(d, img):
    rr(d, [96, 108, 160, 152], 4, DARK)
    rr(d, [104, 78, 152, 112], 3, LIGHT)
    rr(d, [100, 148, 156, 180], 3, ACCENT)

def ui_more(d, img):
    for i in range(3):
        circle(d, 92 + i * 36, 128, 9, DARK)

def ui_forward(d, img):
    ui_arrow(d, "right")

def ui_back(d, img):
    ui_arrow(d, "left")

# ----------------------------------------------------------------------------
# Dispatch table: filename (no ext) -> draw(draw, img)
# ----------------------------------------------------------------------------
DISPATCH = {
    # file_*
    "file_archive": make_file("archive"),
    "file_audio": make_file("audio"),
    "file_code": make_file("code"),
    "file_executable": make_file("executable"),
    "file_folder": make_file("folder"),
    "file_image": make_file("image"),
    "file_pdf": make_file("pdf"),
    "file_presentation": make_file("presentation"),
    "file_spreadsheet": make_file("spreadsheet"),
    "file_text": make_file("text"),
    "file_unknown": make_file("unknown"),
    "file_video": make_file("video"),
    # net_*
    "net_airplane_mode": net_airplane,
    "net_bluetooth": net_bluetooth,
    "net_cloud": lambda d, i: net_cloud_fn(d, i),
    "net_connected": net_connected,
    "net_dialup": net_dialup,
    "net_disconnected": net_disconnected,
    "net_download": lambda d, i: net_cloud_fn(d, i, down=True),
    "net_ethernet": net_ethernet,
    "net_firewall": net_firewall,
    "net_hotspot": net_hotspot,
    "net_mobile_data": net_mobile_data,
    "net_modem": net_modem,
    "net_network_settings": net_network_settings,
    "net_nfc": net_nfc,
    "net_no_signal": net_no_signal,
    "net_proxy": net_proxy,
    "net_router": net_router,
    "net_server": net_server,
    "net_share": net_share,
    "net_switch": net_switch,
    "net_upload": lambda d, i: net_cloud_fn(d, i, up=True),
    "net_vpn": net_vpn,
    "net_wifi": lambda d, i: wifi_fan(d, 128, 150, 3, ACCENT, 11),
}

# start_* dispatch built dynamically
def _build_start():
    m = {}
    def reg(name, fn, base=ACCENT):
        def outer(d, i):
            start_base(d, base)
            fn(d)
        m[name] = outer
    reg("start_account_settings", lambda d: (s_person(d), gear(d, 166, 166, 20, 12, 8, WHITE, hole=6, hole_col=ACCENT)))
    reg("start_all_apps", s_all_apps)
    reg("start_computer", lambda d: (rr(d, [86, 92, 170, 156], 6, WHITE), rr(d, [108, 158, 148, 172], 3, WHITE)))
    reg("start_documents", s_doc)
    reg("start_downloads", lambda d: s_arrow_down(d, WHITE))
    reg("start_files", s_folder)
    reg("start_help", lambda d: text_center(d, 128, 124, "?", _load_font(80, True), WHITE))
    reg("start_logout", s_logout)
    reg("start_music", s_music)
    reg("start_pictures", s_pictures)
    reg("start_pin", s_pin)
    reg("start_power_menu", s_power)
    reg("start_recent", s_recent)
    reg("start_recycle_bin", s_trash)
    reg("start_restart", s_restart)
    reg("start_run", s_run)
    reg("start_search", s_search)
    reg("start_settings", s_gear)
    reg("start_shutdown", lambda d: s_power(d, WHITE), base=DARK)
    reg("start_sleep", s_moon)
    reg("start_start", lambda d: poly(d, [(108, 92), (108, 164), (168, 128)], WHITE))
    reg("start_switch_user", s_switch_user)
    reg("start_user_avatar", s_person)
    reg("start_videos", s_videos)
    return m

# status_*
def _build_status():
    return {
        "status_battery_empty": lambda d, i: status_battery(d, i, 0.0),
        "status_battery_low": lambda d, i: status_battery(d, i, 0.25),
        "status_battery_mid": lambda d, i: status_battery(d, i, 0.5),
        "status_battery_high": lambda d, i: status_battery(d, i, 0.75),
        "status_battery_full": lambda d, i: status_battery(d, i, 1.0),
        "status_charging": lambda d, i: status_battery(d, i, 0.7, charging=True),
        "status_volume_low": lambda d, i: status_volume(d, i, 1),
        "status_volume_mid": lambda d, i: status_volume(d, i, 2),
        "status_volume_high": lambda d, i: status_volume(d, i, 3),
        "status_volume_mute": lambda d, i: status_volume(d, i, 0, mute=True),
        "status_wifi_weak": lambda d, i: status_wifi(d, i, 1),
        "status_wifi_mid": lambda d, i: status_wifi(d, i, 2),
        "status_wifi_strong": lambda d, i: status_wifi(d, i, 3),
        "status_wifi_off": lambda d, i: status_wifi(d, i, 0, off=True),
    }

# ui_*
def _build_ui():
    return {
        "ui_arrow_down": lambda d, i: ui_arrow(d, "down"),
        "ui_arrow_up": lambda d, i: ui_arrow(d, "up"),
        "ui_arrow_left": lambda d, i: ui_arrow(d, "left"),
        "ui_arrow_right": lambda d, i: ui_arrow(d, "right"),
        "ui_back": ui_back,
        "ui_battery": ui_battery,
        "ui_bluetooth": ui_bluetooth,
        "ui_brightness": ui_brightness,
        "ui_calendar": ui_calendar,
        "ui_charging": ui_charging,
        "ui_check": ui_check,
        "ui_clock": ui_clock,
        "ui_close": ui_close,
        "ui_copy": ui_copy,
        "ui_delete": ui_delete,
        "ui_edit": ui_edit,
        "ui_error": ui_error,
        "ui_favorite": ui_favorite,
        "ui_file": ui_file,
        "ui_folder": ui_folder,
        "ui_forward": ui_forward,
        "ui_home": ui_home,
        "ui_info": ui_info,
        "ui_lock": ui_lock,
        "ui_maximize": ui_maximize,
        "ui_minimize": ui_minimize,
        "ui_minus": ui_minus,
        "ui_moon": ui_moon,
        "ui_more": ui_more,
        "ui_mute": ui_mute,
        "ui_network": ui_network,
        "ui_new": ui_new,
        "ui_notification": ui_notification,
        "ui_open": ui_open,
        "ui_palette": ui_palette,
        "ui_paste": ui_paste,
        "ui_plus": ui_plus,
        "ui_power": ui_power,
        "ui_print": ui_print,
        "ui_redo": ui_redo,
        "ui_refresh": ui_refresh,
        "ui_restart": ui_restart,
        "ui_restore": ui_restore,
        "ui_save": ui_save,
        "ui_search": ui_search,
        "ui_settings": ui_settings,
        "ui_share": net_share,
        "ui_shutdown": ui_shutdown,
        "ui_sleep": ui_sleep,
        "ui_sun": ui_sun,
        "ui_undo": ui_undo,
        "ui_user": ui_user,
        "ui_volume": ui_volume,
        "ui_warning": ui_warning,
        "ui_wifi": ui_wifi,
    }

DISPATCH.update(_build_start())
DISPATCH.update(_build_status())
DISPATCH.update(_build_ui())


def fallback(d, img):
    rr(d, [60, 60, 196, 196], 28, ACCENT)
    circle(d, 128, 128, 22, WHITE)


def main():
    files = sorted(f for f in os.listdir(INDIVIDUAL_DIR) if f.endswith(".png"))
    print(f"Generating {len(files)} icons into {INDIVIDUAL_DIR}")
    missing = []
    for f in files:
        stem = os.path.splitext(f)[0]
        img, d = new_canvas()
        fn = DISPATCH.get(stem, fallback)
        fn(d, img)
        if stem not in DISPATCH:
            missing.append(stem)
        out = img.resize((SIZE, SIZE), Image.LANCZOS)
        out.save(os.path.join(INDIVIDUAL_DIR, f))
    if missing:
        print("WARNING fallback used for:", missing)
    else:
        print("All 128 icons have dedicated draw functions.")
    print("Done.")


if __name__ == "__main__":
    main()
