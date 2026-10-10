#include "mainwindow.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

#include "buildinfo.hpp"
#include "diagmodel.hpp"
#include "filelistmodel.hpp"
#include "session.hpp"
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
    // 构建标识放标题栏（最显眼处）+ 状态栏右侧——验证问题先核对此处
    setWindowTitle(tr("SUture — BDMV 字幕合并 ｜ 构建 %1")
                       .arg(QString::fromLatin1(kBuildStamp)));
    resize(1080, 680);
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
    fileTable_->horizontalHeader()->setStretchLastSection(false);
    fileTable_->horizontalHeader()->setSectionResizeMode(FileListModel::ColName,
                                                         QHeaderView::Stretch);
    fileTable_->setColumnWidth(FileListModel::ColOrder, 36);
    fileTable_->setColumnWidth(FileListModel::ColEp, 44);
    fileTable_->setColumnWidth(FileListModel::ColMaxEnd, 90);
    fileTable_->setColumnWidth(FileListModel::ColOffset, 90);
    fileTable_->setColumnWidth(FileListModel::ColSource, 60);

    // 右侧面板
    auto* panel = new QWidget;
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(8, 8, 8, 8);

    auto* hint = new QLabel(tr("拖入字幕文件/文件夹到窗口任意位置\n"
                               "在列表中按住行拖动可排序\n"
                               "双击偏移列改每集起点\n"
                               "（支持 0:24:09.45 / 24:09.45 / 纯秒数）"));
    hint->setWordWrap(true);
    pv->addWidget(hint);

    // MPLS 面板（阶段 3：原盘播放列表 → 每集起点自动偏移，T1/T3）
    auto* mplsBox = new QGroupBox(tr("MPLS 自动偏移"));
    auto* mv = new QVBoxLayout(mplsBox);
    auto* mplsPath = new QLineEdit;
    mplsPath->setPlaceholderText(tr("拖入 .mpls 或点浏览选择"));
    auto* browseBtn = new QPushButton(tr("浏览…"));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(mplsPath, 1);
    pathRow->addWidget(browseBtn);
    mv->addLayout(pathRow);
    auto* parseBtn = new QPushButton(tr("解析"));
    auto* applySuggestBtn = new QPushButton(tr("应用建议偏移"));
    auto* clearMplsBtn = new QPushButton(tr("清除 MPLS"));
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(parseBtn);
    btnRow->addWidget(applySuggestBtn);
    btnRow->addWidget(clearMplsBtn);
    mv->addLayout(btnRow);
    // 起始正片选择（整季连播链只合并中间几集的场景，实证：GTO 44 集链）
    auto* startRow = new QHBoxLayout;
    auto* startSpin = new QSpinBox;
    startSpin->setRange(1, 1);
    startSpin->setSuffix(tr(" / 正片数"));
    startRow->addWidget(new QLabel(tr("字幕第1集对应第")));
    startRow->addWidget(startSpin);
    startRow->addWidget(new QLabel(tr("个正片")));
    mv->addLayout(startRow);
    auto* mplsInfo = new QLabel(tr("未加载。无 MPLS 时按上集结束时刻预填。"));
    mplsInfo->setWordWrap(true);
    mv->addWidget(mplsInfo);
    pv->addWidget(mplsBox);

    const auto applyMplsInfo = [this, mplsInfo, startSpin, applySuggestBtn] {
        applySuggestBtn->setEnabled(session_->mplsSuggestionAvailable());
        if (const MplsInfo* m = session_->mpls()) {
            QString text = tr("已解析：%1 个 PlayItem，总时长 %2")
                               .arg(m->items.size())
                               .arg(formatCs(m->totalCs));
            if (session_->mplsChapterMode()) {
                // v1.4.2：多集一个 m2ts 只给参考值（告警面板），应用是显式动作；
                // 起始正片是 PlayItem 映射的概念，此模式下无意义
                text += tr("｜ 分集点参考值见告警面板");
                startSpin->setRange(1, 1);
                startSpin->setEnabled(false);
            } else {
                startSpin->setRange(1, qMax(1, session_->mplsMainCount()));
                startSpin->setEnabled(session_->mplsMainCount() > 0);
            }
            mplsInfo->setText(text);
        } else {
            mplsInfo->setText(tr("未加载。无 MPLS 时按上集结束时刻预填。"));
            startSpin->setRange(1, 1);
            startSpin->setEnabled(false);
        }
    };
    connect(startSpin, &QSpinBox::valueChanged, this, [this](int v) {
        session_->setMplsStartMain(v - 1);   // UI 1 基 → 内部 0 基
    });
    connect(session_, &Session::structureChanged, this, applyMplsInfo);
    connect(browseBtn, &QPushButton::clicked, this, [this, mplsPath] {
        const QString p = QFileDialog::getOpenFileName(
            this, tr("选择 MPLS 播放列表"), QString(),
            tr("Blu-ray 播放列表 (*.mpls);;所有文件 (*)"));
        if (!p.isEmpty())
            mplsPath->setText(p);
    });
    connect(parseBtn, &QPushButton::clicked, this, [this, mplsPath, applyMplsInfo] {
        const QString p = mplsPath->text().trimmed();
        if (p.isEmpty()) {
            QMessageBox::information(this, tr("SUture"),
                                     tr("请先选择 .mpls 文件。"));
            return;
        }
        QString err;
        if (!session_->loadMplsFile(p, &err)) {
            applyMplsInfo();
            QMessageBox::warning(this, tr("MPLS 解析失败"),
                                 err.isEmpty() ? tr("无法读取该文件") : err);
            return;
        }
        applyMplsInfo();
    });
    connect(clearMplsBtn, &QPushButton::clicked, this, [this, applyMplsInfo] {
        session_->clearMpls();
        applyMplsInfo();
    });
    connect(applySuggestBtn, &QPushButton::clicked, this, [this] {
        // v1.4.2 原则：应用是显式动作（预测只建议、永不自动生效）
        session_->applyMplsSuggestion();
    });

    auto* tuneBox = new QGroupBox(tr("全局微调"));
    auto* tv = new QHBoxLayout(tuneBox);
    auto* shiftSpin = new QDoubleSpinBox;
    shiftSpin->setRange(-36000.0, 36000.0);
    shiftSpin->setDecimals(2);
    shiftSpin->setSuffix(tr(" 秒"));
    shiftSpin->setValue(0.0);
    connect(shiftSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double sec) {
                session_->setGlobalShift(Cs(std::llround(sec * 100.0)));
            });
    tv->addWidget(new QLabel(tr("所有字幕")));
    tv->addWidget(shiftSpin);
    pv->addWidget(tuneBox);

    // T11 校正工具（v1.4.2 人工主线）
    auto* calibBox = new QGroupBox(tr("校正工具"));
    auto* cv = new QVBoxLayout(calibBox);
    auto* bpBtn = new QPushButton(tr("分段校正…"));
    auto* obsBtn = new QPushButton(tr("实测校准…"));
    connect(bpBtn, &QPushButton::clicked, this, &MainWindow::openBreakpointDialog);
    connect(obsBtn, &QPushButton::clicked, this, &MainWindow::openCalibrateDialog);
    cv->addWidget(bpBtn);
    cv->addWidget(obsBtn);
    auto* calibHint = new QLabel(tr("分段校正：在时间点插入黑屏/转场增量\n"
                                    "实测校准：读播放器时刻，工具算偏移"));
    calibHint->setWordWrap(true);
    cv->addWidget(calibHint);
    pv->addWidget(calibBox);

    auto* removeBtn = new QPushButton(tr("移除所选文件"));
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        const auto sel = fileTable_->selectionModel()->selectedRows();
        for (const QModelIndex& i : sel)
            rows.append(i.row());
        if (!rows.isEmpty())
            session_->removeRows(rows);
    });
    pv->addWidget(removeBtn);

    auto* exportBtn = new QPushButton(tr("合并导出"));
    exportBtn->setObjectName(QStringLiteral("exportBtn"));
    exportBtn->setMinimumHeight(40);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportMerged);
    pv->addWidget(exportBtn);
    pv->addStretch(1);

    auto* topSplit = new QSplitter(Qt::Horizontal);
    topSplit->addWidget(fileTable_);
    topSplit->addWidget(panel);
    topSplit->setStretchFactor(0, 1);
    topSplit->setStretchFactor(1, 0);
    topSplit->setSizes({760, 300});

    // ---- 下半部：告警列表 ----
    diagTable_ = new QTableView;
    diagTable_->setModel(diagModel_);
    diagTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diagTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    diagTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diagTable_->verticalHeader()->hide();
    diagTable_->horizontalHeader()->setSectionResizeMode(DiagModel::ColMsg,
                                                         QHeaderView::Stretch);
    diagTable_->setColumnWidth(DiagModel::ColLevel, 70);
    diagTable_->setColumnWidth(DiagModel::ColId, 50);
    diagTable_->setColumnWidth(DiagModel::ColFile, 56);

    auto* diagBox = new QGroupBox(tr("告警"));
    auto* dv = new QVBoxLayout(diagBox);
    dv->setContentsMargins(4, 4, 4, 4);
    dv->addWidget(diagTable_);

    auto* mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(diagBox);
    mainSplit->setStretchFactor(0, 3);
    mainSplit->setStretchFactor(1, 2);
    setCentralWidget(mainSplit);

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
    // .mpls 文件单独分流到 MPLS 面板（阶段 3），其余走字幕装载
    QList<QUrl> subtitleUrls;
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString local = url.toLocalFile();
        if (local.endsWith(QStringLiteral(".mpls"), Qt::CaseInsensitive)) {
            QString err;
            if (!session_->loadMplsFile(local, &err))
                QMessageBox::warning(this, tr("MPLS 解析失败"),
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
        tr("文件 %1 个 ｜ ● 错误 %2  ⚠ 警告 %3  ℹ 提示 %4  ｜ 构建 %5")
            .arg(session_->count()).arg(err).arg(warn).arg(info)
            .arg(QString::fromLatin1(kBuildStamp)));
}

void MainWindow::openBreakpointDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("分段校正（时间点累积）"));
    dlg.resize(420, 340);
    auto* lay = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(tr(
        "从每个时间点起，其后所有字幕平移该增量（逐点累加，可负）。\n"
        "时间点已按各集字幕结束预填；填入增量后下方时间点自动重算。\n"
        "也可改时间点或添加任意时刻（如某集 OP/ED 起点）。删行 = 清空该行增量。"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* table = new QTableWidget;
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("时间点"), tr("增量(秒)")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    lay->addWidget(table);

    auto* addRowBtn = new QPushButton(tr("添加时间点"));
    lay->addWidget(addRowBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [&dlg, table, this] {
        // 逐行解析：时间点宽容解析（0:23:41.69 / 23:41.69 / 秒数均可）
        QList<BreakpointShift> bps;
        for (int r = 0; r < table->rowCount(); ++r) {
            const auto* timeItem = table->item(r, 0);
            auto* spin = qobject_cast<QDoubleSpinBox*>(table->cellWidget(r, 1));
            if (!timeItem || !spin)
                continue;
            const auto cs = parseTimeToCs(timeItem->text());
            if (!cs.has_value()) {
                QMessageBox::warning(&dlg, tr("分段校正"),
                                     tr("第 %1 行的时间点无法解析（支持"
                                        " 0:23:41.69 / 23:41.69 / 纯秒数）")
                                         .arg(r + 1));
                return;
            }
            if (std::llround(spin->value() * 100.0) == 0)
                continue;   // 零增量行不保存
            BreakpointShift b;
            b.at = *cs;
            b.delta = Cs(std::llround(spin->value() * 100.0));
            bps.append(b);
        }
        session_->setBreakpointShifts(bps);
        dlg.accept();
    });

    // 行构造：建议行 = 各集边界（基础坐标 + 上方已填增量实时重算）；
    // 重开对话框时恢复已保存的增量（按行序对应，超出的建议行增量 0）
    const QList<Cs> bases = session_->episodeBoundaries();
    const QList<BreakpointShift> saved = session_->breakpointShifts();
    const int rows = std::max(int(bases.size()), int(saved.size())) + 0;
    QVector<bool> manual(rows, false);   // 用户改过的时间点不再自动重算
    bool updating = false;               // 程序写单元格时不记为 manual

    const auto recomputeSuggested = [table, &bases, &manual, &updating]() {
        if (updating || bases.isEmpty())
            return;
        updating = true;
        Cs cum = 0;
        for (int r = 0; r < int(bases.size()); ++r) {
            if (!manual[r]) {
                auto* it = table->item(r, 0);
                if (it)
                    it->setText(formatCs(bases[r] + cum));
            }
            if (const auto* spin = qobject_cast<QDoubleSpinBox*>(table->cellWidget(r, 1)))
                cum += Cs(std::llround(spin->value() * 100.0));
        }
        updating = false;
    };

    for (int r = 0; r < rows; ++r) {
        auto* timeItem = new QTableWidgetItem;
        timeItem->setText(r < int(bases.size()) ? formatCs(bases[r]) : QString());
        table->setItem(r, 0, timeItem);
        auto* spin = new QDoubleSpinBox;
        spin->setRange(-36000.0, 36000.0);
        spin->setDecimals(2);
        if (r < int(saved.size()))
            spin->setValue(double(saved[r].delta) / 100.0);
        table->setCellWidget(r, 1, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [recomputeSuggested](double) { recomputeSuggested(); });
    }
    connect(table, &QTableWidget::itemChanged, this, [&manual, &updating](QTableWidgetItem* it) {
        if (!updating && it && it->column() == 0)
            manual[it->row()] = true;   // 用户改过的时间点不再自动重算
    });
    connect(addRowBtn, &QPushButton::clicked, this, [table, &dlg]() {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem);
        auto* spin = new QDoubleSpinBox;
        spin->setRange(-36000.0, 36000.0);
        spin->setDecimals(2);
        table->setCellWidget(r, 1, spin);
        table->setCurrentCell(r, 0);
        Q_UNUSED(dlg)
    });

    dlg.exec();
}

void MainWindow::openCalibrateDialog()
{
    if (session_->count() == 0) {
        QMessageBox::information(this, tr("实测校准"), tr("请先拖入字幕文件。"));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("实测校准"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(tr(
        "在播放器中找到某集第一句台词实际出现的时刻，填到下面——\n"
        "工具会算出该集偏移 = 实测时刻 − 该集字幕首行时刻。"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* form = new QFormLayout;
    auto* epCombo = new QComboBox;
    for (int i = 0; i < session_->count(); ++i)
        epCombo->addItem(tr("第 %1 集").arg(i + 1));
    auto* timeEdit = new QLineEdit;
    timeEdit->setPlaceholderText(tr("如 0:23:46.00"));
    form->addRow(tr("校准哪一集"), epCombo);
    form->addRow(tr("实测时刻"), timeEdit);
    lay->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [&dlg, epCombo, timeEdit, this] {
        const auto cs = parseTimeToCs(timeEdit->text());
        if (!cs.has_value()) {
            QMessageBox::warning(&dlg, tr("实测校准"),
                                 tr("时刻无法解析（支持 0:23:46.00 / 23:46 / 纯秒数）"));
            return;
        }
        const int row = epCombo->currentIndex();
        const Cs off = session_->calibrateToObserved(row, *cs);
        statusBar()->showMessage(
            tr("第 %1 集偏移已设为 %2（实测 %3 − 首行 %4）")
                .arg(row + 1).arg(formatCs(off)).arg(formatCs(*cs))
                .arg(formatCs(session_->firstContentStart(row))), 8000);
        dlg.accept();
    });

    dlg.exec();
}

}  // namespace suture
