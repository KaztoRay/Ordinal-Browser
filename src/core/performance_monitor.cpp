#include "performance_monitor.h"
#include <QProcess>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Ordinal {
namespace Core {

PerformanceMonitor::PerformanceMonitor(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PerformanceMonitor::onSampleTick);
    m_frameTimesMs.resize(FRAME_RING_SIZE, 0.0);
}

PerformanceMonitor::~PerformanceMonitor()
{
    stop();
}

void PerformanceMonitor::start(int intervalMs)
{
    m_intervalMs = intervalMs;
    m_timer->start(m_intervalMs);
}

void PerformanceMonitor::stop()
{
    m_timer->stop();
}

bool PerformanceMonitor::isRunning() const
{
    return m_timer->isActive();
}

void PerformanceMonitor::setSamplingInterval(int ms)
{
    m_intervalMs = ms;
    if (m_timer->isActive()) {
        m_timer->setInterval(ms);
    }
}

// --------------- Per-tab resource usage ---------------

TabResourceUsage PerformanceMonitor::tabUsage(int tabId) const
{
    QMutexLocker lock(&m_mutex);
    return m_tabUsages.value(tabId);
}

QList<TabResourceUsage> PerformanceMonitor::allTabUsages() const
{
    QMutexLocker lock(&m_mutex);
    return m_tabUsages.values();
}

QList<int> PerformanceMonitor::topTabsByCpu(int count) const
{
    QMutexLocker lock(&m_mutex);
    auto list = m_tabUsages.values();
    std::sort(list.begin(), list.end(), [](const TabResourceUsage& a, const TabResourceUsage& b) {
        return a.cpuPercent > b.cpuPercent;
    });
    QList<int> result;
    for (int i = 0; i < std::min(count, (int)list.size()); ++i) {
        result.append(list[i].tabId);
    }
    return result;
}

QList<int> PerformanceMonitor::topTabsByMemory(int count) const
{
    QMutexLocker lock(&m_mutex);
    auto list = m_tabUsages.values();
    std::sort(list.begin(), list.end(), [](const TabResourceUsage& a, const TabResourceUsage& b) {
        return a.memoryBytes > b.memoryBytes;
    });
    QList<int> result;
    for (int i = 0; i < std::min(count, (int)list.size()); ++i) {
        result.append(list[i].tabId);
    }
    return result;
}

// --------------- Global snapshot ---------------

BrowserResourceSnapshot PerformanceMonitor::globalSnapshot() const
{
    QMutexLocker lock(&m_mutex);
    return m_globalSnapshot;
}

// --------------- Frame timing ---------------

FrameTimingInfo PerformanceMonitor::frameTiming() const
{
    QMutexLocker lock(&m_mutex);
    return m_frameTiming;
}

void PerformanceMonitor::recordFrame(double frameTimeMs)
{
    QMutexLocker lock(&m_mutex);
    m_frameTimesMs[m_frameRingIdx] = frameTimeMs;
    m_frameRingIdx = (m_frameRingIdx + 1) % FRAME_RING_SIZE;
}

// --------------- Navigation timing ---------------

void PerformanceMonitor::recordNavigation(const NavigationTiming& timing)
{
    QMutexLocker lock(&m_mutex);
    m_navHistory[timing.tabId].prepend(timing);
    // Limit history per tab
    if (m_navHistory[timing.tabId].size() > 100) {
        m_navHistory[timing.tabId].removeLast();
    }

    emit navigationCompleted(timing);
}

NavigationTiming PerformanceMonitor::lastNavigation(int tabId) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_navHistory.find(tabId);
    if (it != m_navHistory.end() && !it.value().isEmpty()) {
        return it.value().first();
    }
    return {};
}

QList<NavigationTiming> PerformanceMonitor::navigationHistory(int tabId, int limit) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_navHistory.find(tabId);
    if (it == m_navHistory.end()) return {};
    return it.value().mid(0, limit);
}

// --------------- Alerts & thresholds ---------------

void PerformanceMonitor::setMemoryThreshold(int64_t bytes)
{
    m_memoryThreshold = bytes;
}

void PerformanceMonitor::setCpuThreshold(double percent)
{
    m_cpuThreshold = percent;
}

int64_t PerformanceMonitor::memoryThreshold() const
{
    return m_memoryThreshold;
}

double PerformanceMonitor::cpuThreshold() const
{
    return m_cpuThreshold;
}

PerformanceMonitor::MemoryPressure PerformanceMonitor::currentMemoryPressure() const
{
    return m_currentPressure;
}

// --------------- Resource suggestions ---------------

QList<PerformanceMonitor::Suggestion> PerformanceMonitor::optimizationSuggestions() const
{
    QMutexLocker lock(&m_mutex);
    QList<Suggestion> suggestions;

    for (auto it = m_tabUsages.constBegin(); it != m_tabUsages.constEnd(); ++it) {
        const auto& usage = it.value();

        if (usage.cpuPercent > 50.0) {
            suggestions.append({usage.tabId,
                QString("CPU usage %.1f%% (high)").arg(usage.cpuPercent),
                "sleep"});
        }

        if (usage.memoryBytes > 500 * 1024 * 1024) { // > 500MB
            suggestions.append({usage.tabId,
                QString("Memory usage %1 MB (excessive)")
                    .arg(usage.memoryBytes / (1024 * 1024)),
                "close"});
        }

        if (usage.domNodeCount > 10000) {
            suggestions.append({usage.tabId,
                QString("DOM node count %1 (very large)").arg(usage.domNodeCount),
                "reduce"});
        }
    }

    return suggestions;
}

// --------------- Serialization ---------------

QJsonObject PerformanceMonitor::toJson() const
{
    QMutexLocker lock(&m_mutex);
    QJsonObject root;

    // Global
    QJsonObject global;
    global["totalMemoryMB"] = (double)(m_globalSnapshot.totalMemoryBytes / (1024 * 1024));
    global["totalCpuPercent"] = m_globalSnapshot.totalCpuPercent;
    global["openTabs"] = m_globalSnapshot.openTabCount;
    global["sleepingTabs"] = m_globalSnapshot.sleepingTabCount;
    global["networkInKB"] = (double)(m_globalSnapshot.totalNetworkIn / 1024);
    global["networkOutKB"] = (double)(m_globalSnapshot.totalNetworkOut / 1024);
    global["gpuMemoryMB"] = (double)(m_globalSnapshot.gpuMemoryBytes / (1024 * 1024));
    global["diskCacheMB"] = (double)(m_globalSnapshot.diskCacheBytes / (1024 * 1024));
    global["memoryPressure"] = static_cast<int>(m_currentPressure);
    root["global"] = global;

    // Frame timing
    QJsonObject frame;
    frame["fps"] = m_frameTiming.fps;
    frame["avgFrameTimeMs"] = m_frameTiming.avgFrameTimeMs;
    frame["p95FrameTimeMs"] = m_frameTiming.p95FrameTimeMs;
    frame["p99FrameTimeMs"] = m_frameTiming.p99FrameTimeMs;
    frame["droppedFrames"] = m_frameTiming.droppedFrames;
    root["frameTiming"] = frame;

    // Tab usages
    QJsonArray tabs;
    for (auto it = m_tabUsages.constBegin(); it != m_tabUsages.constEnd(); ++it) {
        tabs.append(tabUsageToJson(it.key()));
    }
    root["tabs"] = tabs;

    return root;
}

QJsonObject PerformanceMonitor::tabUsageToJson(int tabId) const
{
    auto usage = m_tabUsages.value(tabId);
    QJsonObject obj;
    obj["tabId"] = usage.tabId;
    obj["cpuPercent"] = usage.cpuPercent;
    obj["memoryMB"] = (double)(usage.memoryBytes / (1024 * 1024));
    obj["networkInKB"] = (double)(usage.networkBytesIn / 1024);
    obj["networkOutKB"] = (double)(usage.networkBytesOut / 1024);
    obj["jsHeapMB"] = (double)(usage.jsHeapSizeBytes / (1024 * 1024));
    obj["domNodes"] = usage.domNodeCount;
    obj["timers"] = usage.activeTimersCount;
    obj["workers"] = usage.activeWorkersCount;
    obj["fps"] = usage.fps;
    return obj;
}

// --------------- Private slots ---------------

void PerformanceMonitor::onSampleTick()
{
    sampleTabResources();
    sampleGlobalResources();
    updateFrameStats();
    checkThresholds();

    emit sampled(m_globalSnapshot);
}

// --------------- Private helpers ---------------

void PerformanceMonitor::sampleTabResources()
{
    QMutexLocker lock(&m_mutex);
    // In a real implementation, this would query each V8 isolate's
    // memory stats, DOM counters, and per-process CPU usage.
    // For now, keep existing entries but update timestamp.
    auto now = QDateTime::currentDateTimeUtc();
    for (auto it = m_tabUsages.begin(); it != m_tabUsages.end(); ++it) {
        it.value().sampledAt = now;
    }
}

void PerformanceMonitor::sampleGlobalResources()
{
    QMutexLocker lock(&m_mutex);
    auto now = QDateTime::currentDateTimeUtc();

    int64_t totalMem = 0;
    double totalCpu = 0.0;
    int64_t totalIn = 0, totalOut = 0;

    for (auto it = m_tabUsages.constBegin(); it != m_tabUsages.constEnd(); ++it) {
        totalMem += it.value().memoryBytes;
        totalCpu += it.value().cpuPercent;
        totalIn  += it.value().networkBytesIn;
        totalOut += it.value().networkBytesOut;
    }

    m_globalSnapshot.totalMemoryBytes = totalMem;
    m_globalSnapshot.totalCpuPercent = totalCpu;
    m_globalSnapshot.totalNetworkIn = totalIn;
    m_globalSnapshot.totalNetworkOut = totalOut;
    m_globalSnapshot.openTabCount = m_tabUsages.size();
    m_globalSnapshot.sampledAt = now;

    // Detect memory pressure
    MemoryPressure newPressure = MemoryPressure::Normal;
    if (totalMem > m_memoryThreshold) {
        newPressure = MemoryPressure::Critical;
    } else if (totalMem > m_memoryThreshold * 3 / 4) {
        newPressure = MemoryPressure::Warning;
    }

    if (newPressure != m_currentPressure) {
        m_currentPressure = newPressure;
        emit memoryPressureChanged(m_currentPressure);
    }
}

void PerformanceMonitor::checkThresholds()
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_tabUsages.constBegin(); it != m_tabUsages.constEnd(); ++it) {
        if (it.value().cpuPercent > m_cpuThreshold) {
            emit tabHighCpu(it.key(), it.value().cpuPercent);
        }
        if (it.value().memoryBytes > m_memoryThreshold / m_tabUsages.size()) {
            emit tabHighMemory(it.key(), it.value().memoryBytes);
        }
    }

    // Check for optimization opportunities
    auto suggestions = optimizationSuggestions();
    for (const auto& s : suggestions) {
        emit optimizationAvailable(s);
    }
}

void PerformanceMonitor::updateFrameStats()
{
    QMutexLocker lock(&m_mutex);

    // Collect valid frame times
    QList<double> valid;
    for (double ft : m_frameTimesMs) {
        if (ft > 0.0) valid.append(ft);
    }

    if (valid.isEmpty()) return;

    std::sort(valid.begin(), valid.end());

    double sum = std::accumulate(valid.begin(), valid.end(), 0.0);
    m_frameTiming.avgFrameTimeMs = sum / valid.size();
    m_frameTiming.totalFrames = valid.size();

    int p95Idx = static_cast<int>(valid.size() * 0.95);
    int p99Idx = static_cast<int>(valid.size() * 0.99);
    m_frameTiming.p95FrameTimeMs = valid[std::min(p95Idx, (int)valid.size() - 1)];
    m_frameTiming.p99FrameTimeMs = valid[std::min(p99Idx, (int)valid.size() - 1)];

    if (m_frameTiming.avgFrameTimeMs > 0) {
        m_frameTiming.fps = 1000.0 / m_frameTiming.avgFrameTimeMs;
    }

    // Count dropped frames (> 33ms = below 30fps threshold)
    int dropped = 0;
    for (double ft : valid) {
        if (ft > 33.33) ++dropped;
    }
    m_frameTiming.droppedFrames = dropped;

    if (m_frameTiming.fps < 30.0 && dropped > 5) {
        emit frameDrop(dropped, m_frameTiming.fps);
    }
}

} // namespace Core
} // namespace Ordinal
