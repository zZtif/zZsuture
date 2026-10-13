// tst_drag.cpp —— F02：文件列表鼠标拖拽排序（自动化——用 QTest 模拟
// 按下/移动/松开，之前三次人工验证拖拽失效的教训：不再盲发，机器先验）
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "filelistmodel.hpp"
#include "mplsfixture.hpp"
#include "session.hpp"
#include "timelineview.hpp"

using suture::testfix::buildMpls;
using suture::testfix::ticks;

using namespace suture;

namespace {
// 最小合法 ASS（能通过完整装载链路）
const char* kAss =
    "[Script Info]\nScriptType: v4.00+\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, "
    "MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:23:00.00,Default,,0,0,0,,x\n";
}  // namespace

class TestDrag : public QObject {
    Q_OBJECT
private slots:
    void reorderTop();     // 第 4 行拖到第 1 行
    void reorderBottom();  // 第 1 行拖到末尾
    void samePlace();      // 原地松开 = 无操作
    void offsetEditFormats();  // 偏移列：三种时间写法 + 非法输入拒绝
    void chapterModeEndToEnd();   // T8 集成：章节模式生效 + 同名导出 + C2 阈值
    void chapterModeFallback();   // T8 集成：多正片项回落 PlayItem 映射
    void contentEpisodeSort();    // F16：文件名无集数 → 内容标记识别并自动排序
    void orderMismatchWarning();  // T10：拖动致乱序 → 告警不静默改序
    void missingEpisodeWarning(); // T10：集数有洞 → 缺集告警
    void chapterPrefillDivergence(); // T8 修正：章节建议与预填背离 → 告警
    void segmentPointsSession();     // T11/A2：分段点集内语义 + 清空恢复
    void undoParams();               // A2：参数快照撤销 + 结构变化清栈
    void timelineDragBlock();        // F08：拖块改偏移（独立式）+ 撤销
    void timelineMoveTo();           // F08：内联"移动到"（实测校准形态）
    void timelinePan();              // A-r2：块下方拖动平移 + ←→ 两用语义
};

namespace {
struct Fixture {
    QTemporaryDir dir;
    Session session;
    FileListModel model{&session};
    FileTableView view;

    Fixture()
    {
        for (int i = 1; i <= 4; ++i) {
            const QString p = dir.filePath(
                QStringLiteral("[%1].ass").arg(i, 2, 10, QChar(u'0')));
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.write(kAss);
            f.close();
        }
        QList<QUrl> urls;
        const QStringList files = QDir(dir.path()).entryList(
            {QStringLiteral("*.ass")}, QDir::Files);
        for (const QString& n : files)
            urls << QUrl::fromLocalFile(dir.filePath(n));
        session.addUrls(urls);
        view.setModel(&model);
        // 真实程序里由 MainWindow 完成的接线（测试必须同样接上）
        QObject::connect(&view, &FileTableView::rowsDropped,
                         &session, &Session::moveRows);
        view.resize(900, 400);
        view.show();
    }

    // 模拟真实拖拽：按下 → 带左键状态的移动（QTest::mouseMove 不带按键
    // 状态，必须手动构造事件）→ 松开
    void dragFromTo(int fromRow, const QPoint& to)
    {
        auto* vp = view.viewport();
        const QPoint from = view.visualRect(view.model()->index(fromRow, 0)).center();
        QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, from);
        const QPoint mid = from + (to - from) / 2 + QPoint(40, 0);
        for (const QPoint& p : {mid, to}) {
            QMouseEvent mv(QEvent::MouseMove, p, vp->mapToGlobal(p),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &mv);
        }
        QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, to);
    }
};
}  // namespace

void TestDrag::reorderTop()
{
    Fixture fx;
    QCOMPARE(fx.session.count(), 4);
    QCOMPARE(QFileInfo(fx.session.entry(3).sub->path).fileName(),
             QStringLiteral("[04].ass"));

    // 第 4 行拖到第 1 行上半部
    fx.dragFromTo(3, fx.view.visualRect(fx.view.model()->index(0, 0)).center()
                         - QPoint(0, 6));

    QCOMPARE(QFileInfo(fx.session.entry(0).sub->path).fileName(),
             QStringLiteral("[04].ass"));
    QCOMPARE(QFileInfo(fx.session.entry(1).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    // 预填连锁重算：新的第 1 行偏移归零
    QCOMPARE(fx.session.entry(0).offset, Cs(0));
}

void TestDrag::reorderBottom()
{
    Fixture fx;
    fx.dragFromTo(0, fx.view.visualRect(fx.view.model()->index(3, 0)).center()
                        + QPoint(0, 6));

    QCOMPARE(QFileInfo(fx.session.entry(3).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    QCOMPARE(QFileInfo(fx.session.entry(0).sub->path).fileName(),
             QStringLiteral("[02].ass"));
}

void TestDrag::samePlace()
{
    Fixture fx;
    fx.dragFromTo(1, fx.view.visualRect(fx.view.model()->index(1, 0)).center()
                        + QPoint(40, 20));   // 移动但仍在原行

    // 原地松开：顺序不变
    QCOMPARE(QFileInfo(fx.session.entry(0).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    QCOMPARE(QFileInfo(fx.session.entry(1).sub->path).fileName(),
             QStringLiteral("[02].ass"));
}

void TestDrag::offsetEditFormats()
{
    Fixture fx;
    const auto cell = [&fx](int r) {
        return fx.model.index(r, FileListModel::ColOffset);
    };
    const auto setOffset = [&fx](const QModelIndex& i, const QString& s) {
        return fx.model.setData(i, QVariant(s), Qt::EditRole);   // 覆写无默认参
    };
    // 标准时刻写法（黑契第 2 集分集点）
    QVERIFY(setOffset(cell(1), QStringLiteral("0:24:09.45")));
    QCOMPARE(fx.session.entry(1).offset, Cs(144945));
    // 分秒写法（等价）
    QVERIFY(setOffset(cell(1), QStringLiteral("24:09.45")));
    QCOMPARE(fx.session.entry(1).offset, Cs(144945));
    // 纯秒数写法（等价；回归：旧实现给编辑器 double，QDoubleSpinBox 上限
    // 99.99 秒，1449.45 这种值根本填不进去）
    QVERIFY(setOffset(cell(1), QStringLiteral("1449.45")));
    QCOMPARE(fx.session.entry(1).offset, Cs(144945));
    // 显示层与"最晚结束"列同格式
    QCOMPARE(fx.model.data(cell(1), Qt::DisplayRole).toString(),
             QStringLiteral("0:24:09.45"));
    // 非法输入：拒绝且原值不变
    const Cs before = fx.session.entry(2).offset;
    QVERIFY(!setOffset(cell(2), QStringLiteral("abc")));
    QCOMPARE(fx.session.entry(2).offset, before);
    // 手改后来源标记为"手动"
    QCOMPARE(fx.model.data(fx.model.index(1, FileListModel::ColSource),
                           Qt::DisplayRole).toString(),
             QStringLiteral("手动"));
}

namespace {
// 造 count 个临时字幕文件并载入 session（文件名 [01] 式，集数可识别）
void loadFakeSubs(Session& session, const QTemporaryDir& dir, int count)
{
    QList<QUrl> urls;
    for (int i = 1; i <= count; ++i) {
        const QString p = dir.filePath(
            QStringLiteral("[%1].ass").arg(i, 2, 10, QChar(u'0')));
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(kAss);
        f.close();
        urls << QUrl::fromLocalFile(p);
    }
    session.addUrls(urls);
}
}  // namespace

void TestDrag::chapterModeEndToEnd()
{
    QTemporaryDir dir;
    Session session;
    // 3 集 m2ts（00001.m2ts）：等分位恰为 0 / 24:00 / 48:00，
    // 章节含 OP 签名（分集点 +90.09 秒）与噪声章节
    const quint32 origin = ticks(600 * 100);
    const QByteArray raw = buildMpls(
        {{origin, origin + ticks(3 * 24 * 6000), 0, "00001"}},
        {{1, 0, origin},
         {1, 0, origin + ticks(70 * 100)},           // 噪声章节
         {1, 0, origin + ticks(24 * 6000)},          // 第 2 集起点
         {1, 0, origin + ticks(24 * 6000 + 9009)},   // 其 OP 结束（成对章节）
         {1, 0, origin + ticks(35 * 6000)},          // 噪声章节
         {1, 0, origin + ticks(48 * 6000)},          // 第 3 集起点
         {1, 0, origin + ticks(48 * 6000 + 9009)}});
    const QString mplsPath = dir.filePath(QStringLiteral("00000.mpls"));
    QFile mf(mplsPath);
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(raw);
    mf.close();

    loadFakeSubs(session, dir, 3);
    QString err;
    QVERIFY(session.loadMplsFile(mplsPath, &err));
    QVERIFY(session.mplsChapterMode());
    QCOMPARE(session.mplsClipName(), QStringLiteral("00001"));
    // v1.4.2 降级：加载只解析与建议，偏移保持预填（预测只建议、永不自动生效）
    QVERIFY(session.mplsSuggestionAvailable());
    QCOMPARE(session.entry(1).offset, Cs(23 * 6000));   // 预填 = 第 1 集 maxEnd
    QCOMPARE(session.entry(1).source, EpisodeInput::OffsetSource::Prefill);
    // 显式应用后：偏移 = 章节建议；来源与导出名（F07：与 m2ts 同名）
    QVERIFY(session.applyMplsSuggestion());
    QCOMPARE(session.entry(0).offset, Cs(0));
    QCOMPARE(session.entry(1).offset, Cs(24 * 6000));
    QCOMPARE(session.entry(2).offset, Cs(48 * 6000));
    QCOMPARE(session.entry(0).source, EpisodeInput::OffsetSource::Chapter);
    QCOMPARE(session.suggestFileName(), QStringLiteral("00001.ass"));
    // C2 空档阈值（应用后 5 分钟）：字幕 23:00 结束 vs 下集起点 24:00
    // 的 1 分钟空档不应告警（PlayItem 模式的 2 秒口径会误报）
    QCOMPARE(session.entry(0).mplsEnd.value_or(Cs(-1)), Cs(24 * 6000));
    for (const Diagnostic& d : session.allDiags())
        QVERIFY2(d.code != DiagCode::BoundaryGap, qPrintable(d.message));
    // 结构性变化（移动行）后：已应用的建议回落预填 + 告警
    session.moveRows(0, 1, 3);
    QCOMPARE(session.entry(0).source, EpisodeInput::OffsetSource::Prefill);
    bool hasResetWarn = false;
    for (const Diagnostic& d : session.allDiags())
        if (d.level == DiagLevel::Warning && d.code == DiagCode::MplsUnsupported
            && d.message.contains(QStringLiteral("恢复为自动推算")))
            hasResetWarn = true;
    QVERIFY(hasResetWarn);
}

void TestDrag::chapterModeFallback()
{
    QTemporaryDir dir;
    Session session;
    // 两个正片项（一集一项的规整盘型）→ 章节模式不适用，回落 PlayItem 映射
    const QByteArray raw = buildMpls({
        {0, ticks(24 * 6000), 0, "00002"},
        {0, ticks(25 * 6000), 0, "00003"},
    });
    const QString mplsPath = dir.filePath(QStringLiteral("00001.mpls"));
    QFile mf(mplsPath);
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(raw);
    mf.close();

    loadFakeSubs(session, dir, 2);
    QString err;
    QVERIFY(session.loadMplsFile(mplsPath, &err));
    QVERIFY(!session.mplsChapterMode());
    // v1.4.2：加载不改偏移（预填），显式应用后走 PlayItem 建议路径
    QCOMPARE(session.entry(1).source, EpisodeInput::OffsetSource::Prefill);
    QVERIFY(session.applyMplsSuggestion());
    QCOMPARE(session.entry(0).offset, Cs(0));
    QCOMPARE(session.entry(1).offset, Cs(24 * 6000));
    QCOMPARE(session.entry(1).source, EpisodeInput::OffsetSource::Mpls);
    QCOMPARE(session.suggestFileName(), QStringLiteral("[01-02]_merged.ass"));
}

void TestDrag::contentEpisodeSort()
{
    // 文件名无集数（P6 失败）→ 内容标记（P12）接手：Title 带 Episode 号的
    // 两个文件按内容识别集数并自动排好（F16：不想手动预测集数）
    QTemporaryDir dir;
    Session session;
    const char* kAssTmpl =
        "[Script Info]\nScriptType: v4.00+\nTitle: %1\n\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, "
        "MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:23:00.00,Default,,0,0,0,,x\n";
    for (const auto& pair : {qMakePair(QStringLiteral("aaa.ass"), 2),
                             qMakePair(QStringLiteral("bbb.ass"), 1)}) {
        QFile f(dir.filePath(pair.first));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QString(kAssTmpl)
                    .arg(QStringLiteral("SomeShow Episode %1").arg(pair.second))
                    .toUtf8());
        f.close();
    }
    session.addUrls({QUrl::fromLocalFile(dir.filePath(QStringLiteral("aaa.ass"))),
                     QUrl::fromLocalFile(dir.filePath(QStringLiteral("bbb.ass")))});
    QCOMPARE(session.count(), 2);
    // aaa(第2集) 先拖入，按内容识别后 bbb(第1集) 应排到前面
    QCOMPARE(QFileInfo(session.entry(0).sub->path).fileName(),
             QStringLiteral("bbb.ass"));
    QVERIFY(session.entry(0).sub->epConfident);
    QCOMPARE(session.entry(0).sub->epNumber, 1);
    QCOMPARE(session.entry(1).sub->epNumber, 2);
}

void TestDrag::orderMismatchWarning()
{
    // 用户拖动致乱序：告警提示（不静默改序——顺序是用户的决定权）
    Fixture fx;
    // 初始 [01][02][03][04]，把第 1 行拖到末尾 → [02][03][04][01]
    fx.session.moveRows(0, 1, 4);
    QCOMPARE(QFileInfo(fx.session.entry(3).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    bool hasMismatch = false;
    for (const Diagnostic& d : fx.session.allDiags())
        if (d.level == DiagLevel::Warning && d.code == DiagCode::EpisodeUnrecognized)
            hasMismatch = true;
    QVERIFY(hasMismatch);
    // 拖回原序 → 告警消失
    fx.session.moveRows(3, 1, 0);
    for (const Diagnostic& d : fx.session.allDiags())
        QVERIFY2(!(d.level == DiagLevel::Warning &&
                   d.code == DiagCode::EpisodeUnrecognized),
                 qPrintable(d.message));
}

void TestDrag::missingEpisodeWarning()
{
    // T10：集数有洞（1、2、4 → 缺 3）→ 告警（多集 m2ts 缺集会整体错位）
    QTemporaryDir dir;
    Session session;
    for (const int n : {1, 2, 4}) {
        const QString p = dir.filePath(
            QStringLiteral("[%1].ass").arg(n, 2, 10, QChar(u'0')));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kAss);
        f.close();
        session.addUrls({QUrl::fromLocalFile(p)});
    }
    QCOMPARE(session.count(), 3);
    bool hasGap = false;
    for (const Diagnostic& d : session.allDiags())
        if (d.level == DiagLevel::Warning && d.code == DiagCode::EpisodeUnrecognized
            && d.message.contains(QStringLiteral("缺第 3 集")))
            hasGap = true;
    QVERIFY(hasGap);
}

void TestDrag::chapterPrefillDivergence()
{
    // 死后文实测 bug 的安全网：章节建议与字幕连续预填大幅背离 → 按集告警。
    // 构造：ep1 字幕被截短（10:00 结束 → 预填 ep2 = 10:00），盘内 ED 章节把
    // ep2 锚定到 24:00 → 背离 14 分钟，必须告警（连续切割字幕的预填误差
    // 实测仅 0.05/2.3 秒，大幅背离 = 建议可疑）。
    QTemporaryDir dir;
    Session session;
    // ep1 字幕：单行 10:00 结束（截短）；ep2 字幕：带 ed_ch 行于 20:00
    const char* kAssEp1 =
        "[Script Info]\nScriptType: v4.00+\n\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, "
        "MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:10:00.00,Default,,0,0,0,,x\n";
    const char* kAssEp2 =
        "[Script Info]\nScriptType: v4.00+\n\n"
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, "
        "MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:23:00.00,Default,,0,0,0,,x\n"
        "Dialogue: 0,0:20:00.00,0:20:05.00,ed_ch,,0,0,0,,ED\n";
    int n = 1;
    for (const char* tpl : {kAssEp1, kAssEp2}) {
        const QString p = dir.filePath(
            QStringLiteral("[%1].ass").arg(n++, 2, 10, QChar(u'0')));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(tpl);
        f.close();
        session.addUrls({QUrl::fromLocalFile(p)});
    }
    // 盘：2 集 × 24 分钟；ED 对 44:00→45:30.09；ep2 起点 24:00（无标记）
    const quint32 origin = ticks(600 * 100);
    const QByteArray raw = buildMpls(
        {{origin, origin + ticks(2 * 24 * 6000), 0, "00001"}},
        {{1, 0, origin + ticks(264000)}, {1, 0, origin + ticks(273009)}});
    const QString mplsPath = dir.filePath(QStringLiteral("00000.mpls"));
    QFile mf(mplsPath);
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(raw);
    mf.close();
    QString err;
    QVERIFY(session.loadMplsFile(mplsPath, &err));
    QVERIFY(session.mplsChapterMode());
    QVERIFY(session.applyMplsSuggestion());             // v1.4.2：显式应用
    QCOMPARE(session.entry(1).offset, Cs(24 * 6000));   // ED 锚定结果
    bool hasDiverge = false;
    for (const Diagnostic& d : session.allDiags())
        if (d.level == DiagLevel::Warning && d.code == DiagCode::MplsUnsupported
            && d.fileIndex == 1 && d.message.contains(QStringLiteral("相差")))
            hasDiverge = true;
    QVERIFY(hasDiverge);
}

void TestDrag::segmentPointsSession()
{
    Fixture fx;   // 4 个文件，事件 0:00:01.00→0:23:00.00，fileId = 1..4
    // 集内分段点：第 2 集文件在 0 处 +3.5s（= 整集后移，含 EP 标记），
    // 其它集完全不动（集内语义，无隐式联动）
    QList<SegmentPoint> pts;
    pts.append({fx.session.entry(1).sub->id, 0, 350});
    fx.session.setSegmentPoints(pts);
    const MergeResult* r = fx.session.result();
    QVERIFY(r);
    // events: [EP1标记, ep1行, EP2标记, ep2行, ...]
    QCOMPARE(r->merged.events[0].start, Cs(0));                  // EP1 标记不动
    QCOMPARE(r->merged.events[1].start, Cs(100));                // ep1 首行不动
    QCOMPARE(r->merged.events[2].start, Cs(23 * 6000 + 350));    // EP2 标记后移
    QCOMPARE(r->merged.events[3].start, Cs(23 * 6000 + 100 + 350));   // ep2 首行后移
    QCOMPARE(r->merged.events[4].start, Cs(46 * 6000));          // EP3 标记不动
    // 清空恢复
    fx.session.setSegmentPoints({});
    QCOMPARE(fx.session.result()->merged.events[3].start, Cs(23 * 6000 + 100));
}

void TestDrag::undoParams()
{
    Fixture fx;
    // 偏移手改 → 撤销恢复
    QCOMPARE(fx.session.entry(1).offset, Cs(23 * 6000));
    fx.session.setOffset(1, Cs(25 * 6000));
    QCOMPARE(fx.session.entry(1).offset, Cs(25 * 6000));
    QVERIFY(fx.session.canUndo());
    fx.session.undo();
    QCOMPARE(fx.session.entry(1).offset, Cs(23 * 6000));
    QCOMPARE(fx.session.entry(1).source, EpisodeInput::OffsetSource::Prefill);
    // 分段点 → 撤销恢复
    QList<SegmentPoint> pts;
    pts.append({fx.session.entry(1).sub->id, 0, 350});
    fx.session.setSegmentPoints(pts);
    QVERIFY(fx.session.canUndo());
    fx.session.undo();
    QCOMPARE(fx.session.segmentPoints().size(), 0);
    // 结构变化清栈（决定 32：增删移不进撤销）
    fx.session.setOffset(1, Cs(30 * 6000));
    QVERIFY(fx.session.canUndo());
    fx.session.moveRows(0, 1, 3);
    QVERIFY(!fx.session.canUndo());
}

// ---- 时间轴（F08）GUI 用例：模拟真实鼠标/键盘操作 ----
// 几何换算（TimelineView 私有映射的黑盒等价）：首次内容 refresh 自动适配，
// viewLeft=-2000、pxPerCs = 宽/(总长+4000)；块带 y = 工具条+标尺+6+半高

void TestDrag::timelineDragBlock()
{
    Fixture fx;
    TimelineView view(&fx.session);
    view.resize(900, 360);
    view.show();

    const double pxPerCs = 900.0 / (4.0 * 138000 + 4000);
    const int y = 34 + 30 + 6 + 46 / 2;
    const int x = int((138000 + 2000) * pxPerCs) + 20;   // 第 2 集块内
    const QPoint press(x, y);
    const QPoint release(x + 80, y);   // 右拖 80px

    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, press);
    QMouseEvent mv(QEvent::MouseMove, release, view.mapToGlobal(release),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &mv);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, release);

    // 涟漪（决定 45）：第 2 集按 Δ 平移，其后的 3/4 集同 Δ 跟随，
    // 第 1 集不动；依据标"时间轴"。整数截断允许 ±2 厘秒
    const Cs delta = Cs(80 / pxPerCs);
    QCOMPARE(fx.session.entry(1).source, EpisodeInput::OffsetSource::Timeline);
    QVERIFY(qAbs(fx.session.entry(1).offset - (138000 + delta)) <= 2);
    QCOMPARE(fx.session.entry(0).offset, Cs(0));
    QVERIFY(qAbs(fx.session.entry(2).offset - (2 * 138000 + delta)) <= 2);
    QVERIFY(qAbs(fx.session.entry(3).offset - (3 * 138000 + delta)) <= 2);

    // Ctrl+Z 一步撤销整个手势（含涟漪的所有行）
    QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(fx.session.entry(1).offset, Cs(138000));
    QCOMPARE(fx.session.entry(2).offset, Cs(2 * 138000));
    QCOMPARE(fx.session.entry(1).source, EpisodeInput::OffsetSource::Prefill);

    // Alt+拖 = 只动该块（决定 45：个别集误差不同的兜底）
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, press);
    QMouseEvent mv2(QEvent::MouseMove, release, view.mapToGlobal(release),
                    Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    QApplication::sendEvent(&view, &mv2);
    QMouseEvent rel2(QEvent::MouseButtonRelease, release,
                     view.mapToGlobal(release), Qt::LeftButton, Qt::NoButton,
                     Qt::AltModifier);
    QApplication::sendEvent(&view, &rel2);
    QVERIFY(qAbs(fx.session.entry(1).offset - (138000 + delta)) <= 2);
    QCOMPARE(fx.session.entry(2).offset, Cs(2 * 138000));   // 后面不动
}

void TestDrag::timelineMoveTo()
{
    Fixture fx;
    TimelineView view(&fx.session);
    view.resize(900, 360);
    view.show();
    // 单击选中第 1 集块后敲数字 → 内联编辑器 → 回车提交
    const double pxPerCs = 900.0 / (4.0 * 138000 + 4000);
    const int x = int(2000 * pxPerCs) + 20;
    const int y = 34 + 30 + 6 + 46 / 2;
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(x, y));
    // 首个数字触发内联编辑器并获得焦点；后续按键发给编辑器（真实使用
    // 中焦点自然转移，QTest 需显式找到编辑器——它不是 view 的焦点代理）
    QTest::keyClicks(&view, QStringLiteral("0"));
    auto* edit = view.findChild<QLineEdit*>();
    QVERIFY(edit);
    QVERIFY(edit->isVisible());
    edit->clear();
    QTest::keyClicks(edit, QStringLiteral("0:10:00.00"));
    QTest::keyClick(edit, Qt::Key_Return);
    // 块起点（最终坐标）= 0:10:00 → 偏移 = 60000（全局微调 0）
    QCOMPARE(fx.session.entry(0).offset, Cs(60000));
    QCOMPARE(fx.session.entry(0).source, EpisodeInput::OffsetSource::Timeline);
}

void TestDrag::timelinePan()
{
    Fixture fx;
    TimelineView view(&fx.session);
    view.resize(900, 360);
    view.show();
    const double pxPerCs = 900.0 / (4.0 * 138000 + 4000);
    const int y = 34 + 30 + 6 + 46 / 2;              // 块带中线
    const int yBelow = 34 + 30 + 6 + 46 + 30;        // 块下方平移区
    Q_UNUSED(pxPerCs)

    // 对照：平移前 ←→ 两用——无选中时 ← 平移，任何偏移都不动
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(50, yBelow));
    QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(fx.session.entry(0).offset, Cs(0));
    QCOMPARE(fx.session.entry(1).offset, Cs(138000));

    // 对照：有选中时 ← = 移动块（-0.1 秒）
    const int x2 = int((138000 + 2000) * pxPerCs) + 20;   // 第 2 集块内
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(x2, y));
    QTest::keyClick(&view, Qt::Key_Left);
    QCOMPARE(fx.session.entry(1).offset, Cs(138000 - 10));

    // 块下方左键拖动 100px = 平移视口：所有偏移不变
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                      QPoint(50, yBelow));   // 先清空选择
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(300, yBelow));
    QMouseEvent mv(QEvent::MouseMove, QPoint(400, yBelow),
                   view.mapToGlobal(QPoint(400, yBelow)),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &mv);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(400, yBelow));
    QCOMPARE(fx.session.entry(0).offset, Cs(0));
    QCOMPARE(fx.session.entry(1).offset, Cs(138000 - 10));   // 平移不改数据

    // 平移 100px 后块 1 起点从 x≈3 移到 x≈103：原位置 (50,y) 变空白，
    // 点击不再选中任何块 → ← 键是平移而非移动，偏移保持不变
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(50, y));
    QTest::keyClick(&view, Qt::Key_Left);
    QCOMPARE(fx.session.entry(1).offset, Cs(138000 - 10));

    // 中键平移同样只动视口不动数据
    QTest::mousePress(&view, Qt::MiddleButton, Qt::NoModifier, QPoint(300, y));
    QMouseEvent mv2(QEvent::MouseMove, QPoint(500, y),
                    view.mapToGlobal(QPoint(500, y)),
                    Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &mv2);
    QTest::mouseRelease(&view, Qt::MiddleButton, Qt::NoModifier, QPoint(500, y));
    QCOMPARE(fx.session.entry(0).offset, Cs(0));
}

QTEST_MAIN(TestDrag)
#include "tst_drag.moc"
