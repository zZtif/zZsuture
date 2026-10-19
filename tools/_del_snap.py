# -*- coding: utf-8 -*-
# 一次性补丁：timelineview 删除吸附全链路（按钮/候选/指示/逻辑）+
# 加"恢复最初状态"按钮 + 右键"显示台词时间"替换悬停 + 删 hover tooltip。
# 用完即删。

p = 'src/app/timelineview.cpp'
s = open(p, encoding='utf-8').read()

def rep(old, new, required=True):
    global s
    if old not in s:
        if required:
            print('MISS:', old[:60]); raise SystemExit(1)
        return
    s = s.replace(old, new)

def cut(a, b):
    global s
    i = s.find(a); j = s.find(b)
    assert 0 < i < j, 'CUT MISS: ' + a[:50]
    s = s[:i] + s[j:]

# ---- 2.5 删吸附 ----
rep('''constexpr Cs kSnapTolCs = 30;                   // 吸附容差 0.3 秒（决定 28）
''', '')

rep('''    auto* magnet = new QPushButton(tr("吸附"));
    magnet->setCheckable(true);
    magnet->setChecked(snapEnabled_);
    connect(magnet, &QPushButton::toggled, this,
            [this](bool on) { snapEnabled_ = on; });
''', '')

cut('// ---------------------------------------------------------------- 吸附',
    'int TimelineView::hitBlock(const QPoint& pos) const')

rep('''        // 吸附在首句标记处（验收反馈 3）：字幕开头不在块左端，块左缘吸附
        // 实际无用——白三角（首句）对齐目标才有意义。吸附目标也改为其它
        // 块的首句位置（snapCandidate 内部处理）
        const int dr = dragRows_.isEmpty() ? -1 : dragRows_.first();
        const Cs firstOff = dr >= 0 ? rows_[dr].contentFirst : Cs(0);
        if (snapEnabled_ && !(e->modifiers() & Qt::AltModifier)) {
            const Cs hit = snapCandidate(raw + firstOff, dr);
            if (hit >= 0)
                target = hit - firstOff;
        }
        if (target < 0)
            target = 0;
        snapTarget_ = (target != raw) ? target + firstOff : Cs(-1);
        previewStart_ = target;''',
'''        // 吸附已删除（整改文档 2.5：实测没用）——拖动就是裸平移，仅受
        // x=0 硬墙约束
        previewStart_ = target;''')

rep('''    // 吸附指示（决定 28：看得见的吸附才不是误操作）
    if (drag_ != Drag::None && snapTarget_ >= 0) {
        const int x = timeToX(snapTarget_);
        p.setPen(QPen(QColor(0xff, 0xd1, 0x64), 2));
        p.drawLine(x, rulerBottom(), x, readoutTop());
        p.drawText(QRect(x + 4, trackTop(), 200, 14), Qt::AlignLeft,
                   tr("吸附 %1").arg(formatCs(snapTarget_)));
    }

''', '')

# 拖动释放/取消里的 snapTarget_ 清理行（保留其余）
s = s.replace('''    drag_ = Drag::None;
    dragRows_.clear();
    previewStart_ = -1;
    snapTarget_ = -1;
    rubber_ = QRect();
    update();''',
'''    drag_ = Drag::None;
    dragRows_.clear();
    previewStart_ = -1;
    rubber_ = QRect();
    update();''')
s = s.replace('''    drag_ = Drag::None;
    previewStart_ = -1;
    snapTarget_ = -1;
    update();''',
'''    drag_ = Drag::None;
    previewStart_ = -1;
    update();''')

# ---- 2.2 删悬停 tooltip，右键"显示台词时间" ----
rep('''        const int hr = hitBlock(e->pos());
        if (hr != hoverRow_) {
            hoverRow_ = hr;
            if (hr < 0) {
                setToolTip(QString());
            } else {
                const RowInfo& ri = rows_[hr];
                const Cs base = rowFinalStart(hr);
                setToolTip(tr("第%1集\\n"
                              "首句 %2 起「%3」\\n"
                              "末句 %4 止「%5」")
                               .arg(rows_[hr].epNumber >= 0
                                        ? rows_[hr].epNumber : hr + 1)
                               .arg(formatCs(base + ri.contentFirst),
                                    ri.firstText,
                                    formatCs(base + ri.maxEnd + ri.totalWarp),
                                    ri.lastText));
            }
        }
        update();   // 读数条光标时刻
        return;''',
'''        update();   // 读数条光标时刻
        return;''')

# 右键菜单加"显示台词时间"（验收反馈 2）
rep('''    QMenu menu(this);
    if (const int r = hitBlock(e->pos()); r >= 0) {
        menu.addAction(tr("首句移动到…"), this, [this, r] {
            selection_ = {r};
            openMoveEditor(r, QString());
        });
    } else {''',
'''    QMenu menu(this);
    if (const int r = hitBlock(e->pos()); r >= 0) {
        menu.addAction(tr("首句移动到…"), this, [this, r] {
            selection_ = {r};
            openMoveEditor(r, QString());
        });
        // 验收反馈 2：常显台词时间（悬停不稳定已删）——右键切换，画在
        // 轨道下方的信息条，实时同步块位置（含拖动预览）
        auto* act = menu.addAction(tr("显示台词时间"), this, [this, r] {
            scriptRow_ = (scriptRow_ == r) ? -1 : r;
            update();
        });
        act->setCheckable(true);
        act->setChecked(scriptRow_ == r);
    } else {''')

# drawTrack 末尾（框选矩形绘制之后）画常显信息条
rep('''    // 框选矩形
    if (drag_ == Drag::Rubber) {
        p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 180), 1, Qt::DashLine));
        p.setBrush(QColor(0x4f, 0x8c, 0xff, 25));
        p.drawRect(rubber_.normalized());
    }
    p.restore();''',
'''    // 框选矩形
    if (drag_ == Drag::Rubber) {
        p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 180), 1, Qt::DashLine));
        p.setBrush(QColor(0x4f, 0x8c, 0xff, 25));
        p.drawRect(rubber_.normalized());
    }

    // 常显台词信息条（验收反馈 2/3）：画在轨道下方空间；时刻从当前块
    // 位置实时换算（rowFinalStart 预览感知——拖动/键盘/涟漪/撤销全路径同步）
    if (scriptRow_ >= 0 && scriptRow_ < rows_.size()) {
        const RowInfo& ri = rows_[scriptRow_];
        const Cs base = rowFinalStart(scriptRow_);
        const int y0 = pointsY() + 10;
        p.setPen(QPen(QColor(0x33, 0x36, 0x3f), 1));
        p.setBrush(QColor(0x22, 0x24, 0x2b));
        p.drawRect(margin_(), y0, width() - 2 * margin_(), 34);
        p.setPen(QColor(0xdd, 0xe0, 0xe8));
        p.drawText(QRect(margin_() + 10, y0 + 2, width() - 2 * margin_() - 20, 15),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   tr("第%1集  首句 %2 起「%3」")
                       .arg(ri.epNumber >= 0 ? ri.epNumber : scriptRow_ + 1)
                       .arg(formatCs(base + ri.contentFirst), ri.firstText));
        p.drawText(QRect(margin_() + 10, y0 + 18, width() - 2 * margin_() - 20, 15),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   tr("末句 %1 止「%2」")
                       .arg(formatCs(base + ri.maxEnd + ri.totalWarp), ri.lastText));
    }
    p.restore();''')

# 辅助：margin_ 不是成员，用固定 8。修正上面的 margin_() 调用
s = s.replace('margin_()', '8')

# ---- 2.1 恢复最初状态按钮进工具条 ----
rep('''    auto* fit = new QPushButton(tr("复位视野"));''',
'''    auto* reset = new QPushButton(tr("恢复最初状态"));
    reset->setToolTip(tr("整理打乱后一键回到刚导入时的排布（可 Ctrl+Z 撤销）"));
    connect(reset, &QPushButton::clicked, this,
            [this] { s_->resetToInitial(); });
    auto* fit = new QPushButton(tr("复位视野"));''')
rep('''    lay->addWidget(magnet);
    lay->addStretch(1);
    lay->addWidget(zoomOut);
    lay->addWidget(zoomIn);
    lay->addWidget(fit);
    lay->addWidget(help);''',
'''    lay->addWidget(reset);
    lay->addStretch(1);
    lay->addWidget(zoomOut);
    lay->addWidget(zoomIn);
    lay->addWidget(fit);
    lay->addWidget(help);''')

# 帮助文本同步（删吸附行——若有）
s = s.replace('        块下方或标尺按住左键拖动 / 中键拖动 = 平移视口<br>"\\n',
              '        块下方或标尺按住左键拖动 / 中键拖动 = 平移视口<br>"\\n')

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('snap refs left:', s.count('snapEnabled_'), s.count('snapCandidate'),
      s.count('snapTarget_'), '| hoverRow_:', s.count('hoverRow_'))
