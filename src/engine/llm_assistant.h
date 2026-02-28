#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEnginePage>

namespace Ordinal {
namespace Engine {

/**
 * @brief OrdinalV8 AI 어시스턴트 — ChatGPT 웹 연동
 *
 * ChatGPT (chat.openai.com)를 사이드바에 임베드합니다.
 * - API 키 불필요 (ChatGPT 계정으로 로그인)
 * - 모든 데이터는 사용자 로컬에만 저장
 * - 별도 프로필로 쿠키/세션 격리
 */
class LLMAssistant : public QWidget {
    Q_OBJECT

public:
    explicit LLMAssistant(QWidget* parent = nullptr);
    ~LLMAssistant() override = default;

    void setPageContext(const QString& title, const QString& url, const QString& selectedText = "");

signals:
    void requestPageContent();
    void navigateRequested(const QUrl& url);
    void searchRequested(const QString& query);

public slots:
    void toggle();

private:
    void setupUI();
    void loadChatGPT();

    QVBoxLayout* m_mainLayout = nullptr;
    QLabel* m_titleLabel = nullptr;
    QWebEngineView* m_webView = nullptr;
    QWebEngineProfile* m_profile = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    bool m_isVisible = false;
    QString m_currentPageTitle;
    QString m_currentPageUrl;
    QString m_selectedText;
};

} // namespace Engine
} // namespace Ordinal
