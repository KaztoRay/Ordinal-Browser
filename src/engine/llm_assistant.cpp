#include "llm_assistant.h"
#include <QDir>
#include <QStandardPaths>
#include <QWebEngineSettings>
#include <QToolBar>
#include <QApplication>
#include <QUrl>

namespace Ordinal {
namespace Engine {

LLMAssistant::LLMAssistant(QWidget* parent)
    : QWidget(parent)
{
    // ChatGPT 전용 프로필 (로컬에만 저장, 격리)
    QString storagePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + "/OrdinalV8/chatgpt_profile";
    QDir().mkpath(storagePath);

    m_profile = new QWebEngineProfile("ordinalv8_chatgpt", this);
    m_profile->setPersistentStoragePath(storagePath);
    m_profile->setCachePath(storagePath + "/cache");
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);

    // 보안 설정
    m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    m_profile->setHttpUserAgent(
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    setupUI();
    setFixedWidth(420);
}

void LLMAssistant::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 헤더 바
    auto* headerWidget = new QWidget(this);
    headerWidget->setStyleSheet("background: #1e1e1e; border-bottom: 1px solid #333;");
    headerWidget->setFixedHeight(36);
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(8, 0, 8, 0);

    m_titleLabel = new QLabel("🤖 ChatGPT", this);
    m_titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #87CEEB;");
    headerLayout->addWidget(m_titleLabel);

    headerLayout->addStretch();

    // 새로고침 버튼
    m_refreshBtn = new QPushButton("↻", this);
    m_refreshBtn->setFixedSize(24, 24);
    m_refreshBtn->setStyleSheet(
        "QPushButton { border: none; color: #888; font-size: 16px; }"
        "QPushButton:hover { color: #87CEEB; }");
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        if (m_webView) m_webView->reload();
    });
    headerLayout->addWidget(m_refreshBtn);

    // 새 채팅 버튼
    auto* newChatBtn = new QPushButton("+", this);
    newChatBtn->setFixedSize(24, 24);
    newChatBtn->setStyleSheet(
        "QPushButton { border: none; color: #888; font-size: 16px; }"
        "QPushButton:hover { color: #87CEEB; }");
    connect(newChatBtn, &QPushButton::clicked, this, [this]() {
        if (m_webView) m_webView->load(QUrl("https://chatgpt.com/"));
    });
    headerLayout->addWidget(newChatBtn);

    // 닫기 버튼
    auto* closeBtn = new QPushButton("✕", this);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet(
        "QPushButton { border: none; color: #888; font-size: 14px; }"
        "QPushButton:hover { color: #ff6b6b; }");
    connect(closeBtn, &QPushButton::clicked, this, [this]() { toggle(); });
    headerLayout->addWidget(closeBtn);

    m_mainLayout->addWidget(headerWidget);

    // ChatGPT 웹뷰
    auto* page = new QWebEnginePage(m_profile, this);
    m_webView = new QWebEngineView(this);
    m_webView->setPage(page);

    // 웹엔진 설정
    auto* settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);

    m_mainLayout->addWidget(m_webView, 1);

    // 보안 안내
    auto* secLabel = new QLabel("🔒 로그인 데이터는 이 컴퓨터에만 저장됩니다", this);
    secLabel->setStyleSheet("font-size: 10px; color: #555; padding: 2px 8px; background: #1a1a1a;");
    secLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(secLabel);

    loadChatGPT();
}

void LLMAssistant::loadChatGPT()
{
    m_webView->load(QUrl("https://chatgpt.com/"));
}

void LLMAssistant::toggle()
{
    m_isVisible = !m_isVisible;
    setVisible(m_isVisible);
}

void LLMAssistant::setPageContext(const QString& title, const QString& url, const QString& selectedText)
{
    m_currentPageTitle = title;
    m_currentPageUrl = url;
    m_selectedText = selectedText;
}

} // namespace Engine
} // namespace Ordinal
