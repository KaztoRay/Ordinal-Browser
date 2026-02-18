#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QToolButton>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QDateTime>
#include <QSqlDatabase>
#include <QStyle>
#include <QJsonObject>

namespace Ordinal {
namespace Engine {

class BookmarkManager;
class HistoryManager;

// ============================================================
// SidebarPanel — 사이드바 (북마크/히스토리/읽기 목록)
// ============================================================
class SidebarPanel : public QWidget {
    Q_OBJECT

public:
    enum Panel {
        Bookmarks = 0,
        History = 1,
        ReadingList = 2,
        Downloads = 3
    };

    explicit SidebarPanel(QWidget* parent = nullptr);

    void setManagers(BookmarkManager* bookmarks, HistoryManager* history);
    void showPanel(Panel panel);
    void toggle();
    bool isVisible() const { return m_visible; }

signals:
    void urlSelected(const QUrl& url);
    void panelToggled(bool visible);

private:
    void setupUI();
    void setupBookmarksPanel();
    void setupHistoryPanel();
    void setupReadingListPanel();
    void setupDownloadsPanel();

    void refreshBookmarks();
    void refreshHistory();

    bool m_visible = false;

    // 패널 버튼
    QToolButton* m_bookmarkBtn = nullptr;
    QToolButton* m_historyBtn = nullptr;
    QToolButton* m_readingListBtn = nullptr;
    QToolButton* m_downloadsBtn = nullptr;

    // 패널 컨텐츠
    QStackedWidget* m_stack = nullptr;

    // 북마크 패널
    QTreeWidget* m_bookmarkTree = nullptr;
    QLineEdit* m_bookmarkSearch = nullptr;

    // 히스토리 패널
    QListWidget* m_historyList = nullptr;
    QLineEdit* m_historySearch = nullptr;

    // 읽기 목록
    QListWidget* m_readingList = nullptr;

    // 다운로드
    QListWidget* m_downloadsList = nullptr;

    // 매니저
    BookmarkManager* m_bookmarks = nullptr;
    HistoryManager* m_history = nullptr;
};

// ============================================================
// TranslationEngine — 페이지 번역 (LibreTranslate API)
// ============================================================
class TranslationEngine : public QObject {
    Q_OBJECT

public:
    explicit TranslationEngine(QObject* parent = nullptr);

    // 지원 언어
    struct Language {
        QString code;
        QString name;
    };
    static QList<Language> supportedLanguages();

    // 텍스트 번역
    void translateText(const QString& text, const QString& from, const QString& to);

    // 페이지 번역 — JS 인젝션으로 텍스트 노드 교체
    QString generateTranslationScript(const QString& targetLang) const;

    // 언어 감지
    static QString detectLanguage(const QString& text);

    // 설정
    void setApiUrl(const QString& url) { m_apiUrl = url; }
    QString apiUrl() const { return m_apiUrl; }

signals:
    void translationReady(const QString& translated);
    void translationError(const QString& error);

private:
    QString m_apiUrl = "https://libretranslate.com/translate";
};

// ============================================================
// ReadingListItem — 읽기 목록 항목
// ============================================================
struct ReadingListItem {
    int64_t id = -1;
    QString title;
    QUrl url;
    bool read = false;
    QDateTime addedAt;
    QString excerpt;

    QJsonObject toJson() const;
};

// ============================================================
// ReadingListManager — 읽기 목록 관리
// ============================================================
class ReadingListManager : public QObject {
    Q_OBJECT

public:
    explicit ReadingListManager(const QString& storagePath, QObject* parent = nullptr);
    ~ReadingListManager() override;

    int64_t addItem(const QString& title, const QUrl& url, const QString& excerpt = "");
    bool removeItem(int64_t id);
    bool markAsRead(int64_t id, bool read = true);
    QList<ReadingListItem> getAll() const;
    QList<ReadingListItem> getUnread() const;
    int unreadCount() const;

signals:
    void itemAdded(const ReadingListItem& item);
    void itemRemoved(int64_t id);
    void itemUpdated(const ReadingListItem& item);

private:
    void initDatabase();
    QSqlDatabase m_db;
    QString m_dbPath;
};

} // namespace Engine
} // namespace Ordinal
