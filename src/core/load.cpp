#include "load.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringDecoder>

#include "assparse.hpp"
#include "epnum.hpp"
#include "textdec.hpp"

// GBK/Big5 覆盖解码用 Win32（与 textdec 同一决策 D5）
#define NOMINMAX
#include <windows.h>

namespace suture {
namespace {

// 手动指定编码的解码（UI 覆盖路径）：UTF 系走 QStringConverter，GBK/Big5 走 Win32
bool decodeWithNamed(const QByteArray& raw, const QString& name, QString* out)
{
    const QString low = name.toLower();
    if (low == QStringLiteral("utf-8")) {
        *out = QString::fromUtf8(raw);
        return true;
    }
    if (low == QStringLiteral("utf-16le") || low == QStringLiteral("utf-16le (bom)") ||
        low == QStringLiteral("utf-16 le (bom)")) {
        QByteArray b = raw;
        if (b.startsWith("\xFF\xFE"))
            b.remove(0, 2);
        QStringDecoder dec(QStringConverter::Utf16LE);
        *out = dec(b);
        return !dec.hasError();
    }
    if (low == QStringLiteral("utf-16be") || low == QStringLiteral("utf-16 be (bom)")) {
        QByteArray b = raw;
        if (b.startsWith("\xFE\xFF"))
            b.remove(0, 2);
        QStringDecoder dec(QStringConverter::Utf16BE);
        *out = dec(b);
        return !dec.hasError();
    }
    const uint32_t cp = (low == QStringLiteral("big5")) ? 950 : 936;   // 其余按 GBK
    const int wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS,
                                         raw.constData(), raw.size(), nullptr, 0);
    if (wlen <= 0)
        return false;
    out->resize(wlen);
    MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, raw.constData(), raw.size(),
                        reinterpret_cast<wchar_t*>(out->data()), wlen);
    return true;
}

// 换行归一（覆盖解码不走 textdec 的归一逻辑，这里补上）
QString normalizeNewlines(QString s)
{
    s.replace(QStringLiteral(u"\r\n"), QStringLiteral(u"\n"));
    s.replace(u'\r', u'\n');
    while (s.endsWith(u'\n'))
        s.chop(1);
    return s;
}

// 已知字幕扩展名（.txt 也在内：六组样例全是 .txt，靠内容嗅探兜住）
bool isSubtitleExtension(const QString& ext)
{
    return ext == QStringLiteral("ass") || ext == QStringLiteral("ssa") ||
           ext == QStringLiteral("srt") || ext == QStringLiteral("txt");
}

}  // namespace

LoadOutcome loadSubtitleFile(const QString& path, quint64 assignId,
                             const QString& encodingOverride)
{
    LoadOutcome out;

    // ---- 读文件（P8：全程 Qt 路径 API，分号等保留字符无注入面）----
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::FileUnreadable,
            QStringLiteral("无法读取文件：%1（%2）").arg(path, file.errorString())));
        return out;
    }
    const QByteArray raw = file.readAll();
    if (raw.isEmpty()) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::FileUnreadable,
            QStringLiteral("文件为空：%1").arg(path)));
        return out;
    }

    // ---- 解码 ----
    QString text;
    QString encodingName;
    if (!encodingOverride.isEmpty()) {
        if (!decodeWithNamed(raw, encodingOverride, &text)) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::EncodingUncertain,
                QStringLiteral("按指定编码 %1 解码失败：%2")
                    .arg(encodingOverride, path)));
            return out;
        }
        text = normalizeNewlines(text);
        encodingName = encodingOverride;
    } else {
        DecodeResult dec = decodeToQString(raw);
        text = dec.text;
        encodingName = dec.encodingName;
        for (Diagnostic& d : dec.diags)
            out.diags.push_back(std::move(d));
        if (text.isEmpty()) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::FileUnreadable,
                QStringLiteral("解码后文本为空（编码无法判定）：%1").arg(path)));
            return out;
        }
    }

    // ---- 格式判定：扩展名优先，未知扩展名/无扩展名做内容嗅探 ----
    const QString ext = QFileInfo(path).suffix().toLower();
    const QString lowText = text.left(4096).toLower();
    const bool looksAss = lowText.contains(QStringLiteral("[script info]"));
    const bool looksSrt = text.contains(QStringLiteral("-->"));

    if (ext == QStringLiteral("srt") || (ext.isEmpty() && looksSrt) ||
        (ext != QStringLiteral("ass") && ext != QStringLiteral("ssa") &&
         ext != QStringLiteral("txt") && looksSrt)) {
        out.diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::FormatNotSupported,
            QStringLiteral("SRT 格式将在阶段 4 支持：%1").arg(path)));
        return out;
    }
    if (ext != QStringLiteral("ass") && ext != QStringLiteral("ssa") &&
        ext != QStringLiteral("txt")) {
        // 未知扩展名但内容像 ASS → 容错接受；否则拒绝
        if (!looksAss) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Error, DiagCode::UnknownFormat,
                QStringLiteral("无法识别的字幕格式：%1").arg(path)));
            return out;
        }
    }

    // ---- 解析 ----
    ParseOutcome parsed = parseAss(text);
    if (!parsed.ok) {
        for (Diagnostic& d : parsed.diags)
            out.diags.push_back(std::move(d));
        return out;
    }

    LoadedSubtitle sub;
    sub.id = assignId;
    sub.path = path;
    sub.doc = std::move(parsed.doc);
    sub.doc.sourcePath = path;
    sub.doc.encodingName = encodingName;
    sub.loadDiags = std::move(parsed.diags);
    // F16/P12 集数识别两级：文件名（P6）→ 内容标记（Title/第X話/Episode X）。
    // 内容级成功时不告警（识别链正常工作）；两级都失败才提示自然排序。
    EpGuess ep = guessEpisodeNumber(QFileInfo(path).fileName());
    if (!ep.confident) {
        const EpGuess content = guessEpisodeFromContent(sub.doc);
        if (content.confident) {
            ep = content;
            sub.loadDiags.push_back(Diagnostic::make(
                DiagLevel::Info, DiagCode::EpisodeUnrecognized,
                QStringLiteral("文件名无集数，按内容标记识别为第 %1 集（Title/"
                               "正文中的集号），排序已自动采用——请核对")
                    .arg(ep.number)));
        }
    }
    sub.epNumber = ep.number;
    sub.epConfident = ep.confident;
    if (!ep.confident)
        sub.loadDiags.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::EpisodeUnrecognized,
            QStringLiteral("文件名未识别出集数，按自然排序，可手动调整：%1")
                .arg(QFileInfo(path).fileName())));

    out.sub = std::move(sub);
    out.ok = true;
    return out;
}

DropExpansion expandDrop(const QList<QUrl>& urls)
{
    DropExpansion out;
    QSet<QString> seen;   // 规范化路径去重（Windows 大小写不敏感）

    auto addFile = [&](const QString& path) {
        const QString key = QDir::cleanPath(path).toLower();
        if (seen.contains(key)) {
            out.diags.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::DuplicateFile,
                QStringLiteral("重复文件已忽略：%1").arg(path)));
            return;
        }
        seen.insert(key);
        out.files.append(path);
    };

    for (const QUrl& url : urls) {
        const QString local = url.toLocalFile();
        if (local.isEmpty())
            continue;
        QFileInfo fi(local);
        if (fi.isDir()) {
            // 文件夹：非递归展开（子文件夹提示）
            QDir dir(local);
            const auto entries = dir.entryInfoList(
                QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                QDir::Name);
            int ignored = 0;
            for (const QFileInfo& e : entries) {
                if (isSubtitleExtension(e.suffix().toLower()))
                    addFile(e.absoluteFilePath());
                else
                    ++ignored;
            }
            if (ignored > 0)
                out.diags.push_back(Diagnostic::make(
                    DiagLevel::Info, DiagCode::NonSubtitleIgnored,
                    QStringLiteral("已忽略 %1 个非字幕文件（文件夹 %2）")
                        .arg(ignored).arg(dir.dirName())));
            const auto subdirs = dir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            if (!subdirs.isEmpty())
                out.diags.push_back(Diagnostic::make(
                    DiagLevel::Info, DiagCode::NonSubtitleIgnored,
                    QStringLiteral("文件夹 %1 含 %2 个子文件夹，未递归展开")
                        .arg(dir.dirName()).arg(subdirs.size())));
        } else if (fi.isFile()) {
            if (isSubtitleExtension(fi.suffix().toLower()))
                addFile(fi.absoluteFilePath());
            else
                out.diags.push_back(Diagnostic::make(
                    DiagLevel::Info, DiagCode::NonSubtitleIgnored,
                    QStringLiteral("已忽略非字幕文件：%1").arg(fi.fileName())));
        }
    }
    return out;
}

}  // namespace suture
