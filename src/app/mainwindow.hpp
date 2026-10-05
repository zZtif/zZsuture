// MainWindow —— 主窗口（阶段 2：MVP 主闭环；MPLS 面板阶段 3 上线）
#pragma once

#include <QtWidgets/QMainWindow>

class QDoubleSpinBox;
class QLabel;
class QTableView;

namespace suture {

class DiagModel;
class FileListModel;
class FileTableView;
class Session;

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
    QLabel*       statusLabel_ = nullptr;
};

}  // namespace suture
