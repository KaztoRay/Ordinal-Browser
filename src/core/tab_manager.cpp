#include "tab_manager.h"
#include "tab.h"
#include <QVariant>
#include <algorithm>

namespace Ordinal {
namespace Core {

TabManager::TabManager(QObject* parent)
    : QObject(parent)
{
    m_sleepTimer = new QTimer(this);
    m_sleepTimer->setInterval(60000); // check every 60 seconds
    connect(m_sleepTimer, &QTimer::timeout, this, &TabManager::onSleepCheckTimer);
    m_sleepTimer->start();
}

TabManager::~TabManager()
{
    for (auto* t : m_tabs) {
        delete t;
    }
    m_tabs.clear();
}

// --------------- Tab lifecycle ---------------

int TabManager::createTab(const QUrl& url, int groupId)
{
    int id = nextTabId();
    auto* t = new Tab(id, this);
    m_tabs.insert(id, t);

    TabMeta meta;
    meta.tabId = id;
    meta.lastActiveAt = QDateTime::currentDateTimeUtc();
    m_meta.insert(id, meta);

    if (groupId >= 0 && m_groups.contains(groupId)) {
        addTabToGroup(id, groupId);
    }

    if (!url.isEmpty()) {
        t->navigate(url);
    }

    // First tab becomes active automatically
    if (m_activeTabId < 0) {
        m_activeTabId = id;
    }

    emit tabCreated(id);
    return id;
}

void TabManager::closeTab(int tabId)
{
    if (!m_tabs.contains(tabId)) return;

    auto* t = m_tabs.value(tabId);
    auto& meta = m_meta[tabId];

    // Save to closed-tabs stack
    ClosedTabEntry entry;
    entry.tabId = tabId;
    entry.title = t->title();
    entry.url = t->url();
    entry.closedAt = QDateTime::currentDateTimeUtc();
    entry.groupId = meta.groupId;
    // entry.sessionState = t->saveState(); // if available
    pushClosedTab(entry);

    // Remove from group
    if (meta.groupId >= 0 && m_groups.contains(meta.groupId)) {
        m_groups[meta.groupId].tabIds.removeAll(tabId);
    }

    m_tabs.remove(tabId);
    m_meta.remove(tabId);
    delete t;

    // If this was the active tab, activate the next one
    if (m_activeTabId == tabId) {
        m_activeTabId = m_tabs.isEmpty() ? -1 : m_tabs.keys().last();
        if (m_activeTabId >= 0) {
            emit tabActivated(m_activeTabId);
        }
    }

    emit tabClosed(tabId);
}

void TabManager::activateTab(int tabId)
{
    if (!m_tabs.contains(tabId)) return;
    if (m_activeTabId == tabId) return;

    m_activeTabId = tabId;
    markTabActive(tabId);

    // Wake if sleeping
    if (isTabSleeping(tabId)) {
        wakeTab(tabId);
    }

    emit tabActivated(tabId);
}

int TabManager::activeTabId() const
{
    return m_activeTabId;
}

QList<int> TabManager::tabIds() const
{
    return m_tabs.keys();
}

Tab* TabManager::tab(int tabId) const
{
    return m_tabs.value(tabId, nullptr);
}

// --------------- Pin / Mute ---------------

void TabManager::pinTab(int tabId, bool pinned)
{
    if (!m_meta.contains(tabId)) return;
    if (m_meta[tabId].pinned == pinned) return;

    m_meta[tabId].pinned = pinned;
    emit tabPinChanged(tabId, pinned);
}

bool TabManager::isTabPinned(int tabId) const
{
    return m_meta.contains(tabId) ? m_meta[tabId].pinned : false;
}

void TabManager::muteTab(int tabId, bool muted)
{
    if (!m_meta.contains(tabId)) return;
    if (m_meta[tabId].muted == muted) return;

    m_meta[tabId].muted = muted;
    emit tabMuteChanged(tabId, muted);
}

bool TabManager::isTabMuted(int tabId) const
{
    return m_meta.contains(tabId) ? m_meta[tabId].muted : false;
}

// --------------- Groups ---------------

int TabManager::createGroup(const QString& label, const QColor& color)
{
    int id = nextGroupId();
    TabGroup grp;
    grp.id = id;
    grp.label = label;
    grp.color = color;
    m_groups.insert(id, grp);
    emit groupCreated(id);
    return id;
}

void TabManager::deleteGroup(int groupId, bool closeTabs)
{
    if (!m_groups.contains(groupId)) return;

    auto& grp = m_groups[groupId];
    QList<int> tabs = grp.tabIds;

    if (closeTabs) {
        for (int tid : tabs) {
            closeTab(tid);
        }
    } else {
        // Just ungroup all tabs
        for (int tid : tabs) {
            if (m_meta.contains(tid)) {
                m_meta[tid].groupId = -1;
                emit tabGroupChanged(tid, -1);
            }
        }
    }

    m_groups.remove(groupId);
    emit groupDeleted(groupId);
}

void TabManager::renameGroup(int groupId, const QString& label)
{
    if (!m_groups.contains(groupId)) return;
    m_groups[groupId].label = label;
    emit groupChanged(groupId);
}

void TabManager::recolorGroup(int groupId, const QColor& color)
{
    if (!m_groups.contains(groupId)) return;
    m_groups[groupId].color = color;
    emit groupChanged(groupId);
}

void TabManager::collapseGroup(int groupId, bool collapsed)
{
    if (!m_groups.contains(groupId)) return;
    m_groups[groupId].collapsed = collapsed;
    emit groupChanged(groupId);
}

void TabManager::addTabToGroup(int tabId, int groupId)
{
    if (!m_meta.contains(tabId)) return;
    if (!m_groups.contains(groupId)) return;

    // Remove from old group
    int oldGroup = m_meta[tabId].groupId;
    if (oldGroup >= 0 && m_groups.contains(oldGroup)) {
        m_groups[oldGroup].tabIds.removeAll(tabId);
    }

    m_meta[tabId].groupId = groupId;
    m_groups[groupId].tabIds.append(tabId);
    emit tabGroupChanged(tabId, groupId);
}

void TabManager::removeTabFromGroup(int tabId)
{
    if (!m_meta.contains(tabId)) return;
    int gid = m_meta[tabId].groupId;
    if (gid < 0) return;

    if (m_groups.contains(gid)) {
        m_groups[gid].tabIds.removeAll(tabId);
    }
    m_meta[tabId].groupId = -1;
    emit tabGroupChanged(tabId, -1);
}

int TabManager::tabGroup(int tabId) const
{
    return m_meta.contains(tabId) ? m_meta[tabId].groupId : -1;
}

QList<TabGroup> TabManager::groups() const
{
    return m_groups.values();
}

TabGroup TabManager::group(int groupId) const
{
    return m_groups.value(groupId);
}

// --------------- Sleep ---------------

void TabManager::sleepTab(int tabId)
{
    if (!m_meta.contains(tabId)) return;
    if (m_meta[tabId].sleepState == TabSleepState::Sleeping) return;
    if (m_meta[tabId].pinned) return;  // Never sleep pinned tabs

    m_meta[tabId].sleepState = TabSleepState::Sleeping;

    // Capture thumbnail before sleeping
    // TODO: request screenshot from tab renderer
    // t->suspendRendering();

    emit tabSlept(tabId);
}

void TabManager::wakeTab(int tabId)
{
    if (!m_meta.contains(tabId)) return;
    if (m_meta[tabId].sleepState != TabSleepState::Sleeping) return;

    m_meta[tabId].sleepState = TabSleepState::WakingUp;
    // TODO: resume V8 isolate & rendering
    // t->resumeRendering();

    m_meta[tabId].sleepState = TabSleepState::Active;
    m_meta[tabId].lastActiveAt = QDateTime::currentDateTimeUtc();
    emit tabWoke(tabId);
}

bool TabManager::isTabSleeping(int tabId) const
{
    return m_meta.contains(tabId) &&
           m_meta[tabId].sleepState == TabSleepState::Sleeping;
}

void TabManager::setSleepTimeout(int tabId, qint64 ms)
{
    if (m_meta.contains(tabId)) {
        m_meta[tabId].sleepTimeoutMs = ms;
    }
}

void TabManager::setGlobalSleepTimeout(qint64 ms)
{
    m_globalSleepTimeoutMs = ms;
}

void TabManager::enableAutoSleep(bool enable)
{
    m_autoSleepEnabled = enable;
    if (enable && !m_sleepTimer->isActive()) {
        m_sleepTimer->start();
    } else if (!enable && m_sleepTimer->isActive()) {
        m_sleepTimer->stop();
    }
}

// --------------- Search ---------------

QList<int> TabManager::searchTabs(const QString& query) const
{
    QList<int> results;
    if (query.isEmpty()) return results;

    const QString lower = query.toLower();

    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        Tab* t = it.value();
        if (t->title().toLower().contains(lower) ||
            t->url().toString().toLower().contains(lower)) {
            results.append(it.key());
        }
    }
    return results;
}

// --------------- Recently closed ---------------

void TabManager::pushClosedTab(const ClosedTabEntry& entry)
{
    m_closedStack.push(entry);
    // Limit stack size to 50
    while (m_closedStack.size() > 50) {
        m_closedStack.remove(0);
    }
    emit closedTabsChanged();
}

ClosedTabEntry TabManager::popClosedTab()
{
    if (m_closedStack.isEmpty()) return {};
    auto entry = m_closedStack.pop();
    emit closedTabsChanged();
    return entry;
}

QList<ClosedTabEntry> TabManager::closedTabsStack() const
{
    QList<ClosedTabEntry> list;
    for (int i = m_closedStack.size() - 1; i >= 0; --i) {
        list.append(m_closedStack[i]);
    }
    return list;
}

void TabManager::clearClosedTabs()
{
    m_closedStack.clear();
    emit closedTabsChanged();
}

int TabManager::restoreLastClosedTab()
{
    if (m_closedStack.isEmpty()) return -1;

    auto entry = popClosedTab();
    int newId = createTab(entry.url, entry.groupId);
    return newId;
}

// --------------- Thumbnails ---------------

void TabManager::updateThumbnail(int tabId, const QImage& image)
{
    if (!m_meta.contains(tabId)) return;
    m_meta[tabId].thumbnail = image;
    m_meta[tabId].thumbnailAt = QDateTime::currentDateTimeUtc();
    emit thumbnailUpdated(tabId);
}

QImage TabManager::thumbnail(int tabId) const
{
    return m_meta.contains(tabId) ? m_meta[tabId].thumbnail : QImage();
}

// --------------- Drag between windows ---------------

Tab* TabManager::detachTab(int tabId)
{
    if (!m_tabs.contains(tabId)) return nullptr;

    auto* t = m_tabs.take(tabId);
    m_meta.remove(tabId);

    if (m_activeTabId == tabId) {
        m_activeTabId = m_tabs.isEmpty() ? -1 : m_tabs.keys().last();
    }

    t->setParent(nullptr);
    emit tabDetached(tabId);
    return t;
}

void TabManager::attachTab(Tab* t, int insertAtIndex)
{
    if (!t) return;
    int id = t->id();

    t->setParent(this);
    m_tabs.insert(id, t);

    TabMeta meta;
    meta.tabId = id;
    meta.lastActiveAt = QDateTime::currentDateTimeUtc();
    m_meta.insert(id, meta);

    Q_UNUSED(insertAtIndex) // ordering TBD

    emit tabAttached(id);
}

// --------------- Serialization ---------------

QVariantMap TabManager::toVariantMap() const
{
    QVariantMap map;

    // Tabs
    QVariantList tabList;
    for (auto it = m_meta.constBegin(); it != m_meta.constEnd(); ++it) {
        QVariantMap tm;
        const auto& meta = it.value();
        const auto* t = m_tabs.value(it.key());
        tm["id"] = meta.tabId;
        tm["pinned"] = meta.pinned;
        tm["muted"] = meta.muted;
        tm["groupId"] = meta.groupId;
        if (t) {
            tm["url"] = t->url().toString();
            tm["title"] = t->title();
        }
        tabList.append(tm);
    }
    map["tabs"] = tabList;

    // Groups
    QVariantList groupList;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it) {
        QVariantMap gm;
        gm["id"] = it.value().id;
        gm["label"] = it.value().label;
        gm["color"] = it.value().color.name();
        gm["collapsed"] = it.value().collapsed;
        groupList.append(gm);
    }
    map["groups"] = groupList;

    map["activeTabId"] = m_activeTabId;
    return map;
}

void TabManager::fromVariantMap(const QVariantMap& map)
{
    // Groups first
    auto groupList = map["groups"].toList();
    for (const auto& gv : groupList) {
        auto gm = gv.toMap();
        TabGroup grp;
        grp.id = gm["id"].toInt();
        grp.label = gm["label"].toString();
        grp.color = QColor(gm["color"].toString());
        grp.collapsed = gm["collapsed"].toBool();
        m_groups.insert(grp.id, grp);
        if (grp.id >= m_nextGroupId) m_nextGroupId = grp.id + 1;
    }

    // Tabs
    auto tabList = map["tabs"].toList();
    for (const auto& tv : tabList) {
        auto tm = tv.toMap();
        int id = tm["id"].toInt();
        QUrl url = QUrl(tm["url"].toString());
        int gid = tm["groupId"].toInt(-1);

        auto* t = new Tab(id, this);
        m_tabs.insert(id, t);

        TabMeta meta;
        meta.tabId = id;
        meta.pinned = tm["pinned"].toBool();
        meta.muted = tm["muted"].toBool();
        meta.groupId = gid;
        meta.lastActiveAt = QDateTime::currentDateTimeUtc();
        m_meta.insert(id, meta);

        if (gid >= 0 && m_groups.contains(gid)) {
            m_groups[gid].tabIds.append(id);
        }

        if (!url.isEmpty()) {
            t->navigate(url);
        }

        if (id >= m_nextTabId) m_nextTabId = id + 1;
    }

    m_activeTabId = map.value("activeTabId", -1).toInt();
}

// --------------- Private slots ---------------

void TabManager::onSleepCheckTimer()
{
    if (!m_autoSleepEnabled) return;

    auto now = QDateTime::currentDateTimeUtc();
    for (auto it = m_meta.begin(); it != m_meta.end(); ++it) {
        auto& meta = it.value();
        if (meta.tabId == m_activeTabId) continue;
        if (meta.pinned) continue;
        if (meta.sleepState != TabSleepState::Active) continue;

        qint64 timeout = meta.sleepTimeoutMs > 0 ? meta.sleepTimeoutMs
                                                  : m_globalSleepTimeoutMs;
        qint64 elapsed = meta.lastActiveAt.msecsTo(now);
        if (elapsed >= timeout) {
            sleepTab(meta.tabId);
        }
    }
}

void TabManager::onTabTitleChanged(int tabId, const QString& title)
{
    Q_UNUSED(tabId)
    Q_UNUSED(title)
    // Could be used for quick-search index updates
}

void TabManager::onTabUrlChanged(int tabId, const QUrl& url)
{
    Q_UNUSED(tabId)
    Q_UNUSED(url)
}

// --------------- Private helpers ---------------

int TabManager::nextTabId()
{
    return m_nextTabId++;
}

int TabManager::nextGroupId()
{
    return m_nextGroupId++;
}

void TabManager::markTabActive(int tabId)
{
    if (m_meta.contains(tabId)) {
        m_meta[tabId].lastActiveAt = QDateTime::currentDateTimeUtc();
        m_meta[tabId].sleepState = TabSleepState::Active;
    }
}

} // namespace Core
} // namespace Ordinal
