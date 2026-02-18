#pragma once

#include <QCompleter>
#include <QStyledItemDelegate>
#include <QAbstractListModel>
#include <QLineEdit>
#include <QUrl>
#include <QIcon>
#include <QList>
#include <QPainter>

namespace Ordinal {
namespace Engine {

class HistoryManager;
class BookmarkManager;

// ============================================================
// SuggestionItem — 자동완성 제안 항목
// ============================================================
struct SuggestionItem {
    enum Type { History, Bookmark, SearchEngine };
    Type type;
    QString title;
    QUrl url;
    int visitCount = 0;
    QIcon icon;
};

// ============================================================
// SuggestionModel — 자동완성 데이터 모델
// ============================================================
class SuggestionModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit SuggestionModel(QObject* parent = nullptr);

    void setManagers(HistoryManager* history, BookmarkManager* bookmarks);
    void updateSuggestions(const QString& prefix);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    QUrl urlAt(int row) const;

private:
    QList<SuggestionItem> m_items;
    HistoryManager* m_history = nullptr;
    BookmarkManager* m_bookmarks = nullptr;
};

// ============================================================
// SuggestionDelegate — 커스텀 드롭다운 아이템 렌더링
// ============================================================
class SuggestionDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit SuggestionDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

// ============================================================
// OrdinalUrlBar — 커스텀 URL 바 (자동완성 + 보안 표시)
// ============================================================
class OrdinalUrlBar : public QLineEdit {
    Q_OBJECT

public:
    explicit OrdinalUrlBar(QWidget* parent = nullptr);

    void setManagers(HistoryManager* history, BookmarkManager* bookmarks);
    void setSecurityLevel(int level); // 0=unknown, 1=safe, 2=warning, 3=danger

signals:
    void urlEntered(const QString& text);
    void suggestionSelected(const QUrl& url);

protected:
    void focusInEvent(QFocusEvent* event) override;

private:
    void setupCompleter();
    void onTextEdited(const QString& text);

    SuggestionModel* m_model = nullptr;
    QCompleter* m_completer = nullptr;
    SuggestionDelegate* m_delegate = nullptr;
};

} // namespace Engine
} // namespace Ordinal
