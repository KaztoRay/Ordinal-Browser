#include "llm_assistant.h"
#include <QScrollBar>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>

namespace Ordinal {
namespace Engine {

LLMAssistant::LLMAssistant(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    loadSettings();
    setFixedWidth(380);
}

void LLMAssistant::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(6);

    // 헤더
    auto* headerLayout = new QHBoxLayout();
    m_titleLabel = new QLabel("🤖 OrdinalV8 AI", this);
    m_titleLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #87CEEB;");
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    auto* closeBtn = new QPushButton("✕", this);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet("QPushButton { border: none; color: #888; font-size: 14px; } QPushButton:hover { color: white; }");
    connect(closeBtn, &QPushButton::clicked, this, [this]() { toggle(); });
    headerLayout->addWidget(closeBtn);
    m_mainLayout->addLayout(headerLayout);

    // ===== 로그인 패널 =====
    m_loginPanel = new QWidget(this);
    auto* loginLayout = new QVBoxLayout(m_loginPanel);
    loginLayout->setContentsMargins(0, 20, 0, 0);

    auto* logoLabel = new QLabel("🔑", m_loginPanel);
    logoLabel->setStyleSheet("font-size: 48px;");
    logoLabel->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(logoLabel);

    auto* titleLabel = new QLabel("OpenAI API 로그인", m_loginPanel);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #87CEEB;");
    titleLabel->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(titleLabel);

    auto* descLabel = new QLabel("AI 어시스턴트를 사용하려면\nOpenAI API 키를 입력하세요.", m_loginPanel);
    descLabel->setStyleSheet("font-size: 12px; color: #888;");
    descLabel->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(descLabel);

    loginLayout->addSpacing(12);

    m_apiKeyInput = new QLineEdit(m_loginPanel);
    m_apiKeyInput->setPlaceholderText("sk-... OpenAI API Key");
    m_apiKeyInput->setEchoMode(QLineEdit::Password);
    m_apiKeyInput->setStyleSheet(
        "QLineEdit { background: #252525; color: #87CEEB; border: 1px solid #444; "
        "border-radius: 8px; padding: 8px 12px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #87CEEB; }");
    loginLayout->addWidget(m_apiKeyInput);

    auto* modelLabel = new QLabel("모델: gpt-4o-mini (기본)", m_loginPanel);
    modelLabel->setStyleSheet("font-size: 11px; color: #666;");
    modelLabel->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(modelLabel);

    loginLayout->addSpacing(8);

    auto* loginBtn = new QPushButton("🚀 연결", m_loginPanel);
    loginBtn->setStyleSheet(
        "QPushButton { background: #4285f4; color: white; border: none; "
        "border-radius: 8px; padding: 10px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background: #5a9bf4; }");
    connect(loginBtn, &QPushButton::clicked, this, [this]() {
        QString key = m_apiKeyInput->text().trimmed();
        if (key.isEmpty() || !key.startsWith("sk-")) {
            m_apiKeyInput->setStyleSheet(
                "QLineEdit { background: #252525; color: #ff6b6b; border: 1px solid #ff6b6b; "
                "border-radius: 8px; padding: 8px 12px; font-size: 13px; }");
            return;
        }
        m_apiKey = key;
        m_isLoggedIn = true;
        saveSettings();
        m_loginPanel->hide();
        m_chatPanel->show();
        addMessage("AI", "✅ OpenAI 연결 완료! 무엇이든 물어보세요.\n\n💡 팁: /help 로 명령어 확인", false);
        m_inputField->setFocus();
    });
    loginLayout->addWidget(loginBtn);

    auto* helpLabel = new QLabel("<a href='https://platform.openai.com/api-keys' style='color:#87CEEB;'>API 키 발급받기 →</a>", m_loginPanel);
    helpLabel->setOpenExternalLinks(true);
    helpLabel->setAlignment(Qt::AlignCenter);
    helpLabel->setStyleSheet("font-size: 11px;");
    loginLayout->addWidget(helpLabel);

    loginLayout->addStretch();
    m_mainLayout->addWidget(m_loginPanel);

    // ===== 채팅 패널 =====
    m_chatPanel = new QWidget(this);
    auto* chatLayout = new QVBoxLayout(m_chatPanel);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(6);

    // 퀵 액션
    m_quickActionsBar = new QWidget(m_chatPanel);
    auto* quickLayout = new QHBoxLayout(m_quickActionsBar);
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setSpacing(4);

    auto addQuickBtn = [&](const QString& emoji, const QString& label, const QString& action) {
        auto* btn = new QPushButton(emoji + " " + label, m_quickActionsBar);
        btn->setStyleSheet(
            "QPushButton { background: #2d2d2d; color: #ddd; border: 1px solid #444; "
            "border-radius: 12px; padding: 4px 10px; font-size: 11px; }"
            "QPushButton:hover { background: #3d3d3d; border-color: #87CEEB; }");
        connect(btn, &QPushButton::clicked, this, [this, action]() { onQuickAction(action); });
        quickLayout->addWidget(btn);
    };

    addQuickBtn("📝", "요약", "summarize");
    addQuickBtn("🌐", "번역", "translate");
    addQuickBtn("🔒", "보안", "security");
    addQuickBtn("💻", "코드", "code");
    chatLayout->addWidget(m_quickActionsBar);

    // 채팅 디스플레이
    m_chatDisplay = new QTextEdit(m_chatPanel);
    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setStyleSheet(
        "QTextEdit { background: #1a1a1a; color: #dcdcdc; border: 1px solid #333; "
        "border-radius: 8px; padding: 8px; font-size: 13px; }");
    chatLayout->addWidget(m_chatDisplay, 1);

    // 입력
    auto* inputLayout = new QHBoxLayout();
    m_inputField = new QLineEdit(m_chatPanel);
    m_inputField->setPlaceholderText("AI에게 질문하세요...");
    m_inputField->setStyleSheet(
        "QLineEdit { background: #252525; color: #87CEEB; border: 1px solid #444; "
        "border-radius: 14px; padding: 6px 14px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #87CEEB; }");
    connect(m_inputField, &QLineEdit::returnPressed, this, &LLMAssistant::onSendMessage);
    inputLayout->addWidget(m_inputField);

    m_sendBtn = new QPushButton("→", m_chatPanel);
    m_sendBtn->setFixedSize(32, 32);
    m_sendBtn->setStyleSheet(
        "QPushButton { background: #4285f4; color: white; border: none; "
        "border-radius: 16px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: #5a9bf4; }");
    connect(m_sendBtn, &QPushButton::clicked, this, &LLMAssistant::onSendMessage);
    inputLayout->addWidget(m_sendBtn);
    chatLayout->addLayout(inputLayout);

    // 로그아웃 버튼
    auto* logoutBtn = new QPushButton("🔓 로그아웃", m_chatPanel);
    logoutBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #666; border: none; font-size: 11px; }"
        "QPushButton:hover { color: #ff6b6b; }");
    connect(logoutBtn, &QPushButton::clicked, this, [this]() {
        m_apiKey.clear();
        m_isLoggedIn = false;
        m_conversationHistory.clear();
        m_chatDisplay->clear();
        saveSettings();
        m_chatPanel->hide();
        m_loginPanel->show();
        m_apiKeyInput->clear();
    });
    chatLayout->addWidget(logoutBtn, 0, Qt::AlignCenter);

    m_mainLayout->addWidget(m_chatPanel);

    // 초기 상태
    m_chatPanel->hide();
    m_loginPanel->show();
}

void LLMAssistant::toggle()
{
    m_isVisible = !m_isVisible;
    setVisible(m_isVisible);
    if (m_isVisible) {
        if (m_isLoggedIn && m_inputField) m_inputField->setFocus();
        else if (m_apiKeyInput) m_apiKeyInput->setFocus();
    }
}

void LLMAssistant::setPageContext(const QString& title, const QString& url, const QString& selectedText)
{
    m_currentPageTitle = title;
    m_currentPageUrl = url;
    m_selectedText = selectedText;
}

void LLMAssistant::addMessage(const QString& sender, const QString& text, bool isUser)
{
    QString color = isUser ? "#87CEEB" : "#a8d8a8";
    QString icon = isUser ? "👤" : "🤖";
    QString escapedText = text.toHtmlEscaped().replace("\n", "<br>");
    QString html = QString("<p><span style='color:%1;font-weight:bold;'>%2 %3</span></p>"
                          "<p style='color:#dcdcdc;margin-left:20px;'>%4</p><hr style='border-color:#333;'>")
                       .arg(color, icon, sender, escapedText);
    m_chatDisplay->append(html);
    if (auto* sb = m_chatDisplay->verticalScrollBar())
        sb->setValue(sb->maximum());
}

void LLMAssistant::onSendMessage()
{
    if (!m_inputField) return;
    QString input = m_inputField->text().trimmed();
    if (input.isEmpty()) return;
    m_inputField->clear();

    if (input.startsWith("/")) {
        processCommand(input);
        return;
    }

    addMessage("나", input, true);
    m_conversationHistory.append("user:" + input);

    QString contextPrompt = input;
    if (!m_currentPageTitle.isEmpty()) {
        contextPrompt = QString("[현재 페이지: %1 (%2)]\n%3")
            .arg(m_currentPageTitle, m_currentPageUrl, input);
    }
    if (!m_selectedText.isEmpty()) {
        contextPrompt += "\n[선택된 텍스트: " + m_selectedText + "]";
    }

    sendToOpenAI(contextPrompt,
        "당신은 OrdinalV8 브라우저의 AI 어시스턴트입니다. "
        "웹 브라우징, 보안 분석, 번역, 코드 분석, 일반 질문에 도움을 줍니다. "
        "한국어로 간결하고 정확하게 답변하세요.");
}

void LLMAssistant::processCommand(const QString& input)
{
    if (input == "/help") {
        addMessage("시스템",
            "📋 명령어:\n/help — 도움말\n/clear — 대화 초기화\n"
            "/summarize — 페이지 요약\n/translate — 페이지 번역\n"
            "/security — 보안 분석\n/search [검색어] — 검색\n/logout — 로그아웃", false);
    } else if (input == "/clear") {
        m_chatDisplay->clear();
        m_conversationHistory.clear();
        addMessage("시스템", "대화가 초기화되었습니다.", false);
    } else if (input == "/summarize") {
        onQuickAction("summarize");
    } else if (input.startsWith("/translate")) {
        onQuickAction("translate");
    } else if (input == "/security") {
        onQuickAction("security");
    } else if (input.startsWith("/search ")) {
        QString query = input.mid(8).trimmed();
        emit searchRequested(query);
        addMessage("시스템", "🔍 검색: " + query, false);
    } else if (input == "/logout") {
        m_apiKey.clear();
        m_isLoggedIn = false;
        saveSettings();
        m_chatPanel->hide();
        m_loginPanel->show();
    } else {
        addMessage("시스템", "❌ 알 수 없는 명령어. /help 입력", false);
    }
}

void LLMAssistant::onQuickAction(const QString& action)
{
    QString prompt, system;
    if (action == "summarize") {
        addMessage("나", "📝 페이지 요약 요청", true);
        prompt = QString("다음 웹페이지를 한국어로 간결하게 요약:\n제목: %1\nURL: %2")
            .arg(m_currentPageTitle, m_currentPageUrl);
        system = "콘텐츠 요약 전문가. 핵심을 불릿 포인트로 정리.";
    } else if (action == "translate") {
        addMessage("나", "🌐 번역 요청", true);
        prompt = QString("다음 웹페이지를 한국어로 번역:\n제목: %1\nURL: %2")
            .arg(m_currentPageTitle, m_currentPageUrl);
        system = "전문 번역 AI. 자연스러운 한국어.";
    } else if (action == "security") {
        addMessage("나", "🔒 보안 분석 요청", true);
        prompt = QString("보안 분석:\nURL: %1\n제목: %2\nSSL, 도메인 신뢰도, 위협 평가")
            .arg(m_currentPageUrl, m_currentPageTitle);
        system = "웹 보안 전문가.";
    } else if (action == "code") {
        if (m_selectedText.isEmpty()) {
            addMessage("시스템", "💡 코드를 선택한 후 다시 시도하세요.", false);
            return;
        }
        addMessage("나", "💻 코드 분석 요청", true);
        prompt = "코드 분석:\n```\n" + m_selectedText + "\n```";
        system = "코드 리뷰 전문가.";
    }

    if (!prompt.isEmpty())
        sendToOpenAI(prompt, system);
}

void LLMAssistant::sendToOpenAI(const QString& prompt, const QString& systemPrompt)
{
    if (m_apiKey.isEmpty()) {
        addMessage("시스템", "❌ API 키가 설정되지 않았습니다. /logout 후 다시 로그인하세요.", false);
        return;
    }

    addMessage("AI", "⏳ 생각 중...", false);

    // JSON 요청 생성
    QJsonObject requestBody;
    QJsonArray messages;

    if (!systemPrompt.isEmpty())
        messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});

    // 최근 대화 히스토리 (최대 10개)
    int start = qMax(0, m_conversationHistory.size() - 10);
    for (int i = start; i < m_conversationHistory.size(); ++i) {
        const QString& msg = m_conversationHistory[i];
        if (msg.startsWith("user:"))
            messages.append(QJsonObject{{"role", "user"}, {"content", msg.mid(5)}});
        else if (msg.startsWith("assistant:"))
            messages.append(QJsonObject{{"role", "assistant"}, {"content", msg.mid(10)}});
    }

    messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
    requestBody["model"] = m_model.isEmpty() ? "gpt-4o-mini" : m_model;
    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 2048;

    QString jsonData = QJsonDocument(requestBody).toJson(QJsonDocument::Compact);

    // curl로 OpenAI API 호출
    auto* process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus) {
        QByteArray output = process->readAllStandardOutput();
        process->deleteLater();

        if (exitCode != 0 || output.isEmpty()) {
            addMessage("시스템", "❌ OpenAI 연결 실패. API 키를 확인하세요.", false);
            return;
        }

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(output, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            addMessage("시스템", "❌ 응답 파싱 실패: " + parseErr.errorString(), false);
            return;
        }

        QJsonObject obj = doc.object();

        // 에러 체크
        if (obj.contains("error")) {
            QString errMsg = obj.value("error").toObject().value("message").toString();
            addMessage("시스템", "❌ OpenAI 오류: " + errMsg, false);
            return;
        }

        // 응답 추출 (안전하게)
        QString text;
        QJsonArray choices = obj.value("choices").toArray();
        if (!choices.isEmpty()) {
            QJsonObject firstChoice = choices.at(0).toObject();
            QJsonObject msg = firstChoice.value("message").toObject();
            text = msg.value("content").toString();
        }

        if (text.isEmpty()) {
            addMessage("시스템", "⚠️ 빈 응답을 받았습니다.", false);
            return;
        }

        m_conversationHistory.append("assistant:" + text);
        addMessage("AI", text, false);
    });

    QStringList args;
    args << "-s" << "--max-time" << "60"
         << "https://api.openai.com/v1/chat/completions"
         << "-H" << "Content-Type: application/json"
         << "-H" << ("Authorization: Bearer " + m_apiKey)
         << "-d" << jsonData;

    process->start("curl", args);
}

void LLMAssistant::summarizePage(const QString& pageContent)
{
    sendToOpenAI("요약:\n" + pageContent.left(4000), "콘텐츠 요약 전문가.");
}

void LLMAssistant::translatePage(const QString& content, const QString& targetLang)
{
    sendToOpenAI(QString("%1로 번역:\n%2").arg(targetLang, content.left(4000)), "번역 전문가.");
}

void LLMAssistant::analyzeSecurityThreat(const QString& threatInfo)
{
    sendToOpenAI("보안 분석:\n" + threatInfo, "사이버 보안 전문가.");
}

void LLMAssistant::analyzeCode(const QString& code)
{
    sendToOpenAI("코드 분석:\n```\n" + code + "\n```", "코드 리뷰 전문가.");
}

void LLMAssistant::showApiKeyDialog() {}

void LLMAssistant::loadSettings()
{
    QSettings s("OrdinalV8", "OrdinalV8");
    m_apiKey = s.value("ai/openai_key").toString();
    m_model = s.value("ai/model", "gpt-4o-mini").toString();

    if (!m_apiKey.isEmpty()) {
        m_isLoggedIn = true;
        m_loginPanel->hide();
        m_chatPanel->show();
        addMessage("AI", "✅ OpenAI 연결됨. 무엇이든 물어보세요!\n💡 /help 로 명령어 확인", false);
    }
}

void LLMAssistant::saveSettings()
{
    QSettings s("OrdinalV8", "OrdinalV8");
    s.setValue("ai/openai_key", m_apiKey);
    s.setValue("ai/model", m_model);
}

} // namespace Engine
} // namespace Ordinal
