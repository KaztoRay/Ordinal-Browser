#include "media_controller.h"
#include <QJsonArray>
#include <algorithm>

namespace Ordinal {
namespace Core {

MediaController::MediaController(QObject* parent)
    : QObject(parent)
{
}

MediaController::~MediaController() = default;

// --------------- Global controls ---------------

void MediaController::play(int mediaId)
{
    if (!m_media.contains(mediaId)) return;
    m_media[mediaId].state = MediaState::Playing;
    m_media[mediaId].startedAt = QDateTime::currentDateTimeUtc();
    setActiveMedia(mediaId);
    emit mediaStateChanged(mediaId, MediaState::Playing);
}

void MediaController::pause(int mediaId)
{
    if (!m_media.contains(mediaId)) return;
    if (m_media[mediaId].state != MediaState::Playing &&
        m_media[mediaId].state != MediaState::Buffering) return;
    m_media[mediaId].state = MediaState::Paused;
    emit mediaStateChanged(mediaId, MediaState::Paused);
}

void MediaController::togglePlayPause(int mediaId)
{
    if (!m_media.contains(mediaId)) return;
    if (m_media[mediaId].state == MediaState::Playing) {
        pause(mediaId);
    } else {
        play(mediaId);
    }
}

void MediaController::stop(int mediaId)
{
    if (!m_media.contains(mediaId)) return;
    m_media[mediaId].state = MediaState::Idle;
    m_media[mediaId].currentTime = 0.0;
    emit mediaStateChanged(mediaId, MediaState::Idle);
    emit mediaTimeUpdated(mediaId, 0.0);
}

void MediaController::seek(int mediaId, double timeSec)
{
    if (!m_media.contains(mediaId)) return;
    auto& info = m_media[mediaId];
    info.currentTime = std::clamp(timeSec, 0.0, info.duration);
    emit mediaTimeUpdated(mediaId, info.currentTime);
}

void MediaController::seekRelative(int mediaId, double deltaSec)
{
    if (!m_media.contains(mediaId)) return;
    seek(mediaId, m_media[mediaId].currentTime + deltaSec);
}

void MediaController::setVolume(int mediaId, double volume)
{
    if (!m_media.contains(mediaId)) return;
    m_media[mediaId].volume = std::clamp(volume, 0.0, 1.0);
    emit mediaVolumeChanged(mediaId, m_media[mediaId].volume);
}

void MediaController::setMuted(int mediaId, bool muted)
{
    if (!m_media.contains(mediaId)) return;
    m_media[mediaId].muted = muted;
}

void MediaController::setPlaybackRate(int mediaId, double rate)
{
    if (!m_media.contains(mediaId)) return;
    m_media[mediaId].playbackRate = std::clamp(rate, 0.25, 16.0);
}

void MediaController::muteAll()
{
    m_globalMuted = true;
    for (auto it = m_media.begin(); it != m_media.end(); ++it) {
        it.value().muted = true;
    }
}

void MediaController::unmuteAll()
{
    m_globalMuted = false;
    for (auto it = m_media.begin(); it != m_media.end(); ++it) {
        it.value().muted = false;
    }
}

bool MediaController::isGlobalMuted() const { return m_globalMuted; }

void MediaController::pauseAll()
{
    for (auto it = m_media.begin(); it != m_media.end(); ++it) {
        if (it.value().state == MediaState::Playing) {
            pause(it.key());
        }
    }
}

void MediaController::resumeAll()
{
    for (auto it = m_media.begin(); it != m_media.end(); ++it) {
        if (it.value().state == MediaState::Paused) {
            play(it.key());
        }
    }
}

// --------------- Media discovery ---------------

void MediaController::registerMedia(const MediaInfo& info)
{
    int id = info.mediaId > 0 ? info.mediaId : nextMediaId();
    MediaInfo registered = info;
    registered.mediaId = id;
    m_media.insert(id, registered);

    // Auto-activate if this is the first or only playing media
    if (m_activeMediaId < 0 || m_media.size() == 1) {
        setActiveMedia(id);
    }

    emit mediaRegistered(id);
}

void MediaController::unregisterMedia(int mediaId)
{
    if (!m_media.contains(mediaId)) return;
    m_media.remove(mediaId);

    if (m_activeMediaId == mediaId) {
        m_activeMediaId = m_media.isEmpty() ? -1 : m_media.keys().first();
        if (m_activeMediaId >= 0) {
            emit activeMediaChanged(m_activeMediaId);
        }
    }

    // Stop PiP if this was the PiP media
    if (m_pip.active && m_pip.mediaId == mediaId) {
        disablePip();
    }

    emit mediaUnregistered(mediaId);
}

void MediaController::updateMedia(int mediaId, const MediaInfo& info)
{
    if (!m_media.contains(mediaId)) return;

    auto prev = m_media[mediaId];
    m_media[mediaId] = info;
    m_media[mediaId].mediaId = mediaId; // preserve ID

    if (prev.state != info.state) {
        emit mediaStateChanged(mediaId, info.state);
        if (info.state == MediaState::Ended) {
            emit mediaEnded(mediaId);
        } else if (info.state == MediaState::Error) {
            emit mediaError(mediaId, "Media playback error");
        }
    }

    if (std::abs(prev.currentTime - info.currentTime) > 0.1) {
        emit mediaTimeUpdated(mediaId, info.currentTime);
    }

    if (std::abs(prev.volume - info.volume) > 0.01) {
        emit mediaVolumeChanged(mediaId, info.volume);
    }
}

MediaInfo MediaController::mediaInfo(int mediaId) const
{
    return m_media.value(mediaId);
}

QList<MediaInfo> MediaController::allMedia() const
{
    return m_media.values();
}

QList<MediaInfo> MediaController::mediaInTab(int tabId) const
{
    QList<MediaInfo> result;
    for (const auto& info : m_media) {
        if (info.tabId == tabId) result.append(info);
    }
    return result;
}

QList<MediaInfo> MediaController::playingMedia() const
{
    QList<MediaInfo> result;
    for (const auto& info : m_media) {
        if (info.state == MediaState::Playing ||
            info.state == MediaState::Buffering) {
            result.append(info);
        }
    }
    return result;
}

MediaInfo MediaController::activeMedia() const
{
    return m_media.value(m_activeMediaId);
}

// --------------- Picture-in-Picture ---------------

void MediaController::enablePip(int mediaId)
{
    if (!m_media.contains(mediaId)) return;
    m_pip.active = true;
    m_pip.mediaId = mediaId;
    m_pip.tabId = m_media[mediaId].tabId;
    emit pipStateChanged(m_pip);
}

void MediaController::disablePip()
{
    m_pip.active = false;
    m_pip.mediaId = -1;
    m_pip.tabId = -1;
    emit pipStateChanged(m_pip);
}

PipState MediaController::pipState() const { return m_pip; }

void MediaController::setPipPosition(int x, int y)
{
    m_pip.x = x;
    m_pip.y = y;
    if (m_pip.active) emit pipStateChanged(m_pip);
}

void MediaController::setPipSize(int w, int h)
{
    m_pip.width = std::max(160, w);
    m_pip.height = std::max(90, h);
    if (m_pip.active) emit pipStateChanged(m_pip);
}

// --------------- Media session ---------------

void MediaController::updateMediaSession(const MediaInfo& info)
{
    // TODO: Integrate with platform media controls
    // macOS: MPNowPlayingInfoCenter / MPRemoteCommandCenter
    // Linux: MPRIS D-Bus interface
    // Windows: SMTC (SystemMediaTransportControls)
    Q_UNUSED(info)
}

// --------------- Audio routing ---------------

void MediaController::setAudioOutput(AudioOutput output)
{
    m_audioOutput = output;
}

MediaController::AudioOutput MediaController::audioOutput() const
{
    return m_audioOutput;
}

// --------------- Casting ---------------

QList<MediaController::CastTarget> MediaController::availableCastTargets() const
{
    // TODO: Implement mDNS discovery for AirPlay, Chromecast, DLNA
    return {};
}

void MediaController::castTo(int mediaId, const QString& targetId)
{
    if (!m_media.contains(mediaId)) return;
    m_casting = true;
    m_castTargetId = targetId;
    m_castMediaId = mediaId;
    emit castStarted(mediaId, targetId);
}

void MediaController::stopCasting()
{
    if (!m_casting) return;
    m_casting = false;
    int prevMedia = m_castMediaId;
    m_castTargetId.clear();
    m_castMediaId = -1;
    Q_UNUSED(prevMedia)
    emit castStopped();
}

bool MediaController::isCasting() const { return m_casting; }

// --------------- Serialization ---------------

QJsonObject MediaController::toJson() const
{
    QJsonObject root;

    QJsonArray mediaArr;
    for (const auto& info : m_media) {
        QJsonObject m;
        m["mediaId"] = info.mediaId;
        m["tabId"] = info.tabId;
        m["src"] = info.src.toString();
        m["title"] = info.title;
        m["artist"] = info.artist;
        m["type"] = static_cast<int>(info.type);
        m["state"] = static_cast<int>(info.state);
        m["duration"] = info.duration;
        m["currentTime"] = info.currentTime;
        m["volume"] = info.volume;
        m["muted"] = info.muted;
        m["playbackRate"] = info.playbackRate;
        m["width"] = info.width;
        m["height"] = info.height;
        m["isLive"] = info.isLive;
        mediaArr.append(m);
    }
    root["media"] = mediaArr;
    root["activeMediaId"] = m_activeMediaId;
    root["globalMuted"] = m_globalMuted;

    QJsonObject pip;
    pip["active"] = m_pip.active;
    pip["mediaId"] = m_pip.mediaId;
    pip["x"] = m_pip.x;
    pip["y"] = m_pip.y;
    pip["width"] = m_pip.width;
    pip["height"] = m_pip.height;
    root["pip"] = pip;

    root["casting"] = m_casting;
    root["castTarget"] = m_castTargetId;

    return root;
}

// --------------- Private ---------------

int MediaController::nextMediaId()
{
    return m_nextMediaId++;
}

void MediaController::setActiveMedia(int mediaId)
{
    if (m_activeMediaId == mediaId) return;
    m_activeMediaId = mediaId;
    emit activeMediaChanged(mediaId);

    // Update OS media session
    if (m_media.contains(mediaId)) {
        updateMediaSession(m_media[mediaId]);
    }
}

} // namespace Core
} // namespace Ordinal
