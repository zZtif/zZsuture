# -*- coding: utf-8 -*-
# 一次性分析脚本 2：验证 SubPath 边界 + 全盘 MPLS 扫描（用完即删）
import struct, sys, glob, os

def ticks2str(t):
    s = t / 45000.0
    return f"{int(s//3600)}:{int(s%3600//60):02d}:{s%60:04.1f}"

def parse_items(data):
    """按实证布局解析：PlayList@0x08 地址，PlayItem = u16 长度 + clip5 + codec4 + 3B + IN@+14 + OUT@+18"""
    pl, = struct.unpack('>I', data[8:12])
    if pl + 10 > len(data) or data[:4] != b'MPLS':
        return None
    sec_len, = struct.unpack('>I', data[pl:pl+4])
    n_items, = struct.unpack('>H', data[pl+6:pl+8])
    n_sub, = struct.unpack('>H', data[pl+8:pl+10])
    items = []
    off = pl + 10
    for i in range(n_items):
        if off + 22 > len(data):
            return None
        ilen, = struct.unpack('>H', data[off:off+2])
        clip = data[off+2:off+7].decode('ascii', 'replace')
        inh, = struct.unpack('>I', data[off+14:off+18])
        outh, = struct.unpack('>I', data[off+18:off+22])
        if outh < inh or ilen < 20:
            return None
        items.append((clip, inh, outh, off))
        off += 2 + ilen
    return pl, sec_len, n_sub, items, off

path = sys.argv[1]
data = open(path, 'rb').read()
r = parse_items(data)
if not r:
    print("解析失败"); sys.exit(1)
pl, sec_len, n_sub, items, items_end = r
print(f"PlayList@{pl} 段长{sec_len} nItems={len(items)} nSubPaths={n_sub}")
print(f"PlayItem 区结束于 {items_end}，段尾应为 {pl+4+sec_len}")
if pl + 4 + sec_len - items_end > 4:
    sublen, = struct.unpack('>I', data[items_end:items_end+4])
    print(f"其后 u32 = {sublen}（若≈剩余字节数即 SubPath 验证通过）")

durs = [(outh - inh) / 45000.0 for _, inh, outh, _ in items]
uniq = set(round(d) for d in durs)
print(f"\n时长分布：{len(uniq)} 种取值")
for i, (clip, inh, outh, off) in enumerate(items):
    if i < 10 or i >= len(items) - 3:
        print(f"  #{i:2d} clip={clip} IN={ticks2str(inh)} OUT={ticks2str(outh)} dur={ticks2str(outh-inh)}")
    elif i == 10:
        print("  ...")
total = sum(durs)
print(f"总时长 {ticks2str(int(total*45000))}")
