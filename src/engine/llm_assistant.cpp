#include "llm_assistant.h"
#include <QScrollBar>
#include <QDateTime>
#include <QDesktopServices>
#include <QApplication>
#include <QStyle>

namespace Ordinal {
namespace Engine {

LLMAssistant::LLMAssistant(QWidget* parent)
    : QWidget(parent)
    , m_netManager(new QNetworkAccessManager(this))
{
    setupUI();
    setupQuickActions();
    loadSettings();
    connect(m_netManager, &QNetworkAccessManager::finished, this, &LLMAssistant::onApiResponse);
    setVisible(false);
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

    m_modelSelector = new QComboBox(this);
    m_modelSelector->addItems({"Ollama (로컬)", "OpenAI", "Anthropic", "커스텀"});
    m_modelSelector->setFixedWidth(120);
    connect(m_modelSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_provider = static_cast<Provider>(idx);
        saveSettings();
    });
    headerLayout->addWidget(m_modelSelector);

    auto* closeBtn = new QPushButton("✕", this);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet("QPushButton { border: none; color: #888; font-size: 14px; } QPushButton:hover { color: white; }");
    connect(closeBtn, &QPushButton::clicked, this, [this]() { toggle(); });
    headerLayout->addWidget(closeBtn);
    m_mainLayout->addLayout(headerLayout);

    // 퀵 액션 바
    m_quickActionsBar = new QWidget(this);
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
    m_mainLayout->addWidget(m_quickActionsBar);

    // 채팅 디스플레이
    m_chatDisplay = new QTextEdit(this);
    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setStyleSheet(
        "QTextEdit { background: #1a1a1a; color: #dcdcdc; border: 1px solid #333; "
        "border-radius: 8px; padding: 8px; font-size: 13px; }"
        "QTextEdit a { color: #87CEEB; }");
    m_chatDisplay->setHtml(
        "<p style='color:#87CEEB;'>👋 안녕하세요! OrdinalV8 AI 어시스턴트입니다.</p>"
        "<p style='color:#888;'>질문하거나 퀵 액션을 사용해보세요.</p>"
        "<p style='color:#666; font-size:11px;'>💡 팁: /help 로 명령어 목록 확인</p>");
    m_mainLayout->addWidget(m_chatDisplay, 1);

    // 입력 영역
    auto* inputLayout = new QHBoxLayout();
    m_inputField = new QLineEdit(this);
    m_inputField->setPlaceholderText("AI에게 질문하세요...");
    m_inputField->setStyleSheet(
        "QLineEdit { background: #252525; color: #87CEEB; border: 1px solid #444; "
        "border-radius: 14px; padding: 6px 14px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #87CEEB; }");
    connect(m_inputField, &QLineEdit::returnPressed, this, &LLMAssistant::onSendMessage);
    inputLayout->addWidget(m_inputField);

    m_sendBtn = new QPushButton("→", this);
    m_sendBtn->setFixedSize(32, 32);
    m_sendBtn->setStyleSheet(
        "QPushButton { background: #4285f4; color: white; border: none; "
        "border-radius: 16px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: #5a9bf4; }");
    connect(m_sendBtn, &QPushButton::clicked, this, &LLMAssistant::onSendMessage);
    inputLayout->addWidget(m_sendBtn);
    m_mainLayout->addLayout(inputLayout);
}

void LLMAssistant::setupQuickActions() {}

void LLMAssistant::toggle()
{
    m_isVisible = !m_isVisible;
    setVisible(m_isVisible);
    if (m_isVisible) m_inputField->setFocus();
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
    QString html = QString("<p><span style='color:%1;font-weight:bold;'>%2 %3</span></p>"
                          "<p style='color:#dcdcdc;margin-left:20px;'>%4</p><hr style='border-color:#333;'>")
                       .arg(color, icon, sender, text.toHtmlEscaped().replace("\n", "<br>"));
    m_chatDisplay->append(html);
    auto* sb = m_chatDisplay->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void LLMAssistant::onSendMessage()
{
    QString input = m_inputField->text().trimmed();
    if (input.isEmpty()) return;
    m_inputField->clear();

    if (input.startsWith("/")) {
        processCommand(input);
        return;
    }

    addMessage("나", input, true);
    m_conversationHistory.append("user: " + input);

    // 페이지 컨텍스트 포함
    QString contextPrompt = input;
    if (!m_currentPageTitle.isEmpty()) {
        contextPrompt = QString("[현재 페이지: %1 (%2)]\n%3")
            .arg(m_currentPageTitle, m_currentPageUrl, input);
    }
    if (!m_selectedText.isEmpty()) {
        contextPrompt += "\n[선택된 텍스트: " + m_selectedText + "]";
    }

    sendToLLM(contextPrompt, buildSystemPrompt());
}

void LLMAssistant::processCommand(const QString& input)
{
    if (input == "/help") {
        addMessage("시스템", 
            "📋 명령어 목록:\n"
            "/help — 도움말\n"
            "/clear — 대화 초기화\n"
            "/model — 현재 모델 정보\n"
            "/summarize — 현재 페이지 요약\n"
            "/translate [언어] — 페이지 번역\n"
            "/security — 보안 분석\n"
            "/search [검색어] — 구글 검색\n"
            "/settings — AI 설정", false);
    } else if (input == "/clear") {
        m_chatDisplay->clear();
        m_conversationHistory.clear();
        addMessage("시스템", "대화가 초기화되었습니다.", false);
    } else if (input == "/model") {
        QString info = QString("Provider: %1\nEndpoint: %2\nModel: %3")
            .arg(m_modelSelector->currentText(), m_apiEndpoint, m_modelName);
        addMessage("시스템", info, false);
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
    } else {
        addMessage("시스템", "❌ 알 수 없는 명령어입니다. /help를 입력하세요.", false);
    }
}

void LLMAssistant::onQuickAction(const QString& action)
{
    if (action == "summarize") {
        addMessage("나", "📝 현재 페이지 요약 요청", true);
        emit requestPageContent();
        QString prompt = QString("다음 웹페이지를 한국어로 간결하게 요약해주세요.\n제목: %1\nURL: %2")
            .arg(m_currentPageTitle, m_currentPageUrl);
        sendToLLM(prompt, "당신은 웹 콘텐츠를 명확하고 간결하게 요약하는 AI입니다. 핵심 내용을 불릿 포인트로 정리하세요.");
    } else if (action == "translate") {
        addMessage("나", "🌐 페이지 번역 요청", true);
        emit requestPageContent();
        QString prompt = QString("다음 웹페이지 내용을 한국어로 자연스럽게 번역해주세요.\n제목: %1\nURL: %2")
            .arg(m_currentPageTitle, m_currentPageUrl);
        sendToLLM(prompt, "당신은 전문 번역 AI입니다. 자연스러운 한국어로 번역하세요.");
    } else if (action == "security") {
        addMessage("나", "🔒 보안 분석 요청", true);
        QString prompt = QString("다음 웹사이트의 보안 상태를 분석해주세요.\nURL: %1\n제목: %2\n"
                                "SSL 인증서, 도메인 신뢰도, 잠재적 위험 요소를 평가하세요.")
            .arg(m_currentPageUrl, m_currentPageTitle);
        sendToLLM(prompt, "당신은 웹 보안 전문가 AI입니다. 보안 위협을 분석하고 위험도를 평가하세요.");
    } else if (action == "code") {
        addMessage("나", "💻 코드 분석 요청", true);
        if (!m_selectedText.isEmpty()) {
            sendToLLM("다음 코드를 분석하고 설명해주세요:\n```\n" + m_selectedText + "\n```",
                      "당신은 코드 분석 전문가 AI입니다. 코드의 기능, 잠재적 버그, 개선점을 설명하세요.");
        } else {
            addMessage("시스템", "💡 분석할 코드를 페이지에서 선택한 후 다시 시도하세요.", false);
        }
    }
}

QString LLMAssistant::buildSystemPrompt() const
{
    return "당신은 OrdinalV8 브라우저의 AI 어시스턴트입니다. "
           "웹 브라우징, 보안 분석, 번역, 코드 분석, 일반 질문에 도움을 줍니다. "
           "한국어와 영어 모두 지원하며, 사용자 언어에 맞춰 응답합니다. "
           "간결하고 정확한 답변을 제공하세요. "
           "보안 위협이 감지되면 즉시 경고하세요.";
}

void LLMAssistant::sendToLLM(const QString& prompt, const QString& systemPrompt)
{
    QJsonObject requestBody;
    QUrl endpoint;

    switch (m_provider) {
    case Provider::Ollama: {
        endpoint = QUrl(m_apiEndpoint.isEmpty() ? "http://localhost:11434/api/chat" : m_apiEndpoint);
        QJsonArray messages;
        if (!systemPrompt.isEmpty()) {
            messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        }
        // 대화 히스토리 (최근 10개)
        int start = qMax(0, m_conversationHistory.size() - 10);
        for (int i = start; i < m_conversationHistory.size(); ++i) {
            auto& msg = m_conversationHistory[i];
            if (msg.startsWith("user: ")) {
                messages.append(QJsonObject{{"role", "user"}, {"content", msg.mid(6)}});
            } else if (msg.startsWith("assistant: ")) {
                messages.append(QJsonObject{{"role", "assistant"}, {"content", msg.mid(11)}});
            }
        }
        messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
        requestBody["model"] = m_modelName.isEmpty() ? "llama3.2" : m_modelName;
        requestBody["messages"] = messages;
        requestBody["stream"] = false;
        break;
    }
    case Provider::OpenAI: {
        endpoint = QUrl("https://api.openai.com/v1/chat/completions");
        QJsonArray messages;
        if (!systemPrompt.isEmpty())
            messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
        requestBody["model"] = m_modelName.isEmpty() ? "gpt-4o-mini" : m_modelName;
        requestBody["messages"] = messages;
        requestBody["max_tokens"] = 2048;
        break;
    }
    case Provider::Anthropic: {
        endpoint = QUrl("https://api.anthropic.com/v1/messages");
        QJsonArray messages;
        messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
        requestBody["model"] = m_modelName.isEmpty() ? "claude-sonnet-4-20250514" : m_modelName;
        requestBody["messages"] = messages;
        requestBody["max_tokens"] = 2048;
        if (!systemPrompt.isEmpty())
            requestBody["system"] = systemPrompt;
        break;
    }
    case Provider::Custom: {
        endpoint = QUrl(m_apiEndpoint);
        QJsonArray messages;
        if (!systemPrompt.isEmpty())
            messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
        requestBody["messages"] = messages;
        requestBody["model"] = m_modelName;
        break;
    }
    }

    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (m_provider == Provider::OpenAI && !m_apiKey.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    else if (m_provider == Provider::Anthropic && !m_apiKey.isEmpty()) {
        request.setRawHeader("x-api-key", m_apiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
    }

    addMessage("AI", "⏳ 생각 중...", false);
    m_netManager->post(request, QJsonDocument(requestBody).toJson());
}

void LLMAssistant::onApiResponse(QNetworkReply* reply)
{
    reply->deleteLater();

    // 마지막 "생각 중..." 메시지 제거
    QString html = m_chatDisplay->toHtml();
    int thinkingIdx = html.lastIndexOf("⏳ 생각 중...");
    if (thinkingIdx >= 0) {
        // 간단히 마지막 몇 줄 제거하고 다시 추가
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString errMsg;
        if (reply->error() == QNetworkReply::ConnectionRefusedError) {
            errMsg = "❌ LLM 서버에 연결할 수 없습니다.\n\n";
            if (m_provider == Provider::Ollama) {
                errMsg += "💡 Ollama가 실행 중인지 확인하세요:\n"
                         "  brew install ollama\n"
                         "  ollama serve\n"
                         "  ollama pull llama3.2";
            } else {
                errMsg += "API 키와 엔드포인트를 확인하세요.";
            }
        } else {
            errMsg = "❌ 오류: " + reply->errorString();
        }
        addMessage("시스템", errMsg, false);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    QString responseText;

    switch (m_provider) {
    case Provider::Ollama:
        responseText = obj["message"].toObject()["content"].toString();
        break;
    case Provider::OpenAI:
    case Provider::Custom:
        responseText = obj["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString();
        break;
    case Provider::Anthropic:
        responseText = obj["content"].toArray()[0].toObject()["text"].toString();
        break;
    }

    if (responseText.isEmpty()) {
        responseText = "⚠️ 응답을 파싱할 수 없습니다.";
    }

    m_conversationHistory.append("assistant: " + responseText);
    addMessage("AI", responseText, false);
}

void LLMAssistant::summarizePage(const QString& pageContent)
{
    sendToLLM("다음 웹페이지 내용을 한국어로 간결하게 요약해주세요:\n\n" + pageContent.left(4000),
              "당신은 콘텐츠 요약 전문가입니다. 핵심을 불릿 포인트로 정리하세요.");
}

void LLMAssistant::translatePage(const QString& content, const QString& targetLang)
{
    sendToLLM(QString("다음 텍스트를 %1로 번역해주세요:\n\n%2").arg(targetLang, content.left(4000)),
              "당신은 전문 번역 AI입니다.");
}

void LLMAssistant::analyzeSecurityThreat(const QString& threatInfo)
{
    sendToLLM("다음 보안 위협을 분석해주세요:\n\n" + threatInfo,
              "당신은 사이버 보안 전문가 AI입니다.");
}

void LLMAssistant::analyzeCode(const QString& code)
{
    sendToLLM("다음 코드를 분석해주세요:\n```\n" + code + "\n```",
              "당신은 코드 리뷰 전문가 AI입니다.");
}

void LLMAssistant::streamResponse(const QString& text)
{
    addMessage("AI", text, false);
}

void LLMAssistant::loadSettings()
{
    QSettings s("OrdinalV8", "OrdinalV8");
    m_provider = static_cast<Provider>(s.value("ai/provider", 0).toInt());
    m_apiKey = s.value("ai/apiKey").toString();
    m_apiEndpoint = s.value("ai/endpoint", "http://localhost:11434/api/chat").toString();
    m_modelName = s.value("ai/model", "llama3.2").toString();
    m_modelSelector->setCurrentIndex(static_cast<int>(m_provider));
}

void LLMAssistant::saveSettings()
{
    QSettings s("OrdinalV8", "OrdinalV8");
    s.setValue("ai/provider", static_cast<int>(m_provider));
    s.setValue("ai/apiKey", m_apiKey);
    s.setValue("ai/endpoint", m_apiEndpoint);
    s.setValue("ai/model", m_modelName);
}

} // namespace Engine
} // namespace Ordinal
