# -*- coding: utf-8 -*-
# 一次性分析脚本：解剖真实 MPLS 文件的 PlayItem 布局（用完即删）
import struct, sys

path = sys.argv[1]
data = open(path, 'rb').read()
print(f"文件大小 {len(data)}，魔数 {data[:8]}")

# 段地址
pl_addr, = struct.unpack('>I', data[8:12])
mark_addr, = struct.unpack('>I', data[12:16])
ext_addr, = struct.unpack('>I', data[16:20])
print(f"0x08={pl_addr}(0x{pl_addr:x}) 0x0C={mark_addr}(0x{mark_addr:x}) 0x10={ext_addr}")

# 所有 M2TS（clip codec）位置
clips = []
i = data.find(b'M2TS')
while i >= 0:
    clips.append(i - 5)   # clip 名起点
    i = data.find(b'M2TS', i + 1)
print(f"\n共 {len(clips)} 个 clip：", [f"@{c}" for c in clips[:6]], "...")

# 相邻 clip 间距
deltas = [clips[i+1] - clips[i] for i in range(len(clips)-1)]
print("clip 间距（前 10）:", deltas[:10])

def ticks2str(t):
    s = t / 45000.0
    return f"{int(s//3600)}:{int(s%3600//60):02d}:{s%60:06.3f}"

# 每个 clip 的 clip 相对字段（-12..+25）与候选 IN/OUT
print("\nclip#  pos   clip_rel[-12..+22]                          IN(+12)         OUT(+16)      dur")
prev_out = None
for n, c in enumerate(clips[:8]):
    ctx = data[c-12:c+22]
    inh, = struct.unpack('>I', data[c+12:c+16])
    outh, = struct.unpack('>I', data[c+16:c+20])
    dur = (outh - inh) if outh >= inh else -1
    print(f"{n:3d} @{c:5d} {ctx.hex(' ')}")
    print(f"      IN(+12)={inh}={ticks2str(inh)}  OUT(+16)={outh}={ticks2str(outh)}  dur={ticks2str(dur) if dur>=0 else '??'}")

# 在 clip 前后找"间距值"的 u32/u16 出现（定位长度字段）
print("\n--- 长度字段探测（以第 1→2 间距 %d 为目标）---" % deltas[0])
target = deltas[0]
for off in range(clips[0]-16, clips[0]+8):
    if 0 <= off and off+4 <= len(data):
        v, = struct.unpack('>I', data[off:off+4])
        if v == target or v == target-4 or v == target-2:
            print(f"u32@{off} (clip{clips[0]}{off-clips[0]:+d}) = {v}")
    if 0 <= off and off+2 <= len(data):
        v, = struct.unpack('>H', data[off:off+2])
        if v == target or v == target-4 or v == target-2:
            print(f"u16@{off} (clip{clips[0]}{off-clips[0]:+d}) = {v}")
