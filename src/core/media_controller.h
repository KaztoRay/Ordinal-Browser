#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QHash>
#include <QImage>
#include <QDateTime>
#include <QJsonObject>

namespace Ordinal {
namespace Core {

// Media controller for in-browser video/audio playback management
// Provides unified control over all media elements across tabs

enum class MediaState {
    Idle,
    Playing,
    Paused,
    Buffering,
    Ended,
    Error
};

enum class MediaType {
    Unknown,
    Audio,
    Video,
    Stream,       // Live stream (HLS, DASH)
    WebRTC        // WebRTC media
};

struct MediaInfo {
    int tabId = -1;
    int mediaId = -1;
    QUrl src;
    QString title;
    QString artist;
    QString album;
    QImage thumbnail;
    MediaType type = MediaType::Unknown;
    MediaState state = MediaState::Idle;
    double duration = 0.0;           // seconds
    double currentTime = 0.0;        // seconds
    double volume = 1.0;             // 0.0 - 1.0
    bool muted = false;
    double playbackRate = 1.0;
    int width = 0;                   // video width
    int height = 0;                  // video height
    double buffered = 0.0;           // seconds buffered ahead
    bool isLive = false;
    bool hasPictureInPicture = false;
    QDateTime startedAt;
};

// Picture-in-Picture state
struct PipState {
    bool active = false;
    int mediaId = -1;
    int tabId = -1;
    int x = 0, y = 0;
    int width = 320, height = 180;
    bool alwaysOnTop = true;
};

class MediaController : public QObject {
    Q_OBJECT
public:
    explicit MediaController(QObject* parent = nullptr);
    ~MediaController() override;

    // --------------- Global controls ---------------
    void play(int mediaId);
    void pause(int mediaId);
    void togglePlayPause(int mediaId);
    void stop(int mediaId);
    void seek(int mediaId, double timeSec);
    void seekRelative(int mediaId, double deltaSec);

    void setVolume(int mediaId, double volume);
    void setMuted(int mediaId, bool muted);
    void setPlaybackRate(int mediaId, double rate);

    // Global mute (all media)
    void muteAll();
    void unmuteAll();
    bool isGlobalMuted() const;

    // Global pause (all media)
    void pauseAll();
    void resumeAll();

    // --------------- Media discovery ---------------
    // Called when a new media element is detected in a tab
    void registerMedia(const MediaInfo& info);
    void unregisterMedia(int mediaId);
    void updateMedia(int mediaId, const MediaInfo& info);

    // Queries
    MediaInfo mediaInfo(int mediaId) const;
    QList<MediaInfo> allMedia() const;
    QList<MediaInfo> mediaInTab(int tabId) const;
    QList<MediaInfo> playingMedia() const;
    MediaInfo activeMedia() const;     // currently focused/primary media

    // --------------- Picture-in-Picture ---------------
    void enablePip(int mediaId);
    void disablePip();
    PipState pipState() const;
    void setPipPosition(int x, int y);
    void setPipSize(int w, int h);

    // --------------- Media session (OS integration) ---------------
    // Integrates with OS media controls (macOS Now Playing, MPRIS on Linux, etc.)
    void updateMediaSession(const MediaInfo& info);

    // --------------- Audio routing ---------------
    enum class AudioOutput { Default, Headphones, Speakers, Bluetooth, Custom };
    void setAudioOutput(AudioOutput output);
    AudioOutput audioOutput() const;

    // --------------- Casting ---------------
    // AirPlay / Chromecast discovery
    struct CastTarget {
        QString id;
        QString name;
        QString type;      // "airplay", "chromecast", "dlna"
        bool available = true;
    };
    QList<CastTarget> availableCastTargets() const;
    void castTo(int mediaId, const QString& targetId);
    void stopCasting();
    bool isCasting() const;

    // --------------- Serialization ---------------
    QJsonObject toJson() const;

signals:
    void mediaRegistered(int mediaId);
    void mediaUnregistered(int mediaId);
    void mediaStateChanged(int mediaId, MediaState state);
    void mediaTimeUpdated(int mediaId, double currentTime);
    void mediaVolumeChanged(int mediaId, double volume);
    void mediaEnded(int mediaId);
    void mediaError(int mediaId, const QString& error);
    void pipStateChanged(const PipState& state);
    void castStarted(int mediaId, const QString& targetId);
    void castStopped();
    void activeMediaChanged(int mediaId);

private:
    int nextMediaId();
    void setActiveMedia(int mediaId);

    QHash<int, MediaInfo> m_media;
    int m_activeMediaId = -1;
    int m_nextMediaId = 1;
    bool m_globalMuted = false;

    PipState m_pip;
    AudioOutput m_audioOutput = AudioOutput::Default;

    // Cast state
    bool m_casting = false;
    QString m_castTargetId;
    int m_castMediaId = -1;
};

} // namespace Core
} // namespace Ordinal
