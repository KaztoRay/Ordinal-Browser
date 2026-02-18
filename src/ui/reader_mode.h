#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QHash>
#include <QVariant>
#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <memory>

namespace Ordinal {
namespace UI {

// Reader mode extracts the main article content from a page,
// strips ads/navigation/clutter, and presents clean readable text.

struct ReaderArticle {
    QString title;
    QString author;
    QString siteName;
    QUrl url;
    QString htmlContent;     // cleaned article HTML
    QString plainText;       // plain text version
    QString excerpt;         // first ~300 chars
    int wordCount = 0;
    int estimatedReadTimeSec = 0;
    QUrl heroImage;
    QString language;
    QDateTime publishDate;
    bool isReaderReady = false;
};

struct ReaderSettings {
    enum class Theme { Light, Dark, Sepia, Custom };
    enum class FontFamily { Serif, SansSerif, Monospace };

    Theme theme = Theme::Light;
    FontFamily fontFamily = FontFamily::Serif;
    int fontSize = 18;         // px
    int lineHeight = 160;      // percent
    int contentWidth = 680;    // px
    int marginTop = 60;        // px

    // Custom theme colors
    QColor bgColor = QColor("#FFFFFF");
    QColor textColor = QColor("#1A1A1A");
    QColor linkColor = QColor("#0066CC");
    QColor accentColor = QColor("#E8E0D0");

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["theme"] = static_cast<int>(theme);
        obj["fontFamily"] = static_cast<int>(fontFamily);
        obj["fontSize"] = fontSize;
        obj["lineHeight"] = lineHeight;
        obj["contentWidth"] = contentWidth;
        obj["bgColor"] = bgColor.name();
        obj["textColor"] = textColor.name();
        obj["linkColor"] = linkColor.name();
        return obj;
    }

    void fromJson(const QJsonObject& obj) {
        theme = static_cast<Theme>(obj["theme"].toInt(0));
        fontFamily = static_cast<FontFamily>(obj["fontFamily"].toInt(0));
        fontSize = obj["fontSize"].toInt(18);
        lineHeight = obj["lineHeight"].toInt(160);
        contentWidth = obj["contentWidth"].toInt(680);
        bgColor = QColor(obj["bgColor"].toString("#FFFFFF"));
        textColor = QColor(obj["textColor"].toString("#1A1A1A"));
        linkColor = QColor(obj["linkColor"].toString("#0066CC"));
    }
};

class ReaderMode : public QObject {
    Q_OBJECT
public:
    explicit ReaderMode(QObject* parent = nullptr);
    ~ReaderMode() override;

    // Check if a page is suitable for reader mode
    bool canExtract(const QString& html, const QUrl& url) const;

    // Extract article from raw page HTML
    ReaderArticle extract(const QString& html, const QUrl& url) const;

    // Generate styled HTML for the reader view
    QString renderArticle(const ReaderArticle& article) const;

    // Settings
    ReaderSettings settings() const;
    void setSettings(const ReaderSettings& settings);

    void setTheme(ReaderSettings::Theme theme);
    void setFontSize(int px);
    void setFontFamily(ReaderSettings::FontFamily family);
    void setContentWidth(int px);
    void setLineHeight(int percent);

    // Text-to-speech integration
    bool isSpeaking() const;
    void speak(const ReaderArticle& article);
    void pauseSpeech();
    void resumeSpeech();
    void stopSpeech();
    void setSpeechRate(double rate);  // 0.5 - 2.0

    // Print-friendly version
    QString generatePrintHtml(const ReaderArticle& article) const;

signals:
    void settingsChanged(const ReaderSettings& settings);
    void articleExtracted(const ReaderArticle& article);
    void speechStarted();
    void speechPaused();
    void speechFinished();
    void speechProgress(int charIndex, int totalChars);

private:
    // Content extraction helpers
    QString extractTitle(const QString& html) const;
    QString extractAuthor(const QString& html) const;
    QString extractContent(const QString& html) const;
    QString extractExcerpt(const QString& text, int maxLen = 300) const;
    QUrl extractHeroImage(const QString& html, const QUrl& baseUrl) const;
    QString detectLanguage(const QString& text) const;
    QDateTime extractPublishDate(const QString& html) const;

    // HTML cleaning
    QString removeElements(const QString& html, const QStringList& selectors) const;
    QString removeAttributes(const QString& html, const QStringList& attrs) const;
    QString fixRelativeUrls(const QString& html, const QUrl& baseUrl) const;

    // Scoring: which DOM nodes are content vs. boilerplate
    struct ScoredBlock {
        QString tag;
        QString text;
        double score = 0.0;
        int linkDensity = 0;
        int textLength = 0;
    };
    QList<ScoredBlock> scoreBlocks(const QString& html) const;
    double computeBlockScore(const ScoredBlock& block) const;

    // CSS generation
    QString generateReaderCss() const;

    ReaderSettings m_settings;
    bool m_speaking = false;
    double m_speechRate = 1.0;
};

} // namespace UI
} // namespace Ordinal
