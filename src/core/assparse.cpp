#include "assparse.hpp"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace suture {
namespace {

// ---- V4+ 标准字段序（Format 行缺失时的兜底；与 Aegisub 行为一致）----
// Aegisub 自身不读 Format 行（src/ass_dialogue.cpp 按固定序硬切），
// 我们更稳：有 Format 行按列名映射（P9），没有则用此默认序 + 告警。
const QStringList kDefaultEventFields = {
    QStringLiteral("layer"), QStringLiteral("start"), QStringLiteral("end"),
    QStringLiteral("style"), QStringLiteral("actor"),
    QStringLiteral("marginl"), QStringLiteral("marginr"), QStringLiteral("marginv"),
    QStringLiteral("effect"), QStringLiteral("text")};

const QStringList kDefaultStyleFields = {
    QStringLiteral("name"), QStringLiteral("fontname"), QStringLiteral("fontsize"),
    QStringLiteral("primarycolour"), QStringLiteral("secondarycolour"),
    QStringLiteral("outlinecolour"), QStringLiteral("backcolour"),
    QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"),
    QStringLiteral("strikeout"), QStringLiteral("scalex"), QStringLiteral("scaley"),
    QStringLiteral("spacing"), QStringLiteral("angle"), QStringLiteral("borderstyle"),
    QStringLiteral("outline"), QStringLiteral("shadow"), QStringLiteral("alignment"),
    QStringLiteral("marginl"), QStringLiteral("marginr"), QStringLiteral("marginv"),
    QStringLiteral("encoding")};

// 段类型（大小写不敏感匹配，同 Aegisub）
enum class Section { None, ScriptInfo, Styles, Events, Garbage, Embedded, Unknown };

// 解析 "Format: A, B, C" → 小写列名表（P9）
QStringList parseFormatLine(const QString& line)
{
    const qsizetype colon = line.indexOf(u':');
    if (colon < 0)
        return {};
    QStringList names = line.mid(colon + 1).split(u',');
    for (QString& n : names)
        n = n.trimmed().toLower();
    names.removeAll(QStringLiteral(""));
    return names;
}

struct Parser {
    AssDocument doc;
    std::vector<Diagnostic> diags;

    Section section = Section::None;
    QStringList eventFields;   // 当前 Events 段的列名表（空 = 默认序）
    QStringList styleFields;   // 当前 Styles 段的列名表（空 = 默认序）
    QSet<QString> styleNames;  // 重名检测

    // 段内计数（离开段时汇总成一条诊断，避免逐行刷屏）
    int garbageLines = 0, embeddedLines = 0, unknownLines = 0;
    QString embeddedKind, unknownKind;
    int zeroDurCount = 0, commentCount = 0, malformedCount = 0;

    void addDiag(DiagLevel l, DiagCode c, const QString& msg, int lineNo = -1)
    { diags.push_back(Diagnostic::make(l, c, msg, -1, lineNo)); }

    // ---- 段切换 ----
    void enterSection(const QString& header, int lineNo)
    {
        leaveSection(lineNo);
        const QString low = header.trimmed().toLower();
        if (low == QStringLiteral("[script info]")) {
            section = Section::ScriptInfo;
        } else if (low == QStringLiteral("[v4 styles]")) {
            // P11：SSA v4 老格式——告警 + 原样无损往返（字段级转换待真实样例出现）
            if (!doc.ssaV4) {
                doc.ssaV4 = true;
                addDiag(DiagLevel::Warning, DiagCode::UnsupportedSsaV4,
                        QStringLiteral("SSA v4.00 老格式（[V4 Styles]），"
                                       "本版本原样保留不转换"), lineNo);
            }
            section = Section::Styles;
        } else if (low == QStringLiteral("[v4+ styles]")) {
            section = Section::Styles;
        } else if (low == QStringLiteral("[events]")) {
            section = Section::Events;
        } else if (low == QStringLiteral("[aegisub project garbage]")) {
            section = Section::Garbage;
            garbageLines = 0;
        } else if (low == QStringLiteral("[fonts]") ||
                   low == QStringLiteral("[graphics]") ||
                   low == QStringLiteral("[aegisub extradata]")) {
            section = Section::Embedded;
            embeddedLines = 0;
            embeddedKind = header.trimmed();
        } else {
            section = Section::Unknown;
            unknownLines = 0;
            unknownKind = header.trimmed();
        }
    }

    void leaveSection(int lineNo)
    {
        switch (section) {
        case Section::Garbage:
            if (garbageLines > 0)
                addDiag(DiagLevel::Info, DiagCode::GarbageDropped,
                        QStringLiteral("已丢弃编辑器状态段 [Aegisub Project Garbage]"
                                       "（%1 行）").arg(garbageLines), lineNo);
            break;
        case Section::Embedded:
            if (embeddedLines > 0)
                addDiag(DiagLevel::Warning, DiagCode::EmbeddedDropped,
                        QStringLiteral("已丢弃内嵌段 %1（%2 行）——内嵌字体/图形/"
                                       "扩展数据本版本不保留，若特效引用可能受影响")
                            .arg(embeddedKind).arg(embeddedLines), lineNo);
            break;
        case Section::Unknown:
            if (unknownLines > 0)
                addDiag(DiagLevel::Info, DiagCode::ParseQuirk,
                        QStringLiteral("已忽略未知段 %1（%2 行）")
                            .arg(unknownKind).arg(unknownLines), lineNo);
            break;
        default:
            break;
        }
        section = Section::None;
    }

    // ---- Script Info 行 ----
    void parseInfoLine(const QString& line, int lineNo)
    {
        // ';' 注释行原样保留（无损往返）
        if (line.startsWith(u';')) {
            doc.info.entries.push_back({line, QString()});
            return;
        }
        const qsizetype colon = line.indexOf(u':');
        if (colon < 0)
            return;   // 无冒号的行按 Aegisub 习惯直接忽略
        const QString key = line.left(colon).trimmed();
        QString value = line.mid(colon + 1);
        while (value.startsWith(u' ') || value.startsWith(u'\t'))
            value.remove(0, 1);   // P2：只去左侧空白（冒号后有无空格都兼容）
        doc.info.entries.push_back({key, value});

        const QString low = key.toLower();
        if (low == QStringLiteral("scripttype")) {
            doc.info.scriptType = value;
            const QString v = value.trimmed().toLower();
            if (v == QStringLiteral("v4.00") && !doc.ssaV4) {
                doc.ssaV4 = true;
                addDiag(DiagLevel::Warning, DiagCode::UnsupportedSsaV4,
                        QStringLiteral("ScriptType: v4.00（SSA 老格式），"
                                       "本版本原样保留不转换"), lineNo);
            }
        } else if (low == QStringLiteral("wrapstyle")) {
            doc.info.wrapStyle = value;
        } else if (low == QStringLiteral("scaledborderandshadow")) {
            doc.info.sbs = value;
        } else if (low == QStringLiteral("ycbcr matrix")) {
            doc.info.ycbcrMatrix = value;
        } else if (low == QStringLiteral("playresx") || low == QStringLiteral("playresy")) {
            const int v = value.trimmed().toInt();
            if (v > 0) {
                QSize cur = doc.info.playRes.value_or(QSize());
                low.endsWith(u'x') ? cur.setWidth(v) : cur.setHeight(v);
                doc.info.playRes = cur;
            }
        } else if (low == QStringLiteral("layoutresx") || low == QStringLiteral("layoutresy")) {
            const int v = value.trimmed().toInt();
            if (v > 0) {
                QSize cur = doc.info.layoutRes.value_or(QSize());
                low.endsWith(u'x') ? cur.setWidth(v) : cur.setHeight(v);
                doc.info.layoutRes = cur;
            }
        } else if (low == QStringLiteral("timer")) {
            doc.info.timer = value;
            bool okNum = false;
            const double t = value.trimmed().toDouble(&okNum);
            if (okNum && qAbs(t - 100.0) > 1e-9)
                addDiag(DiagLevel::Warning, DiagCode::TimerNot100,
                        QStringLiteral("Timer = %1（≠100，时间轴可能被线性拉伸过，"
                                       "偏移结果或需修正）").arg(value.trimmed()),
                        lineNo);
        }
    }

    // ---- Style 行 ----
    void parseStyleLine(const QString& line, int lineNo)
    {
        const qsizetype colon = line.indexOf(u':');
        if (colon < 0)
            return;
        const QStringList fields = line.mid(colon + 1).split(u',');
        const QStringList& fmt = styleFields.isEmpty() ? kDefaultStyleFields
                                                       : styleFields;
        const int nameIdx = fmt.indexOf(QStringLiteral("name"));
        if (nameIdx < 0 || fields.size() <= nameIdx) {
            ++malformedCount;
            addDiag(DiagLevel::Warning, DiagCode::MalformedLine,
                    QStringLiteral("Style 行字段不足，已跳过：%1")
                        .arg(line.left(60)), lineNo);
            return;
        }
        StyleDef st;
        st.originalName = fields.at(nameIdx).trimmed();
        st.outputName   = st.originalName;
        st.rawLine      = line;
        if (styleNames.contains(st.originalName)) {
            addDiag(DiagLevel::Warning, DiagCode::StyleNameDuplicate,
                    QStringLiteral("样式重名 \"%1\"（本文件内重复定义，"
                                   "合并时将附加序号消歧）").arg(st.originalName),
                    lineNo);
        }
        styleNames.insert(st.originalName);
        doc.styles.push_back(std::move(st));
    }

    // ---- Dialogue / Comment 行 ----
    void parseEventLine(const QString& line, int lineNo)
    {
        const bool isComment = line.startsWith(QStringLiteral("Comment:"),
                                               Qt::CaseInsensitive);
        const qsizetype colon = line.indexOf(u':');
        if (colon < 0)
            return;
        QString body = line.mid(colon + 1);
        if (body.startsWith(u' '))
            body.remove(0, 1);   // 剥掉冒号后的一个分隔空格（无空格写法也兼容，P2）

        // 列序：Format 行优先（P9）；缺失/病格式退回默认序 + 告警
        QStringList fmt = eventFields;
        if (fmt.isEmpty() || !fmt.contains(QStringLiteral("text"))) {
            if (!fmt.isEmpty())
                addDiag(DiagLevel::Warning, DiagCode::ParseQuirk,
                        QStringLiteral("Events Format 行缺少 Text 列，"
                                       "按默认列序解析"), lineNo);
            fmt = kDefaultEventFields;
        }
        const int nText = fmt.indexOf(QStringLiteral("text"));
        const bool textIsLast = (nText == fmt.size() - 1);

        // 全部按逗号切开（P9：Text 在最后时其段内逗号原样拼回，文本可含逗号）
        const QStringList parts = body.split(u',');
        const int need = textIsLast ? nText + 1 : int(fmt.size());
        if (parts.size() < need) {
            ++malformedCount;
            addDiag(DiagLevel::Warning, DiagCode::MalformedLine,
                    QStringLiteral("事件行字段不足（%1/%2），已跳过：%3")
                        .arg(parts.size()).arg(need)
                        .arg(line.left(60)), lineNo);
            return;
        }

        EventLine ev;
        ev.isComment = isComment;
        ev.sourceLine = lineNo;
        bool timeOk = true;
        for (int i = 0; i < fmt.size(); ++i) {
            const QString& name = fmt.at(i);
            // Text 在最后 → 该段之后全部（含逗号）归文本；否则只取单段
            const QString val = (i == nText)
                ? (textIsLast ? QStringList(parts.mid(nText)).join(u',')
                              : parts.at(nText))
                : parts.at(i);
            if (i == nText) {
                ev.text = val;   // 文本不 trim——一字不动（S5）
                continue;
            }
            const QString t = val.trimmed();
            if (name == QStringLiteral("layer")) {
                // SSA 老格式首字段是 Marked=0（借鉴 Aegisub ass_dialogue.cpp:104）
                if (t.startsWith(QStringLiteral("marked="), Qt::CaseInsensitive)) {
                    ev.layer = 0;
                } else {
                    bool ok = false;
                    ev.layer = t.toInt(&ok);
                    if (!ok) {
                        ev.layer = 0;
                        addDiag(DiagLevel::Info, DiagCode::ParseQuirk,
                                QStringLiteral("Layer 字段非法（%1），按 0 处理")
                                    .arg(t), lineNo);
                    }
                }
            } else if (name == QStringLiteral("start")) {
                const auto tv = parseTimeToCs(t);
                if (tv) ev.start = *tv; else timeOk = false;
            } else if (name == QStringLiteral("end")) {
                const auto tv = parseTimeToCs(t);
                if (tv) ev.end = *tv; else timeOk = false;
            } else if (name == QStringLiteral("style")) {
                ev.style = t;
            } else if (name == QStringLiteral("actor")) {
                ev.actor = t;
            } else if (name == QStringLiteral("effect")) {
                ev.effect = t;
            } else if (name == QStringLiteral("marginl")) {
                ev.margins[0] = t;
            } else if (name == QStringLiteral("marginr")) {
                ev.margins[1] = t;
            } else if (name == QStringLiteral("marginv")) {
                ev.margins[2] = t;
            } else if (name == QStringLiteral("marked")) {
                ev.layer = 0;   // Format 声明了 Marked 列
            }
        }
        if (!timeOk) {
            ++malformedCount;
            addDiag(DiagLevel::Warning, DiagCode::MalformedLine,
                    QStringLiteral("事件行时间字段非法，已跳过：%1")
                        .arg(line.left(60)), lineNo);
            return;
        }
        if (isComment)
            ++commentCount;
        else if (ev.zeroDuration())
            ++zeroDurCount;
        doc.events.push_back(std::move(ev));
    }

    // ---- 主循环 ----
    void run(const QString& text)
    {
        qsizetype from = 0;
        int lineNo = 0;
        while (from <= text.size()) {
            const qsizetype nl = text.indexOf(u'\n', from);
            const QString line = text.mid(from, (nl < 0 ? text.size() : nl) - from);
            from = (nl < 0) ? text.size() + 1 : nl + 1;
            ++lineNo;

            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (trimmed.startsWith(u'[') && trimmed.endsWith(u']')) {
                enterSection(trimmed, lineNo);
                continue;
            }
            switch (section) {
            case Section::ScriptInfo:
                parseInfoLine(line, lineNo);
                break;
            case Section::Styles:
                if (trimmed.startsWith(QStringLiteral("Format:"),
                                       Qt::CaseInsensitive)) {
                    styleFields = parseFormatLine(trimmed);
                    doc.stylesFormatLine = trimmed;   // 原样保留（v4 往返）
                } else if (trimmed.startsWith(QStringLiteral("Style:"),
                                              Qt::CaseInsensitive)) {
                    parseStyleLine(trimmed, lineNo);
                }
                break;
            case Section::Events:
                if (trimmed.startsWith(QStringLiteral("Format:"),
                                       Qt::CaseInsensitive)) {
                    eventFields = parseFormatLine(trimmed);
                } else if (trimmed.startsWith(QStringLiteral("Dialogue:"),
                                              Qt::CaseInsensitive) ||
                           trimmed.startsWith(QStringLiteral("Comment:"),
                                              Qt::CaseInsensitive)) {
                    parseEventLine(trimmed, lineNo);
                }
                break;
            case Section::Garbage:  ++garbageLines;  break;
            case Section::Embedded:  ++embeddedLines; break;
            case Section::Unknown:   ++unknownLines;  break;
            case Section::None:      break;   // 段外的行忽略（常见空行/前导注释）
            }
        }
        leaveSection(-1);
        finish();
    }

    void finish()
    {
        // T2：全扫取最大 End（只算非 Comment 非零时长 Dialogue；P3/P4 口径）
        for (const EventLine& ev : doc.events) {
            if (!ev.isComment && !ev.zeroDuration())
                doc.maxDialogueEnd = std::max(doc.maxDialogueEnd, ev.end);
        }
        // T2：稳定排序索引（校验/可视化用，不用于输出）
        doc.sortedByStart.reserve(int(doc.events.size()));
        for (int i = 0; i < int(doc.events.size()); ++i)
            doc.sortedByStart.append(i);
        std::stable_sort(doc.sortedByStart.begin(), doc.sortedByStart.end(),
                         [this](int a, int b) {
                             return doc.events[size_t(a)].start <
                                    doc.events[size_t(b)].start;
                         });
        // 汇总诊断（避免逐行刷屏）
        if (zeroDurCount > 0)
            addDiag(DiagLevel::Info, DiagCode::ZeroDurationIgnored,
                    QStringLiteral("%1 行零时长行：保留平移但不参与任何"
                                   "统计/校验").arg(zeroDurCount));
        if (commentCount > 0)
            addDiag(DiagLevel::Info, DiagCode::CommentIgnoredStat,
                    QStringLiteral("%1 行 Comment 行：保留平移但统计只算 "
                                   "Dialogue").arg(commentCount));
        if (malformedCount > 0)
            addDiag(DiagLevel::Warning, DiagCode::MalformedLine,
                    QStringLiteral("共 %1 行无法解析被跳过").arg(malformedCount));
    }
};

}  // namespace

ParseOutcome parseAss(const QString& text)
{
    Parser p;
    if (text.trimmed().isEmpty()) {
        p.addDiag(DiagLevel::Error, DiagCode::FileUnreadable,
                  QStringLiteral("文本为空，无法解析"));
        return ParseOutcome{std::move(p.doc), std::move(p.diags), false};
    }
    p.run(text);
    ParseOutcome out{std::move(p.doc), std::move(p.diags), true};
    // 完全没有段结构 → 文件级不可解析
    if (out.doc.events.empty() && out.doc.styles.empty() &&
        out.doc.info.entries.empty()) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::UnknownFormat,
            QStringLiteral("未找到任何 ASS/SSA 段结构（[Script Info] 等）")));
        out.ok = false;
    }
    return out;
}

// ScriptInfo::value —— 大小写不敏感查找
std::optional<QString> ScriptInfo::value(const QString& key) const
{
    for (const auto& e : entries)
        if (e.key.compare(key, Qt::CaseInsensitive) == 0)
            return e.value;
    return std::nullopt;
}

}  // namespace suture
