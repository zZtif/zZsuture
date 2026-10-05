#include "asswrite.hpp"

namespace suture {
namespace {

// Style/Actor/Effect 中的逗号替换为分号——社区标准序列化惯例
// （借鉴 Aegisub ass_dialogue.cpp append_unsafe_str：逗号是字段分隔符，
//  字段值内出现会破坏格式，分号是约定替代字符）
QString sanitizeField(QString s)
{
    s.replace(u',', u';');
    return s;
}

// Margin 字段规范化（P2）：空值输出为 "0"——Aegisub 对该字段做
// lexical_cast<int>，空串会直接抛 "bad lexical cast" 拒开整个文件
// （阶段 2 人工验证发现的实锤 bug）
QString marginField(const QString& m)
{
    return m.isEmpty() ? QStringLiteral("0") : m;
}

}  // namespace

QByteArray serializeAss(const AssDocument& doc)
{
    QString out;
    out.reserve(64 * 1024);

    // ---- [Script Info]：条目原样回写（含 ';' 注释行），保持原始顺序 ----
    out += QStringLiteral("[Script Info]\n");
    for (const auto& e : doc.info.entries) {
        if (e.key.startsWith(u';'))
            out += e.key;                       // 注释行整行原样
        else
            out += e.key + QStringLiteral(": ") + e.value;
        out += u'\n';
    }
    out += u'\n';

    // ---- [V4+ Styles]（SSA v4 原样保留段名，P11）----
    out += doc.ssaV4 ? QStringLiteral("[V4 Styles]\n") : QStringLiteral("[V4+ Styles]\n");
    if (!doc.stylesFormatLine.isEmpty()) {
        out += doc.stylesFormatLine + u'\n';    // 原始 Format 行原样回写
    } else {
        out += QStringLiteral("Format: Name, Fontname, Fontsize, PrimaryColour, "
                              "SecondaryColour, OutlineColour, BackColour, Bold, "
                              "Italic, Underline, StrikeOut, ScaleX, ScaleY, "
                              "Spacing, Angle, BorderStyle, Outline, Shadow, "
                              "Alignment, MarginL, MarginR, MarginV, Encoding\n");
    }
    for (const StyleDef& st : doc.styles) {
        // D3 外科手术式：rawLine 仅替换名字段，其余 22 个字段不经手
        const qsizetype colon = st.rawLine.indexOf(u':');
        const qsizetype comma = st.rawLine.indexOf(u',');
        if (colon >= 0 && comma > colon) {
            out += st.rawLine.left(colon + 1) + u' ' + sanitizeField(st.outputName)
                 + st.rawLine.mid(comma) + u'\n';
        } else if (colon >= 0) {
            // 无逗号：整行只有名字
            out += st.rawLine.left(colon + 1) + u' ' + sanitizeField(st.outputName)
                 + u'\n';
        } else {
            out += st.rawLine + u'\n';   // 极端病行：原样保留
        }
    }
    out += u'\n';

    // ---- [Events] ----
    out += QStringLiteral("[Events]\n");
    out += QStringLiteral("Format: Layer, Start, End, Style, Actor, MarginL, "
                          "MarginR, MarginV, Effect, Text\n");
    for (const EventLine& ev : doc.events) {
        out += ev.isComment ? QStringLiteral("Comment: ") : QStringLiteral("Dialogue: ");
        out += QString::number(ev.layer) + u','
             + formatCs(ev.start) + u','
             + formatCs(ev.end) + u','
             + sanitizeField(ev.style) + u','
             + sanitizeField(ev.actor) + u','
             + marginField(ev.margins[0]) + u','
             + marginField(ev.margins[1]) + u','
             + marginField(ev.margins[2]) + u','
             + sanitizeField(ev.effect) + u','
             + ev.text                          // 一字不动（S5）
             + u'\n';
    }

    // 输出规范：UTF-8 BOM + CRLF（S6）
    QByteArray bytes = out.toUtf8();
    QByteArray result;
    result.reserve(bytes.size() + bytes.count(u'\n') + 3);
    result.append("\xEF\xBB\xBF", 3);
    for (char c : bytes) {
        if (c == '\n')
            result.append("\r\n", 2);
        else
            result.append(c);
    }
    return result;
}

}  // namespace suture
