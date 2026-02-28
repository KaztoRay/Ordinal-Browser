#include "url_completer.h"
#include "data_manager.h"

#include <QLineEdit>
#include <QListView>
#include <QFocusEvent>
#include <QApplication>
#include <QPalette>

namespace Ordinal {
namespace Engine {

// ============================================================
// SuggestionModel
// ============================================================

SuggestionModel::SuggestionModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void SuggestionModel::setManagers(HistoryManager* history, BookmarkManager* bookmarks)
{
    m_history = history;
    m_bookmarks = bookmarks;
}

void SuggestionModel::updateSuggestions(const QString& prefix)
{
    beginResetModel();
    m_items.clear();

    if (prefix.length() < 2) {
        endResetModel();
        return;
    }

    // 북마크 검색 (최대 3개)
    if (m_bookmarks) {
        auto bookmarks = m_bookmarks->search(prefix);
        int count = 0;
        for (const auto& bm : bookmarks) {
            if (count >= 3) break;
            SuggestionItem item;
            item.type = SuggestionItem::Bookmark;
            item.title = bm.title;
            item.url = bm.url;
            m_items.append(item);
            count++;
        }
    }

    // 히스토리 검색 (최대 5개)
    if (m_history) {
        auto entries = m_history->suggest(prefix, 5);
        for (const auto& entry : entries) {
            // 이미 북마크에 있으면 스킵
            bool duplicate = false;
            for (const auto& existing : m_items) {
                if (existing.url == entry.url) { duplicate = true; break; }
            }
            if (duplicate) continue;

            SuggestionItem item;
            item.type = SuggestionItem::History;
            item.title = entry.title;
            item.url = entry.url;
            item.visitCount = entry.visitCount;
            m_items.append(item);
        }
    }

    // 검색 엔진 제안 (입력값 자체)
    if (!prefix.startsWith("http") && !prefix.contains(".")) {
        SuggestionItem item;
        item.type = SuggestionItem::SearchEngine;
        item.title = "\"" + prefix + "\" Google 검색";
        item.url = QUrl("https://www.google.com/search?q=" + QUrl::toPercentEncoding(prefix));
        m_items.append(item);
    }

    endResetModel();
}

int SuggestionModel::rowCount(const QModelIndex&) const
{
    return m_items.size();
}

QVariant SuggestionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size()) return {};

    const auto& item = m_items[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        return item.title;
    case Qt::ToolTipRole:
        return item.url.toString();
    case Qt::UserRole:
        return item.url;
    case Qt::UserRole + 1:
        return static_cast<int>(item.type);
    case Qt::UserRole + 2:
        return item.url.toString();
    default:
        return {};
    }
}

QUrl SuggestionModel::urlAt(int row) const
{
    if (row < 0 || row >= m_items.size()) return {};
    return m_items[row].url;
}

// ============================================================
// SuggestionDelegate
// ============================================================

SuggestionDelegate::SuggestionDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void SuggestionDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    painter->save();

    // 배경
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
        painter->setPen(option.palette.highlightedText().color());
    } else {
        painter->fillRect(option.rect, option.palette.window());
        painter->setPen(option.palette.text().color());
    }

    QRect rect = option.rect.adjusted(8, 4, -8, -4);

    // 타입 아이콘
    int type = index.data(Qt::UserRole + 1).toInt();
    QString prefix;
    switch (type) {
    case SuggestionItem::Bookmark: prefix = "★ "; break;
    case SuggestionItem::History: prefix = "🕐 "; break;
    case SuggestionItem::SearchEngine: prefix = "🔍 "; break;
    }

    // 제목
    QString title = prefix + index.data(Qt::DisplayRole).toString();
    QFont titleFont = option.font;
    titleFont.setPointSize(titleFont.pointSize());
    painter->setFont(titleFont);
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop, title);

    // URL (작은 글자)
    QString url = index.data(Qt::UserRole + 2).toString();
    QFont urlFont = option.font;
    urlFont.setPointSize(urlFont.pointSize() - 1);
    QColor urlColor = (option.state & QStyle::State_Selected) ?
        option.palette.highlightedText().color() : QColor("#666");
    painter->setFont(urlFont);
    painter->setPen(urlColor);
    painter->drawText(rect.adjusted(0, rect.height() / 2, 0, 0),
                      Qt::AlignLeft | Qt::AlignTop, url);

    painter->restore();
}

QSize SuggestionDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(400, 44);
}

// ============================================================
// OrdinalUrlBar
// ============================================================

OrdinalUrlBar::OrdinalUrlBar(QWidget* parent)
    : QLineEdit(parent)
{
    setPlaceholderText("URL 또는 검색어 입력...");
    setClearButtonEnabled(true);
    setMinimumHeight(30);
    setStyleSheet(
        "QLineEdit {"
        "  border: 1px solid #ccc; border-radius: 15px;"
        "  padding: 4px 14px; font-size: 13px;"
        "  background: #f5f5f5;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #4285f4; background: white;"
        "}");

    setupCompleter();

    connect(this, &QLineEdit::textEdited, this, &OrdinalUrlBar::onTextEdited);
    connect(this, &QLineEdit::returnPressed, this, [this]() {
        emit urlEntered(text());
    });
}

void OrdinalUrlBar::setManagers(HistoryManager* history, BookmarkManager* bookmarks)
{
    m_model->setManagers(history, bookmarks);
}

void OrdinalUrlBar::setSecurityLevel(int level)
{
    // 보안 수준에 따라 배경색 변경
    QString borderColor;
    switch (level) {
    case 1: borderColor = "#34a853"; break; // Safe
    case 2: borderColor = "#fbbc05"; break; // Warning
    case 3: borderColor = "#ea4335"; break; // Danger
    default: borderColor = "#4285f4"; break;
    }

    setStyleSheet(QString(
        "QLineEdit {"
        "  border: 1px solid #ccc; border-radius: 15px;"
        "  padding: 4px 14px; font-size: 13px;"
        "  background: #f5f5f5;"
        "}"
        "QLineEdit:focus {"
        "  border-color: %1; background: white;"
        "}").arg(borderColor));
}

void OrdinalUrlBar::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    selectAll();
}

void OrdinalUrlBar::setupCompleter()
{
    m_model = new SuggestionModel(this);
    m_delegate = new SuggestionDelegate(this);

    m_completer = new QCompleter(m_model, this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(8);

    auto* popup = qobject_cast<QListView*>(m_completer->popup());
    if (popup) {
        popup->setItemDelegate(m_delegate);
        popup->setMinimumWidth(500);
    }

    setCompleter(m_completer);

    connect(m_completer, QOverload<const QModelIndex&>::of(&QCompleter::activated),
            this, [this](const QModelIndex& index) {
        QUrl url = index.data(Qt::UserRole).toUrl();
        if (url.isValid()) {
            emit suggestionSelected(url);
        }
    });
}

void OrdinalUrlBar::onTextEdited(const QString& text)
{
    m_model->updateSuggestions(text);
}

} // namespace Engine
} // namespace Ordinal
