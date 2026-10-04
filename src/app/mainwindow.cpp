#include "mainwindow.hpp"

#include <QLabel>
#include <QStatusBar>

#include "version.hpp"

namespace suture {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("SUture — BDMV 字幕合并"));
    resize(960, 600);

    // 阶段 0 占位：中央拖放提示区（阶段 2 替换为 文件列表 + 偏移面板 + 告警区）
    auto* hint = new QLabel(
        tr("拖入字幕文件 / 文件夹\n（阶段 2 上线拖放与合并功能）"), this);
    hint->setObjectName(QStringLiteral("dropHint"));
    hint->setAlignment(Qt::AlignCenter);
    setCentralWidget(hint);

    statusBar()->showMessage(tr("骨架版 v%1").arg(QLatin1StringView(versionString())));
}

}  // namespace suture
