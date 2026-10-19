// MainWindow —— 主窗口（阶段 2：MVP 主闭环；MPLS 面板阶段 3 上线）
#pragma once

#include <QtWidgets/QMainWindow>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTableView;

namespace suture {

class DiagModel;
class FileListModel;
class FileTableView;
class Session;
class TimelineView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void buildUi();
    void exportMerged();
    void refreshDiags();
    void refreshStatus();

    Session*      session_;
    FileListModel* fileModel_;
    DiagModel*    diagModel_;
    FileTableView* fileTable_ = nullptr;
    QTableView*   diagTable_ = nullptr;
    TimelineView* timeline_ = nullptr;       // A2：时间轴主视图
    QTabWidget*   fileTabs_ = nullptr;       // A-r2：[文件] [原盘解析] 页签
    QLineEdit*    mplsPathEdit_ = nullptr;  // 解析页路径栏（拖入时同步显示）
    class DiscAxisView* discAxis_ = nullptr; // A-r2：解析页纯数轴
    QLabel*       statusLabel_ = nullptr;
};

}  // namespace suture
