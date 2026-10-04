// MainWindow —— 主窗口（阶段 0：暗色骨架；阶段 2 起填充主流程控件）
#pragma once

#include <QtWidgets/QMainWindow>

namespace suture {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();   // 阶段 0 仅占位中央区
};

}  // namespace suture
