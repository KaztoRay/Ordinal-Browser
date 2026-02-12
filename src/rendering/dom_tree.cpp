/**
 * @file dom_tree.cpp
 * @brief DOM 트리 구현
 * 
 * 노드 계층 관리, 트리 순회, 쿼리 셀렉터, 직렬화를 구현합니다.
 */

#include "dom_tree.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <cctype>
#include <stdexcept>

namespace ordinal::rendering {

// ============================================================
// DomNode 기본 클래스 구현
// ============================================================

DomNode::DomNode(NodeType type) : type_(type) {}

void DomNode::appendChild(DomNodePtr child) {
    if (!child) return;
    
    // 기존 부모에서 제거
    if (auto old_parent = child->parent()) {
        old_parent->removeChild(child);
    }
    
    // 새 부모 설정 및 자식 추가
    child->parent_ = shared_from_this();
    children_.push_back(std::move(child));
}

void DomNode::insertChild(size_t index, DomNodePtr child) {
    if (!child) return;
    
    // 인덱스 범위 확인 — 끝에 추가 허용
    if (index > children_.size()) {
        index = children_.size();
    }
    
    // 기존 부모에서 제거
    if (auto old_parent = child->parent()) {
        old_parent->removeChild(child);
    }
    
    child->parent_ = shared_from_this();
    children_.insert(children_.begin() + static_cast<ptrdiff_t>(index), std::move(child));
}

void DomNode::removeChild(size_t index) {
    if (index >= children_.size()) return;
    
    children_[index]->parent_.reset();
    children_.erase(children_.begin() + static_cast<ptrdiff_t>(index));
}

void DomNode::removeChild(const DomNodePtr& child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        (*it)->parent_.reset();
        children_.erase(it);
    }
}

void DomNode::replaceChild(size_t index, DomNodePtr new_child) {
    if (index >= children_.size() || !new_child) return;
    
    // 기존 자식의 부모 해제
    children_[index]->parent_.reset();
    
    // 새 자식의 기존 부모 제거
    if (auto old_parent = new_child->parent()) {
        old_parent->removeChild(new_child);
    }
    
    new_child->parent_ = shared_from_this();
    children_[index] = std::move(new_child);
}

void DomNode::clearChildren() {
    for (auto& child : children_) {
        child->parent_.reset();
    }
    children_.clear();
}

DomNodePtr DomNode::previousSibling() const {
    auto par = parent();
    if (!par) return nullptr;
    
    const auto& siblings = par->children();
    for (size_t i = 1; i < siblings.size(); ++i) {
        if (siblings[i].get() == this) {
            return siblings[i - 1];
        }
    }
    return nullptr;
}

DomNodePtr DomNode::nextSibling() const {
    auto par = parent();
    if (!par) return nullptr;
    
    const auto& siblings = par->children();
    for (size_t i = 0; i + 1 < siblings.size(); ++i) {
        if (siblings[i].get() == this) {
            return siblings[i + 1];
        }
    }
    return nullptr;
}

DomNodePtr DomNode::firstChild() const {
    return children_.empty() ? nullptr : children_.front();
}

DomNodePtr DomNode::lastChild() const {
    return children_.empty() ? nullptr : children_.back();
}

void DomNode::walkDepthFirst(const std::function<bool(DomNodePtr)>& visitor) {
    // 깊이 우선 순회 (전위)
    if (!visitor(shared_from_this())) return;
    
    for (auto& child : children_) {
        child->walkDepthFirst(visitor);
    }
}

void DomNode::walkBreadthFirst(const std::function<bool(DomNodePtr)>& visitor) {
    // 너비 우선 순회
    std::queue<DomNodePtr> q;
    q.push(shared_from_this());
    
    while (!q.empty()) {
        auto node = q.front();
        q.pop();
        
        if (!visitor(node)) return;
        
        for (auto& child : node->children()) {
            q.push(child);
        }
    }
}

std::string DomNode::textContent() const {
    // 모든 자식 텍스트를 재귀적으로 수집
    std::string result;
    for (const auto& child : children_) {
        result += child->textContent();
    }
    return result;
}

// ============================================================
// ElementNode 구현
// ============================================================

ElementNode::ElementNode(const std::string& tag_name)
    : DomNode(NodeType::Element), tag_name_(tag_name) {
    // 태그 이름을 소문자로 정규화
    std::transform(tag_name_.begin(), tag_name_.end(), tag_name_.begin(),
                   [](unsigned char c) { return std::tolower(c); });
}

void ElementNode::setAttribute(const std::string& name, const std::string& value) {
    // 속성 이름을 소문자로 정규화
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    attributes_[lower_name] = value;
}

std::optional<std::string> ElementNode::getAttribute(const std::string& name) const {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    auto it = attributes_.find(lower_name);
    if (it != attributes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool ElementNode::hasAttribute(const std::string& name) const {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return attributes_.contains(lower_name);
}

void ElementNode::removeAttribute(const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    attributes_.erase(lower_name);
}

std::string ElementNode::id() const {
    auto val = getAttribute("id");
    return val.value_or("");
}

std::string ElementNode::className() const {
    auto val = getAttribute("class");
    return val.value_or("");
}

std::vector<std::string> ElementNode::classList() const {
    std::vector<std::string> classes;
    std::string cls = className();
    std::istringstream iss(cls);
    std::string token;
    while (iss >> token) {
        classes.push_back(token);
    }
    return classes;
}

bool ElementNode::matchesSelector(const std::string& selector) const {
    if (selector.empty()) return false;
    
    // ID 셀렉터: #id
    if (selector[0] == '#') {
        return id() == selector.substr(1);
    }
    
    // 클래스 셀렉터: .class
    if (selector[0] == '.') {
        std::string target_class = selector.substr(1);
        auto classes = classList();
        return std::find(classes.begin(), classes.end(), target_class) != classes.end();
    }
    
    // 속성 셀렉터: [attr] 또는 [attr=value]
    if (selector[0] == '[' && selector.back() == ']') {
        std::string inner = selector.substr(1, selector.size() - 2);
        auto eq_pos = inner.find('=');
        if (eq_pos == std::string::npos) {
            // [attr] — 속성 존재 여부
            return hasAttribute(inner);
        } else {
            // [attr=value] — 속성 값 일치
            std::string attr_name = inner.substr(0, eq_pos);
            std::string attr_value = inner.substr(eq_pos + 1);
            // 따옴표 제거
            if (attr_value.size() >= 2 &&
                ((attr_value.front() == '"' && attr_value.back() == '"') ||
                 (attr_value.front() == '\'' && attr_value.back() == '\''))) {
                attr_value = attr_value.substr(1, attr_value.size() - 2);
            }
            auto val = getAttribute(attr_name);
            return val.has_value() && val.value() == attr_value;
        }
    }
    
    // 타입 셀렉터: tag name
    std::string lower_sel = selector;
    std::transform(lower_sel.begin(), lower_sel.end(), lower_sel.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return tag_name_ == lower_sel;
}

ElementPtr ElementNode::querySelector(const std::string& selector) const {
    // 단순 셀렉터만 지원 (조합 셀렉터 미지원)
    ElementPtr result;
    
    // const_cast로 shared_from_this 사용을 위한 우회
    // 대신 자식 노드를 직접 순회
    std::function<bool(const DomNodePtr&)> search = [&](const DomNodePtr& node) -> bool {
        if (node->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(node);
            if (elem && elem.get() != this && elem->matchesSelector(selector)) {
                result = elem;
                return false; // 첫 번째 매칭에서 중단
            }
        }
        for (const auto& child : node->children()) {
            if (!search(child)) return false;
        }
        return true;
    };
    
    for (const auto& child : children_) {
        if (!search(child)) break;
    }
    
    return result;
}

std::vector<ElementPtr> ElementNode::querySelectorAll(const std::string& selector) const {
    std::vector<ElementPtr> results;
    
    std::function<void(const DomNodePtr&)> search = [&](const DomNodePtr& node) {
        if (node->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(node);
            if (elem && elem.get() != this && elem->matchesSelector(selector)) {
                results.push_back(elem);
            }
        }
        for (const auto& child : node->children()) {
            search(child);
        }
    };
    
    for (const auto& child : children_) {
        search(child);
    }
    
    return results;
}

std::vector<ElementPtr> ElementNode::getElementsByTagName(const std::string& tag) const {
    std::string lower_tag = tag;
    std::transform(lower_tag.begin(), lower_tag.end(), lower_tag.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    std::vector<ElementPtr> results;
    
    std::function<void(const DomNodePtr&)> search = [&](const DomNodePtr& node) {
        if (node->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(node);
            if (elem && (lower_tag == "*" || elem->tagName() == lower_tag)) {
                results.push_back(elem);
            }
        }
        for (const auto& child : node->children()) {
            search(child);
        }
    };
    
    for (const auto& child : children_) {
        search(child);
    }
    
    return results;
}

std::vector<ElementPtr> ElementNode::getElementsByClassName(const std::string& cls) const {
    std::vector<ElementPtr> results;
    
    std::function<void(const DomNodePtr&)> search = [&](const DomNodePtr& node) {
        if (node->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(node);
            if (elem) {
                auto classes = elem->classList();
                if (std::find(classes.begin(), classes.end(), cls) != classes.end()) {
                    results.push_back(elem);
                }
            }
        }
        for (const auto& child : node->children()) {
            search(child);
        }
    };
    
    for (const auto& child : children_) {
        search(child);
    }
    
    return results;
}

ElementPtr ElementNode::getElementById(const std::string& target_id) const {
    ElementPtr result;
    
    std::function<bool(const DomNodePtr&)> search = [&](const DomNodePtr& node) -> bool {
        if (node->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(node);
            if (elem && elem->id() == target_id) {
                result = elem;
                return false;
            }
        }
        for (const auto& child : node->children()) {
            if (!search(child)) return false;
        }
        return true;
    };
    
    for (const auto& child : children_) {
        if (!search(child)) break;
    }
    
    return result;
}

std::string ElementNode::innerHTML() const {
    std::string result;
    for (const auto& child : children_) {
        result += child->serialize(0);
    }
    return result;
}

std::string ElementNode::outerHTML() const {
    return serialize(0);
}

bool ElementNode::isVoidElement() const {
    // HTML5 void 요소 목록
    static const std::unordered_set<std::string> void_elements = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    return void_elements.contains(tag_name_);
}

std::string ElementNode::serialize(int indent) const {
    std::string result;
    std::string indent_str(indent * 2, ' ');
    
    // 여는 태그
    result += indent_str + "<" + tag_name_;
    
    // 속성 출력
    for (const auto& [name, value] : attributes_) {
        result += " " + name + "=\"" + value + "\"";
    }
    
    // void 요소는 자동 닫힘
    if (isVoidElement()) {
        result += " />\n";
        return result;
    }
    
    result += ">";
    
    // 자식이 하나이고 텍스트 노드인 경우 인라인으로 출력
    if (children_.size() == 1 && children_[0]->nodeType() == NodeType::Text) {
        result += children_[0]->textContent();
        result += "</" + tag_name_ + ">\n";
        return result;
    }
    
    // 자식 노드가 있으면 줄바꿈 후 들여쓰기
    if (!children_.empty()) {
        result += "\n";
        for (const auto& child : children_) {
            result += child->serialize(indent + 1);
        }
        result += indent_str;
    }
    
    // 닫는 태그
    result += "</" + tag_name_ + ">\n";
    return result;
}

std::string ElementNode::textContent() const {
    std::string result;
    for (const auto& child : children_) {
        result += child->textContent();
    }
    return result;
}

// ============================================================
// TextNode 구현
// ============================================================

TextNode::TextNode(const std::string& text)
    : DomNode(NodeType::Text), text_(text) {}

std::string TextNode::serialize(int indent) const {
    // 텍스트 노드는 공백만 있는 경우 무시
    bool all_whitespace = std::all_of(text_.begin(), text_.end(),
                                       [](unsigned char c) { return std::isspace(c); });
    if (all_whitespace && text_.size() > 1) return "";
    
    std::string indent_str(indent * 2, ' ');
    return indent_str + text_ + "\n";
}

// ============================================================
// CommentNode 구현
// ============================================================

CommentNode::CommentNode(const std::string& comment)
    : DomNode(NodeType::Comment), comment_(comment) {}

std::string CommentNode::serialize(int indent) const {
    std::string indent_str(indent * 2, ' ');
    return indent_str + "<!--" + comment_ + "-->\n";
}

// ============================================================
// DocumentNode 구현
// ============================================================

DocumentNode::DocumentNode() : DomNode(NodeType::Document) {}

ElementPtr DocumentNode::documentElement() const {
    // <html> 요소 찾기
    for (const auto& child : children_) {
        if (child->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(child);
            if (elem && elem->tagName() == "html") {
                return elem;
            }
        }
    }
    return nullptr;
}

ElementPtr DocumentNode::body() const {
    auto html = documentElement();
    if (!html) return nullptr;
    
    for (const auto& child : html->children()) {
        if (child->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(child);
            if (elem && elem->tagName() == "body") {
                return elem;
            }
        }
    }
    return nullptr;
}

ElementPtr DocumentNode::head() const {
    auto html = documentElement();
    if (!html) return nullptr;
    
    for (const auto& child : html->children()) {
        if (child->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(child);
            if (elem && elem->tagName() == "head") {
                return elem;
            }
        }
    }
    return nullptr;
}

ElementPtr DocumentNode::querySelector(const std::string& selector) const {
    // 문서 전체에서 검색
    for (const auto& child : children_) {
        if (child->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(child);
            if (elem) {
                if (elem->matchesSelector(selector)) return elem;
                auto found = elem->querySelector(selector);
                if (found) return found;
            }
        }
    }
    return nullptr;
}

std::vector<ElementPtr> DocumentNode::querySelectorAll(const std::string& selector) const {
    std::vector<ElementPtr> results;
    
    for (const auto& child : children_) {
        if (child->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(child);
            if (elem) {
                if (elem->matchesSelector(selector)) {
                    results.push_back(elem);
                }
                auto sub_results = elem->querySelectorAll(selector);
                results.insert(results.end(), sub_results.begin(), sub_results.end());
            }
        }
    }
    
    return results;
}

ElementPtr DocumentNode::getElementById(const std::string& target_id) const {
    for (const auto& child : children_) {
        if (child->nodeType() == NodeType::Element) {
            auto elem = std::dynamic_pointer_cast<ElementNode>(child);
            if (elem) {
                if (elem->id() == target_id) return elem;
                auto found = elem->getElementById(target_id);
                if (found) return found;
            }
        }
    }
    return nullptr;
}

std::string DocumentNode::serialize(int indent) const {
    std::string result;
    
    // DOCTYPE 출력
    if (!doctype_.empty()) {
        result += "<!DOCTYPE " + doctype_ + ">\n";
    }
    
    // 자식 노드 직렬화
    for (const auto& child : children_) {
        result += child->serialize(indent);
    }
    
    return result;
}

} // namespace ordinal::rendering
