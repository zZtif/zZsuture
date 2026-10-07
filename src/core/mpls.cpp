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
            QStringLiteral("该播放列表含 %1 条 SubPath（画中画/BD-J 等），"
                           "已忽略，仅按主路径 PlayItem 计算偏移")
                .arg(nSubPaths)));

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
                QStringLiteral("PlayListMark 段结构异常，已忽略章节信息"
                               "（PlayItem 偏移不受影响）")));
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
                        QStringLiteral("第 %1 个章节引用了不存在的 PlayItem"
                                       "（%2），已忽略该章节")
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
            QStringLiteral("起始正片 #%1 起只有 %2 个正片 PlayItem，少于字幕"
                           "文件数（%3）——请调低起始集或减少文件，或在偏移列"
                           "手动设定（前 %4 项时长：%5）")
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
            QStringLiteral("字幕第 1 集对应该链的第 %1 个正片，起点偏移已含"
                           "其前全部内容（%2）")
                .arg(startMain + 1).arg(formatCs(m.offsets.value(0)))));
    return m;
}

}  // namespace suture
