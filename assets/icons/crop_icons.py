"""
Crop icon sets into individual icons.
Each icon set is a grid of icons on white background.
"""
from PIL import Image
import os

ICONS_DIR = r"D:\11\monios\monios_x64\assets\icons"
OUT_DIR = os.path.join(ICONS_DIR, "individual")

os.makedirs(OUT_DIR, exist_ok=True)

def crop_grid(filename, rows, cols, icon_names, prefix=""):
    """Crop a grid of icons into individual files.
    
    Args:
        filename: input image filename (in ICONS_DIR)
        rows: number of rows
        cols: number of columns
        icon_names: list of names, row-major order
        prefix: output filename prefix
    """
    img_path = os.path.join(ICONS_DIR, filename)
    img = Image.open(img_path).convert("RGBA")
    w, h = img.size
    
    # Calculate cell size
    cell_w = w // cols
    cell_h = h // rows
    
    count = 0
    for row in range(rows):
        for col in range(cols):
            if count >= len(icon_names):
                break
            
            # Crop cell
            left = col * cell_w
            top = row * cell_h
            right = left + cell_w
            bottom = top + cell_h
            
            # Crop with some margin
            margin = int(min(cell_w, cell_h) * 0.15)
            left += margin
            top += margin
            right -= margin
            bottom -= margin
            
            icon = img.crop((left, top, right, bottom))
            
            # Trim whitespace
            bbox = icon.getbbox()
            if bbox:
                icon = icon.crop(bbox)
            
            # Save
            name = icon_names[count]
            out_name = f"{prefix}{name}.png"
            out_path = os.path.join(OUT_DIR, out_name)
            icon.save(out_path, "PNG")
            print(f"  Saved: {out_name} ({icon.width}x{icon.height})")
            
            count += 1
    
    print(f"Done: {filename} -> {count} icons\n")


# ========== 1. UI Line Icons Set 1 (2 rows x 10 cols = 20 icons) ==========
ui_line1_names = [
    # Row 1
    "settings", "search", "power", "shutdown", "restart",
    "sleep", "lock", "user", "notification", "volume",
    # Row 2
    "mute", "brightness", "wifi", "bluetooth", "battery",
    "charging", "network", "folder", "file", "save"
]
crop_grid("ui_icons_line1.png", 2, 10, ui_line1_names, prefix="ui_")


# ========== 2. UI Line Icons Set 2 (2 rows x 10 cols = 19 icons) ==========
ui_line2_names = [
    # Row 1
    "open", "new", "delete", "copy", "paste",
    "undo", "redo", "close", "minimize", "maximize",
    # Row 2
    "restore", "more", "check", "plus", "minus",
    "arrow_up", "arrow_down", "arrow_left", "arrow_right"
]
crop_grid("ui_icons_line2.png", 2, 10, ui_line2_names, prefix="ui_")


# ========== 3. Status Icons Group 1 (3 rows: 4 + 6 + 4) ==========
# This one is not uniform grid, handle manually
def crop_status_group1():
    img_path = os.path.join(ICONS_DIR, "status_icons_group1.png")
    img = Image.open(img_path).convert("RGBA")
    w, h = img.size
    
    # Row 1: volume (4 icons)
    row1_h = h // 3
    cell_w = w // 10  # approximate
    
    volume_names = ["volume_mute", "volume_low", "volume_mid", "volume_high"]
    for i, name in enumerate(volume_names):
        left = i * cell_w + int(cell_w * 0.1)
        top = int(row1_h * 0.2)
        right = (i + 1) * cell_w - int(cell_w * 0.1)
        bottom = int(row1_h * 0.8)
        icon = img.crop((left, top, right, bottom))
        bbox = icon.getbbox()
        if bbox:
            icon = icon.crop(bbox)
        out_path = os.path.join(OUT_DIR, f"status_{name}.png")
        icon.save(out_path, "PNG")
        print(f"  Saved: status_{name}.png ({icon.width}x{icon.height})")
    
    # Row 2: battery (6 icons)
    row2_top = row1_h
    row2_h = row1_h
    cell_w2 = w // 12
    battery_names = ["battery_empty", "battery_low", "battery_mid", "battery_high", "battery_full", "charging"]
    for i, name in enumerate(battery_names):
        left = i * cell_w2 + int(cell_w2 * 0.1)
        top = row2_top + int(row2_h * 0.2)
        right = (i + 1) * cell_w2 - int(cell_w2 * 0.1)
        bottom = row2_top + int(row2_h * 0.8)
        icon = img.crop((left, top, right, bottom))
        bbox = icon.getbbox()
        if bbox:
            icon = icon.crop(bbox)
        out_path = os.path.join(OUT_DIR, f"status_{name}.png")
        icon.save(out_path, "PNG")
        print(f"  Saved: status_{name}.png ({icon.width}x{icon.height})")
    
    # Row 3: wifi (4 icons)
    row3_top = row1_h * 2
    wifi_names = ["wifi_off", "wifi_weak", "wifi_mid", "wifi_strong"]
    for i, name in enumerate(wifi_names):
        left = i * cell_w + int(cell_w * 0.1)
        top = row3_top + int(row1_h * 0.2)
        right = (i + 1) * cell_w - int(cell_w * 0.1)
        bottom = row3_top + int(row1_h * 0.8)
        icon = img.crop((left, top, right, bottom))
        bbox = icon.getbbox()
        if bbox:
            icon = icon.crop(bbox)
        out_path = os.path.join(OUT_DIR, f"status_{name}.png")
        icon.save(out_path, "PNG")
        print(f"  Saved: status_{name}.png ({icon.width}x{icon.height})")
    
    print("  Done: status_icons_group1.png -> 14 icons\n")

crop_status_group1()


# ========== 4. Filetype Icons (3 rows x 4 cols = 12 icons) ==========
filetype_names = [
    # Row 1
    "folder", "text", "image", "audio",
    # Row 2
    "video", "code", "archive", "pdf",
    # Row 3
    "spreadsheet", "presentation", "executable", "unknown"
]
crop_grid("filetype_icons.png", 3, 4, filetype_names, prefix="file_")


# ========== 5. Network Icons (3 rows x 4 cols = 12 icons) ==========
network_names = [
    # Row 1
    "ethernet", "wifi", "vpn", "dialup",
    # Row 2
    "upload", "download", "cloud", "server",
    # Row 3
    "firewall", "proxy", "share", "network_settings"
]
crop_grid("network_icons.png", 3, 4, network_names, prefix="net_")


# ========== 6. Start Menu Icons (3 rows x 4 cols = 12 icons) ==========
start_names = [
    # Row 1
    "start", "all_apps", "recent", "pin",
    # Row 2
    "user_avatar", "account_settings", "logout", "switch_user",
    # Row 3
    "power_menu", "shutdown", "restart", "sleep"
]
crop_grid("start_menu_icons.png", 3, 4, start_names, prefix="start_")


# ========== Summary ==========
files = os.listdir(OUT_DIR)
png_files = [f for f in files if f.endswith(".png")]
print(f"\n=== Total: {len(png_files)} individual icons saved to {OUT_DIR} ===")
