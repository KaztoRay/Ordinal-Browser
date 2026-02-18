#pragma once
#include <QObject>
#include <QString>
#include <QHash>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <cstdint>

namespace Ordinal {
namespace Core {

// Per-tab resource usage
struct TabResourceUsage {
    int tabId = -1;
    double cpuPercent = 0.0;
    int64_t memoryBytes = 0;
    int64_t networkBytesIn = 0;
    int64_t networkBytesOut = 0;
    int jsHeapSizeBytes = 0;
    int domNodeCount = 0;
    int activeTimersCount = 0;
    int activeWorkersCount = 0;
    double fps = 0.0;
    QDateTime sampledAt;
};

// Global browser resource snapshot
struct BrowserResourceSnapshot {
    int64_t totalMemoryBytes = 0;
    double totalCpuPercent = 0.0;
    int64_t totalNetworkIn = 0;
    int64_t totalNetworkOut = 0;
    int openTabCount = 0;
    int sleepingTabCount = 0;
    int extensionCount = 0;
    int64_t gpuMemoryBytes = 0;
    int64_t diskCacheBytes = 0;
    QDateTime sampledAt;
};

// Frame timing measurement
struct FrameTimingInfo {
    double avgFrameTimeMs = 0.0;
    double p95FrameTimeMs = 0.0;
    double p99FrameTimeMs = 0.0;
    int droppedFrames = 0;
    int totalFrames = 0;
    double fps = 0.0;
};

// Navigation timing (W3C spec aligned)
struct NavigationTiming {
    int tabId = -1;
    QUrl url;
    int64_t dnsLookupMs = 0;
    int64_t tcpConnectMs = 0;
    int64_t tlsHandshakeMs = 0;
    int64_t requestMs = 0;
    int64_t responseMs = 0;
    int64_t domParseMs = 0;
    int64_t domContentLoadedMs = 0;
    int64_t loadCompleteMs = 0;
    int64_t firstPaintMs = 0;
    int64_t firstContentfulPaintMs = 0;
    int64_t largestContentfulPaintMs = 0;
    int64_t timeToInteractiveMs = 0;
    int64_t totalLoadMs = 0;
    QDateTime timestamp;
};

class PerformanceMonitor : public QObject {
    Q_OBJECT
public:
    explicit PerformanceMonitor(QObject* parent = nullptr);
    ~PerformanceMonitor() override;

    // Sampling control
    void start(int intervalMs = 1000);
    void stop();
    bool isRunning() const;
    void setSamplingInterval(int ms);

    // Per-tab resource usage
    TabResourceUsage tabUsage(int tabId) const;
    QList<TabResourceUsage> allTabUsages() const;
    QList<int> topTabsByCpu(int count = 5) const;
    QList<int> topTabsByMemory(int count = 5) const;

    // Global snapshot
    BrowserResourceSnapshot globalSnapshot() const;

    // Frame timing (rendering performance)
    FrameTimingInfo frameTiming() const;
    void recordFrame(double frameTimeMs);

    // Navigation timing
    void recordNavigation(const NavigationTiming& timing);
    NavigationTiming lastNavigation(int tabId) const;
    QList<NavigationTiming> navigationHistory(int tabId, int limit = 20) const;

    // Alerts & thresholds
    void setMemoryThreshold(int64_t bytes);
    void setCpuThreshold(double percent);
    int64_t memoryThreshold() const;
    double cpuThreshold() const;

    // Memory pressure
    enum class MemoryPressure { Normal, Warning, Critical };
    MemoryPressure currentMemoryPressure() const;

    // Resource suggestions
    struct Suggestion {
        int tabId;
        QString reason;
        QString action; // "sleep", "close", "reduce"
    };
    QList<Suggestion> optimizationSuggestions() const;

    // Serialization for DevTools / UI
    QJsonObject toJson() const;
    QJsonObject tabUsageToJson(int tabId) const;

signals:
    void sampled(const BrowserResourceSnapshot& snapshot);
    void tabHighCpu(int tabId, double percent);
    void tabHighMemory(int tabId, int64_t bytes);
    void memoryPressureChanged(MemoryPressure level);
    void frameDrop(int droppedCount, double fps);
    void navigationCompleted(const NavigationTiming& timing);
    void optimizationAvailable(const Suggestion& suggestion);

private slots:
    void onSampleTick();

private:
    void sampleTabResources();
    void sampleGlobalResources();
    void checkThresholds();
    void updateFrameStats();

    QTimer* m_timer = nullptr;
    int m_intervalMs = 1000;

    mutable QMutex m_mutex;

    QHash<int, TabResourceUsage> m_tabUsages;
    BrowserResourceSnapshot m_globalSnapshot;

    // Frame tracking
    QList<double> m_frameTimesMs;     // ring buffer
    int m_frameRingIdx = 0;
    static constexpr int FRAME_RING_SIZE = 300; // ~5 seconds @ 60fps
    FrameTimingInfo m_frameTiming;

    // Navigation history per tab
    QHash<int, QList<NavigationTiming>> m_navHistory;

    // Thresholds
    int64_t m_memoryThreshold = 4LL * 1024 * 1024 * 1024; // 4 GB
    double m_cpuThreshold = 80.0;

    MemoryPressure m_currentPressure = MemoryPressure::Normal;
};

} // namespace Core
} // namespace Ordinal
