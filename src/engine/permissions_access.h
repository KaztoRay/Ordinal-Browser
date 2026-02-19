#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QTimer>
#include <QWebEngineProfile>

namespace Ordinal {
namespace Engine {

// ============================================================
// WebNotification — 웹 알림 항목
// ============================================================
struct WebNotification {
    int64_t id = -1;
    QUrl origin;
    QString title;
    QString body;
    QString icon;
    QDateTime timestamp;
    bool read = false;
};

// ============================================================
// NotificationManager — 웹 알림 관리
// ============================================================
class NotificationManager : public QObject {
    Q_OBJECT

public:
    explicit NotificationManager(QObject* parent = nullptr);

    void addNotification(const QUrl& origin, const QString& title, const QString& body);
    QList<WebNotification> recent(int limit = 50) const;
    void markAsRead(int64_t id);
    void clearAll();
    int unreadCount() const;

signals:
    void notificationReceived(const WebNotification& notif);

private:
    QList<WebNotification> m_notifications;
    int64_t m_nextId = 1;
};

// ============================================================
// SitePermission — 사이트별 권한
// ============================================================
struct SitePermission {
    enum Permission {
        Notifications, Camera, Microphone, Location,
        Clipboard, Popups, AutoPlay, Sensors, Midi,
        FullScreen, FileSystem
    };
    enum State { Ask, Allow, Deny };

    QUrl origin;
    Permission permission;
    State state = Ask;
    QDateTime lastModified;
};

// ============================================================
// PermissionManager — 사이트 권한 관리
// ============================================================
class PermissionManager : public QObject {
    Q_OBJECT

public:
    explicit PermissionManager(const QString& storagePath, QObject* parent = nullptr);
    ~PermissionManager() override;

    // 권한 설정
    void setPermission(const QUrl& origin, SitePermission::Permission perm,
                        SitePermission::State state);
    SitePermission::State getPermission(const QUrl& origin,
                                         SitePermission::Permission perm) const;
    void removePermission(const QUrl& origin, SitePermission::Permission perm);
    void removeAllPermissions(const QUrl& origin);

    // 조회
    QList<SitePermission> permissionsForSite(const QUrl& origin) const;
    QList<SitePermission> allPermissions() const;
    QMap<QString, int> permissionStats() const;

    // 기본 정책
    void setDefaultPolicy(SitePermission::Permission perm, SitePermission::State state);
    SitePermission::State defaultPolicy(SitePermission::Permission perm) const;

    static QString permissionName(SitePermission::Permission perm);

signals:
    void permissionChanged(const QUrl& origin, SitePermission::Permission perm,
                           SitePermission::State state);

private:
    void initDatabase();

    QSqlDatabase m_db;
    QString m_dbPath;
    QMap<SitePermission::Permission, SitePermission::State> m_defaults;
};

// ============================================================
// AccessibilityTools — 접근성 도구
// ============================================================
class AccessibilityTools : public QObject {
    Q_OBJECT

public:
    explicit AccessibilityTools(QObject* parent = nullptr);

    // 텍스트 크기 조정 (전역)
    void setTextScale(double scale);  // 1.0 = 100%
    double textScale() const { return m_textScale; }

    // 고대비 모드
    void setHighContrast(bool enabled);
    bool highContrast() const { return m_highContrast; }
    static QString highContrastCss();

    // 다이슬렉시아 친화 폰트
    void setDyslexiaFont(bool enabled);
    bool dyslexiaFont() const { return m_dyslexiaFont; }
    static QString dyslexiaFontCss();

    // 포커스 하이라이트
    void setFocusHighlight(bool enabled);
    bool focusHighlight() const { return m_focusHighlight; }
    static QString focusHighlightCss();

    // 모션 감소
    void setReduceMotion(bool enabled);
    bool reduceMotion() const { return m_reduceMotion; }
    static QString reduceMotionCss();

    // 커서 크기 확대
    void setLargeCursor(bool enabled);
    bool largeCursor() const { return m_largeCursor; }
    static QString largeCursorCss();

    // 모든 활성 CSS 인젝션 생성
    QString generateInjectionCss() const;

    // 읽기 도우미 (TTS 용 텍스트 추출)
    static QString extractReadableText();

signals:
    void settingsChanged();

private:
    double m_textScale = 1.0;
    bool m_highContrast = false;
    bool m_dyslexiaFont = false;
    bool m_focusHighlight = false;
    bool m_reduceMotion = false;
    bool m_largeCursor = false;
};

// ============================================================
// TabSleepManager — 탭 수면 관리 (메모리 절약)
// ============================================================
class TabSleepManager : public QObject {
    Q_OBJECT

public:
    explicit TabSleepManager(QObject* parent = nullptr);

    // 설정
    void setSleepTimeout(int minutes);
    int sleepTimeout() const { return m_sleepMinutes; }
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // 탭 활동 기록
    void onTabActivated(int tabIndex);
    void onTabCreated(int tabIndex);
    void onTabRemoved(int tabIndex);

    // 수면 확인
    bool shouldSleep(int tabIndex) const;
    QList<int> sleepCandidates() const;

    // 수동 수면/깨우기
    void sleepTab(int tabIndex);
    void wakeTab(int tabIndex);
    bool isSleeping(int tabIndex) const;

    int sleepingCount() const { return m_sleeping.size(); }
    int64_t estimatedMemorySaved() const; // bytes

signals:
    void tabSlept(int tabIndex);
    void tabWoken(int tabIndex);

private:
    void checkSleepCandidates();

    int m_sleepMinutes = 30;
    bool m_enabled = true;
    QMap<int, QDateTime> m_lastActive;
    QSet<int> m_sleeping;
    QSet<int> m_pinned;   // 고정 탭은 수면하지 않음
    QTimer* m_checkTimer;
};

} // namespace Engine
} // namespace Ordinal
