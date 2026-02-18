#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QHash>
#include <QList>
#include <QDateTime>
#include <QFile>
#include <QTimer>
#include <QJsonObject>
#include <QMutex>
#include <memory>
#include <atomic>

namespace Ordinal {
namespace Network {

// Multi-threaded download accelerator with segmented downloading,
// resume capability, and bandwidth management.

enum class DownloadState {
    Queued,
    Connecting,
    Downloading,
    Paused,
    Merging,        // Merging segments
    Completed,
    Failed,
    Cancelled
};

enum class DownloadPriority {
    Low,
    Normal,
    High,
    Critical
};

struct DownloadSegment {
    int segmentIndex = 0;
    int64_t startByte = 0;
    int64_t endByte = 0;
    int64_t downloadedBytes = 0;
    bool completed = false;
    double speedBps = 0.0;
    QString tempFilePath;
};

struct DownloadItem {
    int downloadId = -1;
    QUrl url;
    QUrl referrer;
    QString fileName;
    QString filePath;            // final output path
    QString tempDir;             // temp dir for segments
    QString mimeType;
    DownloadState state = DownloadState::Queued;
    DownloadPriority priority = DownloadPriority::Normal;

    int64_t totalBytes = -1;     // -1 if unknown
    int64_t downloadedBytes = 0;
    double progressPercent = 0.0;
    double speedBps = 0.0;       // bytes per second
    double avgSpeedBps = 0.0;
    int64_t etaSeconds = -1;

    int segmentCount = 0;
    QList<DownloadSegment> segments;

    bool supportsResume = false;  // server supports Range header
    int retryCount = 0;
    int maxRetries = 3;

    QDateTime createdAt;
    QDateTime startedAt;
    QDateTime completedAt;

    QString errorMessage;
    int httpStatus = 0;

    // Metadata
    int tabId = -1;
    QHash<QString, QString> headers;
};

struct DownloadSettings {
    int maxConcurrentDownloads = 3;
    int segmentsPerDownload = 8;          // parallel segments
    int64_t minSegmentSize = 1024 * 1024; // 1 MB minimum per segment
    int64_t maxBandwidthBps = 0;          // 0 = unlimited
    QString defaultDownloadDir;
    bool autoResume = true;
    bool showNotification = true;
    bool autoOpenCompleted = false;
    int retryDelayMs = 3000;
    int connectionTimeoutMs = 30000;
};

class DownloadAccelerator : public QObject {
    Q_OBJECT
public:
    explicit DownloadAccelerator(QObject* parent = nullptr);
    ~DownloadAccelerator() override;

    // --------------- Download management ---------------
    int startDownload(const QUrl& url, const QString& savePath = {},
                      const QHash<QString, QString>& headers = {});
    void pauseDownload(int downloadId);
    void resumeDownload(int downloadId);
    void cancelDownload(int downloadId);
    void retryDownload(int downloadId);
    void removeDownload(int downloadId, bool deleteFile = false);

    // Batch operations
    void pauseAll();
    void resumeAll();
    void cancelAll();

    // --------------- Queries ---------------
    DownloadItem downloadInfo(int downloadId) const;
    QList<DownloadItem> allDownloads() const;
    QList<DownloadItem> activeDownloads() const;
    QList<DownloadItem> completedDownloads() const;
    QList<DownloadItem> failedDownloads() const;

    int activeCount() const;
    int queuedCount() const;
    double totalSpeedBps() const;

    // --------------- Settings ---------------
    DownloadSettings settings() const;
    void setSettings(const DownloadSettings& settings);
    void setMaxConcurrent(int max);
    void setMaxBandwidth(int64_t bps);
    void setDefaultDir(const QString& dir);

    // --------------- Priority ---------------
    void setPriority(int downloadId, DownloadPriority priority);
    void moveToTop(int downloadId);

    // --------------- File analysis ---------------
    // Check if the server supports range requests (for resume/segments)
    void probeUrl(const QUrl& url);

    // Suggest filename from Content-Disposition or URL
    static QString suggestFileName(const QUrl& url, const QString& contentDisposition = {});

    // --------------- Serialization ---------------
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
    void saveState(const QString& filePath) const;
    void loadState(const QString& filePath);

signals:
    void downloadStarted(int downloadId);
    void downloadProgress(int downloadId, double percent, double speedBps);
    void downloadPaused(int downloadId);
    void downloadResumed(int downloadId);
    void downloadCompleted(int downloadId, const QString& filePath);
    void downloadFailed(int downloadId, const QString& error);
    void downloadCancelled(int downloadId);
    void segmentCompleted(int downloadId, int segmentIndex);
    void probeCompleted(const QUrl& url, bool supportsResume, int64_t fileSize);
    void queueChanged();
    void speedUpdated(double totalBps);

private slots:
    void onProgressTimer();
    void processQueue();

private:
    int nextDownloadId();
    void startSegmentedDownload(DownloadItem& item);
    void startSimpleDownload(DownloadItem& item);
    void mergeSegments(DownloadItem& item);
    int calculateSegmentCount(int64_t fileSize) const;
    void updateSpeed(DownloadItem& item);
    void cleanupTemp(const DownloadItem& item);

    mutable QMutex m_mutex;
    QHash<int, DownloadItem> m_downloads;
    QList<int> m_queue;      // download IDs in queue order
    int m_nextId = 1;

    DownloadSettings m_settings;

    QTimer* m_progressTimer = nullptr;
    QTimer* m_queueTimer = nullptr;
};

} // namespace Network
} // namespace Ordinal
