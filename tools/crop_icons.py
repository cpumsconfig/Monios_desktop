"""
Crop icon sprite sheets into individual transparent PNG icons.
Also normalizes existing icons to uniform 64x64 RGBA format.
"""
import os
from PIL import Image

INDIVIDUAL_DIR = r"D:\11\monios\monios_x64\assets\icons\individual"
SHEET_DIR = r"D:\11\monios\monios_x64\assets\icons"
TARGET_SIZE = 64

def remove_white_background(img, threshold=240):
    """Convert near-white pixels to transparent."""
    img = img.convert("RGBA")
    pixels = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if r > threshold and g > threshold and b > threshold:
                pixels[x, y] = (r, g, b, 0)
    return img

def trim_whitespace(img):
    """Crop to non-transparent bounding box."""
    bbox = img.getbbox()
    if bbox:
        return img.crop(bbox)
    return img

def resize_icon(img, size=TARGET_SIZE):
    """Resize icon to fit within size x size, preserving aspect ratio, centered."""
    img = trim_whitespace(img)
    w, h = img.size
    if w == 0 or h == 0:
        return Image.new("RGBA", (size, size), (0, 0, 0, 0))
    scale = min(size / w, size / h) * 0.8  # 80% to leave padding
    new_w = max(1, int(w * scale))
    new_h = max(1, int(h * scale))
    img = img.resize((new_w, new_h), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(img, ((size - new_w) // 2, (size - new_h) // 2), img)
    return canvas

def crop_sheet(sheet_path, names, cols, rows, out_dir):
    """Crop a sprite sheet into individual icons."""
    img = Image.open(sheet_path)
    w, h = img.size
    cell_w = w // cols
    cell_h = h // rows
    saved = []
    for i, name in enumerate(names):
        col = i % cols
        row = i // cols
        if row >= rows:
            break
        left = col * cell_w
        upper = row * cell_h
        right = left + cell_w
        lower = upper + cell_h
        cell = img.crop((left, upper, right, lower))
        cell = remove_white_background(cell)
        cell = resize_icon(cell)
        out_path = os.path.join(out_dir, name)
        cell.save(out_path, "PNG")
        saved.append(name)
    return saved

def main():
    os.makedirs(INDIVIDUAL_DIR, exist_ok=True)

    # Sheet 1: Network icons (4 cols x 3 rows)
    network_names = [
        "net_bluetooth.png",
        "net_hotspot.png",
        "net_mobile_data.png",
        "net_airplane_mode.png",
        "net_ethernet.png",
        "net_router.png",
        "net_no_signal.png",
        "net_connected.png",
        "net_disconnected.png",
        "net_modem.png",
        "net_nfc.png",
        "net_switch.png",
    ]
    saved1 = crop_sheet(os.path.join(SHEET_DIR, "sheet_network.png"),
                         network_names, 4, 3, INDIVIDUAL_DIR)
    print(f"Network icons: {len(saved1)} saved")

    # Sheet 2: Start menu icons (4 cols x 3 rows)
    start_names = [
        "start_settings.png",
        "start_files.png",
        "start_pictures.png",
        "start_music.png",
        "start_videos.png",
        "start_documents.png",
        "start_downloads.png",
        "start_computer.png",
        "start_recycle_bin.png",
        "start_run.png",
        "start_help.png",
        "start_search.png",
    ]
    saved2 = crop_sheet(os.path.join(SHEET_DIR, "sheet_start.png"),
                         start_names, 4, 3, INDIVIDUAL_DIR)
    print(f"Start menu icons: {len(saved2)} saved")

    # Sheet 3: System/UI icons (4 cols x 4 rows)
    system_names = [
        "ui_home.png",
        "ui_back.png",
        "ui_forward.png",
        "ui_refresh.png",
        "ui_edit.png",
        "ui_share.png",
        "ui_favorite.png",
        "ui_print.png",
        "ui_info.png",
        "ui_warning.png",
        "ui_error.png",
        "ui_clock.png",
        "ui_calendar.png",
        "ui_sun.png",
        "ui_moon.png",
        "ui_palette.png",
    ]
    saved3 = crop_sheet(os.path.join(SHEET_DIR, "sheet_system.png"),
                         system_names, 4, 4, INDIVIDUAL_DIR)
    print(f"System icons: {len(saved3)} saved")

    # Normalize existing icons to uniform 64x64
    existing = [f for f in os.listdir(INDIVIDUAL_DIR)
                if f.endswith(".png") and f not in saved1 + saved2 + saved3]
    normalized = 0
    for name in existing:
        path = os.path.join(INDIVIDUAL_DIR, name)
        try:
            img = Image.open(path)
            if img.size != (TARGET_SIZE, TARGET_SIZE) or img.mode != "RGBA":
                img = img.convert("RGBA")
                # For existing icons, check if they have transparency already
                # If not, remove white background
                if img.mode == "RGBA":
                    alpha = img.split()[3]
                    if alpha.getextrema() == (255, 255):
                        # No transparency, remove white bg
                        img = remove_white_background(img, threshold=235)
                img = resize_icon(img)
                img.save(path, "PNG")
                normalized += 1
        except Exception as e:
            print(f"  Warning: could not normalize {name}: {e}")
    print(f"Existing icons normalized: {normalized}")

    # Final count
    total = len([f for f in os.listdir(INDIVIDUAL_DIR) if f.endswith(".png")])
    print(f"\nTotal icons in individual/: {total}")

if __name__ == "__main__":
    main()
