#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QSqlDatabase>
#include <QUrl>
#include <QDateTime>
#include <QJsonObject>
#include <optional>

namespace Ordinal {
namespace Engine {

class HistoryManager;

// ============================================================
// SpeedDialItem — 새 탭 페이지 바로가기 항목
// ============================================================
struct SpeedDialItem {
    int64_t id = -1;
    QString title;
    QUrl url;
    QString favicon;     // base64 favicon data
    int position = 0;
    int visitCount = 0;  // 자동 정렬용
    bool pinned = false; // 고정 항목

    QJsonObject toJson() const;
};

// ============================================================
// NewTabPageGenerator — 새 탭 HTML 생성기
// ============================================================
class NewTabPageGenerator : public QObject {
    Q_OBJECT

public:
    explicit NewTabPageGenerator(HistoryManager* history, QObject* parent = nullptr);

    // 새 탭 HTML 생성
    QString generateHtml() const;

    // 스피드 다이얼 관리
    void addSpeedDial(const QString& title, const QUrl& url);
    void removeSpeedDial(int64_t id);
    void pinSpeedDial(int64_t id, bool pinned);
    QList<SpeedDialItem> getSpeedDialItems() const;

private:
    QString generateSpeedDialHtml() const;
    QString generateSearchBarHtml() const;
    QString generateStylesheet(bool darkMode) const;
    QList<SpeedDialItem> getMostVisitedSites(int limit = 8) const;
    QString faviconUrl(const QUrl& siteUrl) const;

    HistoryManager* m_history;
    QList<SpeedDialItem> m_pinnedItems;
};

// ============================================================
// PasswordEntry — 저장된 비밀번호 항목
// ============================================================
struct PasswordEntry {
    int64_t id = -1;
    QUrl siteUrl;
    QString username;
    QString encryptedPassword;  // AES-256 암호화
    QDateTime created;
    QDateTime lastUsed;
    bool autoFill = true;

    QJsonObject toJson() const;
};

// ============================================================
// CredentialManager — 비밀번호 저장/자동완성
// ============================================================
class CredentialManager : public QObject {
    Q_OBJECT

public:
    explicit CredentialManager(const QString& storagePath, QObject* parent = nullptr);
    ~CredentialManager() override;

    // 비밀번호 CRUD
    int64_t saveCredential(const QUrl& siteUrl, const QString& username, const QString& password);
    bool removeCredential(int64_t id);
    bool removeByUrl(const QUrl& siteUrl);
    bool updatePassword(int64_t id, const QString& newPassword);

    // 조회
    QList<PasswordEntry> getCredentials(const QUrl& siteUrl) const;
    QList<PasswordEntry> getAllCredentials() const;
    std::optional<PasswordEntry> findCredential(const QUrl& siteUrl, const QString& username) const;
    bool hasCredential(const QUrl& siteUrl) const;

    // 보안
    bool setMasterPassword(const QString& password);
    bool verifyMasterPassword(const QString& password) const;
    bool isLocked() const { return m_locked; }
    void lock();
    bool unlock(const QString& masterPassword);

    // 암호화
    QString encrypt(const QString& plaintext) const;
    QString decrypt(const QString& ciphertext) const;

    // 비밀번호 생성기
    static QString generatePassword(int length = 20, bool symbols = true,
                                     bool numbers = true, bool uppercase = true);

    int totalCount() const;

signals:
    void credentialSaved(const PasswordEntry& entry);
    void credentialRemoved(int64_t id);
    void lockedStateChanged(bool locked);

private:
    void initDatabase();
    QString deriveKey(const QString& password) const;

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_derivedKey;
    bool m_locked = true;
    QString m_salt;
};

// ============================================================
// ScreenCapture — 스크린샷/PDF 캡처
// ============================================================
class ScreenCapture : public QObject {
    Q_OBJECT

public:
    explicit ScreenCapture(QObject* parent = nullptr);

    // 스크린샷
    void captureVisibleArea(QWidget* webView, const QString& savePath);
    void captureFullPage(QWidget* webView, const QString& savePath);

    // PDF
    void printToPdf(QWidget* webView, const QString& savePath);

    // 기본 저장 경로
    static QString defaultSavePath(const QString& prefix = "screenshot",
                                    const QString& ext = "png");

signals:
    void captureCompleted(const QString& path);
    void captureError(const QString& error);
};

} // namespace Engine
} // namespace Ordinal
