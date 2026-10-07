# -*- coding: utf-8 -*-
# 一次性分析脚本 4：整盘 MPLS 结构扫描 —— 判定分集信息的存放位置
# 关心三件事：
#   ① 主播放列表的 PlayItem/clip 结构（一集一个 m2ts？多集一个 m2ts？）
#   ② 同一 clip 是否被多个 PlayItem 引用（多集一个 m2ts 时的分集边界来源）
#   ③ 章节标记（PlayListMark）落在哪（PlayItem 边界上=分集点，集内=OP/ED 级）
# 只读：仅 open(path, 'rb')，不写任何文件
import struct, sys, os

T = 45000.0

def t2s(t):
    s = t / T
    return "%d:%02d:%05.2f" % (s // 3600, s % 3600 // 60, s % 60)

def parse(path):
    """返回 (items, marks)；items=[(clip, IN, OUT)]，marks=[(itemIdx, time, type)]"""
    data = open(path, 'rb').read()
    if len(data) < 24 or data[:4] != b'MPLS':
        return None
    pl, = struct.unpack('>I', data[8:12])
    mk, = struct.unpack('>I', data[12:16])
    n_items, = struct.unpack('>H', data[pl+6:pl+8])
    items, off = [], pl + 10
    for i in range(n_items):
        if off + 22 > len(data):
            return None
        ilen, = struct.unpack('>H', data[off:off+2])
        clip = data[off+2:off+7].decode('ascii', 'replace')
        inh, = struct.unpack('>I', data[off+14:off+18])
        outh, = struct.unpack('>I', data[off+18:off+22])
        if outh < inh:
            return None
        items.append((clip, inh, outh))
        off += 2 + ilen
    marks = []
    if mk and mk + 6 <= len(data):
        n_marks, = struct.unpack('>H', data[mk+4:mk+6])
        o = mk + 6
        for i in range(n_marks):
            if o + 14 > len(data):
                break
            # 实证布局（三盘原始字节验证）：type u16 + item u16 + time u32
            # （与 PlayItem IN/OUT 同一 clip 时间轴）+ pid u16(0xFFFF) + dur u32
            mtype, item, = struct.unpack('>HH', data[o:o+4])
            mtime, = struct.unpack('>I', data[o+4:o+8])
            marks.append((item, mtime, mtype))
            o += 14
    return items, marks

def pdur(items):
    return sum(b - a for _, a, b in items)

def human(n):
    for u, d in (("GB", 1 << 30), ("MB", 1 << 20)):
        if n >= d:
            return "%.1f%s" % (n / d, u)
    return "%dB" % n

def main(bdmv_dir):
    pdir = bdmv_dir.rstrip("\\/")
    # 不能用 glob：目录名里的方括号（如 "0124[xxx]"、"[BDMV]"）会被当成字符类
    files = sorted(os.path.join(pdir, f) for f in os.listdir(pdir)
                   if f.lower().endswith(".mpls"))
    if not files:
        print("  (no .mpls found)"); return
    sdir = os.path.normpath(os.path.join(pdir, "..", "STREAM"))
    ssize = {}
    if os.path.isdir(sdir):
        for f in os.listdir(sdir):
            if f.lower().endswith(".m2ts"):
                try: ssize[f[:5]] = os.path.getsize(os.path.join(sdir, f))
                except OSError: pass

    parsed = []
    for p in files:
        r = parse(p)
        if r: parsed.append((os.path.basename(p), r[0], r[1]))
    parsed.sort(key=lambda x: -pdur(x[1]))

    print("  MPLS count=%d  parse_ok=%d  STREAM m2ts=%d" % (
        len(files), len(parsed), len(ssize)))
    print("  --- PlayLists by total duration (top 8) ---")
    for name, items, marks in parsed[:8]:
        print("    %-12s items=%-3d marks=%-3d total=%s" % (
            name, len(items), len(marks), t2s(pdur(items))))

    # 主列表（最长）细节
    name, items, marks = parsed[0]
    print("  --- MAIN: %s item detail ---" % name)
    for i, (clip, a, b) in enumerate(items):
        sz = ssize.get(clip, -1)
        print("    #%-2d clip=%s IN=%s OUT=%s dur=%s m2ts=%s" % (
            i, clip, t2s(a), t2s(b), t2s(b - a),
            human(sz) if sz >= 0 else "?"))

    # clip 复用检测：同一列表内同一 clip 被多个 PlayItem 引用
    seen = {}
    for i, (clip, a, b) in enumerate(items):
        seen.setdefault(clip, []).append((i, a, b))
    multi = {c: v for c, v in seen.items() if len(v) > 1}
    print("  --- clips referenced >1 time in MAIN: %d ---" % len(multi))
    for c, v in sorted(multi.items()):
        print("    clip=%s x%d: " % (c, len(v)) +
              ", ".join("#%d[%s..%s]" % (i, t2s(a), t2s(b)) for i, a, b in v))

    # 章节落点：分集点应落在 PlayItem 边界（每集起点）而非集内
    print("  --- MAIN marks: %d ---" % len(marks))
    from collections import Counter
    per_item = Counter(m[0] for m in marks)
    print("    marks per PlayItem:", dict(sorted(per_item.items())))
    # 章节时间：换算为"文件内容相对时间"= markTime - 所在 item 的 IN
    # （IN/OUT 与 markTime 同在 clip 时间轴上，二者相减即得内容内位置）
    for item, mtime, mtype in marks:
        if item < len(items):
            a, b = items[item][1], items[item][2]
            rel = mtime - a
            flag = ""
            if rel < 0 or mtime > b:
                flag = "  <-- OUT OF ITEM RANGE!"
            print("    item#%-2d type=%d abs=%s rel=%s%s" % (
                item, mtype, t2s(mtime), t2s(rel) if rel >= 0 else "?", flag))

    # 跨列表 clip 复用（只在主列表没复用时才有意义）
    if not multi:
        refcnt = Counter()
        for _, its, _ in parsed:
            for clip, a, b in its:
                refcnt[clip] += 1
        top = refcnt.most_common(5)
        print("  --- cross-playlist clip refs (top5): %s ---" % top)

def detail_file(path):
    """单文件详查：全部 PlayItem + 全部章节（换算内容相对时间）"""
    r = parse(path)
    if not r:
        print("  parse FAIL"); return
    items, marks = r
    sdir = os.path.normpath(os.path.join(os.path.dirname(path), "..", "STREAM"))
    print("  items=%d marks=%d total=%s" % (len(items), len(marks), t2s(pdur(items))))
    for i, (clip, a, b) in enumerate(items):
        sz = ""
        try:
            sz = " m2ts=" + human(os.path.getsize(os.path.join(sdir, clip + ".m2ts")))
        except OSError:
            pass
        print("    #%-2d clip=%s IN=%s OUT=%s dur=%s%s" % (
            i, clip, t2s(a), t2s(b), t2s(b - a), sz))
    for item, mtime, mtype in marks:
        if item < len(items):
            a, b = items[item][1], items[item][2]
            rel = mtime - a
            flag = "  <-- OUT OF RANGE!" if (rel < 0 or mtime > b) else ""
            print("    mark item#%-2d type=%d abs=%s rel=%s%s" % (
                item, mtype, t2s(mtime), t2s(rel) if rel >= 0 else "?", flag))

if __name__ == "__main__":
    for d in sys.argv[1:]:
        print("=" * 72)
        if d.lower().endswith(".mpls"):
            print("FILE: %s" % d)
            detail_file(d)
        else:
            print("DIR: %s" % d)
            main(d)
