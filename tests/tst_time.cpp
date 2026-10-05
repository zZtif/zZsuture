// tst_time.cpp —— T4：厘秒整数时间解析/格式化/45kHz 换算
#include <QtTest>

#include "time.hpp"

using namespace suture;

class TestTime : public QObject {
    Q_OBJECT
private slots:
    void parse_data();
    void parse();
    void format();
    void carryOverHour();     // 进位/跨小时
    void ticks45k();
    void clamping();
};

void TestTime::parse_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<Cs>("expected");

    QTest::newRow("标准")        << "0:24:05.40" << Cs(24 * 60 * 100 + 5 * 100 + 40);
    QTest::newRow("整秒无小数")  << "0:00:05"    << Cs(500);
    QTest::newRow("逗号小数")    << "0:00:01,50" << Cs(150);
    QTest::newRow("两位小时")    << "1:02:03.04" << Cs(3723 * 100 + 4);
    QTest::newRow("杂字符容错")  << " 0:00:07.89 " << Cs(789);
    QTest::newRow("零")          << "0:00:00.00" << Cs(0);
}

void TestTime::parse()
{
    QFETCH(QString, input);
    QFETCH(Cs, expected);
    const auto got = parseTimeToCs(input);
    QVERIFY2(got.has_value(), qPrintable(QStringLiteral("无法解析: %1").arg(input)));
    QCOMPARE(*got, expected);
}

void TestTime::format()
{
    QCOMPARE(formatCs(0), QStringLiteral("0:00:00.00"));
    QCOMPARE(formatCs(150), QStringLiteral("0:00:01.50"));
    QCOMPARE(formatCs(24 * 60 * 100 + 5 * 100 + 40), QStringLiteral("0:24:05.40"));
    // 语法性往返：任意值 解析(格式化(v)) == v
    QCOMPARE(*parseTimeToCs(formatCs(123456)), Cs(123456));
}

void TestTime::carryOverHour()
{
    // 跨小时进位：59 分 59.99 秒 = 354000 + 5900 + 99 = 359999
    QCOMPARE(*parseTimeToCs(QStringLiteral("0:59:59.99")),
             Cs(59 * 6000 + 59 * 100 + 99));
    QCOMPARE(formatCs(360000 - 1), QStringLiteral("0:59:59.99"));
    QCOMPARE(formatCs(360000), QStringLiteral("1:00:00.00"));
    QCOMPARE(formatCs(10 * 360000 - 1), QStringLiteral("9:59:59.99"));
}

void TestTime::ticks45k()
{
    QCOMPARE(ticks45kToCs(0), Cs(0));
    QCOMPARE(ticks45kToCs(450), Cs(1));       // 1 cs = 450 tick
    QCOMPARE(ticks45kToCs(225), Cs(1));       // 四舍五入 .5 → 上取
    QCOMPARE(ticks45kToCs(224), Cs(0));
    QCOMPARE(ticks45kToCs(45000), Cs(100));   // 1 秒
    QCOMPARE(ticks45kToCs(45000ull * 60 * 24), Cs(6000 * 24));  // 24 分钟一集
}

void TestTime::clamping()
{
    // 超过 10 小时钳制（与 Aegisub 一致）；负值非法钳 0
    QCOMPARE(*parseTimeToCs(QStringLiteral("99:00:00.00")), kMaxCs);
    // 负号被忽略（同 Aegisub：非数字字符 continue），"-0:00:01.00" ≡ "0:00:01.00"
    QCOMPARE(*parseTimeToCs(QStringLiteral("-0:00:01.00")), Cs(100));
    // 无任何数字 → nullopt
    QVERIFY(!parseTimeToCs(QStringLiteral("abc")).has_value());
    QVERIFY(!parseTimeToCs(QString()).has_value());
}

QTEST_MAIN(TestTime)
#include "tst_time.moc"
