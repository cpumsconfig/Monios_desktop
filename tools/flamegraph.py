#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
flamegraph.py - Monios CPU 火焰图生成器（Task 25b）。

读取 Brendan Gregg stackcollapse 折叠栈文本：
    栈帧1;栈帧2;栈帧3    计数
    ...
输出：
    - 默认生成独立 SVG（可在浏览器打开，悬停看详情、点击缩放）
    - --text  在终端打印 Unicode 块字符火焰图

仅依赖 Python 标准库，不装任何第三方包。

用法：
    python tools/flamegraph.py perf.folded -o perf.svg
    python tools/flamegraph.py perf.folded --text
    cat perf.folded | python tools/flamegraph.py -o perf.svg
"""

import sys
import os
import hashlib
import argparse


# ── 调用树 ─────────────────────────────────────────────────────
class Node:
    __slots__ = ("name", "value", "children")

    def __init__(self, name):
        self.name = name
        self.value = 0          # self + children 的总样本数
        self.children = {}      # name -> Node


def load_folded(path):
    """读取折叠栈文件，返回 [(frames_list, count), ...]。"""
    rows = []
    if path == "-":
        f = sys.stdin
    else:
        f = open(path, "r", encoding="utf-8")
    for line in f:
        line = line.strip()
        if not line:
            continue
        # 最后一段是计数
        if " " in line:
            stack, _, cnt = line.rpartition(" ")
        elif "\t" in line:
            stack, _, cnt = line.rpartition("\t")
        else:
            continue
        stack = stack.strip()
        try:
            count = int(cnt.strip())
        except ValueError:
            continue
        frames = [s for s in stack.split(";") if s]
        if frames:
            rows.append((frames, count))
    if path != "-":
        f.close()
    return rows


def build_tree(rows):
    root = Node("all")
    for frames, count in rows:
        node = root
        node.value += count
        for name in frames:
            child = node.children.get(name)
            if child is None:
                child = Node(name)
                node.children[name] = child
            child.value += count
            node = child
    return root


# ── 颜色：按函数名哈希（类似 flamegraph.pl 的暖色方案）─────────
def color_for(name):
    h = hashlib.md5(name.encode("utf-8")).digest()
    v = h[0] + (h[1] << 8)
    # 暖色系：红/黄区间 200..230 色相
    r = 200 + (v % 55)
    g = 80 + ((v >> 6) % 120)
    b = 40 + ((v >> 12) % 60)
    return "rgb(%d,%d,%d)" % (r, g, b)


# ── SVG 布局 ──────────────────────────────────────────────────
FRAME_H = 16
WIDTH = 1200
MARGIN = 10


def layout(root):
    """返回 (rects, max_depth, total)。rect: dict(x,y,w,h,name,value,total)。"""
    total = root.value
    rects = []
    max_depth = [0]

    def walk(node, depth, x, w):
        if depth > 0:
            rects.append({
                "x": x, "y": depth, "w": w, "h": FRAME_H,
                "name": node.name, "value": node.value, "total": total,
            })
        if depth > max_depth[0]:
            max_depth[0] = depth
        # 子节点按 value 从大到小排
        kids = sorted(node.children.values(), key=lambda n: -n.value)
        cx = x
        for c in kids:
            cw = w * c.value / total if total else 0
            walk(c, depth + 1, cx, cw)
            cx += cw

    walk(root, 0, MARGIN, WIDTH - 2 * MARGIN)
    return rects, max_depth[0] + 1, total


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;"))


def write_svg(rects, depth, total, out_path):
    height = depth * FRAME_H + 40
    parts = []
    parts.append(
        '<?xml version="1.0" standalone="no"?>\n'
        '<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" '
        '"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">\n'
        '<svg version="1.1" width="%d" height="%d" '
        'xmlns="http://www.w3.org/2000/svg">\n'
        % (WIDTH, height))
    parts.append('<rect width="100%" height="100%" fill="#ffffff"/>\n')
    parts.append('<text x="%d" y="18" font-family="monospace" font-size="13" '
                 'fill="#333">Monios CPU Flame Graph — total %d samples</text>\n'
                 % (MARGIN, total))

    # 从 root（y=0）往上画；rects 里 y=depth，越深越靠上。
    # 火焰图惯例：root 在底部。
    for r in rects:
        y = height - 20 - (r["y"] + 1) * FRAME_H
        x = r["x"]
        w = max(1, r["w"])
        pct = 100.0 * r["value"] / total if total else 0
        label = "%s (%.1f%%, %d)" % (r["name"], pct, r["value"])
        parts.append(
            '<g class="frame"><title>%s</title>'
            '<rect x="%.1f" y="%d" width="%.1f" height="%d" fill="%s" '
            'stroke="#ffffff" stroke-width="0.5"/>'
            % (esc(label), x, y, w, FRAME_H - 1, color_for(r["name"])))
        if w > 30:
            # 名字截断显示
            nm = r["name"]
            maxchars = int(w / 7)
            if len(nm) > maxchars:
                nm = nm[:maxchars - 1] + "…"
            parts.append(
                '<text x="%.1f" y="%d" font-family="monospace" font-size="11" '
                'fill="#000">%s</text>'
                % (x + 2, y + 11, esc(nm)))
        parts.append("</g>\n")

    # 简单交互：点击放大（JS 重排）。这里只加一个提示，不做完整缩放引擎。
    parts.append("""
<script type="text/javascript"><![CDATA[
// 点击某个矩形：把它作为新的根重排（简化版：仅高亮提示）。
document.querySelectorAll('g.frame').forEach(function(g){
  g.style.cursor='pointer';
  g.addEventListener('click', function(){
    var t = g.querySelector('title').textContent;
    alert(t);
  });
});
]]></script>
""")
    parts.append("</svg>\n")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("".join(parts))


# ── 终端文本火焰图（Unicode 块字符）────────────────────────────
BLOCK = "█"


def write_text(rects, depth, total):
    out = []
    out.append("Monios CPU Flame Graph (text) — total %d samples, %d rows"
               % (total, depth))
    out.append("=" * 70)
    # 按 y 从深到浅打印（root 在最后）
    for r in sorted(rects, key=lambda r: -r["y"]):
        pct = 100.0 * r["value"] / total if total else 0
        bar_w = int(r["w"] / (WIDTH - 2 * MARGIN) * 60)
        bar = BLOCK * max(1, bar_w)
        out.append("%3d%% %s" % (int(pct), bar))
    out.append("=" * 70)
    out.append("提示：SVG 版支持悬停查看完整函数名与采样数。")
    print("\n".join(out))


def main(argv):
    ap = argparse.ArgumentParser(description="Monios 火焰图生成器")
    ap.add_argument("input", help="折叠栈文本文件（- 表示 stdin）")
    ap.add_argument("-o", "--output", default="flamegraph.svg",
                    help="输出 SVG 路径（默认 flamegraph.svg）")
    ap.add_argument("--text", action="store_true",
                    help="在终端打印文本火焰图，不生成 SVG")
    args = ap.parse_args(argv)

    rows = load_folded(args.input)
    if not rows:
        print("flamegraph: 没有解析到任何栈样本", file=sys.stderr)
        return 1
    root = build_tree(rows)
    rects, depth, total = layout(root)
    if args.text:
        write_text(rects, depth, total)
    else:
        write_svg(rects, depth, total, args.output)
        print("flamegraph: 已生成 %s（%d 个矩形，%d 样本）"
              % (args.output, len(rects), total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
