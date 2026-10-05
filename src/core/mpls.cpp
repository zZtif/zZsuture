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

    // ---- 逐 PlayItem：IN/OUT（45kHz），靠项长字段跳到下一项 ----
    qsizetype off = pl + 10;
    for (quint16 i = 0; i < nItems; ++i) {
        quint32 itemLen = 0;
        if (!readU32(raw, off, &itemLen) || itemLen < 25 ||
            off + 4 + qsizetype(itemLen) > raw.size()) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::MplsUnsupported,
                QStringLiteral("第 %1 个 PlayItem 长度非法（%2），停止解析")
                    .arg(i + 1).arg(itemLen)));
            return out;
        }
        PlayItemSpan span;
        if (!readU32(raw, off + 17, &span.in45k) ||
            !readU32(raw, off + 21, &span.out45k) || span.out45k < span.in45k) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::MplsUnsupported,
                QStringLiteral("第 %1 个 PlayItem 的 IN/OUT 时间非法")
                    .arg(i + 1)));
            return out;
        }
        span.durationCs = ticks45kToCs(span.out45k - span.in45k);
        info.totalCs += span.durationCs;
        info.items.append(span);
        off += 4 + itemLen;   // 多角度等附加数据整体跳过
    }

    if (nSubPaths > 0)
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("该播放列表含 %1 条 SubPath（画中画/BD-J 等），"
                           "已忽略，仅按主路径 PlayItem 计算偏移")
                .arg(nSubPaths)));

    out.info = std::move(info);
    return out;
}

MplsMapping mapItemsToEpisodes(const MplsInfo& info, int episodeCount,
                               std::vector<Diagnostic>& diags)
{
    MplsMapping m;
    if (episodeCount <= 0)
        return m;

    // 短项（Logo/菜单）与正片分离
    QList<int> mainItems;
    for (int i = 0; i < info.items.size(); ++i)
        if (info.items[i].durationCs >= kMainItemMinCs)
            mainItems.append(i);

    if (mainItems.size() != episodeCount) {
        // 数量不等 → 不猜。列出 PlayItem 时长供用户手动对齐（偏移列可编辑）
        QString durations;
        for (int i = 0; i < info.items.size(); ++i) {
            if (i > 0) durations += u'、';
            durations += formatCs(info.items[i].durationCs);
        }
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::MplsUnsupported,
            QStringLiteral("MPLS 有 %1 个正片长度的 PlayItem，与字幕文件数"
                           "（%2）不符，无法自动映射——请在偏移列手动设定"
                           "（各 PlayItem 时长：%3）")
                .arg(mainItems.size()).arg(episodeCount).arg(durations)));
        return m;
    }

    // 顺序对齐：每集起点 = 其前所有 item（含 Logo）的累计时长
    m.ok = true;
    m.offsets.reserve(episodeCount);
    m.episodeEnds.reserve(episodeCount);
    Cs running = 0;
    int ep = 0;
    for (int i = 0; i < info.items.size(); ++i) {
        const Cs dur = info.items[i].durationCs;
        if (mainItems.contains(i)) {
            m.offsets.append(running);
            m.episodeEnds.append(running + dur);
            ++ep;
        }
        running += dur;   // Logo 时长计入下一集起点
    }
    return m;
}

}  // namespace suture
