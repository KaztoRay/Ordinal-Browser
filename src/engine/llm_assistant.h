#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QComboBox>

namespace Ordinal {
namespace Engine {

class LLMAssistant : public QWidget {
    Q_OBJECT

public:
    explicit LLMAssistant(QWidget* parent = nullptr);
    ~LLMAssistant() override = default;

    void setPageContext(const QString& title, const QString& url, const QString& selectedText = "");
    void summarizePage(const QString& pageContent);
    void translatePage(const QString& content, const QString& targetLang = "ko");
    void analyzeSecurityThreat(const QString& threatInfo);
    void analyzeCode(const QString& code);

signals:
    void requestPageContent();
    void navigateRequested(const QUrl& url);
    void searchRequested(const QString& query);

public slots:
    void toggle();

private slots:
    void onSendMessage();
    void onApiResponse(QNetworkReply* reply);
    void onQuickAction(const QString& action);

private:
    void setupUI();
    void setupQuickActions();
    void addMessage(const QString& sender, const QString& text, bool isUser = false);
    void sendToLLM(const QString& prompt, const QString& systemPrompt = "");
    void processCommand(const QString& input);
    QString buildSystemPrompt() const;
    void loadSettings();
    void saveSettings();
    void streamResponse(const QString& text);

    QVBoxLayout* m_mainLayout = nullptr;
    QLabel* m_titleLabel = nullptr;
    QTextEdit* m_chatDisplay = nullptr;
    QLineEdit* m_inputField = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QWidget* m_quickActionsBar = nullptr;
    QComboBox* m_modelSelector = nullptr;

    QNetworkAccessManager* m_netManager = nullptr;

    QString m_apiKey;
    QString m_apiEndpoint;
    QString m_modelName;
    QString m_currentPageTitle;
    QString m_currentPageUrl;
    QString m_selectedText;
    QStringList m_conversationHistory;
    bool m_isVisible = false;

    enum class Provider { OpenAI, Anthropic, Ollama, Custom };
    Provider m_provider = Provider::Ollama;
};

} // namespace Engine
} // namespace Ordinal
