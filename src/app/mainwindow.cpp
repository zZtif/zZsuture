#include "mainwindow.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

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
    setWindowTitle(tr("SUture — BDMV 字幕合并"));
    resize(1080, 680);
    setAcceptDrops(true);

    // ---- 上半部：文件表 + 右侧控制面板 ----
    fileTable_ = new QTableView;
    fileTable_->setModel(fileModel_);
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTable_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                QAbstractItemView::SelectedClicked |
                                QAbstractItemView::EditKeyPressed);
    fileTable_->setDragEnabled(true);
    fileTable_->setAcceptDrops(true);
    fileTable_->setDragDropMode(QAbstractItemView::InternalMove);
    fileTable_->setDefaultDropAction(Qt::MoveAction);
    fileTable_->setDropIndicatorShown(true);
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
                               "双击偏移列可修改每集起点"));
    hint->setWordWrap(true);
    pv->addWidget(hint);

    // MPLS 占位（阶段 3 上线，先占住布局位置）
    auto* mplsBox = new QGroupBox(tr("MPLS 自动偏移（阶段 3）"));
    auto* mv = new QVBoxLayout(mplsBox);
    auto* mplsHint = new QLabel(tr("原盘播放列表解析待阶段 3 上线，\n"
                                   "当前使用预填偏移（上集结束时刻）"));
    mplsHint->setWordWrap(true);
    mv->addWidget(mplsHint);
    pv->addWidget(mplsBox);

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
    statusBar()->addWidget(statusLabel_);
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
    session_->addUrls(e->mimeData()->urls());
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
        tr("文件 %1 个 ｜ ● 错误 %2  ⚠ 警告 %3  ℹ 提示 %4")
            .arg(session_->count()).arg(err).arg(warn).arg(info));
}

}  // namespace suture
