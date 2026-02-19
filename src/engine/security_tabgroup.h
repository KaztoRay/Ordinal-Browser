#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QWebEngineProfile>
#include <QList>
#include <QColor>
#include <QMap>
#include <functional>

namespace Ordinal {
namespace Engine {

// ============================================================
// SecurityAlert — LLM 에이전트 보안 경고
// ============================================================
struct SecurityAlert {
    enum Severity { Info, Low, Medium, High, Critical };
    enum Type { Phishing, Malware, Tracking, Privacy, MixedContent, UnsafeForm, Cryptominer };

    int64_t id = -1;
    Severity severity = Info;
    Type type = Privacy;
    QUrl url;
    QString title;
    QString description;
    QString recommendation;
    QDateTime timestamp;
    bool dismissed = false;

    QJsonObject toJson() const;
    QString severityString() const;
    QColor severityColor() const;
};

// ============================================================
// SecurityScanner — 페이지 보안 스캔 엔진
// ============================================================
class SecurityScanner : public QObject {
    Q_OBJECT

public:
    explicit SecurityScanner(QObject* parent = nullptr);

    // 동기 스캔
    QList<SecurityAlert> scanUrl(const QUrl& url) const;
    QList<SecurityAlert> scanPageContent(const QString& html, const QUrl& pageUrl) const;

    // 피싱 탐지
    bool isPhishing(const QUrl& url) const;
    double phishingScore(const QUrl& url) const;

    // 암호화폐 채굴 탐지
    bool hasCryptominer(const QString& html) const;

    // 혼합 콘텐츠 탐지
    QStringList findMixedContent(const QString& html, const QUrl& pageUrl) const;

    // 안전하지 않은 폼 탐지
    QStringList findUnsafeForms(const QString& html, const QUrl& pageUrl) const;

    // 추적기 탐지
    QStringList findTrackers(const QString& html) const;

    // JS 인젝션 스캔 스크립트
    static QString generateSecurityScanScript();

signals:
    void alertGenerated(const SecurityAlert& alert);

private:
    // 피싱 URL 패턴
    static const QStringList& phishingPatterns();
    // 알려진 트래커 도메인
    static const QStringList& trackerDomains();
    // 크립토마이너 시그니처
    static const QStringList& minerSignatures();
};

// ============================================================
// TabGroup — 탭 그룹
// ============================================================
struct TabGroup {
    int64_t id = -1;
    QString name;
    QColor color;
    QList<int> tabIndices;  // 포함된 탭 인덱스
    bool collapsed = false;

    QJsonObject toJson() const;
    static TabGroup fromJson(const QJsonObject& obj);
};

// ============================================================
// TabGroupManager — 탭 그룹 관리
// ============================================================
class TabGroupManager : public QObject {
    Q_OBJECT

public:
    explicit TabGroupManager(QObject* parent = nullptr);

    int64_t createGroup(const QString& name, const QColor& color = QColor("#4285f4"));
    bool removeGroup(int64_t id);
    bool addTabToGroup(int64_t groupId, int tabIndex);
    bool removeTabFromGroup(int64_t groupId, int tabIndex);
    bool renameGroup(int64_t groupId, const QString& name);
    bool setGroupColor(int64_t groupId, const QColor& color);
    bool toggleCollapse(int64_t groupId);

    QList<TabGroup> allGroups() const { return m_groups; }
    TabGroup* findGroup(int64_t id);
    TabGroup* groupForTab(int tabIndex);

    // 직렬화
    QJsonArray toJson() const;
    void fromJson(const QJsonArray& arr);

    // 기본 색상 목록
    static QList<QColor> defaultColors();

signals:
    void groupCreated(const TabGroup& group);
    void groupRemoved(int64_t id);
    void groupUpdated(const TabGroup& group);

private:
    QList<TabGroup> m_groups;
    int64_t m_nextId = 1;
};

// ============================================================
// BrowserProfile — 브라우저 프로필 (일반/시크릿/게스트)
// ============================================================
class BrowserProfileManager : public QObject {
    Q_OBJECT

public:
    enum ProfileType {
        Normal,
        Incognito,  // 시크릿 — 히스토리/쿠키 저장 안 함
        Guest       // 게스트 — 완전 격리, 종료 시 전부 삭제
    };

    explicit BrowserProfileManager(const QString& basePath, QObject* parent = nullptr);

    // 프로필 생성
    QWebEngineProfile* createProfile(const QString& name, ProfileType type = Normal);
    QWebEngineProfile* getProfile(const QString& name) const;
    QWebEngineProfile* incognitoProfile();
    QWebEngineProfile* guestProfile();

    // 관리
    QStringList profileNames() const;
    bool removeProfile(const QString& name);
    bool switchProfile(const QString& name);
    QString currentProfileName() const { return m_currentProfile; }

signals:
    void profileChanged(const QString& name);

private:
    void setupProfile(QWebEngineProfile* profile, ProfileType type);

    QString m_basePath;
    QString m_currentProfile = "default";
    QMap<QString, QWebEngineProfile*> m_profiles;
    QWebEngineProfile* m_incognito = nullptr;
    QWebEngineProfile* m_guest = nullptr;
};

} // namespace Engine
} // namespace Ordinal
