#include "download_accelerator.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace Ordinal {
namespace Network {

DownloadAccelerator::DownloadAccelerator(QObject* parent)
    : QObject(parent)
{
    m_settings.defaultDownloadDir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(500);
    connect(m_progressTimer, &QTimer::timeout, this, &DownloadAccelerator::onProgressTimer);
    m_progressTimer->start();

    m_queueTimer = new QTimer(this);
    m_queueTimer->setInterval(200);
    connect(m_queueTimer, &QTimer::timeout, this, &DownloadAccelerator::processQueue);
    m_queueTimer->start();
}

DownloadAccelerator::~DownloadAccelerator()
{
    // Clean up temp files for incomplete downloads
    for (const auto& item : m_downloads) {
        if (item.state != DownloadState::Completed) {
            cleanupTemp(item);
        }
    }
}

// --------------- Download management ---------------

int DownloadAccelerator::startDownload(const QUrl& url, const QString& savePath,
                                        const QHash<QString, QString>& headers)
{
    QMutexLocker lock(&m_mutex);

    int id = nextDownloadId();
    DownloadItem item;
    item.downloadId = id;
    item.url = url;
    item.headers = headers;
    item.createdAt = QDateTime::currentDateTimeUtc();

    // Determine save path
    if (!savePath.isEmpty()) {
        item.filePath = savePath;
        item.fileName = QFileInfo(savePath).fileName();
    } else {
        item.fileName = suggestFileName(url);
        item.filePath = m_settings.defaultDownloadDir + "/" + item.fileName;
    }

    // Handle filename conflicts
    QFileInfo fi(item.filePath);
    if (fi.exists()) {
        QString base = fi.completeBaseName();
        QString ext = fi.suffix();
        QString dir = fi.absolutePath();
        int counter = 1;
        while (QFileInfo::exists(item.filePath)) {
            item.filePath = QStringLiteral("%1/%2 (%3).%4")
                .arg(dir, base).arg(counter++).arg(ext);
        }
        item.fileName = QFileInfo(item.filePath).fileName();
    }

    // Create temp directory for segments
    item.tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                   + "/ordinal-dl-" + QString::number(id);
    QDir().mkpath(item.tempDir);

    item.state = DownloadState::Queued;
    m_downloads.insert(id, item);
    m_queue.append(id);

    emit queueChanged();
    return id;
}

void DownloadAccelerator::pauseDownload(int downloadId)
{
    QMutexLocker lock(&m_mutex);
    if (!m_downloads.contains(downloadId)) return;
    auto& item = m_downloads[downloadId];
    if (item.state != DownloadState::Downloading &&
        item.state != DownloadState::Connecting) return;

    item.state = DownloadState::Paused;
    emit downloadPaused(downloadId);
}

void DownloadAccelerator::resumeDownload(int downloadId)
{
    QMutexLocker lock(&m_mutex);
    if (!m_downloads.contains(downloadId)) return;
    auto& item = m_downloads[downloadId];
    if (item.state != DownloadState::Paused) return;

    item.state = DownloadState::Queued;
    if (!m_queue.contains(downloadId)) {
        m_queue.prepend(downloadId); // resume at front of queue
    }
    emit downloadResumed(downloadId);
    emit queueChanged();
}

void DownloadAccelerator::cancelDownload(int downloadId)
{
    QMutexLocker lock(&m_mutex);
    if (!m_downloads.contains(downloadId)) return;

    auto& item = m_downloads[downloadId];
    item.state = DownloadState::Cancelled;
    m_queue.removeAll(downloadId);
    cleanupTemp(item);
    emit downloadCancelled(downloadId);
}

void DownloadAccelerator::retryDownload(int downloadId)
{
    QMutexLocker lock(&m_mutex);
    if (!m_downloads.contains(downloadId)) return;
    auto& item = m_downloads[downloadId];
    if (item.state != DownloadState::Failed) return;

    item.retryCount++;
    item.state = DownloadState::Queued;
    item.errorMessage.clear();
    item.downloadedBytes = 0;
    item.progressPercent = 0.0;

    // Reset segments
    for (auto& seg : item.segments) {
        seg.downloadedBytes = 0;
        seg.completed = false;
    }

    m_queue.append(downloadId);
    emit queueChanged();
}

void DownloadAccelerator::removeDownload(int downloadId, bool deleteFile)
{
    QMutexLocker lock(&m_mutex);
    if (!m_downloads.contains(downloadId)) return;

    auto item = m_downloads[downloadId];
    cleanupTemp(item);

    if (deleteFile && item.state == DownloadState::Completed) {
        QFile::remove(item.filePath);
    }

    m_downloads.remove(downloadId);
    m_queue.removeAll(downloadId);
    emit queueChanged();
}

void DownloadAccelerator::pauseAll()
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it.value().state == DownloadState::Downloading ||
            it.value().state == DownloadState::Connecting) {
            it.value().state = DownloadState::Paused;
            emit downloadPaused(it.key());
        }
    }
}

void DownloadAccelerator::resumeAll()
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it.value().state == DownloadState::Paused) {
            it.value().state = DownloadState::Queued;
            if (!m_queue.contains(it.key())) {
                m_queue.append(it.key());
            }
            emit downloadResumed(it.key());
        }
    }
    emit queueChanged();
}

void DownloadAccelerator::cancelAll()
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it.value().state == DownloadState::Downloading ||
            it.value().state == DownloadState::Queued ||
            it.value().state == DownloadState::Connecting) {
            it.value().state = DownloadState::Cancelled;
            cleanupTemp(it.value());
            emit downloadCancelled(it.key());
        }
    }
    m_queue.clear();
    emit queueChanged();
}

// --------------- Queries ---------------

DownloadItem DownloadAccelerator::downloadInfo(int downloadId) const
{
    QMutexLocker lock(&m_mutex);
    return m_downloads.value(downloadId);
}

QList<DownloadItem> DownloadAccelerator::allDownloads() const
{
    QMutexLocker lock(&m_mutex);
    return m_downloads.values();
}

QList<DownloadItem> DownloadAccelerator::activeDownloads() const
{
    QMutexLocker lock(&m_mutex);
    QList<DownloadItem> result;
    for (const auto& item : m_downloads) {
        if (item.state == DownloadState::Downloading ||
            item.state == DownloadState::Connecting ||
            item.state == DownloadState::Merging) {
            result.append(item);
        }
    }
    return result;
}

QList<DownloadItem> DownloadAccelerator::completedDownloads() const
{
    QMutexLocker lock(&m_mutex);
    QList<DownloadItem> result;
    for (const auto& item : m_downloads) {
        if (item.state == DownloadState::Completed) {
            result.append(item);
        }
    }
    return result;
}

QList<DownloadItem> DownloadAccelerator::failedDownloads() const
{
    QMutexLocker lock(&m_mutex);
    QList<DownloadItem> result;
    for (const auto& item : m_downloads) {
        if (item.state == DownloadState::Failed) {
            result.append(item);
        }
    }
    return result;
}

int DownloadAccelerator::activeCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& item : m_downloads) {
        if (item.state == DownloadState::Downloading ||
            item.state == DownloadState::Connecting) {
            ++count;
        }
    }
    return count;
}

int DownloadAccelerator::queuedCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_queue.size();
}

double DownloadAccelerator::totalSpeedBps() const
{
    QMutexLocker lock(&m_mutex);
    double total = 0.0;
    for (const auto& item : m_downloads) {
        if (item.state == DownloadState::Downloading) {
            total += item.speedBps;
        }
    }
    return total;
}

// --------------- Settings ---------------

DownloadSettings DownloadAccelerator::settings() const { return m_settings; }

void DownloadAccelerator::setSettings(const DownloadSettings& settings)
{
    m_settings = settings;
}

void DownloadAccelerator::setMaxConcurrent(int max)
{
    m_settings.maxConcurrentDownloads = std::max(1, max);
}

void DownloadAccelerator::setMaxBandwidth(int64_t bps)
{
    m_settings.maxBandwidthBps = std::max(int64_t(0), bps);
}

void DownloadAccelerator::setDefaultDir(const QString& dir)
{
    m_settings.defaultDownloadDir = dir;
    QDir().mkpath(dir);
}

// --------------- Priority ---------------

void DownloadAccelerator::setPriority(int downloadId, DownloadPriority priority)
{
    QMutexLocker lock(&m_mutex);
    if (m_downloads.contains(downloadId)) {
        m_downloads[downloadId].priority = priority;
    }
}

void DownloadAccelerator::moveToTop(int downloadId)
{
    QMutexLocker lock(&m_mutex);
    m_queue.removeAll(downloadId);
    m_queue.prepend(downloadId);
    emit queueChanged();
}

// --------------- File analysis ---------------

void DownloadAccelerator::probeUrl(const QUrl& url)
{
    // Would send a HEAD request to check:
    // - Accept-Ranges header
    // - Content-Length
    // - Content-Disposition
    Q_UNUSED(url)
    // For now, emit with defaults
    emit probeCompleted(url, false, -1);
}

QString DownloadAccelerator::suggestFileName(const QUrl& url, const QString& contentDisposition)
{
    // Try Content-Disposition first
    if (!contentDisposition.isEmpty()) {
        static QRegularExpression filenameRe(R"(filename\*?=(?:UTF-8''|")?([^";]+)"?)");
        auto m = filenameRe.match(contentDisposition);
        if (m.hasMatch()) {
            return QUrl::fromPercentEncoding(m.captured(1).toUtf8()).trimmed();
        }
    }

    // Extract from URL path
    QString path = url.path();
    QString fileName = QFileInfo(path).fileName();
    if (!fileName.isEmpty() && fileName.contains('.')) {
        return QUrl::fromPercentEncoding(fileName.toUtf8());
    }

    // Fallback: use host + timestamp
    return QStringLiteral("download_%1_%2")
        .arg(url.host().replace('.', '_'))
        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss"));
}

// --------------- Serialization ---------------

QJsonObject DownloadAccelerator::toJson() const
{
    QMutexLocker lock(&m_mutex);
    QJsonObject root;

    QJsonArray downloadArr;
    for (const auto& item : m_downloads) {
        QJsonObject d;
        d["id"] = item.downloadId;
        d["url"] = item.url.toString();
        d["fileName"] = item.fileName;
        d["filePath"] = item.filePath;
        d["state"] = static_cast<int>(item.state);
        d["priority"] = static_cast<int>(item.priority);
        d["totalBytes"] = item.totalBytes;
        d["downloadedBytes"] = item.downloadedBytes;
        d["progressPercent"] = item.progressPercent;
        d["speedBps"] = item.speedBps;
        d["supportsResume"] = item.supportsResume;
        d["segmentCount"] = item.segmentCount;
        d["createdAt"] = item.createdAt.toString(Qt::ISODate);
        if (item.completedAt.isValid()) {
            d["completedAt"] = item.completedAt.toString(Qt::ISODate);
        }
        downloadArr.append(d);
    }
    root["downloads"] = downloadArr;

    QJsonObject settings;
    settings["maxConcurrent"] = m_settings.maxConcurrentDownloads;
    settings["segmentsPerDownload"] = m_settings.segmentsPerDownload;
    settings["maxBandwidthBps"] = m_settings.maxBandwidthBps;
    settings["defaultDir"] = m_settings.defaultDownloadDir;
    settings["autoResume"] = m_settings.autoResume;
    root["settings"] = settings;

    return root;
}

void DownloadAccelerator::fromJson(const QJsonObject& json)
{
    QMutexLocker lock(&m_mutex);

    if (json.contains("settings")) {
        auto s = json["settings"].toObject();
        m_settings.maxConcurrentDownloads = s["maxConcurrent"].toInt(3);
        m_settings.segmentsPerDownload = s["segmentsPerDownload"].toInt(8);
        m_settings.maxBandwidthBps = s["maxBandwidthBps"].toVariant().toLongLong();
        m_settings.defaultDownloadDir = s["defaultDir"].toString();
        m_settings.autoResume = s["autoResume"].toBool(true);
    }

    auto downloadArr = json["downloads"].toArray();
    for (const auto& dv : downloadArr) {
        auto d = dv.toObject();
        DownloadItem item;
        item.downloadId = d["id"].toInt();
        item.url = QUrl(d["url"].toString());
        item.fileName = d["fileName"].toString();
        item.filePath = d["filePath"].toString();
        item.state = static_cast<DownloadState>(d["state"].toInt());
        item.priority = static_cast<DownloadPriority>(d["priority"].toInt());
        item.totalBytes = d["totalBytes"].toVariant().toLongLong();
        item.downloadedBytes = d["downloadedBytes"].toVariant().toLongLong();
        item.supportsResume = d["supportsResume"].toBool();
        item.segmentCount = d["segmentCount"].toInt();
        item.createdAt = QDateTime::fromString(d["createdAt"].toString(), Qt::ISODate);

        // Only restore paused/completed/failed downloads
        if (item.state == DownloadState::Downloading ||
            item.state == DownloadState::Connecting) {
            item.state = m_settings.autoResume ? DownloadState::Queued : DownloadState::Paused;
        }

        m_downloads.insert(item.downloadId, item);
        if (item.downloadId >= m_nextId) m_nextId = item.downloadId + 1;

        if (item.state == DownloadState::Queued) {
            m_queue.append(item.downloadId);
        }
    }
}

void DownloadAccelerator::saveState(const QString& filePath) const
{
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    }
}

void DownloadAccelerator::loadState(const QString& filePath)
{
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            fromJson(doc.object());
        }
    }
}

// --------------- Private slots ---------------

void DownloadAccelerator::onProgressTimer()
{
    QMutexLocker lock(&m_mutex);

    double totalSpeed = 0.0;
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        auto& item = it.value();
        if (item.state == DownloadState::Downloading) {
            updateSpeed(item);
            totalSpeed += item.speedBps;

            // Calculate progress
            if (item.totalBytes > 0) {
                item.progressPercent =
                    (double)item.downloadedBytes / item.totalBytes * 100.0;
                // ETA
                if (item.speedBps > 0) {
                    item.etaSeconds =
                        (int64_t)((item.totalBytes - item.downloadedBytes) / item.speedBps);
                }
            }

            emit downloadProgress(item.downloadId, item.progressPercent, item.speedBps);
        }
    }

    emit speedUpdated(totalSpeed);
}

void DownloadAccelerator::processQueue()
{
    QMutexLocker lock(&m_mutex);

    int active = activeCount();
    while (active < m_settings.maxConcurrentDownloads && !m_queue.isEmpty()) {
        // Sort queue by priority
        std::stable_sort(m_queue.begin(), m_queue.end(), [this](int a, int b) {
            return static_cast<int>(m_downloads[a].priority) >
                   static_cast<int>(m_downloads[b].priority);
        });

        int nextId = m_queue.takeFirst();
        if (!m_downloads.contains(nextId)) continue;

        auto& item = m_downloads[nextId];
        if (item.state != DownloadState::Queued) continue;

        item.state = DownloadState::Connecting;
        item.startedAt = QDateTime::currentDateTimeUtc();

        // Determine download strategy
        if (item.supportsResume && item.totalBytes > m_settings.minSegmentSize * 2) {
            startSegmentedDownload(item);
        } else {
            startSimpleDownload(item);
        }

        emit downloadStarted(nextId);
        ++active;
    }
}

// --------------- Private helpers ---------------

int DownloadAccelerator::nextDownloadId()
{
    return m_nextId++;
}

void DownloadAccelerator::startSegmentedDownload(DownloadItem& item)
{
    int segments = calculateSegmentCount(item.totalBytes);
    item.segmentCount = segments;
    item.segments.clear();

    int64_t segmentSize = item.totalBytes / segments;

    for (int i = 0; i < segments; ++i) {
        DownloadSegment seg;
        seg.segmentIndex = i;
        seg.startByte = i * segmentSize;
        seg.endByte = (i == segments - 1) ? item.totalBytes - 1
                                           : (i + 1) * segmentSize - 1;
        seg.tempFilePath = QStringLiteral("%1/segment_%2.part")
            .arg(item.tempDir).arg(i);
        item.segments.append(seg);
    }

    item.state = DownloadState::Downloading;

    // In a real implementation, each segment would be downloaded
    // in a separate thread using HTTP Range requests.
    // For now, we set up the structure.
}

void DownloadAccelerator::startSimpleDownload(DownloadItem& item)
{
    item.segmentCount = 1;
    item.segments.clear();

    DownloadSegment seg;
    seg.segmentIndex = 0;
    seg.startByte = 0;
    seg.endByte = item.totalBytes > 0 ? item.totalBytes - 1 : -1;
    seg.tempFilePath = item.tempDir + "/download.part";
    item.segments.append(seg);

    item.state = DownloadState::Downloading;
}

void DownloadAccelerator::mergeSegments(DownloadItem& item)
{
    item.state = DownloadState::Merging;

    QFile outFile(item.filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        item.state = DownloadState::Failed;
        item.errorMessage = "Cannot create output file: " + item.filePath;
        emit downloadFailed(item.downloadId, item.errorMessage);
        return;
    }

    for (const auto& seg : item.segments) {
        QFile segFile(seg.tempFilePath);
        if (segFile.open(QIODevice::ReadOnly)) {
            outFile.write(segFile.readAll());
            segFile.close();
        } else {
            item.state = DownloadState::Failed;
            item.errorMessage = "Cannot read segment: " + seg.tempFilePath;
            emit downloadFailed(item.downloadId, item.errorMessage);
            outFile.close();
            return;
        }
    }
    outFile.close();

    // Cleanup temp files
    cleanupTemp(item);

    item.state = DownloadState::Completed;
    item.completedAt = QDateTime::currentDateTimeUtc();
    item.progressPercent = 100.0;
    emit downloadCompleted(item.downloadId, item.filePath);
}

int DownloadAccelerator::calculateSegmentCount(int64_t fileSize) const
{
    if (fileSize <= 0) return 1;
    if (fileSize < m_settings.minSegmentSize * 2) return 1;

    int segments = m_settings.segmentsPerDownload;
    int64_t perSegment = fileSize / segments;

    // Ensure minimum segment size
    while (perSegment < m_settings.minSegmentSize && segments > 1) {
        segments--;
        perSegment = fileSize / segments;
    }

    return std::max(1, segments);
}

void DownloadAccelerator::updateSpeed(DownloadItem& item)
{
    // Simple speed calculation based on downloaded bytes since last check
    // In a real implementation, this would use a sliding window
    if (item.totalBytes > 0 && item.downloadedBytes > 0) {
        auto elapsed = item.startedAt.msecsTo(QDateTime::currentDateTimeUtc());
        if (elapsed > 0) {
            item.avgSpeedBps = (double)item.downloadedBytes / (elapsed / 1000.0);
        }
    }

    // Aggregate segment speeds
    double totalSegSpeed = 0.0;
    for (const auto& seg : item.segments) {
        totalSegSpeed += seg.speedBps;
    }
    item.speedBps = totalSegSpeed > 0 ? totalSegSpeed : item.avgSpeedBps;
}

void DownloadAccelerator::cleanupTemp(const DownloadItem& item)
{
    if (item.tempDir.isEmpty()) return;
    QDir tempDir(item.tempDir);
    if (tempDir.exists()) {
        tempDir.removeRecursively();
    }
}

} // namespace Network
} // namespace Ordinal
