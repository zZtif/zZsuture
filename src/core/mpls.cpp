#include "mpls.hpp"

namespace suture {
namespace {

// 大端读取（任何越界返回 false——防御式读取是本模块的立身之本）
bool readU32(const QByteArray& raw, qsizetype off, quint32* out)
{
    if (off < 0 || off + 4 > raw.size())
        return false;
    *out = (quint32(uchar(raw[int(off)])) << 24) |
           (quint32(uchar(raw[int(off) + 1])) << 16) |
           (quint32(uchar(raw[int(off) + 2])) << 8) |
           quint32(uchar(raw[int(off) + 3]));
    return true;
}

bool readU16(const QByteArray& raw, qsizetype off, quint16* out)
{
    if (off < 0 || off + 2 > raw.size())
        return false;
    *out = quint16((quint16(uchar(raw[int(off)])) << 8) |
                   uchar(raw[int(off) + 1]));
    return true;
}

// "正片"判定阈值：60 秒（Logo/菜单通常秒级，正片 20+ 分钟，数量级天然可分）
constexpr Cs kMainItemMinCs = 60 * 100;

}  // namespace

MplsOutcome parseMpls(const QByteArray& raw)
{
    MplsOutcome out;

    // ---- 文件头：魔数 + 段地址表 ----
    if (raw.size() < 0x40) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::MplsUnsupported,
            QStringLiteral("MPLS 文件过小（%1 字节），不是有效的播放列表")
                .arg(raw.size())));
        return out;
    }
    if (!raw.startsWith("MPLS")) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::MplsUnsupported,
            QStringLiteral("文件头魔数不是 MPLS（前四字节 %1 %2 %3 %4）")
                .arg(int(uchar(raw[0])), int(uchar(raw[1])),
                     int(uchar(raw[2])), int(uchar(raw[3])))));
        return out;
    }
    MplsInfo info;
    info.version = QString::fromLatin1(raw.mid(4, 4));
    if (info.version != QStringLiteral("0101") &&
        info.version != QStringLiteral("0200") &&
        info.version != QStringLiteral("0100")) {
        // 未知版本：继续解析（结构兼容性靠长度校验兜底），但告知
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("MPLS 版本 %1 未经验证，按已知结构解析，"
                           "结果请与播放器显示时长核对").arg(info.version)));
    }

    quint32 playlistAddr = 0;
    if (!readU32(raw, 0x08, &playlistAddr) ||
        playlistAddr < 10 || playlistAddr + 10 > raw.size()) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::MplsUnsupported,
            QStringLiteral("PlayList 段地址非法（%1）").arg(playlistAddr)));
        return out;
    }

    // ---- PlayList 段 ----
    const qsizetype pl = playlistAddr;
    quint32 plLen = 0;
    quint16 nItems = 0, nSubPaths = 0;
    if (!readU32(raw, pl, &plLen) ||
        !readU16(raw, pl + 6, &nItems) || !readU16(raw, pl + 8, &nSubPaths) ||
        nItems == 0 || nItems > 200) {   // >200 项必然是解析错位，拒绝
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::MplsUnsupported,
            QStringLiteral("PlayList 段结构异常（PlayItem 数 %1）").arg(nItems)));
        return out;
    }
    // 段长语义：u32 计的是长度字段之后的字节数（段实际范围 = pl+4 .. pl+4+plLen）
    if (pl + 4 + qsizetype(plLen) > raw.size()) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::MplsUnsupported,
            QStringLiteral("PlayList 段长度越界（段长 %1 超出文件）").arg(plLen)));
        return out;
    }

    // ---- 逐 PlayItem：IN/OUT（45kHz），靠 u16 项长跳到下一项 ----
    // 项内布局（实证）：+0 u16 项长，+2 clip[5]，+7 "M2TS"[4]，+11 连接信息[3]，
    // +14 IN[u32]，+18 OUT[u32]，其后 UO 表/STN 表/多角度数据由项长整体覆盖
    const qsizetype sectionEnd = pl + 4 + qsizetype(plLen);
    qsizetype off = pl + 10;
    for (quint16 i = 0; i < nItems; ++i) {
        quint16 itemLen = 0;
        if (off + 22 > raw.size() || !readU16(raw, off, &itemLen) ||
            itemLen < 20 || off + 2 + qsizetype(itemLen) > sectionEnd) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::MplsUnsupported,
                QStringLiteral("第 %1 个 PlayItem 长度非法（%2），停止解析")
                    .arg(i + 1).arg(itemLen)));
            return out;
        }
        PlayItemSpan span;
        span.clip = QString::fromLatin1(raw.mid(off + 2, 5));
        if (!readU32(raw, off + 14, &span.in45k) ||
            !readU32(raw, off + 18, &span.out45k) || span.out45k < span.in45k) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::MplsUnsupported,
                QStringLiteral("第 %1 个 PlayItem 的 IN/OUT 时间非法")
                    .arg(i + 1)));
            return out;
        }
        span.durationCs = ticks45kToCs(span.out45k - span.in45k);
        info.totalCs += span.durationCs;
        info.items.append(span);
        off += 2 + itemLen;   // UO/STN/多角度等附加数据整体跳过
    }

    if (nSubPaths > 0)
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("该播放列表含 %1 条附加轨道（画中画等），已忽略，"
                           "仅按主路径的正片计算")
                .arg(nSubPaths)));

    // ---- T9：同文件内完全相同的连续项 → 疑似假/循环播放列表 ----
    // 实证：义妹 00000.mpls = 50 个完全相同的 2:47 短项冒充"总时长最长"，
    // 按它算偏移会得到 2:47/5:34/… 的垃圾值。只告警不拒绝（结构上合法），
    // 由用户换正确的列表文件。
    int runStart = 0;
    for (int i = 1; i <= int(info.items.size()); ++i) {
        const bool same = i < int(info.items.size())
            && info.items[i].clip == info.items[runStart].clip
            && info.items[i].in45k == info.items[runStart].in45k
            && info.items[i].out45k == info.items[runStart].out45k;
        if (!same) {
            if (i - runStart >= 3)
                out.diags.push_back(Diagnostic::make(
                    DiagLevel::Warning, DiagCode::MplsUnsupported,
                    QStringLiteral("第 %1~%2 个正片片段完全相同（%3）——疑似假播放列表/"
                                   "循环列表，请换用真正的播放列表文件")
                        .arg(runStart + 1).arg(i).arg(info.items[runStart].clip)));
            runStart = i;
        }
    }

    // ---- PlayListMark 段（T8）：章节标记 ----
    // 章节是辅助数据（分集点建议用）：段异常 → 告警降级为"无章节"，
    // PlayItem 解析结果保留——宁可没有章节，不可废掉偏移基础。
    quint32 markAddr = 0;
    readU32(raw, 0x0C, &markAddr);
    if (markAddr != 0) {
        quint32 mkLen = 0;
        quint16 nMarks = 0;
        if (!readU32(raw, markAddr, &mkLen) || !readU16(raw, markAddr + 4, &nMarks) ||
            nMarks > 999 ||
            markAddr + 4 + qsizetype(mkLen) > raw.size() ||
            markAddr + 6 + qsizetype(nMarks) * 14 > raw.size()) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::MplsUnsupported,
                QStringLiteral("章节信息段结构异常，已忽略章节（正片解析不受影响）")));
        } else {
            info.marks.reserve(nMarks);
            for (quint16 i = 0; i < nMarks; ++i) {
                const qsizetype o = markAddr + 6 + qsizetype(i) * 14;
                PlayListMark mk;
                quint16 item = 0;
                readU16(raw, o, &mk.type);
                readU16(raw, o + 2, &item);
                readU32(raw, o + 4, &mk.time45k);
                mk.itemIndex = int(item);
                if (mk.itemIndex >= int(info.items.size())) {
                    out.diags.push_back(Diagnostic::make(
                        DiagLevel::Warning, DiagCode::MplsUnsupported,
                        QStringLiteral("第 %1 个章节指向不存在的正片片段，已忽略该章节")
                            .arg(i + 1).arg(mk.itemIndex + 1)));
                    continue;
                }
                info.marks.append(mk);
            }
        }
    }

    out.info = std::move(info);
    return out;
}

MplsMapping mapItemsToEpisodes(const MplsInfo& info, int episodeCount,
                               int startMain, std::vector<Diagnostic>& diags)
{
    MplsMapping m;
    if (episodeCount <= 0 || startMain < 0)
        return m;

    // 短项（Logo/菜单）与正片分离
    for (int i = 0; i < info.items.size(); ++i)
        if (info.items[i].durationCs >= kMainItemMinCs)
            m.mainItems.append(i);

    if (startMain + episodeCount > m.mainItems.size()) {
        // 超出正片总数 → 不猜。列出前若干项时长供用户对齐（超长列表截断）
        QString durations;
        const int shown = qMin(info.items.size(), 8);
        for (int i = 0; i < shown; ++i) {
            if (i > 0) durations += u'、';
            durations += formatCs(info.items[i].durationCs);
        }
        if (info.items.size() > shown)
            durations += QStringLiteral("…（共 %1 项）").arg(info.items.size());
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::MplsUnsupported,
            QStringLiteral("从第 %1 个正片片段起只有 %2 个正片，少于字幕文件数"
                           "（%3）——请调低\"字幕第一集对应盘内第几集\"、减少"
                           "文件，或手动设定各集开始时间（前 %4 项时长：%5）")
                .arg(startMain + 1)
                .arg(m.mainItems.size() - startMain)
                .arg(episodeCount).arg(shown).arg(durations)));
        m.mainItems.clear();
        return m;
    }

    // 每集起点 = 从链首到该集正片项的累计时长（含 Logo 与被跳过的前序正片）
    m.ok = true;
    m.offsets.reserve(episodeCount);
    m.episodeEnds.reserve(episodeCount);
    Cs running = 0;
    int ep = 0;
    for (int i = 0; i < info.items.size(); ++i) {
        const Cs dur = info.items[i].durationCs;
        if (ep < episodeCount && m.mainItems[startMain + ep] == i) {
            m.offsets.append(running);
            m.episodeEnds.append(running + dur);
            ++ep;
        }
        running += dur;
    }
    if (startMain > 0)
        diags.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("字幕第一集对应第 %1 个正片，开始时间已包含其前的"
                           "全部内容（%2）")
                .arg(startMain + 1).arg(formatCs(m.offsets.value(0)))));
    return m;
}

}  // namespace suture

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
    //        下集预告、对话谁最晚取谁；排除 Comment 与零时长行）
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
