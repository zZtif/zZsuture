#include "textdec.hpp"

#include <QList>
#include <QStringDecoder>
#include <QStringView>

#include <algorithm>

// GBK/Big5 转换用 Win32 MultiByteToWideChar（决策 D5：零新依赖，Windows-only 已冻结）
#define NOMINMAX
#include <windows.h>

namespace suture {
namespace {

// ---- 严格 UTF-8 校验（拒绝过长编码、代理区、截断序列）----
// Qt 自带转换器遇到非法字节会静默替换，不符合"判定要确定"的要求，自写校验。
bool isValidUtf8(const QByteArray& raw)
{
    const auto* p = reinterpret_cast<const uchar*>(raw.constData());
    const auto* end = p + raw.size();
    while (p < end) {
        const uchar c = *p;
        if (c < 0x80) {                       // ASCII
            ++p;
            continue;
        }
        int follow = 0;                       // 后续字节数
        uint32_t cp = 0;                      // 码点（用于拒绝过长编码）
        if ((c & 0xE0) == 0xC0)      { follow = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { follow = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { follow = 3; cp = c & 0x07; }
        else return false;                    // 0x80-0xBF 开头 / 0xF8+ 非法
        if (end - p < follow + 1)
            return false;                     // 截断
        for (int i = 1; i <= follow; ++i) {
            const uchar cc = p[i];
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // 拒绝过长编码与代理区
        if ((follow == 1 && cp < 0x80) || (follow == 2 && cp < 0x800) ||
            (follow == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        p += follow + 1;
    }
    return true;
}

// ---- 零字节奇偶探测（BOM 丢失的 UTF-16）----
// 纯 ASCII 的 UTF-16 文本每隔一个字节一个 0x00；CJK 混排的标签/换行也贡献大量 0。
// 阈值 30%：CJK 密集文本会偏低，但配合"判定即告警"兜底（EncodingHeuristic）。
enum class Utf16Parity { None, EvenZero, OddZero };
Utf16Parity sniffUtf16Parity(const QByteArray& raw)
{
    if (raw.size() < 4)
        return Utf16Parity::None;
    const int n = raw.size() - (raw.size() % 2);   // 只看完整码元对
    int evenZero = 0, oddZero = 0;
    for (int i = 0; i < n; ++i) {
        if (raw[i] == 0x00)
            (i % 2 == 0) ? ++evenZero : ++oddZero;
    }
    const int threshold = n / 2 * 3 / 10;          // 码元数的 30%
    if (oddZero > threshold && evenZero < threshold)
        return Utf16Parity::EvenZero;   // 高字节为 0 → LE
    if (evenZero > threshold && oddZero < threshold)
        return Utf16Parity::OddZero;    // 低字节为 0 → BE
    return Utf16Parity::None;
}

// ---- Win32 代码页解码，返回错误字符计数（<0 = 代码页不可用）----
int decodeCodePage(const QByteArray& raw, uint32_t codePage, QString* out)
{
    const int wlen = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                         raw.constData(), raw.size(),
                                         nullptr, 0);
    if (wlen <= 0) {
        const DWORD err = GetLastError();
        if (err == ERROR_NO_UNICODE_TRANSLATION)
            return -1;              // 有非法字节 → 换个算法重试（见下）
        out->clear();
        return 0;                   // 空输入或系统错误，按空处理
    }
    out->resize(wlen);
    MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                        raw.constData(), raw.size(),
                        reinterpret_cast<wchar_t*>(out->data()), wlen);
    return 0;
}

// 评分解码：先整体严格解码（合法文件零误差）；失败才逐 4KB 块容错解码计数。
// 注意：分块边界可能切断双字节字符，仅在"已确认有坏字节"的数据上才走这条路，
// 合法文件不会进入分块路径，不会被误判。
struct ScoredDecode { QString text; int badBytes = 0; };
ScoredDecode decodeCodePageScored(const QByteArray& raw, uint32_t codePage)
{
    ScoredDecode r;
    QString whole;
    if (decodeCodePage(raw, codePage, &whole) == 0) {
        r.text = whole;   // 整体严格解码通过（或输入为空）
        return r;
    }
    constexpr int kChunk = 4096;
    QString chunk;
    chunk.reserve(kChunk);
    for (int off = 0; off < raw.size(); off += kChunk) {
        const QByteArray view = raw.mid(off, kChunk);
        if (decodeCodePage(view, codePage, &chunk) < 0) {
            r.badBytes += view.size();   // 该块全坏（计入字节数作评分权重）
        } else {
            r.text += chunk;
        }
    }
    return r;
}

// ---- 工具：换行归一 + 去尾部空白 ----
QString normalizeNewlines(QString s)
{
    s.replace(QStringLiteral(u"\r\n"), QStringLiteral(u"\n"));
    s.replace(u'\r', u'\n');
    while (s.endsWith(u'\n'))
        s.chop(1);
    return s;
}

// 用 QStringConverter 解 UTF-16（QtCore；带 hasError 检查）
bool decodeUtf16(const QByteArray& raw, QStringConverter::Encoding enc, QString* out)
{
    QStringDecoder decoder(enc);
    *out = decoder(raw);   // 调用运算符整体解码（返回 QString）
    return !decoder.hasError();
}

}  // namespace

DecodeResult decodeToQString(const QByteArray& raw)
{
    DecodeResult r;

    // 1. BOM 优先嗅探
    if (raw.size() >= 3 && (uchar)raw[0] == 0xEF && (uchar)raw[1] == 0xBB &&
        (uchar)raw[2] == 0xBF) {
        r.text = QString::fromUtf8(raw.mid(3));
        r.encodingName = QStringLiteral("UTF-8 (BOM)");
        r.text = normalizeNewlines(r.text);
        return r;
    }
    if (raw.size() >= 2 && (uchar)raw[0] == 0xFF && (uchar)raw[1] == 0xFE) {
        if (decodeUtf16(raw.mid(2), QStringConverter::Utf16LE, &r.text)) {
            r.encodingName = QStringLiteral("UTF-16 LE (BOM)");
            r.text = normalizeNewlines(r.text);
            return r;
        }
    }
    if (raw.size() >= 2 && (uchar)raw[0] == 0xFE && (uchar)raw[1] == 0xFF) {
        if (decodeUtf16(raw.mid(2), QStringConverter::Utf16BE, &r.text)) {
            r.encodingName = QStringLiteral("UTF-16 BE (BOM)");
            r.text = normalizeNewlines(r.text);
            return r;
        }
    }

    // 2. 无 BOM：UTF-16 零字节奇偶探测（v1.1 补：BOM 丢失的老文件）
    switch (sniffUtf16Parity(raw)) {
    case Utf16Parity::EvenZero:
        if (decodeUtf16(raw, QStringConverter::Utf16LE, &r.text)) {
            r.encodingName = QStringLiteral("UTF-16 LE（无 BOM 启发式）");
            r.certain = false;
            r.diags.push_back(Diagnostic::make(
                DiagLevel::Info, DiagCode::EncodingHeuristic,
                QStringLiteral("无 BOM，按零字节奇偶性判定为 UTF-16 LE")));
            r.text = normalizeNewlines(r.text);
            return r;
        }
        break;
    case Utf16Parity::OddZero:
        if (decodeUtf16(raw, QStringConverter::Utf16BE, &r.text)) {
            r.encodingName = QStringLiteral("UTF-16 BE（无 BOM 启发式）");
            r.certain = false;
            r.diags.push_back(Diagnostic::make(
                DiagLevel::Info, DiagCode::EncodingHeuristic,
                QStringLiteral("无 BOM，按零字节奇偶性判定为 UTF-16 BE")));
            r.text = normalizeNewlines(r.text);
            return r;
        }
        break;
    default:
        break;
    }

    // 3. 严格 UTF-8 校验（无 BOM 但合法 UTF-8）
    if (isValidUtf8(raw)) {
        r.text = normalizeNewlines(QString::fromUtf8(raw));
        r.encodingName = QStringLiteral("UTF-8（无 BOM）");
        // 含多字节序列 → 结构性证明是 UTF-8（GBK 中文几乎不可能碰巧是合法 UTF-8）
        // 纯 ASCII → 与单字节 ANSI 编码不可区分（显示无差别），标记不确定
        const bool hasMultibyte =
            std::any_of(raw.cbegin(), raw.cend(),
                        [](char c) { return (uchar)c >= 0x80; });
        r.certain = hasMultibyte;
        if (!hasMultibyte)
            r.diags.push_back(Diagnostic::make(
                DiagLevel::Info, DiagCode::EncodingHeuristic,
                QStringLiteral("无 BOM 且纯 ASCII，按 UTF-8 处理"
                               "（与 ANSI 单字节编码不可区分，显示无影响）")));
        return r;
    }

    // 4. GBK / Big5 评分
    const ScoredDecode gbk  = decodeCodePageScored(raw, 936);
    const ScoredDecode big5 = decodeCodePageScored(raw, 950);
    if (gbk.badBytes == 0 && big5.badBytes == 0) {
        // 两者都合法 → 真歧义（GBK/Big5 中日文文本互判是老问题）
        r.text = normalizeNewlines(gbk.text);
        r.encodingName = QStringLiteral("GBK（与 Big5 歧义，默认 GBK）");
        r.certain = false;
        r.diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::EncodingUncertain,
            QStringLiteral("无 BOM 且 GBK/Big5 均可解码，已按 GBK 处理，"
                           "若乱码请手动指定编码")));
        return r;
    }
    if (gbk.badBytes <= big5.badBytes) {
        r.text = normalizeNewlines(gbk.text);
        r.encodingName = (gbk.badBytes == 0)
            ? QStringLiteral("GBK")
            : QStringLiteral("GBK（含非法字节）");
        if (gbk.badBytes > 0) {
            r.certain = false;
            r.diags.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::EncodingUncertain,
                QStringLiteral("GBK 解码仍有 %1 个非法字节，文本可能损坏")
                    .arg(gbk.badBytes)));
        }
        return r;
    }
    r.text = normalizeNewlines(big5.text);
    r.encodingName = QStringLiteral("Big5");
    return r;
}

}  // namespace suture
