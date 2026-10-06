# Analyze: (1) find single-item mpls with duration ~2250470cs; (2) dump chapter marks of 00003.mpls
import struct, sys, glob

def t2s(t):
    s = t / 45000.0
    return "%d:%02d:%04.1f" % (int(s // 3600), int(s % 3600 // 60), s % 60)

def parse(data):
    pl, = struct.unpack('>I', data[8:12])
    mk, = struct.unpack('>I', data[12:16])
    if data[:4] != b'MPLS' or pl + 10 > len(data):
        return None
    n_items, = struct.unpack('>H', data[pl+6:pl+8])
    items = []
    off = pl + 10
    for i in range(n_items):
        if off + 22 > len(data):
            return None
        ilen, = struct.unpack('>H', data[off:off+2])
        inh, = struct.unpack('>I', data[off+14:off+18])
        outh, = struct.unpack('>I', data[off+18:off+22])
        items.append((ilen, inh, outh))
        off += 2 + ilen
    return pl, mk, n_items, items, off

folder = sys.argv[1]

print("=== 1. find single-item files ===")
for f in sorted(glob.glob(folder + '/*.mpls')):
    data = open(f, 'rb').read()
    r = parse(data)
    if not r:
        print(f + ": parse fail")
        continue
    pl, mk, n_items, items, items_end = r
    if n_items == 1:
        dur_cs = (items[0][2] - items[0][1]) * 450 // 100
        print("%s: single item, dur=%dcs (%s)" % (f, dur_cs, t2s(items[0][2] - items[0][1])))

print("")
print("=== 2. 00003.mpls chapter marks ===")
data = open(folder + '/00003.mpls', 'rb').read()
pl, mk, n_items, items, items_end = parse(data)
print("PlayItem area ends at %d, mark section addr = %d, file size = %d" % (items_end, mk, len(data)))
seg = data[mk:mk+240]
print("first 240 bytes of mark section:")
for row in range(0, min(240, len(seg)), 16):
    hexs = ' '.join('%02x' % b for b in seg[row:row+16])
    print("  %5d: %s" % (mk + row, hexs))

msec_len, n_marks = struct.unpack('>II', data[mk:mk+8])
print("")
print("layout u32 len + u32 count: len=%d count=%d" % (msec_len, n_marks))
if 0 < n_marks <= 500:
    off = mk + 8
    print("per-mark dump (first 60), trying 14-byte entries:")
    for i in range(min(n_marks, 60)):
        e = data[off + i*14: off + (i+1)*14]
        if len(e) < 14:
            break
        t, = struct.unpack('>I', e[4:8])
        print("  mark %2d: item=%d type=%d rsv=%s time(+4)=%d (%s) tail=%s" %
              (i, e[0], e[1], e[2:4].hex(), t, t2s(t), e[8:].hex()))
