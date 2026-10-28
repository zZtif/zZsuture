#include "mainwindow.hpp"

#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

#include "buildinfo.hpp"
#include "diagmodel.hpp"
#include "filelistmodel.hpp"
#include "session.hpp"
#include "discaxisview.hpp"
#include "timelineview.hpp"
#include "version.hpp"

namespace suture {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    session_ = new Session(this);
    fileModel_ = new FileListModel(session_, this);
    diagModel_ = new DiagModel(this);
    buildUi();

    connect(session_, &Session::resultChanged, this, [this] {
        refreshDiags();
        refreshStatus();
    });
    connect(diagTable_, &QTableView::clicked, this, [this](const QModelIndex& idx) {
        // 告警点击 → 定位文件（F05：告警行可点击跳转）
        const Diagnostic* d = diagModel_->at(idx.row());
        if (d && d->fileIndex >= 0 && d->fileIndex < fileModel_->rowCount())
            fileTable_->selectRow(d->fileIndex);
    });
}

void MainWindow::buildUi()
{
    // 标题栏显示版本号；构建标识退到状态栏（发布版仍可凭它核对
    // "跑的是哪个提交构建的 exe"，报问题用）
    setWindowTitle(tr("SUture — BDMV 字幕合并 v%1")
                       .arg(QString::fromUtf8(versionString())));
    resize(1080, 680);
    setMinimumSize(1000, 640);   // A1：下限兜底（时间轴再窄不可用）
    setAcceptDrops(true);

    // ---- 上半部：文件表 + 右侧控制面板 ----
    fileTable_ = new FileTableView;
    fileTable_->setModel(fileModel_);
    // 行拖拽排序（F02）：视图纯鼠标实现 → Session::moveRows
    connect(fileTable_, &FileTableView::rowsDropped,
            session_, &Session::moveRows);
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTable_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                QAbstractItemView::SelectedClicked |
                                QAbstractItemView::EditKeyPressed);
    fileTable_->verticalHeader()->hide();
    fileTable_->setAlternatingRowColors(true);   // 去网格线后的行区分（§5.3）
    fileTable_->horizontalHeader()->setStretchLastSection(false);
    fileTable_->horizontalHeader()->setSectionResizeMode(FileListModel::ColName,
                                                         QHeaderView::Stretch);
    fileTable_->setColumnWidth(FileListModel::ColOrder, 36);
    fileTable_->setColumnWidth(FileListModel::ColEp, 44);
    fileTable_->setColumnWidth(FileListModel::ColMaxEnd, 92);
    fileTable_->setColumnWidth(FileListModel::ColOffset, 100);
    fileTable_->setColumnWidth(FileListModel::ColSource, 76);

    // ---- A-r2（决定 42/43）：原盘解析页 = 左上大板块第二页签 ----
    // 心智模型：工具的主要功能是字幕自定义，盘的数据是"摆出来给你看的
    // 参考"——纯数轴读数仪 + 悬停依据 + 套用（仅供参考）
    auto* parsePage = new QWidget;
    auto* ppv = new QVBoxLayout(parsePage);
    ppv->setContentsMargins(8, 8, 8, 8);
    ppv->setSpacing(8);
    mplsPathEdit_ = new QLineEdit;
    auto* mplsPath = mplsPathEdit_;
    mplsPath->setPlaceholderText(tr("拖入 .mpls 或点浏览选择"));
    auto* browseBtn = new QPushButton(tr("浏览…"));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(mplsPath, 1);
    pathRow->addWidget(browseBtn);
    ppv->addLayout(pathRow);
    auto* parseBtn = new QPushButton(tr("读取原盘"));
    auto* clearMplsBtn = new QPushButton(tr("清除"));
    auto* mappingToggle = new QPushButton(tr("显示映射"));
    mappingToggle->setCheckable(true);
    mappingToggle->setToolTip(
        tr("把盘上的时间点以淡色线叠加到时间轴（吸附不受影响）"));
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(parseBtn, 1);
    btnRow->addWidget(clearMplsBtn);
    btnRow->addWidget(mappingToggle);
    ppv->addLayout(btnRow);
    auto* mplsInfo = new QLabel(tr("未读取。各集开始时间按上一集字幕结束自动推算。"));
    mplsInfo->setWordWrap(true);
    ppv->addWidget(mplsInfo);
    // 纯数轴（决定 43）：细线刻度 + ◆建议分集点(EP标签) + ▽疑似片尾曲起点
    // + 章节细刻度 + 图例 + 悬停依据——不用色块，与字幕轴视觉彻底区分
    discAxis_ = new DiscAxisView(session_);
    ppv->addWidget(discAxis_, 1);
    auto* applySuggestBtn = new QPushButton(tr("套用参考起点（仅供参考）"));
    applySuggestBtn->setObjectName(QStringLiteral("applySuggestBtn"));
    connect(applySuggestBtn, &QPushButton::clicked, this, [this] {
        // v1.4.2 原则：应用是显式动作（预测只建议、永不自动生效）
        session_->applyMplsSuggestion();
    });
    ppv->addWidget(applySuggestBtn);

    // 左上大板块页签：[文件] [原盘解析]，读取原盘自动跳转（决定 42）
    fileTabs_ = new QTabWidget;
    fileTabs_->addTab(fileTable_, tr("文件"));
    fileTabs_->addTab(parsePage, tr("原盘解析"));

    // ---- 右侧面板（A-r2 瘦身：高级选项 + 移除 + 导出）----
    auto* panel = new QWidget;
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(8, 8, 8, 8);
    pv->setSpacing(10);

    auto* hint = new QLabel(tr("拖入字幕文件/文件夹到窗口任意位置\n"
                               "列表中按住行拖动可排序"));
    hint->setWordWrap(true);
    pv->addWidget(hint);

    // ---- 高级选项（折叠）：低频功能收纳 ----
    auto* advToggle = new QPushButton(tr("▸ 高级选项"));
    advToggle->setCheckable(true);
    advToggle->setObjectName(QStringLiteral("advToggle"));
    auto* advPanel = new QWidget;
    auto* av = new QVBoxLayout(advPanel);
    av->setContentsMargins(0, 4, 0, 4);
    // 字幕第一集对应盘内第几集（整季链只合并中间几集的场景；GTO 44 集链实证）
    auto* startRow = new QHBoxLayout;
    auto* startSpin = new QSpinBox;
    startSpin->setRange(1, 1);
    startRow->addWidget(new QLabel(tr("第一集对应盘内第")), 1);
    startRow->addWidget(startSpin);
    startRow->addWidget(new QLabel(tr("个正片")));
    av->addLayout(startRow);
    auto* advHint1 = new QLabel(tr("（整季连播链只合并中间几集时使用；多集"
                                   "一个视频文件的盘无需此项）"));
    advHint1->setWordWrap(true);
    av->addWidget(advHint1);
    // 整体提前/延后
    auto* shiftRow = new QHBoxLayout;
    auto* shiftSpin = new QDoubleSpinBox;
    shiftSpin->setRange(-36000.0, 36000.0);
    shiftSpin->setDecimals(2);
    shiftSpin->setSuffix(tr(" 秒"));
    shiftSpin->setValue(0.0);
    connect(shiftSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double sec) {
                session_->setGlobalShift(Cs(std::llround(sec * 100.0)));
            });
    shiftRow->addWidget(new QLabel(tr("全部字幕提前/延后")), 1);
    shiftRow->addWidget(shiftSpin);
    av->addLayout(shiftRow);
    advPanel->setVisible(false);
    connect(advToggle, &QPushButton::toggled, this, [advToggle, advPanel](bool on) {
        advPanel->setVisible(on);
        advToggle->setText(on ? tr("▾ 高级选项") : tr("▸ 高级选项"));
    });
    pv->addWidget(advToggle);
    pv->addWidget(advPanel);

    const auto applyMplsInfo = [this, mplsInfo, startSpin, applySuggestBtn] {
        applySuggestBtn->setEnabled(session_->mplsSuggestionAvailable());
        if (const MplsInfo* m = session_->mpls()) {
            // 验收反馈 3：直接给出"播哪个 m2ts、多长、约几集"——用户按
            // 时长自行判断哪个列表是主内容（不自动选，预测只建议）
            QString clips;
            for (const PlayItemSpan& it : m->items) {
                if (!clips.isEmpty())
                    clips += QStringLiteral("、");
                clips += it.clip + QStringLiteral(".m2ts");
            }
            const int estEps =
                int((m->totalCs + 12 * 6000) / (24 * 6000));   // 24 分钟/集
            QString text = tr("播放 %1 ｜ 总时长 %2")
                               .arg(clips)
                               .arg(formatCs(m->totalCs));
            text += estEps > 0
                ? tr(" ｜ 约 %1 集（按 24 分钟/集估算，仅供参考）").arg(estEps)
                : tr("（不足一集——可能是警告/菜单列表，请换下一个）");
            if (session_->mplsChapterMode()) {
                // 多集一个 m2ts：参考值在上方数轴；"第一集对应第几个正片"
                // 是一集一个片段的概念，此模式下无意义
                text += tr("｜ 分集点见上方数轴，悬停可看依据");
                startSpin->setRange(1, 1);
                startSpin->setEnabled(false);
            } else {
                startSpin->setRange(1, qMax(1, session_->mplsMainCount()));
                startSpin->setEnabled(session_->mplsMainCount() > 0);
            }
            mplsInfo->setText(text);
        } else {
            mplsInfo->setText(tr("未读取。各集开始时间按上一集字幕结束"
                                       "自动推算。"));
            startSpin->setRange(1, 1);
            startSpin->setEnabled(false);
        }
        discAxis_->refresh();
    };
    connect(startSpin, &QSpinBox::valueChanged, this, [this](int v) {
        session_->setMplsStartMain(v - 1);   // UI 1 基 → 内部 0 基
    });
    connect(browseBtn, &QPushButton::clicked, this, [this, mplsPath] {
        const QString p = QFileDialog::getOpenFileName(
            this, tr("选择 MPLS 播放列表"), QString(),
            tr("Blu-ray 播放列表 (*.mpls);;所有文件 (*)"));
        if (!p.isEmpty())
            mplsPath->setText(p);
    });
    connect(parseBtn, &QPushButton::clicked, this,
            [this, mplsPath, applyMplsInfo, parsePage] {
        // 决定 42：按下解析按钮自动跳到本页
        fileTabs_->setCurrentWidget(parsePage);
        const QString p = mplsPath->text().trimmed();
        if (p.isEmpty()) {
            QMessageBox::information(this, tr("SUture"),
                                     tr("请先选择 .mpls 文件。"));
            return;
        }
        QString err;
        if (!session_->loadMplsFile(p, &err)) {
            applyMplsInfo();
            QMessageBox::warning(this, tr("原盘解析失败"),
                                 err.isEmpty() ? tr("无法读取该文件") : err);
            return;
        }
        applyMplsInfo();
    });
    connect(clearMplsBtn, &QPushButton::clicked, this, [this, applyMplsInfo] {
        session_->clearMpls();
        applyMplsInfo();
    });
    // 决定 44：显示映射开关 → 盘标记叠加到时间轴（吸附不受影响）
    connect(mappingToggle, &QPushButton::toggled, this, [this](bool on) {
        timeline_->setDiscOverlay(on);
    });

    auto* removeBtn = new QPushButton(tr("移除所选文件"));
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        const auto sel = fileTable_->selectionModel()->selectedRows();
        for (const QModelIndex& i : sel)
            rows.append(i.row());
        if (!rows.isEmpty())
            session_->removeRows(rows);
    });

    auto* exportBtn = new QPushButton(tr("合并导出"));
    exportBtn->setObjectName(QStringLiteral("exportBtn"));
    exportBtn->setMinimumHeight(40);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportMerged);
    pv->addWidget(removeBtn);
    pv->addWidget(exportBtn);
    pv->addStretch(1);

    auto* topSplit = new QSplitter(Qt::Horizontal);
    topSplit->addWidget(fileTabs_);
    topSplit->addWidget(panel);
    topSplit->setStretchFactor(0, 1);
    topSplit->setStretchFactor(1, 0);
    topSplit->setSizes({760, 300});

    // ---- 下半部：页签（时间轴为默认展示，A2 填充真实视图；告警第二页）----
    diagTable_ = new QTableView;
    diagTable_->setModel(diagModel_);
    diagTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diagTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    diagTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diagTable_->verticalHeader()->hide();
    diagTable_->setAlternatingRowColors(true);
    diagTable_->horizontalHeader()->setSectionResizeMode(DiagModel::ColMsg,
                                                         QHeaderView::Stretch);
    diagTable_->setColumnWidth(DiagModel::ColLevel, 70);
    diagTable_->setColumnWidth(DiagModel::ColId, 50);
    diagTable_->setColumnWidth(DiagModel::ColFile, 56);
    auto* diagBox = new QGroupBox(tr("告警"));
    auto* dv = new QVBoxLayout(diagBox);
    dv->setContentsMargins(4, 4, 4, 4);
    dv->addWidget(diagTable_);

    // A2：时间轴主视图（F08）
    timeline_ = new TimelineView(session_);
    auto* tabs = new QTabWidget;
    tabs->addTab(timeline_, tr("时间轴"));
    tabs->addTab(diagBox, tr("告警"));

    auto* mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(tabs);
    mainSplit->setStretchFactor(0, 3);
    mainSplit->setStretchFactor(1, 2);
    mainSplit->setSizes({400, 260});
    setCentralWidget(mainSplit);

    // 解析页随会话结构变化刷新（数轴标记与按钮可用性）
    connect(session_, &Session::structureChanged, this, applyMplsInfo);

    statusLabel_ = new QLabel;
    // 常驻右侧：不被 showMessage 的临时消息（导出成功提示等）覆盖
    statusBar()->addPermanentWidget(statusLabel_);
    refreshStatus();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    if (!e->mimeData()->hasUrls())
        return;
    // .mpls 文件单独分流到解析页（A-r2 决定 42：自动跳转），其余走字幕装载
    QList<QUrl> subtitleUrls;
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString local = url.toLocalFile();
        if (local.endsWith(QStringLiteral(".mpls"), Qt::CaseInsensitive)) {
            fileTabs_->setCurrentIndex(1);
            mplsPathEdit_->setText(local);   // 拖入路径同步显示（验收反馈）
            QString err;
            if (!session_->loadMplsFile(local, &err))
                QMessageBox::warning(this, tr("原盘解析失败"),
                                     err.isEmpty() ? tr("无法读取该文件") : err);
        } else {
            subtitleUrls.append(url);
        }
    }
    if (!subtitleUrls.isEmpty())
        session_->addUrls(subtitleUrls);
    e->acceptProposedAction();
}

void MainWindow::exportMerged()
{
    if (session_->count() == 0) {
        QMessageBox::information(this, tr("SUture"),
                                 tr("请先拖入字幕文件。"));
        return;
    }
    if (session_->errorCount() > 0) {
        // 不静默带病输出（§5.9）：Error 级诊断存在时需显式确认
        const auto btn = QMessageBox::warning(
            this, tr("SUture"),
            tr("当前存在 %1 条错误级诊断（见告警列表）。\n"
               "通常意味着部分文件装载失败——继续导出将缺少这些文件的内容。\n"
               "仍要导出吗？").arg(session_->errorCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes)
            return;
    }
    const QString suggested = session_->suggestFileName();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("合并导出"), suggested,
        tr("Advanced SubStation (*.ass);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    QString err;
    if (!session_->exportTo(path, &err)) {
        QMessageBox::critical(this, tr("导出失败"), err);
        return;
    }
    statusBar()->showMessage(tr("已导出：%1").arg(path), 5000);
}

void MainWindow::refreshDiags()
{
    diagModel_->setDiags(session_->allDiags());
}

void MainWindow::refreshStatus()
{
    const QList<Diagnostic> diags = session_->allDiags();
    int err = 0, warn = 0, info = 0;
    for (const auto& d : diags) {
        switch (d.level) {
        case DiagLevel::Error:   ++err;   break;
        case DiagLevel::Warning: ++warn;  break;
        default:                 ++info;  break;
        }
    }
    statusLabel_->setText(
        tr("文件 %1 个 ｜ ● 错误 %2  ⚠ 警告 %3  ℹ 提示 %4  ｜ v%5（构建 %6）")
            .arg(session_->count()).arg(err).arg(warn).arg(info)
            .arg(QString::fromUtf8(versionString()))
            .arg(QString::fromLatin1(kBuildStamp)));
}

}  // namespace suture
