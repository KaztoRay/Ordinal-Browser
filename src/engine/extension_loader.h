#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QList>
#include <QIcon>
#include <QWebEngineProfile>
#include <QWebEngineScript>

namespace Ordinal {
namespace Engine {

// ============================================================
// WebExtension — 웹 확장 프로그램 정보
// ============================================================
struct WebExtension {
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QString path;          // 확장 디렉토리 경로
    QIcon icon;
    bool enabled = true;

    // manifest.json 기반
    QStringList permissions;
    QStringList contentScriptPatterns;
    QStringList contentScripts;       // JS 파일 경로
    QStringList contentStyles;        // CSS 파일 경로
    QString backgroundScript;
    QString popupHtml;

    QJsonObject toJson() const;
    static WebExtension fromManifest(const QString& extensionDir);
};

// ============================================================
// ExtensionLoader — 확장 프로그램 로더/관리
// ============================================================
class ExtensionLoader : public QObject {
    Q_OBJECT

public:
    explicit ExtensionLoader(const QString& extensionsDir,
                             QWebEngineProfile* profile,
                             QObject* parent = nullptr);
    ~ExtensionLoader() override;

    // 로드/언로드
    bool loadExtension(const QString& extensionDir);
    bool unloadExtension(const QString& id);
    void loadAllExtensions();
    void reloadExtension(const QString& id);

    // 관리
    bool enableExtension(const QString& id);
    bool disableExtension(const QString& id);

    // 조회
    QList<WebExtension> extensions() const { return m_extensions; }
    WebExtension* findExtension(const QString& id);
    int extensionCount() const { return m_extensions.size(); }

    // 콘텐츠 스크립트 주입 (페이지 로드 시 호출)
    void injectContentScripts(const QString& url);

signals:
    void extensionLoaded(const WebExtension& ext);
    void extensionUnloaded(const QString& id);
    void extensionError(const QString& id, const QString& error);

private:
    void injectScript(const WebExtension& ext);
    void removeScript(const QString& id);
    QString readFile(const QString& path) const;
    bool matchesPattern(const QString& url, const QString& pattern) const;

    QString m_extensionsDir;
    QWebEngineProfile* m_profile;
    QList<WebExtension> m_extensions;
};

} // namespace Engine
} // namespace Ordinal
