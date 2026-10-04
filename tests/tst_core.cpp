// tst_core —— suture-core 单元测试（阶段 0：框架空跑 + 版本常量）
// 数据驱动用例随阶段 1 加入（六组样例 → 期望输出/期望诊断码）。
#include <QtTest>

#include "version.hpp"

class TestCore : public QObject {
    Q_OBJECT
private slots:
    // 阶段 0 冒烟：库可链接、版本常量可读
    void versionSmoke();
};

void TestCore::versionSmoke()
{
    QVERIFY(strlen(suture::kVersionString) > 0);
    QCOMPARE(suture::versionString(), suture::kVersionString);
}

QTEST_MAIN(TestCore)
#include "tst_core.moc"
