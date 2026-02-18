#include "extension_loader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QWebEngineScriptCollection>
#include <QRegularExpression>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// WebExtension
// ============================================================

QJsonObject WebExtension::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["version"] = version;
    obj["description"] = description;
    obj["author"] = author;
    obj["enabled"] = enabled;
    obj["path"] = path;
    obj["permissions"] = QJsonArray::fromStringList(permissions);
    return obj;
}

WebExtension WebExtension::fromManifest(const QString& extensionDir)
{
    WebExtension ext;
    ext.path = extensionDir;

    QFile file(extensionDir + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly)) {
        return ext;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject manifest = doc.object();

    ext.name = manifest["name"].toString("Unknown");
    ext.version = manifest["version"].toString("0.0.0");
    ext.description = manifest["description"].toString();
    ext.author = manifest["author"].toString();
    ext.id = QFileInfo(extensionDir).fileName(); // 디렉토리 이름을 ID로

    // 아이콘
    auto icons = manifest["icons"].toObject();
    if (icons.contains("48")) {
        ext.icon = QIcon(extensionDir + "/" + icons["48"].toString());
    } else if (icons.contains("128")) {
        ext.icon = QIcon(extensionDir + "/" + icons["128"].toString());
    }

    // 권한
    auto perms = manifest["permissions"].toArray();
    for (const auto& p : perms) {
        ext.permissions.append(p.toString());
    }

    // 콘텐츠 스크립트
    auto contentScriptsArray = manifest["content_scripts"].toArray();
    for (const auto& cs : contentScriptsArray) {
        auto csObj = cs.toObject();

        // URL 패턴
        auto matches = csObj["matches"].toArray();
        for (const auto& m : matches) {
            ext.contentScriptPatterns.append(m.toString());
        }

        // JS 파일
        auto jsFiles = csObj["js"].toArray();
        for (const auto& js : jsFiles) {
            ext.contentScripts.append(extensionDir + "/" + js.toString());
        }

        // CSS 파일
        auto cssFiles = csObj["css"].toArray();
        for (const auto& css : cssFiles) {
            ext.contentStyles.append(extensionDir + "/" + css.toString());
        }
    }

    // 백그라운드 스크립트
    auto background = manifest["background"].toObject();
    if (background.contains("scripts")) {
        auto scripts = background["scripts"].toArray();
        if (!scripts.isEmpty()) {
            ext.backgroundScript = extensionDir + "/" + scripts[0].toString();
        }
    } else if (background.contains("service_worker")) {
        ext.backgroundScript = extensionDir + "/" + background["service_worker"].toString();
    }

    // 팝업
    auto browserAction = manifest["browser_action"].toObject();
    if (browserAction.isEmpty()) {
        browserAction = manifest["action"].toObject(); // Manifest V3
    }
    if (browserAction.contains("default_popup")) {
        ext.popupHtml = extensionDir + "/" + browserAction["default_popup"].toString();
    }

    return ext;
}

// ============================================================
// ExtensionLoader
// ============================================================

ExtensionLoader::ExtensionLoader(const QString& extensionsDir,
                                 QWebEngineProfile* profile,
                                 QObject* parent)
    : QObject(parent)
    , m_extensionsDir(extensionsDir)
    , m_profile(profile)
{
    QDir().mkpath(extensionsDir);
}

ExtensionLoader::~ExtensionLoader() = default;

bool ExtensionLoader::loadExtension(const QString& extensionDir)
{
    QFileInfo fi(extensionDir + "/manifest.json");
    if (!fi.exists()) {
        emit extensionError("", "manifest.json을 찾을 수 없습니다: " + extensionDir);
        return false;
    }

    auto ext = WebExtension::fromManifest(extensionDir);
    if (ext.name.isEmpty()) {
        emit extensionError("", "확장 프로그램 로드 실패: " + extensionDir);
        return false;
    }

    // 이미 로드되어 있는지 확인
    for (const auto& existing : m_extensions) {
        if (existing.id == ext.id) {
            emit extensionError(ext.id, "이미 로드됨: " + ext.name);
            return false;
        }
    }

    // 보안 검사 — 위험한 권한 경고
    static const QStringList dangerousPerms = {
        "webRequestBlocking", "nativeMessaging", "debugger",
        "management", "proxy", "privacy"
    };
    for (const auto& perm : ext.permissions) {
        if (dangerousPerms.contains(perm)) {
            std::cerr << "[Extension] ⚠️ " << ext.name.toStdString()
                      << " 위험 권한: " << perm.toStdString() << std::endl;
        }
    }

    // 콘텐츠 스크립트 주입
    injectScript(ext);

    m_extensions.append(ext);
    std::cout << "[Extension] ✅ 로드: " << ext.name.toStdString()
              << " v" << ext.version.toStdString() << std::endl;

    emit extensionLoaded(ext);
    return true;
}

bool ExtensionLoader::unloadExtension(const QString& id)
{
    for (int i = 0; i < m_extensions.size(); ++i) {
        if (m_extensions[i].id == id) {
            removeScript(id);
            m_extensions.removeAt(i);
            emit extensionUnloaded(id);
            return true;
        }
    }
    return false;
}

void ExtensionLoader::loadAllExtensions()
{
    QDir dir(m_extensionsDir);
    auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        QString extDir = m_extensionsDir + "/" + entry;
        if (QFile::exists(extDir + "/manifest.json")) {
            loadExtension(extDir);
        }
    }
}

void ExtensionLoader::reloadExtension(const QString& id)
{
    auto* ext = findExtension(id);
    if (!ext) return;

    QString path = ext->path;
    unloadExtension(id);
    loadExtension(path);
}

bool ExtensionLoader::enableExtension(const QString& id)
{
    auto* ext = findExtension(id);
    if (!ext || ext->enabled) return false;

    ext->enabled = true;
    injectScript(*ext);
    return true;
}

bool ExtensionLoader::disableExtension(const QString& id)
{
    auto* ext = findExtension(id);
    if (!ext || !ext->enabled) return false;

    ext->enabled = false;
    removeScript(id);
    return true;
}

WebExtension* ExtensionLoader::findExtension(const QString& id)
{
    for (auto& ext : m_extensions) {
        if (ext.id == id) return &ext;
    }
    return nullptr;
}

void ExtensionLoader::injectContentScripts(const QString& url)
{
    Q_UNUSED(url)
    // 콘텐츠 스크립트는 QWebEngineScript를 통해 자동 주입됨
    // 이 메소드는 동적 주입이 필요한 경우를 위해 예약
}

void ExtensionLoader::injectScript(const WebExtension& ext)
{
    if (!m_profile || !ext.enabled) return;

    auto* scripts = m_profile->scripts();

    // 콘텐츠 스크립트 주입
    for (int i = 0; i < ext.contentScripts.size(); ++i) {
        QString source = readFile(ext.contentScripts[i]);
        if (source.isEmpty()) continue;

        QWebEngineScript script;
        script.setName(ext.id + "_content_" + QString::number(i));
        script.setSourceCode(source);
        script.setInjectionPoint(QWebEngineScript::DocumentReady);
        script.setWorldId(QWebEngineScript::ApplicationWorld);
        script.setRunsOnSubFrames(false);

        scripts->insert(script);
    }

    // CSS 주입 (JS로 래핑)
    for (int i = 0; i < ext.contentStyles.size(); ++i) {
        QString css = readFile(ext.contentStyles[i]);
        if (css.isEmpty()) continue;

        css.replace("'", "\\'").replace("\n", "\\n");
        QString injection = QString(
            "(function() {"
            "  var style = document.createElement('style');"
            "  style.textContent = '%1';"
            "  document.head.appendChild(style);"
            "})();").arg(css);

        QWebEngineScript script;
        script.setName(ext.id + "_style_" + QString::number(i));
        script.setSourceCode(injection);
        script.setInjectionPoint(QWebEngineScript::DocumentReady);
        script.setWorldId(QWebEngineScript::ApplicationWorld);

        scripts->insert(script);
    }
}

void ExtensionLoader::removeScript(const QString& id)
{
    if (!m_profile) return;

    auto* scripts = m_profile->scripts();
    auto allScripts = scripts->toList();

    for (const auto& script : allScripts) {
        if (script.name().startsWith(id + "_")) {
            scripts->remove(script);
        }
    }
}

QString ExtensionLoader::readFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

bool ExtensionLoader::matchesPattern(const QString& url, const QString& pattern) const
{
    if (pattern == "<all_urls>") return true;

    // 간단한 매치 패턴 구현 (*://*/* 스타일)
    QString regexStr = QRegularExpression::escape(pattern);
    regexStr.replace("\\*", ".*");
    QRegularExpression rx("^" + regexStr + "$");
    return rx.match(url).hasMatch();
}

} // namespace Engine
} // namespace Ordinal
