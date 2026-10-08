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

EpGuess guessEpisodeFromContent(const AssDocument& doc)
{
    // 三种集数模式（P12）。第X話/话 全角数字兼容（\d 匹配全角）。
    static const QRegularExpression pats[] = {
        QRegularExpression{QStringLiteral("第\\s*(\\d{1,3})\\s*[話话]")},
        QRegularExpression{QStringLiteral("Episode\\s*#?\\s*(\\d{1,3})"),
                           QRegularExpression::CaseInsensitiveOption},
        QRegularExpression{QStringLiteral("\\bEP\\s*0*(\\d{1,3})\\b"),
                           QRegularExpression::CaseInsensitiveOption},
    };
    // 计分：Title 命中 ×3（作者刻意声明），事件文本命中 ×1
    QMap<int, int> score;
    const QString title = doc.info.value(QStringLiteral("Title")).value_or(QString());
    for (const auto& re : pats) {
        for (auto it = re.globalMatch(title); it.hasNext(); )
            score[it.next().captured(1).toInt()] += 3;
        for (const EventLine& ev : doc.events) {
            for (auto it = re.globalMatch(ev.text); it.hasNext(); )
                score[it.next().captured(1).toInt()] += 1;
        }
    }
    // 总分最高且严格领先第二名才算识别（并列 = 证据冲突，不硬猜）；
    // ≥2 分门槛挡住"仅次回预告提到一次下一集集号"的单 hit 噪声
    int best = -1, bestScore = 0, runnerUp = 0;
    for (auto it = score.cbegin(); it != score.cend(); ++it) {
        if (it.value() > bestScore) {
            runnerUp = bestScore;
            bestScore = it.value();
            best = it.key();
        } else if (it.value() > runnerUp) {
            runnerUp = it.value();
        }
    }
    if (best < 0 || bestScore < 2 || bestScore <= runnerUp)
        return {-1, false};
    return {best, true};
}

Cs detectEdStartCs(const AssDocument& doc)
{
    // 独立 ed/ending 词：匹配 ed_chi / ED_jap / ed_jpn / ending，排除
    // Credited 之类子串误命中（"Default" 无相邻 e-d，天然不匹配）
    static const QRegularExpression re(
        QStringLiteral("(^|[^a-zA-Z])(ed|ending)([^a-zA-Z]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    Cs best = -1;
    for (const EventLine& ev : doc.events) {
        if (ev.isComment || ev.zeroDuration() || !re.match(ev.style).hasMatch())
            continue;
        if (best < 0 || ev.start < best)
            best = ev.start;
    }
    return best;
}

bool naturalLessThan(const QString& a, const QString& b)
{
    static QCollator collator(QLocale::system());
    collator.setNumericMode(true);          // "2" < "10"（而非字典序 "10"<"2"）
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(a, b) < 0;
}

}  // namespace suture
