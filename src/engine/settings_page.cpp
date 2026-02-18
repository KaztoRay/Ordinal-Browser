#include "settings_page.h"
#include "web_engine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QApplication>

namespace Ordinal {
namespace Engine {

SettingsPage::SettingsPage(OrdinalProfile* profile, QWidget* parent)
    : QDialog(parent)
    , m_profile(profile)
    , m_settings("Ordinal", "OrdinalBrowser")
{
    setWindowTitle("설정 — Ordinal Browser");
    setMinimumSize(640, 520);
    setupUI();
    loadSettings();
}

SettingsPage::~SettingsPage() = default;

void SettingsPage::setupUI()
{
    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(createGeneralTab(), "일반");
    tabs->addTab(createPrivacyTab(), "개인정보");
    tabs->addTab(createSecurityTab(), "보안");
    tabs->addTab(createAppearanceTab(), "모양");
    tabs->addTab(createSearchTab(), "검색");
    tabs->addTab(createAdvancedTab(), "고급");

    layout->addWidget(tabs);

    // 버튼
    auto* buttons = new QDialogButtonBox(this);
    auto* saveBtn = buttons->addButton("저장", QDialogButtonBox::AcceptRole);
    auto* cancelBtn = buttons->addButton("취소", QDialogButtonBox::RejectRole);
    auto* resetBtn = buttons->addButton("기본값 복원", QDialogButtonBox::ResetRole);

    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        saveSettings();
        emit settingsChanged();
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsPage::resetDefaults);

    layout->addWidget(buttons);
}

QWidget* SettingsPage::createGeneralTab()
{
    auto* widget = new QWidget();
    auto* layout = new QFormLayout(widget);
    layout->setSpacing(12);

    // 홈 페이지
    m_homePage = new QLineEdit("https://duckduckgo.com");
    layout->addRow("홈 페이지:", m_homePage);

    // 시작 시 동작
    m_startupBehavior = new QComboBox();
    m_startupBehavior->addItems({"홈 페이지 열기", "이전 세션 복원", "새 탭 열기"});
    layout->addRow("시작 시:", m_startupBehavior);

    // 세션 복원
    m_restoreSession = new QCheckBox("종료 시 열린 탭 기억");
    m_restoreSession->setChecked(true);
    layout->addRow("", m_restoreSession);

    // 다운로드 위치
    m_downloadLocation = new QComboBox();
    m_downloadLocation->addItems({
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        "매번 묻기"
    });
    m_downloadLocation->setEditable(true);
    layout->addRow("다운로드 위치:", m_downloadLocation);

    return widget;
}

QWidget* SettingsPage::createPrivacyTab()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    // 쿠키
    auto* cookieGroup = new QGroupBox("쿠키");
    auto* cookieLayout = new QVBoxLayout(cookieGroup);
    m_blockThirdPartyCookies = new QCheckBox("서드파티 쿠키 차단");
    m_blockThirdPartyCookies->setChecked(true);
    m_autoRejectCookieConsent = new QCheckBox("쿠키 동의 배너 자동 거부");
    m_autoRejectCookieConsent->setChecked(true);
    m_clearOnExit = new QCheckBox("종료 시 쿠키/캐시 삭제");
    cookieLayout->addWidget(m_blockThirdPartyCookies);
    cookieLayout->addWidget(m_autoRejectCookieConsent);
    cookieLayout->addWidget(m_clearOnExit);
    layout->addWidget(cookieGroup);

    // 추적 방지
    auto* trackGroup = new QGroupBox("추적 방지");
    auto* trackLayout = new QVBoxLayout(trackGroup);
    m_doNotTrack = new QCheckBox("Do Not Track 요청 보내기");
    m_doNotTrack->setChecked(true);
    m_antiFingerprint = new QCheckBox("핑거프린팅 방지 (Canvas/AudioContext 노이즈)");
    m_antiFingerprint->setChecked(true);
    trackLayout->addWidget(m_doNotTrack);
    trackLayout->addWidget(m_antiFingerprint);
    layout->addWidget(trackGroup);

    // WebRTC
    auto* webRtcGroup = new QGroupBox("WebRTC");
    auto* webRtcLayout = new QFormLayout(webRtcGroup);
    m_blockWebRtcLeak = new QCheckBox("WebRTC IP 유출 방지");
    m_blockWebRtcLeak->setChecked(true);
    m_webRtcPolicy = new QComboBox();
    m_webRtcPolicy->addItems({
        "기본값", "공개 인터페이스만", "릴레이 강제",
        "비프록시 비활성화", "WebRTC 비활성화"
    });
    m_webRtcPolicy->setCurrentIndex(2); // 릴레이 강제
    webRtcLayout->addRow("", m_blockWebRtcLeak);
    webRtcLayout->addRow("WebRTC 정책:", m_webRtcPolicy);
    layout->addWidget(webRtcGroup);

    layout->addStretch();
    return widget;
}

QWidget* SettingsPage::createSecurityTab()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    auto* group = new QGroupBox("보안 기능");
    auto* gLayout = new QVBoxLayout(group);

    m_adBlockEnabled = new QCheckBox("광고 차단");
    m_adBlockEnabled->setChecked(true);
    m_phishingProtection = new QCheckBox("피싱 사이트 보호");
    m_phishingProtection->setChecked(true);
    m_malwareProtection = new QCheckBox("멀웨어 보호");
    m_malwareProtection->setChecked(true);
    m_cryptominerBlock = new QCheckBox("크립토마이너 차단");
    m_cryptominerBlock->setChecked(true);
    m_httpsOnly = new QCheckBox("HTTPS 전용 모드 (HTTP 사이트 경고)");
    m_httpsOnly->setChecked(false);
    m_blockMixedContent = new QCheckBox("혼합 콘텐츠 차단 (HTTP+HTTPS)");
    m_blockMixedContent->setChecked(true);

    gLayout->addWidget(m_adBlockEnabled);
    gLayout->addWidget(m_phishingProtection);
    gLayout->addWidget(m_malwareProtection);
    gLayout->addWidget(m_cryptominerBlock);
    gLayout->addWidget(m_httpsOnly);
    gLayout->addWidget(m_blockMixedContent);

    layout->addWidget(group);

    // LLM Agent 상태
    auto* agentGroup = new QGroupBox("LLM 보안 에이전트");
    auto* agentLayout = new QVBoxLayout(agentGroup);
    auto* agentLabel = new QLabel(
        "AI 기반 실시간 보안 분석이 백그라운드에서 동작합니다.\n"
        "피싱, 멀웨어, 프라이버시 위협을 자동 탐지합니다.");
    agentLabel->setWordWrap(true);
    agentLayout->addWidget(agentLabel);
    layout->addWidget(agentGroup);

    layout->addStretch();
    return widget;
}

QWidget* SettingsPage::createAppearanceTab()
{
    auto* widget = new QWidget();
    auto* layout = new QFormLayout(widget);
    layout->setSpacing(12);

    m_theme = new QComboBox();
    m_theme->addItems({"시스템 기본", "라이트", "다크"});
    layout->addRow("테마:", m_theme);

    m_defaultZoom = new QSpinBox();
    m_defaultZoom->setRange(50, 300);
    m_defaultZoom->setValue(100);
    m_defaultZoom->setSuffix("%");
    layout->addRow("기본 확대/축소:", m_defaultZoom);

    m_defaultFont = new QComboBox();
    m_defaultFont->addItems({"시스템 기본", "Noto Sans", "Pretendard", "SF Pro",
                             "Inter", "Roboto", "D2Coding"});
    layout->addRow("기본 글꼴:", m_defaultFont);

    m_fontSize = new QSpinBox();
    m_fontSize->setRange(8, 32);
    m_fontSize->setValue(16);
    m_fontSize->setSuffix("px");
    layout->addRow("기본 글꼴 크기:", m_fontSize);

    return widget;
}

QWidget* SettingsPage::createSearchTab()
{
    auto* widget = new QWidget();
    auto* layout = new QFormLayout(widget);
    layout->setSpacing(12);

    m_searchEngine = new QComboBox();
    m_searchEngine->addItems({
        "DuckDuckGo", "Google", "Bing", "Brave Search",
        "Startpage", "Ecosia", "Naver"
    });
    layout->addRow("검색 엔진:", m_searchEngine);

    m_searchSuggestions = new QCheckBox("검색어 자동 완성");
    m_searchSuggestions->setChecked(true);
    layout->addRow("", m_searchSuggestions);

    // 검색 엔진 URL 참고
    auto* infoLabel = new QLabel(
        "검색 엔진별 URL:\n"
        "• DuckDuckGo: https://duckduckgo.com/?q=\n"
        "• Google: https://www.google.com/search?q=\n"
        "• Naver: https://search.naver.com/search.naver?query=");
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #666; font-size: 11px;");
    layout->addRow("", infoLabel);

    return widget;
}

QWidget* SettingsPage::createAdvancedTab()
{
    auto* widget = new QWidget();
    auto* layout = new QFormLayout(widget);
    layout->setSpacing(12);

    m_hardwareAcceleration = new QCheckBox("하드웨어 가속 (GPU 렌더링)");
    m_hardwareAcceleration->setChecked(true);
    layout->addRow("", m_hardwareAcceleration);

    m_smoothScrolling = new QCheckBox("부드러운 스크롤링");
    m_smoothScrolling->setChecked(true);
    layout->addRow("", m_smoothScrolling);

    m_dnsOverHttps = new QComboBox();
    m_dnsOverHttps->addItems({"비활성화", "Cloudflare (1.1.1.1)", "Google (8.8.8.8)",
                               "Quad9 (9.9.9.9)", "사용자 지정"});
    layout->addRow("DNS-over-HTTPS:", m_dnsOverHttps);

    m_proxyServer = new QLineEdit();
    m_proxyServer->setPlaceholderText("예: socks5://127.0.0.1:1080");
    layout->addRow("프록시 서버:", m_proxyServer);

    m_maxConnections = new QSpinBox();
    m_maxConnections->setRange(1, 32);
    m_maxConnections->setValue(8);
    layout->addRow("다운로드 병렬 연결 수:", m_maxConnections);

    return widget;
}

void SettingsPage::loadSettings()
{
    m_homePage->setText(m_settings.value("general/homePage", "https://duckduckgo.com").toString());
    m_startupBehavior->setCurrentIndex(m_settings.value("general/startupBehavior", 0).toInt());
    m_restoreSession->setChecked(m_settings.value("general/restoreSession", true).toBool());

    m_blockThirdPartyCookies->setChecked(m_settings.value("privacy/blockThirdPartyCookies", true).toBool());
    m_doNotTrack->setChecked(m_settings.value("privacy/doNotTrack", true).toBool());
    m_clearOnExit->setChecked(m_settings.value("privacy/clearOnExit", false).toBool());
    m_blockWebRtcLeak->setChecked(m_settings.value("privacy/blockWebRtcLeak", true).toBool());
    m_webRtcPolicy->setCurrentIndex(m_settings.value("privacy/webRtcPolicy", 2).toInt());
    m_antiFingerprint->setChecked(m_settings.value("privacy/antiFingerprint", true).toBool());
    m_autoRejectCookieConsent->setChecked(m_settings.value("privacy/autoRejectConsent", true).toBool());

    m_adBlockEnabled->setChecked(m_settings.value("security/adBlock", true).toBool());
    m_phishingProtection->setChecked(m_settings.value("security/phishing", true).toBool());
    m_malwareProtection->setChecked(m_settings.value("security/malware", true).toBool());
    m_cryptominerBlock->setChecked(m_settings.value("security/cryptominer", true).toBool());
    m_httpsOnly->setChecked(m_settings.value("security/httpsOnly", false).toBool());
    m_blockMixedContent->setChecked(m_settings.value("security/mixedContent", true).toBool());

    m_theme->setCurrentIndex(m_settings.value("appearance/theme", 0).toInt());
    m_defaultZoom->setValue(m_settings.value("appearance/zoom", 100).toInt());
    m_fontSize->setValue(m_settings.value("appearance/fontSize", 16).toInt());

    m_searchEngine->setCurrentIndex(m_settings.value("search/engine", 0).toInt());
    m_searchSuggestions->setChecked(m_settings.value("search/suggestions", true).toBool());

    m_hardwareAcceleration->setChecked(m_settings.value("advanced/hwAccel", true).toBool());
    m_smoothScrolling->setChecked(m_settings.value("advanced/smoothScroll", true).toBool());
    m_dnsOverHttps->setCurrentIndex(m_settings.value("advanced/doh", 0).toInt());
    m_proxyServer->setText(m_settings.value("advanced/proxy", "").toString());
    m_maxConnections->setValue(m_settings.value("advanced/maxConnections", 8).toInt());
}

void SettingsPage::saveSettings()
{
    m_settings.setValue("general/homePage", m_homePage->text());
    m_settings.setValue("general/startupBehavior", m_startupBehavior->currentIndex());
    m_settings.setValue("general/restoreSession", m_restoreSession->isChecked());

    m_settings.setValue("privacy/blockThirdPartyCookies", m_blockThirdPartyCookies->isChecked());
    m_settings.setValue("privacy/doNotTrack", m_doNotTrack->isChecked());
    m_settings.setValue("privacy/clearOnExit", m_clearOnExit->isChecked());
    m_settings.setValue("privacy/blockWebRtcLeak", m_blockWebRtcLeak->isChecked());
    m_settings.setValue("privacy/webRtcPolicy", m_webRtcPolicy->currentIndex());
    m_settings.setValue("privacy/antiFingerprint", m_antiFingerprint->isChecked());
    m_settings.setValue("privacy/autoRejectConsent", m_autoRejectCookieConsent->isChecked());

    m_settings.setValue("security/adBlock", m_adBlockEnabled->isChecked());
    m_settings.setValue("security/phishing", m_phishingProtection->isChecked());
    m_settings.setValue("security/malware", m_malwareProtection->isChecked());
    m_settings.setValue("security/cryptominer", m_cryptominerBlock->isChecked());
    m_settings.setValue("security/httpsOnly", m_httpsOnly->isChecked());
    m_settings.setValue("security/mixedContent", m_blockMixedContent->isChecked());

    m_settings.setValue("appearance/theme", m_theme->currentIndex());
    m_settings.setValue("appearance/zoom", m_defaultZoom->value());
    m_settings.setValue("appearance/fontSize", m_fontSize->value());

    m_settings.setValue("search/engine", m_searchEngine->currentIndex());
    m_settings.setValue("search/suggestions", m_searchSuggestions->isChecked());

    m_settings.setValue("advanced/hwAccel", m_hardwareAcceleration->isChecked());
    m_settings.setValue("advanced/smoothScroll", m_smoothScrolling->isChecked());
    m_settings.setValue("advanced/doh", m_dnsOverHttps->currentIndex());
    m_settings.setValue("advanced/proxy", m_proxyServer->text());
    m_settings.setValue("advanced/maxConnections", m_maxConnections->value());

    m_settings.sync();

    // 프로필에 설정 적용
    if (m_profile) {
        m_profile->adBlocker()->setEnabled(m_adBlockEnabled->isChecked());
    }
}

void SettingsPage::resetDefaults()
{
    auto reply = QMessageBox::question(this, "기본값 복원",
        "모든 설정을 기본값으로 되돌리시겠습니까?");
    if (reply != QMessageBox::Yes) return;

    m_settings.clear();
    loadSettings();
}

} // namespace Engine
} // namespace Ordinal
