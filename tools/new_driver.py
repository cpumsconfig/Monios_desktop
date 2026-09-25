#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
new_driver.py - Monios 新驱动脚手架生成器（Task 27b）。

根据用户输入，从 drivers/template/ 复制并替换占位符，生成新驱动目录。
仅依赖 Python 标准库。

用法：
    python tools/new_driver.py --name myuart --type char --vendor "MyCompany"
    python tools/new_driver.py --list            # 列出当前所有驱动
    python tools/new_driver.py --name demo --type block --vendor "ACME"

生成后会打印需要追加到 Makefile 的片段。
"""

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE_DIR = os.path.join(REPO_ROOT, "drivers", "template")
DRIVERS_DIR = os.path.join(REPO_ROOT, "drivers")

# 占位符 -> 替换值
PLACEHOLDERS = {
    "__DRIVER_NAME__": "driver_name",
    "__DRIVER_TYPE__": "char_or_block",
    "__VENDOR__": "VendorName",
    "__DRIVER_MAJOR__": "1",
    "__DRIVER_MINOR__": "0",
}


def list_drivers():
    """列出 drivers/ 下已有的驱动目录（含 .c 的目录）。"""
    print("Monios 现有驱动目录：")
    for entry in sorted(os.listdir(DRIVERS_DIR)):
        full = os.path.join(DRIVERS_DIR, entry)
        if os.path.isdir(full) and not entry.startswith("."):
            # 只显示含 .c 文件的目录
            has_c = any(f.endswith(".c") for f in os.listdir(full))
            if has_c:
                print("  - %s/" % entry)


def render(text, mapping):
    for k, v in mapping.items():
        text = text.replace(k, v)
    return text


def generate(name, dtype, vendor):
    if not name.replace("_", "").isalnum():
        print("错误：驱动名只能含字母数字下划线", file=sys.stderr)
        return 1
    if dtype not in ("char", "block"):
        print("错误：--type 必须是 char 或 block", file=sys.stderr)
        return 1

    target_dir = os.path.join(DRIVERS_DIR, name)
    if os.path.exists(target_dir):
        print("错误：目录已存在：%s" % target_dir, file=sys.stderr)
        return 1

    src_c = os.path.join(TEMPLATE_DIR,
                        "template_%s.c" % dtype)
    src_h = os.path.join(TEMPLATE_DIR, "template.h")
    if not os.path.exists(src_c) or not os.path.exists(src_h):
        print("错误：模板文件缺失（%s）" % src_c, file=sys.stderr)
        return 1

    mapping = dict(PLACEHOLDERS)
    mapping["__DRIVER_NAME__"] = name
    mapping["__DRIVER_TYPE__"] = dtype
    mapping["__VENDOR__"] = vendor

    os.makedirs(target_dir)
    dst_c = os.path.join(target_dir, name + ".c")
    dst_h = os.path.join(target_dir, name + ".h")

    with open(src_c, "r", encoding="utf-8") as f:
        c_text = render(f.read(), mapping)
    # 把模板内的 template 字样替换为驱动名
    c_text = c_text.replace("template_%s" % dtype, name).replace("template", name)
    with open(dst_c, "w", encoding="utf-8") as f:
        f.write(c_text)

    with open(src_h, "r", encoding="utf-8") as f:
        h_text = render(f.read(), mapping)
    h_text = h_text.replace("TEMPLATE_", name.upper() + "_").replace("template", name)
    with open(dst_h, "w", encoding="utf-8") as f:
        f.write(h_text)

    # 复制 README / Makefile.fragment
    for aux in ("README.md", "Makefile.fragment"):
        src = os.path.join(TEMPLATE_DIR, aux)
        if os.path.exists(src):
            with open(src, "r", encoding="utf-8") as f:
                data = f.read()
            data = render(data, mapping).replace("mydriver", name)
            with open(os.path.join(target_dir, aux), "w", encoding="utf-8") as f:
                f.write(data)

    print("已生成驱动脚手架：%s/" % name)
    print("  %s" % dst_c)
    print("  %s" % dst_h)
    print()
    print("接下来：")
    print("  1. 编辑 %s.c，按 TODO 注释填写硬件逻辑" % name)
    print("  2. 在 Makefile 中追加（见 %s/Makefile.fragment）：" % name)
    print("       out/driver_%s.pe.o : drivers/%s/%s.c" % (name, name, name))
    print("       out/%s.unsigned.sys : user/apps/driver.ld out/driver_%s.pe.o out/sysstub.pe.o" % (name, name))
    print("  3. make -j2 out/%s.sys" % name)
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description="Monios 新驱动脚手架生成器")
    ap.add_argument("--name", help="驱动名（如 myuart）")
    ap.add_argument("--type", choices=["char", "block"], help="驱动类型")
    ap.add_argument("--vendor", default="Monios", help="厂商名")
    ap.add_argument("--list", action="store_true", help="列出当前所有驱动")
    args = ap.parse_args(argv)

    if args.list:
        list_drivers()
        return 0
    if not args.name or not args.type:
        ap.print_help()
        return 1
    return generate(args.name, args.type, args.vendor)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
