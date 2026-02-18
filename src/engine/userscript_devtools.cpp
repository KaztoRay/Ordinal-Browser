#include "userscript_devtools.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QWebEngineScriptCollection>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMap>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// UserScript
// ============================================================

QJsonObject UserScript::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["namespace"] = namespace_;
    obj["version"] = version;
    obj["description"] = description;
    obj["author"] = author;
    obj["enabled"] = enabled;
    obj["match"] = QJsonArray::fromStringList(match);
    obj["runAt"] = runAt;
    obj["installed"] = installed.toString(Qt::ISODate);
    return obj;
}

UserScript UserScript::fromSource(const QString& source)
{
    UserScript script;
    script.source = source;
    script.name = extractMetaValue(source, "name");
    script.namespace_ = extractMetaValue(source, "namespace");
    script.version = extractMetaValue(source, "version");
    script.description = extractMetaValue(source, "description");
    script.author = extractMetaValue(source, "author");
    script.match = extractMetaValues(source, "match");
    script.include = extractMetaValues(source, "include");
    script.exclude = extractMetaValues(source, "exclude");
    script.runAt = extractMetaValue(source, "run-at");
    if (script.runAt.isEmpty()) script.runAt = "document-idle";
    script.installed = QDateTime::currentDateTime();
    script.updated = script.installed;
    return script;
}

QString UserScript::extractMetaValue(const QString& source, const QString& key)
{
    QRegularExpression rx("@" + QRegularExpression::escape(key) + "\\s+(.+)");
    auto match = rx.match(source);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QStringList UserScript::extractMetaValues(const QString& source, const QString& key)
{
    QStringList values;
    QRegularExpression rx("@" + QRegularExpression::escape(key) + "\\s+(.+)");
    auto it = rx.globalMatch(source);
    while (it.hasNext()) {
        values.append(it.next().captured(1).trimmed());
    }
    return values;
}

// ============================================================
// UserScriptManager
// ============================================================

UserScriptManager::UserScriptManager(const QString& scriptsDir,
                                       QWebEngineProfile* profile,
                                       QObject* parent)
    : QObject(parent)
    , m_scriptsDir(scriptsDir)
    , m_profile(profile)
{
    QDir().mkpath(scriptsDir);
    loadScripts();
}

UserScriptManager::~UserScriptManager() = default;

int64_t UserScriptManager::installScript(const QString& source)
{
    // 메타데이터 블록 확인
    if (!source.contains("==UserScript==")) {
        emit scriptError("유효한 UserScript가 아닙니다 (==UserScript== 블록 필요)");
        return -1;
    }

    UserScript script = UserScript::fromSource(source);
    if (script.name.isEmpty()) {
        emit scriptError("스크립트 이름이 없습니다 (@name 필수)");
        return -1;
    }

    // 이미 설치되어 있으면 업데이트
    auto* existing = findByName(script.name);
    if (existing) {
        existing->source = source;
        existing->version = script.version;
        existing->updated = QDateTime::currentDateTime();
        saveScript(*existing);
        removeInjection(existing->id);
        injectScript(*existing);
        std::cout << "[UserScript] 업데이트: " << script.name.toStdString() << std::endl;
        return existing->id;
    }

    script.id = m_nextId++;
    m_scripts.append(script);
    saveScript(script);
    injectScript(script);

    std::cout << "[UserScript] 설치: " << script.name.toStdString()
              << " v" << script.version.toStdString() << std::endl;

    emit scriptInstalled(script);
    return script.id;
}

bool UserScriptManager::installFromUrl(const QUrl& url)
{
    auto* manager = new QNetworkAccessManager(this);
    QUrl scriptUrl(url);
    QNetworkRequest request(scriptUrl);

    auto* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString source = QString::fromUtf8(reply->readAll());
            installScript(source);
        } else {
            emit scriptError("다운로드 실패: " + reply->errorString());
        }
        reply->deleteLater();
        manager->deleteLater();
    });
    return true;
}

bool UserScriptManager::removeScript(int64_t id)
{
    for (int i = 0; i < m_scripts.size(); ++i) {
        if (m_scripts[i].id == id) {
            removeInjection(id);

            // 파일 삭제
            QString filePath = m_scriptsDir + "/" + QString::number(id) + ".user.js";
            QFile::remove(filePath);

            m_scripts.removeAt(i);
            emit scriptRemoved(id);
            return true;
        }
    }
    return false;
}

bool UserScriptManager::updateScript(int64_t id, const QString& newSource)
{
    auto* script = findScript(id);
    if (!script) return false;

    auto updated = UserScript::fromSource(newSource);
    script->source = newSource;
    script->version = updated.version;
    script->match = updated.match;
    script->include = updated.include;
    script->exclude = updated.exclude;
    script->runAt = updated.runAt;
    script->updated = QDateTime::currentDateTime();

    saveScript(*script);
    removeInjection(id);
    if (script->enabled) injectScript(*script);
    return true;
}

bool UserScriptManager::enableScript(int64_t id)
{
    auto* script = findScript(id);
    if (!script || script->enabled) return false;
    script->enabled = true;
    injectScript(*script);
    saveScript(*script);
    return true;
}

bool UserScriptManager::disableScript(int64_t id)
{
    auto* script = findScript(id);
    if (!script || !script->enabled) return false;
    script->enabled = false;
    removeInjection(id);
    saveScript(*script);
    return true;
}

void UserScriptManager::reloadAll()
{
    if (!m_profile) return;
    for (const auto& script : m_scripts) {
        removeInjection(script.id);
        if (script.enabled) injectScript(script);
    }
}

UserScript* UserScriptManager::findScript(int64_t id)
{
    for (auto& script : m_scripts) {
        if (script.id == id) return &script;
    }
    return nullptr;
}

UserScript* UserScriptManager::findByName(const QString& name)
{
    for (auto& script : m_scripts) {
        if (script.name == name) return &script;
    }
    return nullptr;
}

bool UserScriptManager::matchesUrl(const UserScript& script, const QUrl& url) const
{
    QString urlStr = url.toString();

    // @exclude 우선
    for (const auto& pattern : script.exclude) {
        if (matchesPattern(urlStr, pattern)) return false;
    }

    // @match
    for (const auto& pattern : script.match) {
        if (matchesPattern(urlStr, pattern)) return true;
    }

    // @include
    for (const auto& pattern : script.include) {
        if (matchesPattern(urlStr, pattern)) return true;
    }

    // 매치 패턴이 없으면 전체 적용
    return script.match.isEmpty() && script.include.isEmpty();
}

void UserScriptManager::loadScripts()
{
    QDir dir(m_scriptsDir);
    auto files = dir.entryList({"*.user.js"}, QDir::Files);
    for (const auto& file : files) {
        QFile f(m_scriptsDir + "/" + file);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QString source = QString::fromUtf8(f.readAll());
        auto script = UserScript::fromSource(source);

        // ID를 파일 이름에서 추출
        QString baseName = QFileInfo(file).baseName();
        script.id = baseName.toLongLong();
        if (script.id == 0) script.id = m_nextId++;

        m_scripts.append(script);
        if (script.enabled) injectScript(script);

        if (script.id >= m_nextId) m_nextId = script.id + 1;
    }

    // 메타 파일 로드
    QFile metaFile(m_scriptsDir + "/scripts.json");
    if (metaFile.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(metaFile.readAll());
        auto arr = doc.array();
        for (const auto& val : arr) {
            auto obj = val.toObject();
            int64_t id = obj["id"].toInteger();
            bool enabled = obj["enabled"].toBool(true);
            auto* script = findScript(id);
            if (script) {
                script->enabled = enabled;
                if (!enabled) removeInjection(id);
            }
        }
    }
}

void UserScriptManager::saveScript(const UserScript& script)
{
    // JS 파일 저장
    QString filePath = m_scriptsDir + "/" + QString::number(script.id) + ".user.js";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(script.source.toUtf8());
    }

    // 메타 정보 저장
    QJsonArray arr;
    for (const auto& s : m_scripts) {
        arr.append(s.toJson());
    }
    QFile metaFile(m_scriptsDir + "/scripts.json");
    if (metaFile.open(QIODevice::WriteOnly)) {
        metaFile.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

void UserScriptManager::injectScript(const UserScript& script)
{
    if (!m_profile) return;

    QWebEngineScript webScript;
    webScript.setName("userscript_" + QString::number(script.id));

    // GM API 래퍼 + 실제 스크립트
    QString wrapped = QString(
        "(function() {\n"
        "  'use strict';\n"
        "  var GM_info = { script: { name: '%1', version: '%2' } };\n"
        "  var GM_getValue = function(k,d) { try { return JSON.parse(localStorage.getItem('gm_'+k)); } catch(e) { return d; } };\n"
        "  var GM_setValue = function(k,v) { localStorage.setItem('gm_'+k, JSON.stringify(v)); };\n"
        "  var GM_deleteValue = function(k) { localStorage.removeItem('gm_'+k); };\n"
        "  var GM_addStyle = function(css) { var s=document.createElement('style'); s.textContent=css; document.head.appendChild(s); };\n"
        "  var GM_xmlhttpRequest = function(opts) { fetch(opts.url, {method:opts.method||'GET'}).then(r=>r.text()).then(opts.onload); };\n"
        "  var GM_log = function(msg) { console.log('[UserScript] '+msg); };\n"
        "  %3\n"
        "})();\n"
    ).arg(QString(script.name).replace("'", "\\'"),
          QString(script.version).replace("'", "\\'"),
          script.source);

    webScript.setSourceCode(wrapped);

    // run-at 매핑
    if (script.runAt == "document-start") {
        webScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    } else if (script.runAt == "document-end") {
        webScript.setInjectionPoint(QWebEngineScript::DocumentReady);
    } else {
        webScript.setInjectionPoint(QWebEngineScript::Deferred);
    }

    webScript.setWorldId(QWebEngineScript::ApplicationWorld);
    webScript.setRunsOnSubFrames(false);

    m_profile->scripts()->insert(webScript);
}

void UserScriptManager::removeInjection(int64_t id)
{
    if (!m_profile) return;

    auto* scripts = m_profile->scripts();
    QString name = "userscript_" + QString::number(id);

    auto allScripts = scripts->toList();
    for (const auto& s : allScripts) {
        if (s.name() == name) {
            scripts->remove(s);
            break;
        }
    }
}

bool UserScriptManager::matchesPattern(const QString& url, const QString& pattern) const
{
    if (pattern == "*") return true;
    if (pattern == "<all_urls>") return true;

    // Tampermonkey 스타일 와일드카드 매칭
    QString regexStr = QRegularExpression::escape(pattern);
    regexStr.replace("\\*", ".*");
    QRegularExpression rx("^" + regexStr + "$", QRegularExpression::CaseInsensitiveOption);
    return rx.match(url).hasMatch();
}

// ============================================================
// NetworkMonitor
// ============================================================

NetworkMonitor::NetworkMonitor(QObject* parent)
    : QObject(parent)
{
}

void NetworkMonitor::recordRequest(const NetworkRequest& req)
{
    NetworkRequest r = req;
    r.id = m_nextId++;
    m_requests.append(r);
    emit requestRecorded(r);

    if (r.blocked) {
        emit requestBlocked(r);
    }
}

void NetworkMonitor::clear()
{
    m_requests.clear();
}

QList<NetworkRequest> NetworkMonitor::blockedRequests() const
{
    QList<NetworkRequest> blocked;
    for (const auto& req : m_requests) {
        if (req.blocked) blocked.append(req);
    }
    return blocked;
}

int NetworkMonitor::blockedCount() const
{
    int count = 0;
    for (const auto& req : m_requests) {
        if (req.blocked) count++;
    }
    return count;
}

int64_t NetworkMonitor::totalBytes() const
{
    int64_t total = 0;
    for (const auto& req : m_requests) {
        total += req.responseSize;
    }
    return total;
}

NetworkMonitor::Stats NetworkMonitor::getStats() const
{
    Stats stats;
    stats.totalRequests = m_requests.size();
    double totalTime = 0;

    for (const auto& req : m_requests) {
        if (req.blocked) stats.blockedRequests++;
        stats.totalBytes += req.responseSize;
        totalTime += req.duration();
        stats.requestsByType[req.resourceType]++;
    }

    if (stats.totalRequests > 0) {
        stats.avgResponseTime = totalTime / stats.totalRequests;
    }

    return stats;
}

// ============================================================
// DevToolsExtended
// ============================================================

DevToolsExtended::DevToolsExtended(QObject* parent)
    : QObject(parent)
{
}

void DevToolsExtended::addConsoleEntry(ConsoleEntry::Level level, const QString& message,
                                        const QString& source, int line)
{
    ConsoleEntry entry;
    entry.level = level;
    entry.message = message;
    entry.source = source;
    entry.line = line;
    entry.timestamp = QDateTime::currentDateTime();
    m_console.append(entry);
    emit consoleEntryAdded(entry);
}

void DevToolsExtended::clearConsole()
{
    m_console.clear();
}

QString DevToolsExtended::generateInspectorScript()
{
    return R"(
(function() {
    document.addEventListener('mouseover', function(e) {
        var prev = document.querySelector('.ordinal-inspect-highlight');
        if (prev) prev.classList.remove('ordinal-inspect-highlight');
        e.target.classList.add('ordinal-inspect-highlight');
    });

    var style = document.createElement('style');
    style.textContent = '.ordinal-inspect-highlight { outline: 2px solid #4285f4 !important; outline-offset: 1px; }';
    document.head.appendChild(style);

    document.addEventListener('click', function(e) {
        if (document.querySelector('.ordinal-inspect-highlight')) {
            e.preventDefault();
            var el = e.target;
            var info = {
                tag: el.tagName,
                id: el.id,
                classes: Array.from(el.classList),
                rect: el.getBoundingClientRect(),
                text: el.textContent.substring(0, 100),
                attributes: {}
            };
            for (var attr of el.attributes) {
                info.attributes[attr.name] = attr.value;
            }
            console.log('[Inspector]', JSON.stringify(info, null, 2));
        }
    });
})();
    )";
}

QString DevToolsExtended::generatePerformanceScript()
{
    return R"(
(function() {
    var perf = window.performance;
    var timing = perf.timing;
    var navStart = timing.navigationStart;

    var metrics = {
        dns: timing.domainLookupEnd - timing.domainLookupStart,
        tcp: timing.connectEnd - timing.connectStart,
        ttfb: timing.responseStart - navStart,
        download: timing.responseEnd - timing.responseStart,
        domParsing: timing.domInteractive - timing.responseEnd,
        domComplete: timing.domComplete - navStart,
        load: timing.loadEventEnd - navStart,
        resources: perf.getEntriesByType('resource').length,
        transferSize: perf.getEntriesByType('resource').reduce(function(sum, r) {
            return sum + (r.transferSize || 0);
        }, 0)
    };

    console.log('[Performance]', JSON.stringify(metrics, null, 2));
    return metrics;
})();
    )";
}

QString DevToolsExtended::generateCookieViewerScript()
{
    return R"(
(function() {
    var cookies = document.cookie.split(';').map(function(c) {
        var parts = c.trim().split('=');
        return {
            name: parts[0],
            value: parts.slice(1).join('='),
            domain: window.location.hostname
        };
    });

    console.log('[Cookies] ' + cookies.length + ' cookies:');
    cookies.forEach(function(c) {
        console.log('  ' + c.name + ' = ' + c.value.substring(0, 50));
    });

    return cookies;
})();
    )";
}

} // namespace Engine
} // namespace Ordinal
