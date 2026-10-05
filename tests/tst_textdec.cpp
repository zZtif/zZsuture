// tst_textdec.cpp —— P1：编码探测（BOM/UTF-8 校验/UTF-16 奇偶/GBK-Big5 评分）
#include <QtTest>

#include "textdec.hpp"

#define NOMINMAX
#include <windows.h>

using namespace suture;

namespace {
// 用 Win32 造 GBK 字节（测试样本构造，避免依赖系统本地代码页）
QByteArray toGbk(const QString& s)
{
    const int len = WideCharToMultiByte(936, 0, (const wchar_t*)s.utf16(),
                                        s.size(), nullptr, 0, nullptr, nullptr);
    QByteArray out(len, 0);
    WideCharToMultiByte(936, 0, (const wchar_t*)s.utf16(), s.size(),
                        out.data(), len, nullptr, nullptr);
    return out;
}
QByteArray toBig5(const QString& s)
{
    const int len = WideCharToMultiByte(950, 0, (const wchar_t*)s.utf16(),
                                        s.size(), nullptr, 0, nullptr, nullptr);
    QByteArray out(len, 0);
    WideCharToMultiByte(950, 0, (const wchar_t*)s.utf16(), s.size(),
                        out.data(), len, nullptr, nullptr);
    return out;
}
}  // namespace

class TestTextDec : public QObject {
    Q_OBJECT
private slots:
    void bomUtf8();
    void bomUtf16LE();      // G2 路径
    void bomUtf16BE();
    void noBomUtf8();
    void noBomUtf16LE();    // v1.1 补：BOM 丢失奇偶探测
    void gbk();
    void big5();
    void ambiguous();       // GBK/Big5 均合法 → 歧义告警
    void newlineNormalize();
};

void TestTextDec::bomUtf8()
{
    const QString text = QStringLiteral("[Script Info]\n中文测试");
    QByteArray raw("\xEF\xBB\xBF", 3);
    raw += text.toUtf8();
    const DecodeResult r = decodeToQString(raw);
    QCOMPARE(r.text, text);
    QVERIFY(r.certain);
    QVERIFY(r.encodingName.contains(QStringLiteral("UTF-8")));
}

void TestTextDec::bomUtf16LE()
{
    const QString text = QStringLiteral("[Script Info]\n死文");
    QByteArray raw("\xFF\xFE", 2);
    raw += QByteArray(reinterpret_cast<const char*>(text.utf16()),
                      text.size() * 2);
    const DecodeResult r = decodeToQString(raw);
    QCOMPARE(r.text, text);
    QVERIFY(r.encodingName.contains(QStringLiteral("UTF-16 LE")));
}

void TestTextDec::bomUtf16BE()
{
    const QString text = QStringLiteral("[Script Info]\n測試");
    QByteArray raw("\xFE\xFF", 2);
    // 手工大端化
    const char16_t* u = reinterpret_cast<const char16_t*>(text.utf16());
    for (int i = 0; i < text.size(); ++i) {
        raw += char(quint16(u[i]) >> 8);
        raw += char(quint16(u[i]) & 0xFF);
    }
    const DecodeResult r = decodeToQString(raw);
    QCOMPARE(r.text, text);
    QVERIFY(r.encodingName.contains(QStringLiteral("UTF-16 BE")));
}

void TestTextDec::noBomUtf8()
{
    const QString text = QStringLiteral("Dialogue 中文");
    const DecodeResult r = decodeToQString(text.toUtf8());
    QCOMPARE(r.text, text);
    // 含多字节序列的合法 UTF-8 是结构性证明 → 确定
    QVERIFY(r.certain);
    QVERIFY(r.diags.empty());
}

void TestTextDec::noBomUtf16LE()
{
    // 无 BOM 的 UTF-16 LE（ASCII 为主 → 偶数位零字节占比高）
    const QString text = QStringLiteral("[Script Info]\nTitle: test");
    QByteArray raw(reinterpret_cast<const char*>(text.utf16()),
                   text.size() * 2);
    const DecodeResult r = decodeToQString(raw);
    QCOMPARE(r.text, text);
    QVERIFY(r.encodingName.contains(QStringLiteral("UTF-16 LE")));
    QVERIFY(!r.diags.empty());   // 奇偶判定 → EncodingHeuristic 告警
}

void TestTextDec::gbk()
{
    const QString text = QStringLiteral("简体中文内容测试");
    const DecodeResult r = decodeToQString(toGbk(text));
    QCOMPARE(r.text, text);
    QVERIFY(r.encodingName.contains(QStringLiteral("GBK")));
}

void TestTextDec::big5()
{
    // Big5 与 GBK 结构域几乎重叠（CP950 双字节区间与 GBK 相同），纯结构校验
    // 无法可靠区分——这是 P1 文档化的已知限制。测试验证的是"文档化行为"：
    // 解码不崩溃、有非空结果；若判为 Big5 则文本正确；若歧义默认 GBK 则必带
    // 不确定告警（UI 可手动覆盖为 Big5）。
    const QString text = QStringLiteral("繁體字測試");
    const QByteArray big5 = toBig5(text);
    const DecodeResult r = decodeToQString(big5);
    QVERIFY(!r.text.isEmpty());
    if (r.text == text) {
        QVERIFY(r.encodingName.contains(QStringLiteral("Big5")));
    } else {
        QVERIFY(!r.certain);
        bool hasWarn = false;
        for (const auto& d : r.diags)
            if (d.code == DiagCode::EncodingUncertain)
                hasWarn = true;
        QVERIFY(hasWarn);
    }
}

void TestTextDec::ambiguous()
{
    // 高位字节的中文（GBK 与 Big5 均可无错解码）→ 歧义告警 + 默认 GBK。
    // 注意纯 ASCII 到不了 GBK 分支（合法 UTF-8 优先），必须用双字节内容。
    const QString text = QStringLiteral("中文标题测试");
    const DecodeResult r = decodeToQString(toGbk(text));
    QCOMPARE(r.text, text);          // 默认按 GBK → 文本正确
    QVERIFY(!r.certain);
    bool hasWarn = false;
    for (const auto& d : r.diags)
        if (d.code == DiagCode::EncodingUncertain)
            hasWarn = true;
    QVERIFY(hasWarn);
}

void TestTextDec::newlineNormalize()
{
    // CRLF / 裸 CR / 尾部空行 → 统一 \n、去尾
    QByteArray raw = "[Script Info]\r\nTitle: x\r\n\r\n\r\n";
    const DecodeResult r = decodeToQString(raw);
    QCOMPARE(r.text, QStringLiteral("[Script Info]\nTitle: x"));
}

QTEST_MAIN(TestTextDec)
#include "tst_textdec.moc"
