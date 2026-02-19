#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QDateTime>
#include <QSqlDatabase>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QNetworkAccessManager>
#include <QIcon>

namespace Ordinal {
namespace Engine {

// ============================================================
// PWAApp — Progressive Web App 정보
// ============================================================
struct PWAApp {
    int64_t id = -1;
    QString name;
    QString shortName;
    QUrl startUrl;
    QUrl scope;
    QString display;        // fullscreen, standalone, minimal-ui, browser
    QColor themeColor;
    QColor backgroundColor;
    QStringList icons;      // icon URL 목록
    QDateTime installed;
    bool pinned = false;

    QJsonObject toJson() const;
    static PWAApp fromManifest(const QJsonObject& manifest, const QUrl& baseUrl);
};

// ============================================================
// PWAManager — PWA 설치/관리
// ============================================================
class PWAManager : public QObject {
    Q_OBJECT

public:
    explicit PWAManager(const QString& storagePath, QObject* parent = nullptr);
    ~PWAManager() override;

    // PWA 감지 (페이지 HTML에서 manifest 링크 찾기)
    void detectPWA(const QString& html, const QUrl& pageUrl);

    // PWA 설치/제거
    int64_t installApp(const PWAApp& app);
    bool removeApp(int64_t id);
    bool pinApp(int64_t id, bool pinned);

    // 조회
    QList<PWAApp> installedApps() const;
    bool isInstalled(const QUrl& startUrl) const;

    // manifest.json 다운로드 후 파싱
    void fetchManifest(const QUrl& manifestUrl, const QUrl& pageUrl);

signals:
    void pwaDetected(const QUrl& manifestUrl, const QUrl& pageUrl);
    void appInstalled(const PWAApp& app);
    void appRemoved(int64_t id);
    void manifestParsed(const PWAApp& app);

private:
    void initDatabase();

    QSqlDatabase m_db;
    QString m_dbPath;
    QNetworkAccessManager* m_network;
};

// ============================================================
// WebScraper — 페이지 데이터 추출
// ============================================================
class WebScraper : public QObject {
    Q_OBJECT

public:
    explicit WebScraper(QObject* parent = nullptr);

    // CSS 셀렉터로 텍스트 추출
    static QString extractBySelector(const QString& selector);

    // 페이지 내 모든 링크 추출
    static QString extractAllLinks();

    // 페이지 내 모든 이미지 추출
    static QString extractAllImages();

    // 메타데이터 추출 (title, description, og:*, twitter:*)
    static QString extractMetadata();

    // 테이블 데이터 추출 (CSV 변환)
    static QString extractTables();

    // 구조화 데이터 추출 (JSON-LD, microdata)
    static QString extractStructuredData();

    // 커스텀 스크래핑 스크립트 실행
    void runScript(QWebEnginePage* page, const QString& js);

signals:
    void dataExtracted(const QString& data);
    void scrapeError(const QString& error);
};

// ============================================================
// SearchEngineEntry — 검색 엔진 정보
// ============================================================
struct SearchEngineEntry {
    QString id;
    QString name;
    QUrl searchUrl;       // {query} 플레이스홀더
    QUrl suggestUrl;      // 자동완성 URL (선택)
    QUrl iconUrl;
    QString shortcut;     // "g" for Google, "d" for DuckDuckGo
    bool isDefault = false;
    bool builtin = true;

    QJsonObject toJson() const;
    QUrl buildSearchUrl(const QString& query) const;
};

// ============================================================
// SearchEngineManager — 검색 엔진 관리
// ============================================================
class SearchEngineManager : public QObject {
    Q_OBJECT

public:
    explicit SearchEngineManager(const QString& storagePath, QObject* parent = nullptr);

    // 내장 엔진
    void initBuiltinEngines();

    // 커스텀 엔진
    void addEngine(const SearchEngineEntry& engine);
    bool removeEngine(const QString& id);
    bool setDefault(const QString& id);

    // 검색
    QUrl search(const QString& query) const;
    QUrl searchWith(const QString& engineId, const QString& query) const;

    // 단축키 검색 ("g hello" → Google 검색)
    QPair<QString, QString> parseShortcut(const QString& input) const;

    // 조회
    QList<SearchEngineEntry> allEngines() const { return m_engines; }
    SearchEngineEntry* defaultEngine();
    SearchEngineEntry* findById(const QString& id);
    SearchEngineEntry* findByShortcut(const QString& shortcut);

signals:
    void defaultChanged(const QString& id);

private:
    void saveEngines();
    void loadEngines();

    QList<SearchEngineEntry> m_engines;
    QString m_storagePath;
    QString m_defaultId = "duckduckgo";
};

// ============================================================
// SyncProtocol — 브라우저 데이터 동기화 프로토콜
// ============================================================
class SyncProtocol : public QObject {
    Q_OBJECT

public:
    explicit SyncProtocol(const QString& storagePath, QObject* parent = nullptr);

    // 동기화 상태
    enum SyncState { Disconnected, Connecting, Connected, Syncing, Error };

    // 내보내기 (로컬 → 파일)
    bool exportBookmarks(const QString& path) const;
    bool exportHistory(const QString& path) const;
    bool exportPasswords(const QString& path) const;
    bool exportSettings(const QString& path) const;
    bool exportAll(const QString& dirPath) const;

    // 가져오기 (파일 → 로컬)
    bool importBookmarks(const QString& path);
    bool importHistory(const QString& path);
    bool importSettings(const QString& path);
    bool importAll(const QString& dirPath);

    // 상태
    SyncState state() const { return m_state; }
    QDateTime lastSync() const { return m_lastSync; }

signals:
    void stateChanged(SyncState state);
    void syncProgress(int percent, const QString& item);
    void syncCompleted();
    void syncError(const QString& error);

private:
    SyncState m_state = Disconnected;
    QDateTime m_lastSync;
    QString m_storagePath;
};

} // namespace Engine
} // namespace Ordinal
