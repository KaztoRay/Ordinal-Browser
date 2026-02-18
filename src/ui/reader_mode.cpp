#include "reader_mode.h"
#include <QRegularExpression>
#include <QUrl>
#include <QStringList>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

namespace Ordinal {
namespace UI {

ReaderMode::ReaderMode(QObject* parent)
    : QObject(parent)
{
}

ReaderMode::~ReaderMode() = default;

// --------------- Extraction check ---------------

bool ReaderMode::canExtract(const QString& html, const QUrl& url) const
{
    Q_UNUSED(url)

    if (html.length() < 500) return false;

    // Quick heuristics: does the page have article-like structure?
    static QRegularExpression articleRe(
        R"(<article|role="article"|class="[^"]*(?:article|post|entry|content)[^"]*"|<main)",
        QRegularExpression::CaseInsensitiveOption);

    if (articleRe.match(html).hasMatch()) return true;

    // Fallback: count <p> tags — articles usually have many
    static QRegularExpression pTagRe(R"(<p[\s>])", QRegularExpression::CaseInsensitiveOption);
    int pCount = 0;
    auto it = pTagRe.globalMatch(html);
    while (it.hasNext()) { it.next(); ++pCount; }

    return pCount >= 5;
}

// --------------- Main extraction ---------------

ReaderArticle ReaderMode::extract(const QString& html, const QUrl& url) const
{
    ReaderArticle article;
    article.url = url;
    article.title = extractTitle(html);
    article.author = extractAuthor(html);
    article.heroImage = extractHeroImage(html, url);
    article.publishDate = extractPublishDate(html);

    // Extract main content
    QString content = extractContent(html);
    content = fixRelativeUrls(content, url);
    article.htmlContent = content;

    // Plain text version
    QTextDocument doc;
    doc.setHtml(content);
    article.plainText = doc.toPlainText();

    // Metadata
    article.excerpt = extractExcerpt(article.plainText);
    article.wordCount = article.plainText.split(QRegularExpression(R"(\s+)"),
                                                 Qt::SkipEmptyParts).size();
    // Average reading speed: ~200 wpm for Korean/Japanese, ~250 for English
    article.estimatedReadTimeSec = (article.wordCount * 60) / 230;
    article.language = detectLanguage(article.plainText);
    article.isReaderReady = !content.isEmpty() && article.wordCount > 50;

    // Extract site name from URL or meta
    article.siteName = url.host().replace("www.", "");

    emit const_cast<ReaderMode*>(this)->articleExtracted(article);
    return article;
}

// --------------- Render ---------------

QString ReaderMode::renderArticle(const ReaderArticle& article) const
{
    QString css = generateReaderCss();

    QString html = QStringLiteral(R"(<!DOCTYPE html>
<html lang="%1">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%2 — Reader Mode</title>
<style>%3</style>
</head>
<body>
<article class="reader-article">
  <header>
    <h1 class="reader-title">%2</h1>
    <div class="reader-meta">
)").arg(article.language, article.title.toHtmlEscaped(), css);

    if (!article.author.isEmpty()) {
        html += QStringLiteral("      <span class=\"reader-author\">%1</span>\n")
                    .arg(article.author.toHtmlEscaped());
    }
    if (!article.siteName.isEmpty()) {
        html += QStringLiteral("      <span class=\"reader-site\">%1</span>\n")
                    .arg(article.siteName.toHtmlEscaped());
    }
    if (article.estimatedReadTimeSec > 0) {
        int mins = std::max(1, article.estimatedReadTimeSec / 60);
        html += QStringLiteral("      <span class=\"reader-time\">%1 min read</span>\n")
                    .arg(mins);
    }

    html += QStringLiteral("    </div>\n  </header>\n");

    if (!article.heroImage.isEmpty()) {
        html += QStringLiteral("  <figure class=\"reader-hero\">"
                               "<img src=\"%1\" alt=\"\"></figure>\n")
                    .arg(article.heroImage.toString());
    }

    html += QStringLiteral("  <div class=\"reader-content\">%1</div>\n").arg(article.htmlContent);
    html += QStringLiteral("</article>\n</body>\n</html>");

    return html;
}

// --------------- Settings ---------------

ReaderSettings ReaderMode::settings() const { return m_settings; }

void ReaderMode::setSettings(const ReaderSettings& settings)
{
    m_settings = settings;
    emit settingsChanged(m_settings);
}

void ReaderMode::setTheme(ReaderSettings::Theme theme)
{
    m_settings.theme = theme;
    switch (theme) {
    case ReaderSettings::Theme::Light:
        m_settings.bgColor = QColor("#FFFFFF");
        m_settings.textColor = QColor("#1A1A1A");
        break;
    case ReaderSettings::Theme::Dark:
        m_settings.bgColor = QColor("#1A1A2E");
        m_settings.textColor = QColor("#E0E0E0");
        break;
    case ReaderSettings::Theme::Sepia:
        m_settings.bgColor = QColor("#F4ECD8");
        m_settings.textColor = QColor("#5B4636");
        break;
    case ReaderSettings::Theme::Custom:
        break;
    }
    emit settingsChanged(m_settings);
}

void ReaderMode::setFontSize(int px)
{
    m_settings.fontSize = std::clamp(px, 12, 40);
    emit settingsChanged(m_settings);
}

void ReaderMode::setFontFamily(ReaderSettings::FontFamily family)
{
    m_settings.fontFamily = family;
    emit settingsChanged(m_settings);
}

void ReaderMode::setContentWidth(int px)
{
    m_settings.contentWidth = std::clamp(px, 400, 1200);
    emit settingsChanged(m_settings);
}

void ReaderMode::setLineHeight(int percent)
{
    m_settings.lineHeight = std::clamp(percent, 100, 250);
    emit settingsChanged(m_settings);
}

// --------------- TTS ---------------

bool ReaderMode::isSpeaking() const { return m_speaking; }

void ReaderMode::speak(const ReaderArticle& article)
{
    Q_UNUSED(article)
    m_speaking = true;
    emit speechStarted();
    // Real TTS integration would go here (e.g., QTextToSpeech)
}

void ReaderMode::pauseSpeech()
{
    if (m_speaking) {
        m_speaking = false;
        emit speechPaused();
    }
}

void ReaderMode::resumeSpeech()
{
    if (!m_speaking) {
        m_speaking = true;
        emit speechStarted();
    }
}

void ReaderMode::stopSpeech()
{
    m_speaking = false;
    emit speechFinished();
}

void ReaderMode::setSpeechRate(double rate)
{
    m_speechRate = std::clamp(rate, 0.5, 2.0);
}

// --------------- Print ---------------

QString ReaderMode::generatePrintHtml(const ReaderArticle& article) const
{
    QString css = R"(
        body { font-family: Georgia, serif; font-size: 12pt; color: #000;
               margin: 1in; line-height: 1.6; }
        .reader-title { font-size: 24pt; margin-bottom: 0.5em; }
        .reader-meta { color: #666; font-size: 10pt; margin-bottom: 2em; }
        .reader-content img { max-width: 100%; }
        @page { margin: 1in; }
    )";

    return QStringLiteral(R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><style>%1</style></head><body>
<h1 class="reader-title">%2</h1>
<div class="reader-meta">%3</div>
<div class="reader-content">%4</div>
</body></html>)")
        .arg(css, article.title.toHtmlEscaped(),
             article.author.isEmpty() ? article.siteName : article.author,
             article.htmlContent);
}

// --------------- Private: Content extraction ---------------

QString ReaderMode::extractTitle(const QString& html) const
{
    // Try <meta property="og:title">
    static QRegularExpression ogTitleRe(
        R"(<meta\s+(?:property|name)="og:title"\s+content="([^"]+)")",
        QRegularExpression::CaseInsensitiveOption);
    auto m = ogTitleRe.match(html);
    if (m.hasMatch()) return m.captured(1);

    // Try <h1>
    static QRegularExpression h1Re(R"(<h1[^>]*>(.*?)</h1>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    m = h1Re.match(html);
    if (m.hasMatch()) {
        QTextDocument doc;
        doc.setHtml(m.captured(1));
        return doc.toPlainText().trimmed();
    }

    // Fallback: <title>
    static QRegularExpression titleRe(R"(<title[^>]*>(.*?)</title>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    m = titleRe.match(html);
    if (m.hasMatch()) return m.captured(1).trimmed();

    return "Untitled";
}

QString ReaderMode::extractAuthor(const QString& html) const
{
    // Meta author
    static QRegularExpression metaAuthorRe(
        R"(<meta\s+(?:name|property)="(?:author|article:author)"\s+content="([^"]+)")",
        QRegularExpression::CaseInsensitiveOption);
    auto m = metaAuthorRe.match(html);
    if (m.hasMatch()) return m.captured(1);

    // JSON-LD
    static QRegularExpression jsonLdAuthorRe(R"("author"\s*:\s*\{[^}]*"name"\s*:\s*"([^"]+)")");
    m = jsonLdAuthorRe.match(html);
    if (m.hasMatch()) return m.captured(1);

    // byline class
    static QRegularExpression bylineRe(
        R"(class="[^"]*(?:byline|author)[^"]*"[^>]*>(.*?)</)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    m = bylineRe.match(html);
    if (m.hasMatch()) {
        QTextDocument doc;
        doc.setHtml(m.captured(1));
        return doc.toPlainText().trimmed();
    }

    return {};
}

QString ReaderMode::extractContent(const QString& html) const
{
    // Strategy: score text blocks and extract the highest-scoring cluster

    // First, try to find an <article> element
    static QRegularExpression articleRe(
        R"(<article[^>]*>(.*?)</article>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    auto m = articleRe.match(html);
    if (m.hasMatch()) {
        QString articleHtml = m.captured(1);
        // Remove nav, aside, footer within article
        articleHtml = removeElements(articleHtml,
            {"nav", "aside", "footer", "header", "script", "style",
             "form", "iframe", "noscript"});
        return articleHtml.trimmed();
    }

    // Try <main>
    static QRegularExpression mainRe(
        R"(<main[^>]*>(.*?)</main>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    m = mainRe.match(html);
    if (m.hasMatch()) {
        QString mainHtml = m.captured(1);
        mainHtml = removeElements(mainHtml,
            {"nav", "aside", "footer", "header", "script", "style",
             "form", "iframe", "noscript"});
        return mainHtml.trimmed();
    }

    // Fallback: look for content-class divs
    static QRegularExpression contentDivRe(
        R"(<div[^>]*class="[^"]*(?:content|article|post|entry|story|body)[^"]*"[^>]*>(.*?)</div>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    m = contentDivRe.match(html);
    if (m.hasMatch()) {
        return removeElements(m.captured(1),
            {"script", "style", "nav", "iframe"}).trimmed();
    }

    // Last resort: extract all <p> tags
    static QRegularExpression pRe(R"(<p[^>]*>(.*?)</p>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    auto pIt = pRe.globalMatch(html);
    QString result;
    while (pIt.hasNext()) {
        auto pm = pIt.next();
        QString pText = pm.captured(1).trimmed();
        if (pText.length() > 40) { // skip short paragraphs (likely nav)
            result += "<p>" + pText + "</p>\n";
        }
    }
    return result;
}

QString ReaderMode::extractExcerpt(const QString& text, int maxLen) const
{
    if (text.length() <= maxLen) return text;
    // Break at word boundary
    int idx = text.lastIndexOf(' ', maxLen);
    if (idx < maxLen / 2) idx = maxLen;
    return text.left(idx) + "...";
}

QUrl ReaderMode::extractHeroImage(const QString& html, const QUrl& baseUrl) const
{
    // og:image
    static QRegularExpression ogImageRe(
        R"(<meta\s+(?:property|name)="og:image"\s+content="([^"]+)")",
        QRegularExpression::CaseInsensitiveOption);
    auto m = ogImageRe.match(html);
    if (m.hasMatch()) {
        return baseUrl.resolved(QUrl(m.captured(1)));
    }

    // First large image in article
    static QRegularExpression imgRe(R"(<img[^>]+src="([^"]+)"[^>]*>)",
        QRegularExpression::CaseInsensitiveOption);
    auto it = imgRe.globalMatch(html);
    while (it.hasNext()) {
        auto im = it.next();
        QString src = im.captured(1);
        // Skip tiny icons / tracking pixels
        if (src.contains("1x1") || src.contains("pixel") ||
            src.contains("tracking") || src.contains("data:image/gif")) {
            continue;
        }
        return baseUrl.resolved(QUrl(src));
    }
    return {};
}

QString ReaderMode::detectLanguage(const QString& text) const
{
    // Simple heuristic: check for CJK characters
    int cjk = 0, latin = 0;
    for (const QChar& ch : text.left(500)) {
        if (ch.unicode() >= 0x3000 && ch.unicode() <= 0x9FFF) ++cjk;
        else if (ch.isLetter() && ch.unicode() < 0x0250) ++latin;
    }
    if (cjk > latin) {
        // Distinguish Korean vs Japanese vs Chinese
        int hangul = 0;
        for (const QChar& ch : text.left(500)) {
            if (ch.unicode() >= 0xAC00 && ch.unicode() <= 0xD7AF) ++hangul;
        }
        if (hangul > cjk / 2) return "ko";
        // Simplified: default CJK to "zh" unless katakana/hiragana present
        int kana = 0;
        for (const QChar& ch : text.left(500)) {
            if (ch.unicode() >= 0x3040 && ch.unicode() <= 0x30FF) ++kana;
        }
        if (kana > cjk / 4) return "ja";
        return "zh";
    }
    return "en"; // default
}

QDateTime ReaderMode::extractPublishDate(const QString& html) const
{
    // Try meta tags
    static QRegularExpression dateMetaRe(
        R"(<meta\s+(?:property|name)="(?:article:published_time|date|datePublished)"\s+content="([^"]+)")",
        QRegularExpression::CaseInsensitiveOption);
    auto m = dateMetaRe.match(html);
    if (m.hasMatch()) {
        return QDateTime::fromString(m.captured(1), Qt::ISODate);
    }

    // JSON-LD datePublished
    static QRegularExpression jsonDateRe(R"("datePublished"\s*:\s*"([^"]+)")");
    m = jsonDateRe.match(html);
    if (m.hasMatch()) {
        return QDateTime::fromString(m.captured(1), Qt::ISODate);
    }

    // <time datetime="">
    static QRegularExpression timeRe(R"(<time[^>]+datetime="([^"]+)")",
        QRegularExpression::CaseInsensitiveOption);
    m = timeRe.match(html);
    if (m.hasMatch()) {
        return QDateTime::fromString(m.captured(1), Qt::ISODate);
    }

    return {};
}

// --------------- Private: HTML cleaning ---------------

QString ReaderMode::removeElements(const QString& html, const QStringList& selectors) const
{
    QString result = html;
    for (const auto& tag : selectors) {
        QRegularExpression re(
            QStringLiteral("<%1[^>]*>.*?</%1>").arg(tag),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        result.replace(re, "");
    }
    return result;
}

QString ReaderMode::removeAttributes(const QString& html, const QStringList& attrs) const
{
    QString result = html;
    for (const auto& attr : attrs) {
        QRegularExpression re(
            QStringLiteral(R"(\s%1="[^"]*")").arg(QRegularExpression::escape(attr)));
        result.replace(re, "");
    }
    return result;
}

QString ReaderMode::fixRelativeUrls(const QString& html, const QUrl& baseUrl) const
{
    QString result = html;

    // Fix src attributes
    static QRegularExpression srcRe(R"(src="(/[^"]+)")");
    auto it = srcRe.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString abs = baseUrl.resolved(QUrl(m.captured(1))).toString();
        result.replace(m.captured(0), QStringLiteral("src=\"%1\"").arg(abs));
    }

    // Fix href attributes
    static QRegularExpression hrefRe(R"(href="(/[^"]+)")");
    it = hrefRe.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        QString abs = baseUrl.resolved(QUrl(m.captured(1))).toString();
        result.replace(m.captured(0), QStringLiteral("href=\"%1\"").arg(abs));
    }

    return result;
}

QList<ReaderMode::ScoredBlock> ReaderMode::scoreBlocks(const QString& html) const
{
    QList<ScoredBlock> blocks;
    static QRegularExpression blockRe(
        R"(<(p|div|section|article)[^>]*>(.*?)</\1>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    auto it = blockRe.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        ScoredBlock block;
        block.tag = m.captured(1).toLower();
        block.text = m.captured(2);

        QTextDocument doc;
        doc.setHtml(block.text);
        QString plain = doc.toPlainText();
        block.textLength = plain.length();

        // Count links
        static QRegularExpression linkRe(R"(<a\s)", QRegularExpression::CaseInsensitiveOption);
        block.linkDensity = 0;
        auto linkIt = linkRe.globalMatch(block.text);
        while (linkIt.hasNext()) { linkIt.next(); ++block.linkDensity; }

        block.score = computeBlockScore(block);
        blocks.append(block);
    }

    return blocks;
}

double ReaderMode::computeBlockScore(const ScoredBlock& block) const
{
    double score = 0.0;

    // More text = more likely content
    score += std::min(block.textLength / 100.0, 5.0);

    // <p> tags are strong content signals
    if (block.tag == "p") score += 3.0;
    if (block.tag == "article") score += 5.0;

    // High link density = navigation, not content
    if (block.textLength > 0) {
        double linkRatio = (double)block.linkDensity * 30.0 / block.textLength;
        score -= linkRatio * 10.0;
    }

    return score;
}

// --------------- Private: CSS ---------------

QString ReaderMode::generateReaderCss() const
{
    QString fontFamily;
    switch (m_settings.fontFamily) {
    case ReaderSettings::FontFamily::Serif:
        fontFamily = "'Georgia', 'Noto Serif KR', serif";
        break;
    case ReaderSettings::FontFamily::SansSerif:
        fontFamily = "'Helvetica Neue', 'Noto Sans KR', sans-serif";
        break;
    case ReaderSettings::FontFamily::Monospace:
        fontFamily = "'JetBrains Mono', 'D2Coding', monospace";
        break;
    }

    return QStringLiteral(R"(
        * { box-sizing: border-box; }
        body {
            background: %1; color: %2;
            font-family: %3; font-size: %4px;
            line-height: %5%%;
            margin: 0; padding: %6px 20px;
        }
        .reader-article {
            max-width: %7px; margin: 0 auto;
        }
        .reader-title {
            font-size: 2em; line-height: 1.2; margin-bottom: 0.3em;
        }
        .reader-meta {
            color: %2; opacity: 0.6; font-size: 0.85em;
            display: flex; gap: 1em; margin-bottom: 2em;
            border-bottom: 1px solid rgba(128,128,128,0.2);
            padding-bottom: 1em;
        }
        .reader-hero img { width: 100%%; border-radius: 8px; margin-bottom: 1.5em; }
        .reader-content p { margin: 0 0 1.2em; }
        .reader-content img { max-width: 100%%; height: auto; border-radius: 4px; }
        .reader-content a { color: %8; text-decoration: underline; }
        .reader-content blockquote {
            border-left: 3px solid %8; margin: 1em 0; padding: 0.5em 1em;
            opacity: 0.85; font-style: italic;
        }
        .reader-content pre, .reader-content code {
            font-family: 'JetBrains Mono', monospace;
            background: rgba(128,128,128,0.1); border-radius: 4px;
        }
        .reader-content pre { padding: 1em; overflow-x: auto; }
        .reader-content code { padding: 0.15em 0.3em; font-size: 0.9em; }
        ::selection { background: %8; color: white; }
    )")
        .arg(m_settings.bgColor.name(), m_settings.textColor.name(), fontFamily)
        .arg(m_settings.fontSize).arg(m_settings.lineHeight).arg(m_settings.marginTop)
        .arg(m_settings.contentWidth)
        .arg(m_settings.linkColor.name());
}

} // namespace UI
} // namespace Ordinal
