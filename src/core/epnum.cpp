#include "epnum.hpp"

#include <QCollator>
#include <QLocale>
#include <QRegularExpression>

namespace suture {

EpGuess guessEpisodeNumber(const QString& fileName)
{
    // 模式 1：半角/全角括号内纯数字 1-3 位（[01] / 【01】 / [00] 特别篇）
    // 注意 \d 会匹配全角数字，配合全角括号场景；CRC32/分辨率段非纯数字自然排除
    static const QRegularExpression bracket(
        QStringLiteral("[\\[【](\\d{1,3})[\\]】]"));
    const auto m1 = bracket.match(fileName);
    if (m1.hasMatch())
        return {m1.captured(1).toInt(), true};

    // 模式 2：分隔符包围的独立数字段（G3 " - 01 [WebRip]"）
    // 边界集含空格 . _ ] 】 与行首尾；后随字母的数字（1080p、x264、AACx2）
    // 与后随 × 的分辨率（3840×2160）都被边界断言排除
    static const QRegularExpression standalone(
        QStringLiteral("(?:^|[\\s._\\-])(\\d{1,3})(?=$|[\\s._\\]\\}】])"));
    const auto m2 = standalone.match(fileName);
    if (m2.hasMatch())
        return {m2.captured(1).toInt(), true};

    return {-1, false};
}

bool naturalLessThan(const QString& a, const QString& b)
{
    static QCollator collator(QLocale::system());
    collator.setNumericMode(true);          // "2" < "10"（而非字典序 "10"<"2"）
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(a, b) < 0;
}

}  // namespace suture
