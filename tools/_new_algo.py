# -*- coding: utf-8 -*-
# 一次性补丁：mpls.cpp 尾部整体替换为链式分界点新算法；用完即删
p = 'src/core/mpls.cpp'
s = open(p, encoding='utf-8').read()

CLOSE = '}  // namespace suture'
first_close = s.find(CLOSE)
assert first_close > 0, 'close marker not found'
head = s[:first_close + len(CLOSE)]   # 含 mapItemsToEpisodes 的完整第一个命名空间

tail = '''
// ---- 链式分界点（所有者设计并实测验证，整改文档 一.2）----
// 旧机制（等分位 / 90 秒签名加分 / DP / ED 锚定）已全部删除：四者皆从
// 盘单侧猜分集点，DISC2 实测翻车（OP 起点章节抢走第 1 集位置，偏 69 秒）。
namespace suture {

ChapterMapping suggestChapterStarts(const MplsInfo& info, int episodeCount,
                                   const QList<Cs>& subLastEnds,
                                   std::vector<Diagnostic>& diags)
{
    // 链式分界点：字幕自身的时间链是主估计，章节只做就近确认
    //   ① 第 1 集分界点 = 文件头（多集 m2ts 第一集在 0:00，三盘实证）
    //   ② 第 k+1 集预估 = 第 k 集分界点 + 第 k 集字幕最晚结束
    //      （最晚结束 = 结束最晚的一行有效台词，不区分样式——ED 歌词、
    //        下集预告、对话谁最晚取谁）
    //   ③ 就近吸附章节节点（±60 秒内），吸不到保持预估值 + 告警
    ChapterMapping m;
    if (episodeCount <= 0)
        return m;

    // 前提：恰一个正片 PlayItem（多集装在一个 m2ts；多正片项走 PlayItem 映射）
    int mainIdx = -1, mainCount = 0;
    for (int i = 0; i < info.items.size(); ++i) {
        if (info.items[i].durationCs >= kMainItemMinCs) {
            ++mainCount;
            mainIdx = i;
        }
    }
    if (mainCount != 1)
        return m;
    const PlayItemSpan& item = info.items[mainIdx];

    // 章节表：该正片项的 Entry Mark 相对时刻（排序去重）
    QList<Cs> rels;
    for (const PlayListMark& mk : info.marks) {
        if (mk.itemIndex != mainIdx || mk.type != 1 || mk.time45k < item.in45k)
            continue;
        const Cs rel = ticks45kToCs(mk.time45k - item.in45k);
        if (!rels.contains(rel))
            rels.append(rel);
    }
    std::sort(rels.begin(), rels.end());

    const int N = episodeCount;
    m.ok = true;
    m.clip = item.clip;
    m.epBasis = QStringList();
    for (int k = 0; k < N; ++k)
        m.epBasis.append(QString());

    // ① 第 1 集 = 文件头
    m.offsets.append(0);
    m.epBasis[0] = QStringLiteral("多集文件的第 1 集在文件头（三盘实证）");

    // ②③ 链式累加 + 就近吸附
    constexpr Cs kAttachTolCs = 60 * 100;   // 60 秒：超时不吸，告警人工核对
    for (int k = 1; k < N; ++k) {
        const Cs lastEnd = (k - 1) < subLastEnds.size() ? subLastEnds[k - 1] : Cs(0);
        const Cs predicted = m.offsets[k - 1] + lastEnd;
        Cs best = -1;
        for (Cs r : rels)
            if (best < 0 || qAbs(r - predicted) < qAbs(best - predicted))
                best = r;
        if (!rels.isEmpty() && best >= 0 && qAbs(best - predicted) <= kAttachTolCs) {
            m.offsets.append(best);
            m.epBasis[k] = QStringLiteral("链式预估 %1 → 吸附章节 %2")
                               .arg(formatCs(predicted))
                               .arg(formatCs(best));
        } else {
            m.offsets.append(predicted);
            m.epBasis[k] = QStringLiteral("链式预估 %1（60 秒内无章节佐证，"
                                           "请人工核对）")
                               .arg(formatCs(predicted));
            diags.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::MplsUnsupported,
                QStringLiteral("第 %1 集链式预估 %2 附近 60 秒内没有章节——"
                               "保持预估值，请人工核对（该集字幕可能不含 "
                               "ED 歌词/预告，或与盘不同源）")
                    .arg(k + 1).arg(formatCs(predicted)), k));
        }
    }

    // 集界 = 下一集分界点（末集 = 项时长），供 C1 交叉验证
    m.episodeEnds.reserve(N);
    for (int k = 0; k < N; ++k)
        m.episodeEnds.append(k + 1 < N ? m.offsets[k + 1] : item.durationCs);
    return m;
}

}  // namespace suture
'''
open(p, 'w', encoding='utf-8', newline='\n').write(head + tail)
print('tail replaced, head kept', first_close + len(CLOSE), 'bytes')
