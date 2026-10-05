#include "time.hpp"

namespace suture {

std::optional<Cs> parseTimeToCs(QStringView s)
{
    Cs total   = 0;    // 已确认的整秒/整厘秒累计
    int current = 0;   // 当前正在累计的数字段
    int afterDecimal = -1;  // <0 = 还在小数点前；否则为当前小数位的厘秒权重

    bool sawDigit = false;
    for (const QChar c : s) {
        const char16_t ch = c.unicode();
        if (ch == u':') {
            total = total * 60 + current;
            current = 0;
        } else if (ch == u'.' || ch == u',') {
            total = (total * 60 + current) * 100;
            current = 0;
            afterDecimal = 10;   // 第一位小数 = 10 厘秒（0.1s）
        } else if (ch < u'0' || ch > u'9') {
            continue;            // 空格等杂字符直接忽略（容错）
        } else {
            sawDigit = true;
            if (afterDecimal < 0) {
                current = current * 10 + (ch - u'0');
            } else if (afterDecimal > 0) {
                total += static_cast<Cs>(ch - u'0') * afterDecimal;
                afterDecimal /= 10;   // 第三位及以后的小数忽略（超出厘秒精度）
            }
        }
    }

    if (!sawDigit)
        return std::nullopt;

    // 全程没遇到小数点 → 最后一段按整秒折算
    if (afterDecimal < 0)
        total = (total * 60 + current) * 100;

    // 钳制到合法区间（负值非法、上限 10 小时）
    if (total < 0)
        total = 0;
    if (total > kMaxCs)
        total = kMaxCs;
    return total;
}

QString formatCs(Cs v)
{
    if (v < 0)
        v = 0;
    if (v > kMaxCs)
        v = kMaxCs;
    const int cs    = static_cast<int>(v % 100);
    const int total = static_cast<int>(v / 100);
    const int sec   = total % 60;
    const int min   = (total / 60) % 60;
    const int hour  = total / 3600;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hour)
        .arg(min, 2, 10, QChar(u'0'))
        .arg(sec, 2, 10, QChar(u'0'))
        .arg(cs, 2, 10, QChar(u'0'));
}

}  // namespace suture
