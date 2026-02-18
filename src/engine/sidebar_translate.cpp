#include "sidebar_translate.h"
#include "data_manager.h"

#include <QHeaderView>
#include <QSplitter>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// SidebarPanel
// ============================================================

SidebarPanel::SidebarPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setFixedWidth(300);
    hide();
}

void SidebarPanel::setManagers(BookmarkManager* bookmarks, HistoryManager* history)
{
    m_bookmarks = bookmarks;
    m_history = history;
    refreshBookmarks();
    refreshHistory();
}

void SidebarPanel::showPanel(Panel panel)
{
    m_stack->setCurrentIndex(static_cast<int>(panel));
    if (!m_visible) {
        m_visible = true;
        show();
        emit panelToggled(true);
    }

    // 패널 전환 시 데이터 새로고침
    if (panel == Bookmarks) refreshBookmarks();
    else if (panel == History) refreshHistory();
}

void SidebarPanel::toggle()
{
    m_visible = !m_visible;
    setVisible(m_visible);
    emit panelToggled(m_visible);
}

void SidebarPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 패널 선택 버튼
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(0);

    m_bookmarkBtn = new QToolButton(this);
    m_bookmarkBtn->setText("★");
    m_bookmarkBtn->setToolTip("북마크");
    m_bookmarkBtn->setCheckable(true);
    m_bookmarkBtn->setChecked(true);
    m_bookmarkBtn->setFixedSize(40, 32);

    m_historyBtn = new QToolButton(this);
    m_historyBtn->setText("🕐");
    m_historyBtn->setToolTip("방문 기록");
    m_historyBtn->setCheckable(true);
    m_historyBtn->setFixedSize(40, 32);

    m_readingListBtn = new QToolButton(this);
    m_readingListBtn->setText("📖");
    m_readingListBtn->setToolTip("읽기 목록");
    m_readingListBtn->setCheckable(true);
    m_readingListBtn->setFixedSize(40, 32);

    m_downloadsBtn = new QToolButton(this);
    m_downloadsBtn->setText("⬇");
    m_downloadsBtn->setToolTip("다운로드");
    m_downloadsBtn->setCheckable(true);
    m_downloadsBtn->setFixedSize(40, 32);

    btnLayout->addWidget(m_bookmarkBtn);
    btnLayout->addWidget(m_historyBtn);
    btnLayout->addWidget(m_readingListBtn);
    btnLayout->addWidget(m_downloadsBtn);
    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);

    // 스택
    m_stack = new QStackedWidget(this);
    setupBookmarksPanel();
    setupHistoryPanel();
    setupReadingListPanel();
    setupDownloadsPanel();
    mainLayout->addWidget(m_stack);

    // 버튼 연결
    auto uncheckAll = [this]() {
        m_bookmarkBtn->setChecked(false);
        m_historyBtn->setChecked(false);
        m_readingListBtn->setChecked(false);
        m_downloadsBtn->setChecked(false);
    };

    connect(m_bookmarkBtn, &QToolButton::clicked, [this, uncheckAll]() {
        uncheckAll(); m_bookmarkBtn->setChecked(true);
        showPanel(Bookmarks);
    });
    connect(m_historyBtn, &QToolButton::clicked, [this, uncheckAll]() {
        uncheckAll(); m_historyBtn->setChecked(true);
        showPanel(History);
    });
    connect(m_readingListBtn, &QToolButton::clicked, [this, uncheckAll]() {
        uncheckAll(); m_readingListBtn->setChecked(true);
        showPanel(ReadingList);
    });
    connect(m_downloadsBtn, &QToolButton::clicked, [this, uncheckAll]() {
        uncheckAll(); m_downloadsBtn->setChecked(true);
        showPanel(Downloads);
    });
}

void SidebarPanel::setupBookmarksPanel()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);

    m_bookmarkSearch = new QLineEdit(widget);
    m_bookmarkSearch->setPlaceholderText("북마크 검색...");
    layout->addWidget(m_bookmarkSearch);

    m_bookmarkTree = new QTreeWidget(widget);
    m_bookmarkTree->setHeaderHidden(true);
    m_bookmarkTree->setIndentation(16);
    layout->addWidget(m_bookmarkTree);

    connect(m_bookmarkTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
        QUrl url = item->data(0, Qt::UserRole).toUrl();
        if (url.isValid()) emit urlSelected(url);
    });

    connect(m_bookmarkSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_bookmarks) return;
        m_bookmarkTree->clear();
        auto results = text.isEmpty() ?
            m_bookmarks->getChildren(m_bookmarks->bookmarkBarId()) :
            m_bookmarks->search(text);
        for (const auto& bm : results) {
            auto* item = new QTreeWidgetItem(m_bookmarkTree);
            item->setText(0, bm.title);
            item->setData(0, Qt::UserRole, bm.url);
            item->setToolTip(0, bm.url.toString());
            item->setIcon(0, bm.isFolder ? style()->standardIcon(QStyle::SP_DirIcon)
                                          : style()->standardIcon(QStyle::SP_FileIcon));
        }
    });

    m_stack->addWidget(widget);
}

void SidebarPanel::setupHistoryPanel()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);

    m_historySearch = new QLineEdit(widget);
    m_historySearch->setPlaceholderText("기록 검색...");
    layout->addWidget(m_historySearch);

    m_historyList = new QListWidget(widget);
    layout->addWidget(m_historyList);

    connect(m_historyList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
        QUrl url = item->data(Qt::UserRole).toUrl();
        if (url.isValid()) emit urlSelected(url);
    });

    connect(m_historySearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_history) return;
        m_historyList->clear();
        auto results = text.isEmpty() ? m_history->getRecent(100) : m_history->search(text);
        for (const auto& entry : results) {
            auto* item = new QListWidgetItem(
                entry.title + "\n" + entry.url.host(), m_historyList);
            item->setData(Qt::UserRole, entry.url);
            item->setToolTip(entry.url.toString());
        }
    });

    m_stack->addWidget(widget);
}

void SidebarPanel::setupReadingListPanel()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* label = new QLabel("📖 읽기 목록", widget);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    m_readingList = new QListWidget(widget);
    layout->addWidget(m_readingList);

    connect(m_readingList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
        QUrl url = item->data(Qt::UserRole).toUrl();
        if (url.isValid()) emit urlSelected(url);
    });

    m_stack->addWidget(widget);
}

void SidebarPanel::setupDownloadsPanel()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* label = new QLabel("⬇ 다운로드", widget);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    m_downloadsList = new QListWidget(widget);
    layout->addWidget(m_downloadsList);

    m_stack->addWidget(widget);
}

void SidebarPanel::refreshBookmarks()
{
    if (!m_bookmarks || !m_bookmarkTree) return;
    m_bookmarkTree->clear();

    auto items = m_bookmarks->getChildren(m_bookmarks->bookmarkBarId());
    for (const auto& bm : items) {
        auto* treeItem = new QTreeWidgetItem(m_bookmarkTree);
        treeItem->setText(0, bm.title);
        treeItem->setData(0, Qt::UserRole, bm.url);
        treeItem->setIcon(0, bm.isFolder ? style()->standardIcon(QStyle::SP_DirIcon)
                                          : style()->standardIcon(QStyle::SP_FileIcon));

        if (bm.isFolder) {
            auto children = m_bookmarks->getChildren(bm.id);
            for (const auto& child : children) {
                auto* childItem = new QTreeWidgetItem(treeItem);
                childItem->setText(0, child.title);
                childItem->setData(0, Qt::UserRole, child.url);
            }
        }
    }
}

void SidebarPanel::refreshHistory()
{
    if (!m_history || !m_historyList) return;
    m_historyList->clear();

    auto entries = m_history->getRecent(100);
    for (const auto& entry : entries) {
        auto* item = new QListWidgetItem(
            entry.title + "\n" + entry.url.host(), m_historyList);
        item->setData(Qt::UserRole, entry.url);
    }
}

// ============================================================
// TranslationEngine
// ============================================================

TranslationEngine::TranslationEngine(QObject* parent)
    : QObject(parent)
{
}

QList<TranslationEngine::Language> TranslationEngine::supportedLanguages()
{
    return {
        {"ko", "한국어"}, {"en", "English"}, {"ja", "日本語"},
        {"zh", "中文"}, {"es", "Español"}, {"fr", "Français"},
        {"de", "Deutsch"}, {"pt", "Português"}, {"ru", "Русский"},
        {"ar", "العربية"}, {"hi", "हिन्दी"}, {"vi", "Tiếng Việt"},
        {"th", "ไทย"}, {"it", "Italiano"}, {"nl", "Nederlands"},
        {"pl", "Polski"}, {"tr", "Türkçe"}, {"id", "Bahasa Indonesia"}
    };
}

void TranslationEngine::translateText(const QString& text, const QString& from, const QString& to)
{
    auto* manager = new QNetworkAccessManager(this);
    QUrl apiUrl(m_apiUrl);
    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["q"] = text;
    body["source"] = from;
    body["target"] = to;
    body["format"] = "text";

    auto* reply = manager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit translationError(reply->errorString());
        } else {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QString translated = doc.object()["translatedText"].toString();
            if (!translated.isEmpty()) {
                emit translationReady(translated);
            } else {
                emit translationError("번역 결과 없음");
            }
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

QString TranslationEngine::generateTranslationScript(const QString& targetLang) const
{
    // 클라이언트 사이드 번역 스크립트 (Google Translate 위젯 삽입)
    return QString(R"(
(function() {
    // Google Translate Element 삽입
    var script = document.createElement('script');
    script.src = 'https://translate.google.com/translate_a/element.js?cb=googleTranslateElementInit';
    document.body.appendChild(script);

    window.googleTranslateElementInit = function() {
        new google.translate.TranslateElement({
            pageLanguage: 'auto',
            includedLanguages: '%1',
            autoDisplay: true
        }, 'google_translate_element');
    };

    var div = document.createElement('div');
    div.id = 'google_translate_element';
    div.style.cssText = 'position:fixed;top:0;right:0;z-index:99999;';
    document.body.insertBefore(div, document.body.firstChild);
})();
    )").arg(targetLang);
}

QString TranslationEngine::detectLanguage(const QString& text)
{
    // 간단한 언어 감지 (첫 100자 분석)
    QString sample = text.left(100);

    // CJK 범위 확인
    int cjkCount = 0, hangulCount = 0, hiraganaCount = 0;
    for (const QChar& c : sample) {
        ushort code = c.unicode();
        if (code >= 0xAC00 && code <= 0xD7AF) hangulCount++;
        else if (code >= 0x3040 && code <= 0x309F) hiraganaCount++;
        else if (code >= 0x4E00 && code <= 0x9FFF) cjkCount++;
    }

    if (hangulCount > sample.length() * 0.2) return "ko";
    if (hiraganaCount > sample.length() * 0.1) return "ja";
    if (cjkCount > sample.length() * 0.2) return "zh";

    return "en"; // 기본값
}

// ============================================================
// ReadingListItem
// ============================================================

QJsonObject ReadingListItem::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["title"] = title;
    obj["url"] = url.toString();
    obj["read"] = read;
    obj["addedAt"] = addedAt.toString(Qt::ISODate);
    obj["excerpt"] = excerpt;
    return obj;
}

// ============================================================
// ReadingListManager
// ============================================================

ReadingListManager::ReadingListManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/readinglist.db")
{
    QDir().mkpath(storagePath);
    initDatabase();
}

ReadingListManager::~ReadingListManager()
{
    if (m_db.isOpen()) m_db.close();
}

void ReadingListManager::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "readinglist");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        std::cerr << "[ReadingList] DB 열기 실패" << std::endl;
        return;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS reading_list (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            url TEXT NOT NULL UNIQUE,
            read INTEGER NOT NULL DEFAULT 0,
            added_at TEXT NOT NULL DEFAULT (datetime('now')),
            excerpt TEXT
        )
    )");
}

int64_t ReadingListManager::addItem(const QString& title, const QUrl& url, const QString& excerpt)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO reading_list (title, url, excerpt) VALUES (?, ?, ?)");
    q.addBindValue(title);
    q.addBindValue(url.toString());
    q.addBindValue(excerpt);
    if (!q.exec()) return -1;

    int64_t id = q.lastInsertId().toLongLong();
    ReadingListItem item;
    item.id = id;
    item.title = title;
    item.url = url;
    item.excerpt = excerpt;
    item.addedAt = QDateTime::currentDateTime();
    emit itemAdded(item);
    return id;
}

bool ReadingListManager::removeItem(int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM reading_list WHERE id = ?");
    q.addBindValue(id);
    if (q.exec()) {
        emit itemRemoved(id);
        return true;
    }
    return false;
}

bool ReadingListManager::markAsRead(int64_t id, bool read)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE reading_list SET read = ? WHERE id = ?");
    q.addBindValue(read ? 1 : 0);
    q.addBindValue(id);
    return q.exec();
}

QList<ReadingListItem> ReadingListManager::getAll() const
{
    QList<ReadingListItem> items;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM reading_list ORDER BY added_at DESC");
    while (q.next()) {
        ReadingListItem item;
        item.id = q.value("id").toLongLong();
        item.title = q.value("title").toString();
        item.url = QUrl(q.value("url").toString());
        item.read = q.value("read").toBool();
        item.addedAt = QDateTime::fromString(q.value("added_at").toString(), Qt::ISODate);
        item.excerpt = q.value("excerpt").toString();
        items.append(item);
    }
    return items;
}

QList<ReadingListItem> ReadingListManager::getUnread() const
{
    QList<ReadingListItem> items;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM reading_list WHERE read = 0 ORDER BY added_at DESC");
    while (q.next()) {
        ReadingListItem item;
        item.id = q.value("id").toLongLong();
        item.title = q.value("title").toString();
        item.url = QUrl(q.value("url").toString());
        item.read = false;
        item.addedAt = QDateTime::fromString(q.value("added_at").toString(), Qt::ISODate);
        item.excerpt = q.value("excerpt").toString();
        items.append(item);
    }
    return items;
}

int ReadingListManager::unreadCount() const
{
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM reading_list WHERE read = 0");
    q.next();
    return q.value(0).toInt();
}

} // namespace Engine
} // namespace Ordinal
