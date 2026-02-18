#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QSettings>
#include <QGroupBox>

namespace Ordinal {
namespace Engine {

class OrdinalProfile;

// ============================================================
// SettingsPage — 설정 다이얼로그
// ============================================================
class SettingsPage : public QDialog {
    Q_OBJECT

public:
    explicit SettingsPage(OrdinalProfile* profile, QWidget* parent = nullptr);
    ~SettingsPage() override;

signals:
    void settingsChanged();

private:
    void setupUI();
    QWidget* createGeneralTab();
    QWidget* createPrivacyTab();
    QWidget* createSecurityTab();
    QWidget* createAppearanceTab();
    QWidget* createSearchTab();
    QWidget* createAdvancedTab();

    void loadSettings();
    void saveSettings();
    void resetDefaults();

    OrdinalProfile* m_profile;
    QSettings m_settings;

    // General
    QLineEdit* m_homePage = nullptr;
    QComboBox* m_startupBehavior = nullptr;
    QCheckBox* m_restoreSession = nullptr;
    QComboBox* m_downloadLocation = nullptr;

    // Privacy
    QCheckBox* m_blockThirdPartyCookies = nullptr;
    QCheckBox* m_doNotTrack = nullptr;
    QCheckBox* m_clearOnExit = nullptr;
    QCheckBox* m_blockWebRtcLeak = nullptr;
    QComboBox* m_webRtcPolicy = nullptr;
    QCheckBox* m_antiFingerprint = nullptr;
    QCheckBox* m_autoRejectCookieConsent = nullptr;

    // Security
    QCheckBox* m_adBlockEnabled = nullptr;
    QCheckBox* m_phishingProtection = nullptr;
    QCheckBox* m_malwareProtection = nullptr;
    QCheckBox* m_cryptominerBlock = nullptr;
    QCheckBox* m_httpsOnly = nullptr;
    QCheckBox* m_blockMixedContent = nullptr;

    // Appearance
    QComboBox* m_theme = nullptr;
    QSpinBox* m_defaultZoom = nullptr;
    QComboBox* m_defaultFont = nullptr;
    QSpinBox* m_fontSize = nullptr;

    // Search
    QComboBox* m_searchEngine = nullptr;
    QCheckBox* m_searchSuggestions = nullptr;

    // Advanced
    QCheckBox* m_hardwareAcceleration = nullptr;
    QCheckBox* m_smoothScrolling = nullptr;
    QComboBox* m_dnsOverHttps = nullptr;
    QLineEdit* m_proxyServer = nullptr;
    QSpinBox* m_maxConnections = nullptr;
};

} // namespace Engine
} // namespace Ordinal
