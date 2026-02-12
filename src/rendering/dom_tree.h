#pragma once

/**
 * @file dom_tree.h
 * @brief DOM 트리 구조체 및 노드 계층
 * 
 * Document, Element, Text, Comment 노드를 포함하는
 * 완전한 DOM 트리를 구현합니다.
 */

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <optional>

namespace ordinal::rendering {

// 전방 선언
class DomNode;
class ElementNode;
class TextNode;
class CommentNode;
class DocumentNode;

using DomNodePtr = std::shared_ptr<DomNode>;
using ElementPtr = std::shared_ptr<ElementNode>;
using TextPtr = std::shared_ptr<TextNode>;
using CommentPtr = std::shared_ptr<CommentNode>;
using DocumentPtr = std::shared_ptr<DocumentNode>;

/**
 * @brief DOM 노드 타입 열거형
 */
enum class NodeType {
    Document,   ///< 문서 루트 노드
    Element,    ///< HTML 요소 노드
    Text,       ///< 텍스트 노드
    Comment     ///< 주석 노드
};

/**
 * @brief DOM 노드 기본 클래스
 * 
 * 모든 DOM 노드의 공통 인터페이스를 정의합니다.
 */
class DomNode : public std::enable_shared_from_this<DomNode> {
public:
    explicit DomNode(NodeType type);
    virtual ~DomNode() = default;

    // 노드 타입 조회
    [[nodiscard]] NodeType nodeType() const { return type_; }
    [[nodiscard]] virtual std::string nodeName() const = 0;
    [[nodiscard]] virtual std::string nodeValue() const { return ""; }

    // 부모/자식 관계
    [[nodiscard]] DomNodePtr parent() const { return parent_.lock(); }
    [[nodiscard]] const std::vector<DomNodePtr>& children() const { return children_; }
    [[nodiscard]] size_t childCount() const { return children_.size(); }

    // 자식 노드 조작
    void appendChild(DomNodePtr child);
    void insertChild(size_t index, DomNodePtr child);
    void removeChild(size_t index);
    void removeChild(const DomNodePtr& child);
    void replaceChild(size_t index, DomNodePtr new_child);
    void clearChildren();

    // 형제 노드 탐색
    [[nodiscard]] DomNodePtr previousSibling() const;
    [[nodiscard]] DomNodePtr nextSibling() const;
    [[nodiscard]] DomNodePtr firstChild() const;
    [[nodiscard]] DomNodePtr lastChild() const;

    // 트리 순회
    void walkDepthFirst(const std::function<bool(DomNodePtr)>& visitor);
    void walkBreadthFirst(const std::function<bool(DomNodePtr)>& visitor);

    // 직렬화
    [[nodiscard]] virtual std::string serialize(int indent = 0) const = 0;
    [[nodiscard]] virtual std::string textContent() const;

protected:
    NodeType type_;
    std::weak_ptr<DomNode> parent_;
    std::vector<DomNodePtr> children_;
};

/**
 * @brief HTML 요소 노드
 * 
 * 태그 이름, 속성, 클래스, ID 등을 관리합니다.
 */
class ElementNode : public DomNode {
public:
    explicit ElementNode(const std::string& tag_name);
    ~ElementNode() override = default;

    [[nodiscard]] std::string nodeName() const override { return tag_name_; }
    [[nodiscard]] const std::string& tagName() const { return tag_name_; }

    // 속성 관리 (CRUD)
    void setAttribute(const std::string& name, const std::string& value);
    [[nodiscard]] std::optional<std::string> getAttribute(const std::string& name) const;
    bool hasAttribute(const std::string& name) const;
    void removeAttribute(const std::string& name);
    [[nodiscard]] const std::unordered_map<std::string, std::string>& attributes() const {
        return attributes_;
    }

    // 편의 속성 접근자
    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::string className() const;
    [[nodiscard]] std::vector<std::string> classList() const;

    // 쿼리 셀렉터
    [[nodiscard]] ElementPtr querySelector(const std::string& selector) const;
    [[nodiscard]] std::vector<ElementPtr> querySelectorAll(const std::string& selector) const;

    // 태그별 검색
    [[nodiscard]] std::vector<ElementPtr> getElementsByTagName(const std::string& tag) const;
    [[nodiscard]] std::vector<ElementPtr> getElementsByClassName(const std::string& cls) const;
    [[nodiscard]] ElementPtr getElementById(const std::string& id) const;

    // innerHTML / outerHTML
    [[nodiscard]] std::string innerHTML() const;
    [[nodiscard]] std::string outerHTML() const;

    // void 요소 여부 (br, img, hr, input 등)
    [[nodiscard]] bool isVoidElement() const;

    // 직렬화
    [[nodiscard]] std::string serialize(int indent = 0) const override;
    [[nodiscard]] std::string textContent() const override;

private:
    /**
     * @brief 간단한 CSS 셀렉터 매칭
     * @param selector 타입/클래스/ID/속성 셀렉터
     */
    [[nodiscard]] bool matchesSelector(const std::string& selector) const;

    std::string tag_name_;
    std::unordered_map<std::string, std::string> attributes_;
};

/**
 * @brief 텍스트 노드
 */
class TextNode : public DomNode {
public:
    explicit TextNode(const std::string& text);
    ~TextNode() override = default;

    [[nodiscard]] std::string nodeName() const override { return "#text"; }
    [[nodiscard]] std::string nodeValue() const override { return text_; }
    [[nodiscard]] std::string textContent() const override { return text_; }

    void setText(const std::string& text) { text_ = text; }
    [[nodiscard]] const std::string& text() const { return text_; }

    [[nodiscard]] std::string serialize(int indent = 0) const override;

private:
    std::string text_;
};

/**
 * @brief 주석 노드
 */
class CommentNode : public DomNode {
public:
    explicit CommentNode(const std::string& comment);
    ~CommentNode() override = default;

    [[nodiscard]] std::string nodeName() const override { return "#comment"; }
    [[nodiscard]] std::string nodeValue() const override { return comment_; }

    void setComment(const std::string& comment) { comment_ = comment; }
    [[nodiscard]] const std::string& comment() const { return comment_; }

    [[nodiscard]] std::string serialize(int indent = 0) const override;

private:
    std::string comment_;
};

/**
 * @brief 문서 노드 (DOM 트리 루트)
 */
class DocumentNode : public DomNode {
public:
    DocumentNode();
    ~DocumentNode() override = default;

    [[nodiscard]] std::string nodeName() const override { return "#document"; }

    // 문서 정보
    void setDoctype(const std::string& doctype) { doctype_ = doctype; }
    [[nodiscard]] const std::string& doctype() const { return doctype_; }

    void setTitle(const std::string& title) { title_ = title; }
    [[nodiscard]] const std::string& title() const { return title_; }

    // document.documentElement (<html>)
    [[nodiscard]] ElementPtr documentElement() const;

    // document.body
    [[nodiscard]] ElementPtr body() const;

    // document.head
    [[nodiscard]] ElementPtr head() const;

    // 전역 쿼리
    [[nodiscard]] ElementPtr querySelector(const std::string& selector) const;
    [[nodiscard]] std::vector<ElementPtr> querySelectorAll(const std::string& selector) const;
    [[nodiscard]] ElementPtr getElementById(const std::string& id) const;

    // 전체 문서 직렬화
    [[nodiscard]] std::string serialize(int indent = 0) const override;

private:
    std::string doctype_;
    std::string title_;
};

} // namespace ordinal::rendering
