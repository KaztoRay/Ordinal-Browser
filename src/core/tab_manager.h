#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QList>
#include <QStack>
#include <QHash>
#include <QColor>
#include <QImage>
#include <QTimer>
#include <QDateTime>
#include <memory>

namespace Ordinal {
namespace Core {

// Forward declarations
class Tab;

// Tab group — color-coded with a label
struct TabGroup {
    int id = -1;
    QString label;
    QColor color = Qt::blue;
    bool collapsed = false;
    QList<int> tabIds;  // ordered list of tab IDs in this group
};

// Snapshot of a closed tab (for restore)
struct ClosedTabEntry {
    int tabId;
    QString title;
    QUrl url;
    QDateTime closedAt;
    QByteArray sessionState;   // serialized navigation history
    int groupId = -1;
};

// Tab sleep state
enum class TabSleepState {
    Active,
    Sleeping,
    WakingUp
};

// Tab metadata managed by TabManager
struct TabMeta {
    int tabId = -1;
    int groupId = -1;

    bool pinned = false;
    bool muted = false;

    TabSleepState sleepState = TabSleepState::Active;
    QDateTime lastActiveAt;
    qint64 sleepTimeoutMs = 30 * 60 * 1000;  // 30 minutes default

    QImage thumbnail;         // last screenshot
    QDateTime thumbnailAt;
};

class TabManager : public QObject {
    Q_OBJECT
public:
    explicit TabManager(QObject* parent = nullptr);
    ~TabManager() override;

    // --------------- Tab lifecycle ---------------
    int createTab(const QUrl& url = QUrl(), int groupId = -1);
    void closeTab(int tabId);
    void activateTab(int tabId);

    int activeTabId() const;
    QList<int> tabIds() const;
    Tab* tab(int tabId) const;

    // --------------- Pin / Mute ---------------
    void pinTab(int tabId, bool pinned = true);
    bool isTabPinned(int tabId) const;

    void muteTab(int tabId, bool muted = true);
    bool isTabMuted(int tabId) const;

    // --------------- Groups ---------------
    int createGroup(const QString& label, const QColor& color = Qt::blue);
    void deleteGroup(int groupId, bool closeTabs = false);
    void renameGroup(int groupId, const QString& label);
    void recolorGroup(int groupId, const QColor& color);
    void collapseGroup(int groupId, bool collapsed = true);

    void addTabToGroup(int tabId, int groupId);
    void removeTabFromGroup(int tabId);
    int tabGroup(int tabId) const;

    QList<TabGroup> groups() const;
    TabGroup group(int groupId) const;

    // --------------- Sleep ---------------
    void sleepTab(int tabId);
    void wakeTab(int tabId);
    bool isTabSleeping(int tabId) const;
    void setSleepTimeout(int tabId, qint64 ms);
    void setGlobalSleepTimeout(qint64 ms);
    void enableAutoSleep(bool enable);

    // --------------- Search ---------------
    // Returns list of tabIds whose title/url match the query
    QList<int> searchTabs(const QString& query) const;

    // --------------- Recently closed ---------------
    void pushClosedTab(const ClosedTabEntry& entry);
    ClosedTabEntry popClosedTab();
    QList<ClosedTabEntry> closedTabsStack() const;
    void clearClosedTabs();
    int restoreLastClosedTab();     // returns new tabId or -1

    // --------------- Thumbnails ---------------
    void updateThumbnail(int tabId, const QImage& image);
    QImage thumbnail(int tabId) const;

    // --------------- Drag between windows ---------------
    // Detach tab from this manager and hand it off (caller manages the tab)
    Tab* detachTab(int tabId);
    void attachTab(Tab* tab, int insertAtIndex = -1);

    // --------------- Serialization ---------------
    QVariantMap toVariantMap() const;
    void fromVariantMap(const QVariantMap& map);

signals:
    void tabCreated(int tabId);
    void tabClosed(int tabId);
    void tabActivated(int tabId);
    void tabPinChanged(int tabId, bool pinned);
    void tabMuteChanged(int tabId, bool muted);
    void tabGroupChanged(int tabId, int groupId);
    void tabSlept(int tabId);
    void tabWoke(int tabId);
    void thumbnailUpdated(int tabId);
    void groupCreated(int groupId);
    void groupDeleted(int groupId);
    void groupChanged(int groupId);
    void closedTabsChanged();
    void tabDetached(int tabId);
    void tabAttached(int tabId);

private slots:
    void onSleepCheckTimer();
    void onTabTitleChanged(int tabId, const QString& title);
    void onTabUrlChanged(int tabId, const QUrl& url);

private:
    int nextTabId();
    int nextGroupId();

    void markTabActive(int tabId);

    QHash<int, Tab*>      m_tabs;
    QHash<int, TabMeta>   m_meta;
    QHash<int, TabGroup>  m_groups;
    QStack<ClosedTabEntry> m_closedStack;

    int m_activeTabId = -1;
    int m_nextTabId   = 1;
    int m_nextGroupId = 1;

    qint64 m_globalSleepTimeoutMs = 30 * 60 * 1000;
    bool   m_autoSleepEnabled = true;

    QTimer* m_sleepTimer = nullptr;
};

} // namespace Core
} // namespace Ordinal
