// tst_ass.cpp —— 解析容错与序列化（P2/P3/P4/P5/P9/P10/P11/T2/T5 + S6 输出）
#include <QtTest>

#include "assparse.hpp"
#include "asswrite.hpp"

using namespace suture;

namespace {
// 造一个最小合法文档骨架
QString wrapEvents(const QString& eventsBody)
{
    return QStringLiteral(
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 384\nPlayResY: 288\n\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
        "SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, "
        "StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
        "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
        "0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, "
        "MarginV, Effect, Text\n") + eventsBody;
}

bool hasCode(const std::vector<Diagnostic>& diags, DiagCode c)
{
    for (const auto& d : diags)
        if (d.code == c)
            return true;
    return false;
}
}  // namespace

class TestAss : public QObject {
    Q_OBJECT
private slots:
    void basicDialogue();       // 基本解析 + 派生数据
    void textKeepsCommas();     // S5：文本中逗号原样
    void disorderMaxEnd();      // T2：乱序取最大 End
    void zeroDuration();        // P3
    void commentStat();         // P4
    void markedSsa();           // P11：Marked= 首字段
    void formatReorder();       // P9：Format 列乱序
    void formatMissing();       // P9：Format 缺失按默认序
    void timerNot100();         // T5
    void garbageSection();      // P5
    void embeddedSections();    // P10：Fonts/Graphics/Extradata
    void malformedLines();      // 脏行不崩溃
    void quirks();              // P2：冒号后无空格 / Margin 0000
    void roundTripSemantic();   // S6：序列化再解析语义等价
    void styleRename();         // D3：输出时替换名字段
    void ssaV4Detection();      // P11：v4 检测 + 原样无损往返
};

void TestAss::basicDialogue()
{
    const ParseOutcome p = parseAss(wrapEvents(
        "Dialogue: 0,0:00:01.00,0:00:02.50,Default,,0,0,0,,你好"));
    QVERIFY(p.ok);
    QCOMPARE(int(p.doc.events.size()), 1);
    const EventLine& ev = p.doc.events[0];
    QCOMPARE(ev.start, Cs(100));
    QCOMPARE(ev.end, Cs(250));
    QCOMPARE(ev.style, QStringLiteral("Default"));
    QCOMPARE(ev.text, QStringLiteral("你好"));
    QCOMPARE(ev.layer, 0);
    QCOMPARE(p.doc.maxDialogueEnd, Cs(250));
    QCOMPARE(int(p.doc.styles.size()), 1);
    QCOMPARE(p.doc.styles[0].originalName, QStringLiteral("Default"));
    QVERIFY(p.doc.info.playRes.has_value());
    QCOMPARE(*p.doc.info.playRes, QSize(384, 288));
}

void TestAss::textKeepsCommas()
{
    const ParseOutcome p = parseAss(wrapEvents(
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a, b, {\\pos(1,2)} c"));
    QCOMPARE(p.doc.events[0].text,
             QStringLiteral("a, b, {\\pos(1,2)} c"));   // 前段字段后的全部归文本
}

void TestAss::disorderMaxEnd()
{
    // G4 实证形态：首行最晚（下集预告）、末行早结束——最大 End 必须全扫
    const ParseOutcome p = parseAss(wrapEvents(
        "Dialogue: 0,0:22:26.00,0:22:29.00,Default,,0,0,0,,预告\n"
        "Dialogue: 0,0:00:07.00,0:00:09.00,Default,,0,0,0,,正片\n"
        "Dialogue: 0,0:14:00.00,0:14:15.00,Default,,0,0,0,,ED"));
    QCOMPARE(p.doc.maxDialogueEnd, Cs(22 * 6000 + 29 * 100));
    // 排序索引稳定且按 start 升序
    QCOMPARE(p.doc.sortedByStart.size(), 3);
    QCOMPARE(p.doc.events[size_t(p.doc.sortedByStart[0])].start, Cs(700));
    QCOMPARE(p.doc.events[size_t(p.doc.sortedByStart[2])].start,
             Cs(22 * 6000 + 26 * 100));
}

void TestAss::zeroDuration()
{
    const ParseOutcome p = parseAss(wrapEvents(
        "Dialogue: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x"));
    QCOMPARE(int(p.doc.events.size()), 2);          // 保留
    QCOMPARE(p.doc.maxDialogueEnd, Cs(200));        // 但不进统计
    QVERIFY(hasCode(p.diags, DiagCode::ZeroDurationIgnored));
}

void TestAss::commentStat()
{
    const ParseOutcome p = parseAss(wrapEvents(
        "Comment: 0,0:00:00.00,0:00:00.00,Default,Staff,0,0,0,,元数据\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x"));
    QCOMPARE(int(p.doc.events.size()), 2);
    QVERIFY(p.doc.events[0].isComment);
    QCOMPARE(p.doc.maxDialogueEnd, Cs(200));        // Comment 不进统计
    QVERIFY(hasCode(p.diags, DiagCode::CommentIgnoredStat));
}

void TestAss::markedSsa()
{
    // SSA 老格式：Layer 位置写 Marked=0（借鉴 Aegisub）
    const ParseOutcome p = parseAss(wrapEvents(
        "Dialogue: Marked=0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x"));
    QVERIFY(p.ok);
    QCOMPARE(p.doc.events[0].layer, 0);
}

void TestAss::formatReorder()
{
    // P9：非标文件 Format 列乱序（Text 在最前）
    const QString text = QStringLiteral(
        "[Script Info]\n\n[Events]\n"
        "Format: Text, Start, End, Layer, Style, Actor, MarginL, MarginR, "
        "MarginV, Effect\n"
        "Dialogue: hello, 0:00:03.00, 0:00:04.00, 5, Default, act, 1, 2, 3, fx");
    const ParseOutcome p = parseAss(text);
    QVERIFY(p.ok);
    const EventLine& ev = p.doc.events[0];
    QCOMPARE(ev.text, QStringLiteral("hello"));
    QCOMPARE(ev.start, Cs(300));
    QCOMPARE(ev.end, Cs(400));
    QCOMPARE(ev.layer, 5);
    QCOMPARE(ev.style, QStringLiteral("Default"));
    QCOMPARE(ev.actor, QStringLiteral("act"));
    QCOMPARE(ev.effect, QStringLiteral("fx"));
    QCOMPARE(ev.margins[0], QStringLiteral("1"));
}

void TestAss::formatMissing()
{
    // P9：Format 行缺失 → 默认 V4+ 序 + 告警
    const QString text = QStringLiteral(
        "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x");
    const ParseOutcome p = parseAss(text);
    QVERIFY(p.ok);
    QCOMPARE(p.doc.events[0].text, QStringLiteral("x"));
}

void TestAss::timerNot100()
{
    const QString text = QStringLiteral(
        "[Script Info]\nScriptType: v4.00+\nTimer: 100.0500\n\n[Events]\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x");
    const ParseOutcome p = parseAss(text);
    QVERIFY(hasCode(p.diags, DiagCode::TimerNot100));
    QCOMPARE(p.doc.info.timer, QStringLiteral("100.0500"));
}

void TestAss::garbageSection()
{
    const QString text = wrapEvents("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x")
        + QStringLiteral("\n[Aegisub Project Garbage]\nAudio File: C:\\x\\a.wav\nVideo File: C:\\x\\v.mkv\n");
    const ParseOutcome p = parseAss(text);
    QVERIFY(hasCode(p.diags, DiagCode::GarbageDropped));
    // 垃圾段内容不得进入文档
    bool leaked = false;
    for (const auto& e : p.doc.info.entries)
        if (e.key.contains(QStringLiteral("Audio File"), Qt::CaseInsensitive))
            leaked = true;
    QVERIFY(!leaked);
}

void TestAss::embeddedSections()
{
    const QString text = wrapEvents("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x")
        + QStringLiteral("\n[Fonts]\nfontname: test.ttf\n")
        + QStringLiteral("AAAA[graphics]\n");   // 干扰行（应按数据行丢弃）
    const QString text2 = wrapEvents("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x")
        + QStringLiteral("\n[Aegisub Extradata]\nData: 1,foo,ebar\n");
    const ParseOutcome p = parseAss(text);
    QVERIFY(hasCode(p.diags, DiagCode::EmbeddedDropped));
    const ParseOutcome p2 = parseAss(text2);
    QVERIFY(hasCode(p2.diags, DiagCode::EmbeddedDropped));
}

void TestAss::malformedLines()
{
    const ParseOutcome p = parseAss(wrapEvents(
        "Dialogue: 0,0:00:01.00\n"                       // 字段不足
        "Dialogue: 0,xx,0:00:02.00,Default,,0,0,0,,bad\n" // 时间非法
        "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,ok"));
    QVERIFY(p.ok);                                       // 文件级仍成功
    QCOMPARE(int(p.doc.events.size()), 1);               // 只有 ok 行进入
    QVERIFY(hasCode(p.diags, DiagCode::MalformedLine));
}

void TestAss::quirks()
{
    // P2：冒号后无空格（组2 集数4 实证）+ Margin "0000" 4 位写法
    const QString text = QStringLiteral(
        "[Script Info]\nTitle:xxx\n\n[Events]\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0000,0000,0000,,x");
    const ParseOutcome p = parseAss(text);
    QVERIFY(p.ok);
    QCOMPARE(*p.doc.info.value(QStringLiteral("Title")), QStringLiteral("xxx"));
    QCOMPARE(p.doc.events[0].margins[0], QStringLiteral("0000"));  // 原样

    // 空 Margin 规范化为 "0"（Aegisub lexical_cast<int>("") 会拒开整文件——
    // 阶段 2 人工验证实锤的 bug，修复回归用例）
    const QString text2 = QStringLiteral(
        "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,,,,,x");
    const ParseOutcome p2 = parseAss(text2);
    QVERIFY(p2.ok);
    const QByteArray out = serializeAss(p2.doc);
    const QString line = QString::fromUtf8(out);
    // 空 actor/margins/effect → actor 仍为空（字符串字段合法），margins 补 "0"
    QVERIFY(line.contains(QStringLiteral(",Default,,0,0,0,,x")));
}

void TestAss::roundTripSemantic()
{
    const QString original = wrapEvents(
        "Dialogue: 0,0:00:01.00,0:00:02.50,Default,actor,10,20,30,fx,{\\pos(1,2)}文本,含逗号"
        "\nComment: 0,0:00:00.00,0:00:00.00,Meta,staff,0,0,0,,注释行");
    const ParseOutcome p1 = parseAss(original);
    QVERIFY(p1.ok);
    const QByteArray bytes = serializeAss(p1.doc);
    // 输出规范：UTF-8 BOM + CRLF
    QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
    QVERIFY(bytes.contains("\r\n"));

    const ParseOutcome p2 = parseAss(QString::fromUtf8(bytes.mid(3)));
    QVERIFY(p2.ok);
    // 语义等价
    QCOMPARE(int(p2.doc.events.size()), int(p1.doc.events.size()));
    for (size_t i = 0; i < p1.doc.events.size(); ++i) {
        QCOMPARE(p2.doc.events[i].start, p1.doc.events[i].start);
        QCOMPARE(p2.doc.events[i].end, p1.doc.events[i].end);
        QCOMPARE(p2.doc.events[i].layer, p1.doc.events[i].layer);
        QCOMPARE(p2.doc.events[i].style, p1.doc.events[i].style);
        QCOMPARE(p2.doc.events[i].text, p1.doc.events[i].text);   // 一字不动
        QCOMPARE(p2.doc.events[i].actor, p1.doc.events[i].actor);
        QCOMPARE(p2.doc.events[i].effect, p1.doc.events[i].effect);
    }
    QCOMPARE(int(p2.doc.styles.size()), int(p1.doc.styles.size()));
    QCOMPARE(p2.doc.styles[0].originalName, p1.doc.styles[0].originalName);
}

void TestAss::styleRename()
{
    // D3：输出时仅替换名字段，其余 22 个字段不经手
    const ParseOutcome p = parseAss(wrapEvents(""));
    QVERIFY(p.ok);
    AssDocument doc = p.doc;
    doc.styles[0].outputName = QStringLiteral("Default@3");
    const QByteArray out = serializeAss(doc);
    const QString line = QString::fromUtf8(out);
    QVERIFY(line.contains(QStringLiteral("Style: Default@3,Arial,20,")));
    // 其余字段与原文一致（抽查 Fontname/Fontsize）
    QVERIFY(line.contains(QStringLiteral(",Arial,20,")));
}

void TestAss::ssaV4Detection()
{
    // P11：ScriptType v4.00 + [V4 Styles] → 检测告警 + 往返保持段名不转换
    const QString text = QStringLiteral(
        "[Script Info]\nScriptType: v4.00\n\n"
        "[V4 Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
        "SecondaryColour, TertiaryColour, BackColour, Bold, Italic, "
        "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, "
        "AlphaLevel, Encoding\n"
        "Style: Default,Arial,20,65535,255,0,0,0,0,1,2,2,2,10,10,10,0,1\n\n"
        "[Events]\n"
        "Dialogue: Marked=0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x");
    const ParseOutcome p = parseAss(text);
    QVERIFY(p.ok);
    QVERIFY(p.doc.ssaV4);
    QVERIFY(hasCode(p.diags, DiagCode::UnsupportedSsaV4));
    // 往返：段名保持 [V4 Styles]、原始 Format 行保留、Marked 行可解析
    const QByteArray out = serializeAss(p.doc);
    const QString s = QString::fromUtf8(out);
    QVERIFY(s.contains(QStringLiteral("[V4 Styles]")));
    QVERIFY(s.contains(QStringLiteral("TertiaryColour")));   // v4 原始 Format 行
    QVERIFY(s.contains(QStringLiteral("AlphaLevel")));
    const ParseOutcome p2 = parseAss(QString::fromUtf8(out.mid(3)));
    QVERIFY(p2.ok);
    QCOMPARE(int(p2.doc.events.size()), 1);
    QCOMPARE(p2.doc.events[0].text, QStringLiteral("x"));
}

QTEST_MAIN(TestAss)
#include "tst_ass.moc"
