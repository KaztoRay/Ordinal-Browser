#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QUrl>
#include <QDateTime>
#include <QRegularExpression>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QJsonObject>

namespace Ordinal {
namespace Engine {

// ============================================================
// UserScript — Tampermonkey 호환 유저스크립트
// ============================================================
struct UserScript {
    int64_t id = -1;
    QString name;
    QString namespace_;   // @namespace
    QString version;
    QString description;
    QString author;
    QString source;       // 전체 JS 소스
    QStringList match;    // @match 패턴
    QStringList include;  // @include 패턴
    QStringList exclude;  // @exclude 패턴
    QString runAt;        // document-start, document-end, document-idle
    bool enabled = true;
    QDateTime installed;
    QDateTime updated;

    QJsonObject toJson() const;
    static UserScript fromSource(const QString& source);
    static QString extractMetaValue(const QString& source, const QString& key);
    static QStringList extractMetaValues(const QString& source, const QString& key);
};

// ============================================================
// UserScriptManager — 유저스크립트 설치/관리/주입
// ============================================================
class UserScriptManager : public QObject {
    Q_OBJECT

public:
    explicit UserScriptManager(const QString& scriptsDir,
                                QWebEngineProfile* profile,
                                QObject* parent = nullptr);
    ~UserScriptManager() override;

    // 설치/제거
    int64_t installScript(const QString& source);
    bool installFromUrl(const QUrl& url);
    bool removeScript(int64_t id);
    bool updateScript(int64_t id, const QString& newSource);

    // 관리
    bool enableScript(int64_t id);
    bool disableScript(int64_t id);
    void reloadAll();

    // 조회
    QList<UserScript> allScripts() const { return m_scripts; }
    UserScript* findScript(int64_t id);
    UserScript* findByName(const QString& name);
    int scriptCount() const { return m_scripts.size(); }

    // URL 매칭
    bool matchesUrl(const UserScript& script, const QUrl& url) const;

signals:
    void scriptInstalled(const UserScript& script);
    void scriptRemoved(int64_t id);
    void scriptError(const QString& error);

private:
    void loadScripts();
    void saveScript(const UserScript& script);
    void injectScript(const UserScript& script);
    void removeInjection(int64_t id);
    bool matchesPattern(const QString& url, const QString& pattern) const;

    QString m_scriptsDir;
    QWebEngineProfile* m_profile;
    QList<UserScript> m_scripts;
    int64_t m_nextId = 1;
};

// ============================================================
// NetworkMonitor — 네트워크 요청 모니터링
// ============================================================
struct NetworkRequest {
    int64_t id;
    QUrl url;
    QString method;
    QString resourceType;   // document, script, image, stylesheet, xhr, etc.
    int statusCode = 0;
    int64_t responseSize = 0;
    qint64 startTime = 0;
    qint64 endTime = 0;
    bool blocked = false;
    QString blockedBy;      // "adblock", "security", etc.

    double duration() const { return (endTime - startTime) / 1000.0; } // ms
};

class NetworkMonitor : public QObject {
    Q_OBJECT

public:
    explicit NetworkMonitor(QObject* parent = nullptr);

    void recordRequest(const NetworkRequest& req);
    void clear();

    QList<NetworkRequest> allRequests() const { return m_requests; }
    QList<NetworkRequest> blockedRequests() const;
    int totalRequests() const { return m_requests.size(); }
    int blockedCount() const;
    int64_t totalBytes() const;

    // 통계
    struct Stats {
        int totalRequests = 0;
        int blockedRequests = 0;
        int64_t totalBytes = 0;
        double avgResponseTime = 0;
        QMap<QString, int> requestsByType;
    };
    Stats getStats() const;

signals:
    void requestRecorded(const NetworkRequest& req);
    void requestBlocked(const NetworkRequest& req);

private:
    QList<NetworkRequest> m_requests;
    int64_t m_nextId = 1;
};

// ============================================================
// DevToolsExtended — 확장 개발자 도구 기능
// ============================================================
class DevToolsExtended : public QObject {
    Q_OBJECT

public:
    explicit DevToolsExtended(QObject* parent = nullptr);

    // JS 콘솔 명령
    struct ConsoleEntry {
        enum Level { Log, Warn, Error, Info, Debug };
        Level level;
        QString message;
        QString source;
        int line;
        QDateTime timestamp;
    };

    void addConsoleEntry(ConsoleEntry::Level level, const QString& message,
                         const QString& source = "", int line = 0);
    QList<ConsoleEntry> consoleEntries() const { return m_console; }
    void clearConsole();

    // DOM 검사 스크립트 생성
    static QString generateInspectorScript();

    // 성능 측정 스크립트
    static QString generatePerformanceScript();

    // Cookie 뷰어 스크립트
    static QString generateCookieViewerScript();

signals:
    void consoleEntryAdded(const ConsoleEntry& entry);

private:
    QList<ConsoleEntry> m_console;
};

} // namespace Engine
} // namespace Ordinal
