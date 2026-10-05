// tst_merge.cpp —— 合并引擎（S1/S2 隔离、S3 未定义引用、S4 决议、S5 只动时间、
// S7 \r 重写、T3 预填、T6 微调、D7 声明式重算）
#include <QDir>
#include <QFile>
#include <QtTest>

#include "assparse.hpp"
#include "asswrite.hpp"
#include "load.hpp"
#include "merge.hpp"

using namespace suture;

namespace {
// 造一个最小文档：两个样式（同名跨文件用于隔离测试）+ 若干事件行
std::shared_ptr<const AssDocument> makeDoc(const QString& events,
                                           const QString& styleExtra = QString(),
                                           const QString& info = QString())
{
    const QString text = QStringLiteral(
        "[Script Info]\nScriptType: v4.00+\n") + info +
        QStringLiteral("\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize\n"
                       "Style: ED_jap,Arial,20\n") + styleExtra +
        QStringLiteral("\n[Events]\n"
                       "Format: Layer, Start, End, Style, Actor, MarginL, "
                       "MarginR, MarginV, Effect, Text\n") + events;
    const ParseOutcome p = parseAss(text);
    Q_ASSERT(p.ok);
    return std::make_shared<const AssDocument>(std::move(p.doc));
}

EpisodeInput makeEp(std::shared_ptr<const AssDocument> doc, Cs offset,
                    quint64 fileId)
{
    EpisodeInput ep;
    ep.doc = std::move(doc);
    ep.offset = offset;
    ep.fileId = fileId;
    return ep;
}

bool hasCode(const std::vector<Diagnostic>& diags, DiagCode c)
{
    for (const auto& d : diags)
        if (d.code == c)
            return true;
    return false;
}
}  // namespace

class TestMerge : public QObject {
    Q_OBJECT
private slots:
    void styleIsolation();      // S1/S2
    void sameFileDupStyle();    // 同文件重名 → #2 消歧
    void undefinedRef();        // S3
    void rTagRewrite();         // S7
    void scriptInfoResolve();   // S4
    void timeShift();           // S5/T4/T6：偏移+全局+逐行微调
    void prefill();             // T3
    void markersAndStats();     // 每集标记 + 零时长/Comment 口径
    void outputReparses();      // 合并输出可再解析（端到端）
    void g1EndToEnd();          // 阶段 2 验收：真实 G1 四集全链路（预填偏移）
};

void TestMerge::styleIsolation()
{
    // 两个文件同名样式 ED_jap（S1 实证：集间定义不同）→ 隔离为 @1/@2
    auto d1 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,一"));
    auto d2 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,二"));
    const MergeResult r = merge({makeEp(d1, 0, 1), makeEp(d2, 60000, 2)}, {});
    QCOMPARE(int(r.merged.styles.size()), 2);
    QCOMPARE(r.merged.styles[0].outputName, QStringLiteral("ED_jap@1"));
    QCOMPARE(r.merged.styles[1].outputName, QStringLiteral("ED_jap@2"));
    // 事件引用同步重写
    QCOMPARE(r.merged.events[1].style, QStringLiteral("ED_jap@1"));
    QCOMPARE(r.merged.events[3].style, QStringLiteral("ED_jap@2"));   // [0]=EP1 标记
    QVERIFY(hasCode(r.diags, DiagCode::StyleIsolated));
}

void TestMerge::sameFileDupStyle()
{
    const QString extra = QStringLiteral("Style: ED_jap,SimSun,22");
    auto d1 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,x"),
                      extra);
    const MergeResult r = merge({makeEp(d1, 0, 1)}, {});
    QCOMPARE(int(r.merged.styles.size()), 2);
    QCOMPARE(r.merged.styles[0].outputName, QStringLiteral("ED_jap@1"));
    QCOMPARE(r.merged.styles[1].outputName, QStringLiteral("ED_jap@1#2"));
}

void TestMerge::undefinedRef()
{
    // G6 实证：引用 *Default 但样式表无定义 → 原样保留 + 告警
    auto d1 = makeDoc(QStringLiteral(
        "Dialogue: 0,0:00:01.00,0:00:02.00,*Default,,0,0,0,,a\n"
        "Dialogue: 0,0:00:03.00,0:00:04.00,*Default,,0,0,0,,b"));
    const MergeResult r = merge({makeEp(d1, 0, 1)}, {});
    QCOMPARE(r.merged.events[1].style, QStringLiteral("*Default"));
    QCOMPARE(r.merged.events[2].style, QStringLiteral("*Default"));
    QVERIFY(hasCode(r.diags, DiagCode::UndefinedStyleRef));
}

void TestMerge::rTagRewrite()
{
    // S7：\rED_jap 指向已定义样式 → 重写；裸 \r 与未定义名不动
    auto d1 = makeDoc(QStringLiteral(
        "Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,"
        "{\\k20}卡{\\rED_jap}拉OK{\\r}裸重置{\\rUnknown}未定义"));
    const MergeResult r = merge({makeEp(d1, 0, 1)}, {});
    const QString& text = r.merged.events[1].text;
    QVERIFY(text.contains(QStringLiteral("{\\rED_jap@1}")));
    QVERIFY(text.contains(QStringLiteral("{\\r}")));              // 裸重置不动
    QVERIFY(text.contains(QStringLiteral("{\\rUnknown}")));       // 未定义不动
    QVERIFY(text.contains(QStringLiteral("{\\k20}")));            // 其余标签不动
    QVERIFY(hasCode(r.diags, DiagCode::RTagRewritten));
}

void TestMerge::scriptInfoResolve()
{
    // S4：PlayRes/SBS/WrapStyle 冲突 → 告警 + 取第一个文件值；Title 差异入注释
    auto d1 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,x"),
                      QString(),
                      QStringLiteral("Title: EP1\nPlayResX: 384\nPlayResY: 288\n"
                                     "ScaledBorderAndShadow: no"));
    auto d2 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,y"),
                      QString(),
                      QStringLiteral("Title: EP2\nPlayResX: 1280\nPlayResY: 720\n"
                                     "ScaledBorderAndShadow: yes\nWrapStyle: 2"));
    const MergeResult r = merge({makeEp(d1, 0, 1), makeEp(d2, 60000, 2)}, {});
    QVERIFY(hasCode(r.diags, DiagCode::ScriptInfoConflict));
    // 取第一个文件的值
    QCOMPARE(*r.merged.info.value(QStringLiteral("PlayResX")), QStringLiteral("384"));
    QCOMPARE(*r.merged.info.value(QStringLiteral("ScaledBorderAndShadow")),
             QStringLiteral("no"));
    // 第二个文件的差异元信息 → 注释保留
    bool hasEp2Title = false;
    for (const auto& e : r.merged.info.entries)
        if (e.key.startsWith(u';') && e.key.contains(QStringLiteral("Title: EP2")))
            hasEp2Title = true;
    QVERIFY(hasEp2Title);
    // 第一个文件的同名同值条目不重复
    int titleCount = 0;
    for (const auto& e : r.merged.info.entries)
        if (!e.key.startsWith(u';') && e.key.compare(QStringLiteral("Title"),
                                                     Qt::CaseInsensitive) == 0)
            ++titleCount;
    QCOMPARE(titleCount, 1);
}

void TestMerge::timeShift()
{
    // S5：只动 Start/End；偏移 + 全局 + 逐行微调三重叠加
    auto d1 = makeDoc(QStringLiteral(
        "Dialogue: 0,0:00:01.00,0:00:02.00,ED_jap,,0,0,0,,{\\pos(1,2)}特效文本"));
    MergeOptions opt;
    opt.globalShift = 100;   // +1 秒
    LineAdjustment adj;
    adj.fileId = 7;
    adj.eventIndex = 0;
    adj.delta = 50;          // +0.5 秒
    opt.adjustments.append(adj);
    const MergeResult r = merge({makeEp(d1, 60000, 7)}, opt);   // +10 分钟
    const EventLine& ev = r.merged.events[1];
    QCOMPARE(ev.start, Cs(60000 + 100 + 50 + 100));   // 10:01:01.50
    QCOMPARE(ev.end,   Cs(60000 + 100 + 50 + 200));
    // 文本一字不动（S5）
    QCOMPARE(ev.text, QStringLiteral("{\\pos(1,2)}特效文本"));
}

void TestMerge::prefill()
{
    // T3：第 N 集 = 前面各集（偏移+maxEnd）累加；maxEnd 不含 Comment/零时长
    auto d1 = makeDoc(QStringLiteral(
        "Dialogue: 0,0:00:01.00,0:23:00.00,ED_jap,,0,0,0,,正片\n"
        "Comment: 0,0:00:00.00,0:00:00.00,Meta,,0,0,0,,不该计入\n"
        "Dialogue: 0,0:00:00.00,0:00:00.00,ED_jap,,0,0,0,,零时长也不计入"));
    auto d2 = makeDoc(QStringLiteral(
        "Dialogue: 0,0:00:05.00,0:24:00.00,ED_jap,,0,0,0,,二"));
    auto d3 = makeDoc(QStringLiteral(
        "Dialogue: 0,0:00:00.00,0:22:00.00,ED_jap,,0,0,0,,三"));
    const QList<Cs> off = computePrefillOffsets({d1, d2, d3});
    QCOMPARE(off.size(), 3);
    QCOMPARE(off[0], Cs(0));
    QCOMPARE(off[1], Cs(23 * 6000));            // 23:00.00
    QCOMPARE(off[2], Cs(23 * 6000 + 24 * 6000));
}

void TestMerge::markersAndStats()
{
    auto d1 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:23:00.00,ED_jap,,0,0,0,,x"));
    auto d2 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:24:00.00,ED_jap,,0,0,0,,y"));
    const MergeResult r = merge({makeEp(d1, 0, 1), makeEp(d2, 23 * 6000, 2)}, {});
    // 每集一个标记（Comment 事件，时间=该集起点）
    QVERIFY(r.merged.events[0].isComment);
    QVERIFY(r.merged.events[0].text.contains(QStringLiteral("EP01")));
    QVERIFY(r.merged.events[0].text.contains(QStringLiteral("starts at 0:00:00.00")));
    const EventLine& m2 = r.merged.events[2];
    QVERIFY(m2.isComment);
    QVERIFY(m2.text.contains(QStringLiteral("EP02")));
    QCOMPARE(m2.start, Cs(23 * 6000));
    // 合并后 maxEnd = 23:00 + 24:00（标记与零时长不计入）
    QCOMPARE(r.merged.maxDialogueEnd, Cs(23 * 6000 + 24 * 6000));
}

void TestMerge::outputReparses()
{
    // 端到端：合并输出 → 序列化 → 再解析，语义保持
    auto d1 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:23:00.00,ED_jap,,0,0,0,,{\\pos(1,2)}一"));
    auto d2 = makeDoc(QStringLiteral("Dialogue: 0,0:00:01.00,0:24:00.00,ED_jap,,0,0,0,,{\\rED_jap}二"));
    const MergeResult r = merge({makeEp(d1, 0, 1), makeEp(d2, 23 * 6000, 2)}, {});
    const QByteArray bytes = serializeAss(r.merged);
    const ParseOutcome p = parseAss(QString::fromUtf8(bytes.mid(3)));
    QVERIFY(p.ok);
    QCOMPARE(int(p.doc.styles.size()), 2);
    // 事件数 = 2 标记 + 2 对白；第二集行时间已平移
    QCOMPARE(int(p.doc.events.size()), 4);
    QCOMPARE(p.doc.events[3].start, Cs(23 * 6000 + 100));
    QCOMPARE(p.doc.events[3].style, QStringLiteral("ED_jap@2"));
    QVERIFY(p.doc.events[3].text.contains(QStringLiteral("{\\rED_jap@2}")));
}

void TestMerge::g1EndToEnd()
{
    // 阶段 2 验收自动化：真实 G1 四集（VCB 黑之契约者）拖入 → 预填 → 合并 → 导出
    const QString dir = QStringLiteral(SUTURE_TEST_DATA_DIR) + QStringLiteral("/1");
    if (!QDir(dir).exists())
        QSKIP("测试数据目录不存在");

    // 1) 装载四集（G1 为 UTF-8 BOM，含卡拉OK/Aegisub 垃圾段）
    QList<std::shared_ptr<const AssDocument>> docs;
    const auto files = QDir(dir).entryInfoList(
        {QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
    QCOMPARE(int(files.size()), 4);
    QList<quint64> ids;
    quint64 id = 1;
    for (const QFileInfo& f : files) {
        const LoadOutcome l = loadSubtitleFile(f.absoluteFilePath(), id);
        QVERIFY2(l.ok, qPrintable(f.absoluteFilePath()));
        docs.push_back(std::make_shared<const AssDocument>(std::move(l.sub->doc)));
        ids.push_back(id++);
    }

    // 2) 预填偏移（T3）
    const QList<Cs> offsets = computePrefillOffsets(docs);
    QCOMPARE(offsets.size(), 4);
    QCOMPARE(offsets[0], Cs(0));

    // 3) 合并
    QList<EpisodeInput> eps;
    for (int i = 0; i < 4; ++i) {
        EpisodeInput ep;
        ep.doc = docs[i];
        ep.offset = offsets[i];
        ep.fileId = ids[i];
        eps.push_back(std::move(ep));
    }
    const MergeResult r = merge(eps, {});
    // 样式全部隔离（四集 × 各自样式数，@1~@4 后缀）
    int styleCount = 0;
    for (int i = 0; i < 4; ++i)
        styleCount += int(docs[i]->styles.size());
    QCOMPARE(int(r.merged.styles.size()), styleCount);
    for (const StyleDef& st : r.merged.styles)
        QVERIFY(st.outputName.contains(u'@'));

    // 4) 导出 → 再解析（Aegisub 兼容性的自动化部分）
    const QByteArray bytes = serializeAss(r.merged);
    const ParseOutcome p = parseAss(QString::fromUtf8(bytes.mid(3)));
    QVERIFY2(p.ok, "合并输出不可解析");
    // 事件数 = 4 标记 + 四集全部事件
    int totalEvents = 4;
    for (const auto& d : docs)
        totalEvents += int(d->events.size());
    QCOMPARE(int(p.doc.events.size()), totalEvents);
    // 时间连续性：EP02 标记时刻 = 第 1 集偏移(0) + 第 1 集 maxEnd
    const Cs m2 = offsets[1];
    QVERIFY(m2 > 0);
    const EventLine* ep2Marker = nullptr;
    for (const EventLine& ev : r.merged.events)
        if (ev.isComment && ev.text.contains(QStringLiteral("EP02")))
            ep2Marker = &ev;
    QVERIFY2(ep2Marker, "缺少 EP02 标记");
    QCOMPARE(ep2Marker->start, m2);
    QCOMPARE(r.merged.maxDialogueEnd, offsets[3] + docs[3]->maxDialogueEnd);
}

QTEST_MAIN(TestMerge)
#include "tst_merge.moc"
