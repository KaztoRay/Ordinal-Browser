/**
 * @file layout_engine.cpp
 * @brief 레이아웃 엔진 구현
 * 
 * 박스 모델 계산, 블록/인라인 레이아웃, 너비/높이 해석,
 * 위치 지정, 마진 겹침 등을 구현합니다.
 */

#include "layout_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace ordinal::rendering {

// ============================================================
// LengthValue 구현
// ============================================================

float LengthValue::toPx(float parent_size, float font_size, float root_font_size,
                         float viewport_width, float viewport_height) const {
    switch (unit) {
        case Unit::Px:
            return value;
        case Unit::Em:
            return value * font_size;
        case Unit::Rem:
            return value * root_font_size;
        case Unit::Percent:
            return value / 100.0f * parent_size;
        case Unit::Vw:
            return value / 100.0f * viewport_width;
        case Unit::Vh:
            return value / 100.0f * viewport_height;
        case Unit::Auto:
        case Unit::None:
            return 0.0f;
    }
    return 0.0f;
}

// ============================================================
// LayoutEngine 생성자
// ============================================================

LayoutEngine::LayoutEngine() = default;

// ============================================================
// 공개 메서드
// ============================================================

LayoutBoxPtr LayoutEngine::layout(const DocumentPtr& document,
                                   const std::vector<CssRule>& rules) {
    if (!document) return nullptr;

    // DOM 트리에서 레이아웃 트리 생성
    auto root = buildLayoutTree(document, rules);
    if (!root) return nullptr;

    // 루트 박스의 너비를 뷰포트 너비로 설정
    root->content_rect.width = viewport_.width;
    root->content_rect.x = 0;
    root->content_rect.y = 0;

    // 레이아웃 계산
    layoutBlock(root, viewport_.width);

    return root;
}

LayoutBoxPtr LayoutEngine::hitTest(const LayoutBoxPtr& root, float x, float y) const {
    if (!root) return nullptr;

    // 역순으로 자식 검사 (위에 그려진 것이 먼저)
    for (auto it = root->children.rbegin(); it != root->children.rend(); ++it) {
        auto result = hitTest(*it, x, y);
        if (result) return result;
    }

    // 현재 박스가 좌표를 포함하는지 확인
    if (root->border_rect.contains(x, y)) {
        return root;
    }

    return nullptr;
}

LengthValue LayoutEngine::parseLength(const std::string& value) {
    LengthValue len;

    if (value.empty()) {
        len.unit = LengthValue::Unit::None;
        return len;
    }

    // "auto" 처리
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "auto") {
        len.unit = LengthValue::Unit::Auto;
        return len;
    }

    if (lower == "0" || lower == "none") {
        len.value = 0.0f;
        len.unit = LengthValue::Unit::Px;
        return len;
    }

    // 숫자 부분 추출
    std::string num_part;
    std::string unit_part;
    size_t i = 0;

    // 음수 부호
    if (i < lower.size() && (lower[i] == '-' || lower[i] == '+')) {
        num_part += lower[i++];
    }

    // 숫자와 소수점
    while (i < lower.size() && (std::isdigit(static_cast<unsigned char>(lower[i])) || lower[i] == '.')) {
        num_part += lower[i++];
    }

    // 단위 부분
    unit_part = lower.substr(i);

    // 숫자 파싱
    if (!num_part.empty()) {
        try {
            len.value = std::stof(num_part);
        } catch (...) {
            len.value = 0.0f;
        }
    }

    // 단위 파싱
    if (unit_part.empty() || unit_part == "px") {
        len.unit = LengthValue::Unit::Px;
    } else if (unit_part == "em") {
        len.unit = LengthValue::Unit::Em;
    } else if (unit_part == "rem") {
        len.unit = LengthValue::Unit::Rem;
    } else if (unit_part == "%") {
        len.unit = LengthValue::Unit::Percent;
    } else if (unit_part == "vw") {
        len.unit = LengthValue::Unit::Vw;
    } else if (unit_part == "vh") {
        len.unit = LengthValue::Unit::Vh;
    } else {
        // 알 수 없는 단위 — px로 처리
        len.unit = LengthValue::Unit::Px;
    }

    return len;
}

DisplayType LayoutEngine::parseDisplay(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "block") return DisplayType::Block;
    if (lower == "inline") return DisplayType::Inline;
    if (lower == "inline-block") return DisplayType::InlineBlock;
    if (lower == "flex") return DisplayType::Flex;
    if (lower == "grid") return DisplayType::Grid;
    if (lower == "none") return DisplayType::None;
    if (lower == "table") return DisplayType::Table;
    if (lower == "table-row") return DisplayType::TableRow;
    if (lower == "table-cell") return DisplayType::TableCell;
    if (lower == "list-item") return DisplayType::ListItem;

    return DisplayType::Block; // 기본값
}

PositionMode LayoutEngine::parsePosition(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "static") return PositionMode::Static;
    if (lower == "relative") return PositionMode::Relative;
    if (lower == "absolute") return PositionMode::Absolute;
    if (lower == "fixed") return PositionMode::Fixed;
    if (lower == "sticky") return PositionMode::Sticky;

    return PositionMode::Static;
}

// ============================================================
// 내부 구현
// ============================================================

LayoutBoxPtr LayoutEngine::buildLayoutTree(const DomNodePtr& node,
                                            const std::vector<CssRule>& rules) {
    if (!node) return nullptr;

    auto box = std::make_shared<LayoutBox>();
    box->node = node;

    if (node->nodeType() == NodeType::Element) {
        auto element = std::dynamic_pointer_cast<ElementNode>(node);
        if (element) {
            // 스타일 계산
            box->style = computeStyle(element, rules);

            // display: none이면 레이아웃 제외
            if (box->style.display == DisplayType::None) {
                return nullptr;
            }
        }
    } else if (node->nodeType() == NodeType::Text) {
        // 텍스트 노드 — 인라인 처리
        box->style.display = DisplayType::Inline;
    } else if (node->nodeType() == NodeType::Document) {
        // 문서 노드 — 블록 처리
        box->style.display = DisplayType::Block;
    } else {
        // 주석 등 — 레이아웃 제외
        return nullptr;
    }

    // 자식 노드 재귀 처리
    for (const auto& child : node->children()) {
        auto child_box = buildLayoutTree(child, rules);
        if (child_box) {
            child_box->parent = box;
            box->children.push_back(std::move(child_box));
        }
    }

    return box;
}

ComputedStyle LayoutEngine::computeStyle(const ElementPtr& element,
                                          const std::vector<CssRule>& rules) {
    ComputedStyle style;

    // 기본 display 값 설정
    style.display = defaultDisplay(element->tagName());

    // 매칭되는 CSS 규칙 수집 (특이성 순으로 정렬)
    struct MatchedRule {
        Specificity specificity;
        const PropertyMap* properties;
    };

    std::vector<MatchedRule> matched;

    for (const auto& rule : rules) {
        if (selectorMatches(rule.selector, element)) {
            matched.push_back({rule.selector.specificity, &rule.properties});
        }
    }

    // 특이성 순 정렬 (낮은 것부터 적용 — 캐스케이드)
    std::sort(matched.begin(), matched.end(),
              [](const MatchedRule& a, const MatchedRule& b) {
                  return a.specificity < b.specificity;
              });

    // 속성 병합
    PropertyMap merged;
    for (const auto& match : matched) {
        for (const auto& [prop, value] : *match.properties) {
            // !important가 아닌 기존 값은 덮어쓰기
            auto it = merged.find(prop);
            if (it == merged.end() || !it->second.important || value.important) {
                merged[prop] = value;
            }
        }
    }

    // 인라인 스타일 추가 (최우선)
    auto inline_style = element->getAttribute("style");
    if (inline_style.has_value()) {
        auto inline_props = CssParser::parseInlineStyle(inline_style.value());
        for (auto& [prop, value] : inline_props) {
            merged[prop] = std::move(value);
        }
    }

    // 속성 해석
    ComputedStyle inherited; // 부모에서 상속 (간략화)
    style = resolveProperties(merged, inherited);

    // 기본 display 유지 (CSS에서 명시하지 않은 경우)
    if (merged.find("display") == merged.end()) {
        style.display = defaultDisplay(element->tagName());
    }

    return style;
}

ComputedStyle LayoutEngine::resolveProperties(const PropertyMap& props,
                                               const ComputedStyle& inherited) {
    ComputedStyle style = inherited;

    auto getVal = [&](const std::string& name) -> std::optional<std::string> {
        auto it = props.find(name);
        if (it != props.end()) return it->second.raw;
        return std::nullopt;
    };

    // display
    if (auto val = getVal("display")) {
        style.display = parseDisplay(*val);
    }

    // position
    if (auto val = getVal("position")) {
        style.position = parsePosition(*val);
    }

    // box-sizing
    if (auto val = getVal("box-sizing")) {
        if (*val == "border-box") style.box_sizing = BoxSizing::BorderBox;
        else style.box_sizing = BoxSizing::ContentBox;
    }

    // float
    if (auto val = getVal("float")) {
        if (*val == "left") style.float_type = FloatType::Left;
        else if (*val == "right") style.float_type = FloatType::Right;
        else style.float_type = FloatType::None;
    }

    // 크기
    if (auto val = getVal("width")) style.width = parseLength(*val);
    if (auto val = getVal("height")) style.height = parseLength(*val);
    if (auto val = getVal("min-width")) style.min_width = parseLength(*val);
    if (auto val = getVal("min-height")) style.min_height = parseLength(*val);
    if (auto val = getVal("max-width")) style.max_width = parseLength(*val);
    if (auto val = getVal("max-height")) style.max_height = parseLength(*val);

    // margin (단축/개별)
    auto parseEdge = [&](const std::string& base) -> EdgeValues {
        EdgeValues edge;
        if (auto val = getVal(base)) {
            // 단축 속성 파싱
            std::istringstream iss(*val);
            std::vector<std::string> parts;
            std::string part;
            while (iss >> part) parts.push_back(part);

            if (parts.size() == 1) {
                auto l = parseLength(parts[0]);
                float px = l.toPx();
                edge = {px, px, px, px};
            } else if (parts.size() == 2) {
                float v = parseLength(parts[0]).toPx();
                float h = parseLength(parts[1]).toPx();
                edge = {v, h, v, h};
            } else if (parts.size() == 3) {
                edge.top = parseLength(parts[0]).toPx();
                edge.right = parseLength(parts[1]).toPx();
                edge.bottom = parseLength(parts[2]).toPx();
                edge.left = parseLength(parts[1]).toPx();
            } else if (parts.size() >= 4) {
                edge.top = parseLength(parts[0]).toPx();
                edge.right = parseLength(parts[1]).toPx();
                edge.bottom = parseLength(parts[2]).toPx();
                edge.left = parseLength(parts[3]).toPx();
            }
        }

        // 개별 속성 오버라이드
        if (auto val = getVal(base + "-top")) edge.top = parseLength(*val).toPx();
        if (auto val = getVal(base + "-right")) edge.right = parseLength(*val).toPx();
        if (auto val = getVal(base + "-bottom")) edge.bottom = parseLength(*val).toPx();
        if (auto val = getVal(base + "-left")) edge.left = parseLength(*val).toPx();

        return edge;
    };

    style.margin = parseEdge("margin");
    style.padding = parseEdge("padding");
    style.border_width = parseEdge("border-width");

    // border 단축 속성에서 너비 추출
    if (auto val = getVal("border")) {
        std::istringstream iss(*val);
        std::string part;
        while (iss >> part) {
            // 숫자로 시작하면 너비
            if (!part.empty() && (std::isdigit(static_cast<unsigned char>(part[0])) || part[0] == '.')) {
                float w = parseLength(part).toPx();
                style.border_width = {w, w, w, w};
                break;
            }
        }
    }

    // 위치 오프셋
    if (auto val = getVal("top")) style.top = parseLength(*val);
    if (auto val = getVal("right")) style.right_offset = parseLength(*val);
    if (auto val = getVal("bottom")) style.bottom_offset = parseLength(*val);
    if (auto val = getVal("left")) style.left_offset = parseLength(*val);

    // 글꼴
    if (auto val = getVal("font-size")) {
        auto len = parseLength(*val);
        if (!len.isNone() && !len.isAuto()) {
            style.font_size = len.toPx(inherited.font_size, inherited.font_size, root_font_size_);
        }
    }
    if (auto val = getVal("line-height")) {
        try {
            style.line_height = std::stof(*val);
        } catch (...) {}
    }
    if (auto val = getVal("font-family")) style.font_family = *val;
    if (auto val = getVal("font-weight")) {
        if (*val == "bold") style.font_weight = 700;
        else if (*val == "normal") style.font_weight = 400;
        else {
            try { style.font_weight = std::stoi(*val); } catch (...) {}
        }
    }

    // z-index
    if (auto val = getVal("z-index")) {
        try { style.z_index = std::stoi(*val); } catch (...) {}
    }

    // visibility
    if (auto val = getVal("visibility")) {
        style.visible = (*val != "hidden" && *val != "collapse");
    }

    // opacity
    if (auto val = getVal("opacity")) {
        try { style.opacity = std::stof(*val); } catch (...) {}
    }

    // overflow
    auto parseOverflow = [](const std::string& v) -> OverflowMode {
        if (v == "hidden") return OverflowMode::Hidden;
        if (v == "scroll") return OverflowMode::Scroll;
        if (v == "auto") return OverflowMode::Auto;
        return OverflowMode::Visible;
    };
    if (auto val = getVal("overflow")) {
        style.overflow_x = parseOverflow(*val);
        style.overflow_y = parseOverflow(*val);
    }
    if (auto val = getVal("overflow-x")) style.overflow_x = parseOverflow(*val);
    if (auto val = getVal("overflow-y")) style.overflow_y = parseOverflow(*val);

    return style;
}

void LayoutEngine::layoutBlock(LayoutBoxPtr& box, float containing_width) {
    // 너비 해석
    resolveWidth(box, containing_width);

    // 자식 레이아웃
    if (!box->children.empty()) {
        // 블록 컨텍스트인지 인라인 컨텍스트인지 판별
        bool has_block_children = false;
        for (const auto& child : box->children) {
            if (child->style.display == DisplayType::Block ||
                child->style.display == DisplayType::Flex ||
                child->style.display == DisplayType::Grid ||
                child->style.display == DisplayType::ListItem ||
                child->style.display == DisplayType::Table) {
                has_block_children = true;
                break;
            }
        }

        if (has_block_children) {
            layoutBlockChildren(box);
        } else {
            layoutInlineChildren(box);
        }
    }

    // 높이 해석
    resolveHeight(box);

    // 박스 영역 계산
    calculateBoxRects(box);

    // 상대 위치 조정
    if (box->style.position == PositionMode::Relative) {
        float dx = box->style.left_offset.toPx(containing_width);
        float dy = box->style.top.toPx(0);
        box->content_rect.x += dx;
        box->content_rect.y += dy;
        calculateBoxRects(box);
    }
}

void LayoutEngine::layoutInline(LayoutBoxPtr& box, float containing_width) {
    // 인라인 요소는 콘텐츠 크기에 따라 결정
    if (box->node && box->node->nodeType() == NodeType::Text) {
        auto text_node = std::dynamic_pointer_cast<TextNode>(box->node);
        if (text_node) {
            // 텍스트 크기 근사 계산 (문자당 평균 0.6 * font_size 너비)
            float char_width = box->style.font_size * 0.6f;
            float text_width = static_cast<float>(text_node->text().length()) * char_width;

            // 줄바꿈 처리
            if (text_width > containing_width && containing_width > 0) {
                int chars_per_line = static_cast<int>(containing_width / char_width);
                if (chars_per_line < 1) chars_per_line = 1;
                int lines = (static_cast<int>(text_node->text().length()) + chars_per_line - 1) / chars_per_line;
                box->content_rect.width = containing_width;
                box->content_rect.height = static_cast<float>(lines) * box->style.font_size * box->style.line_height;
            } else {
                box->content_rect.width = text_width;
                box->content_rect.height = box->style.font_size * box->style.line_height;
            }
        }
    }

    // 자식 레이아웃 (인라인 블록 등)
    for (auto& child : box->children) {
        if (child->style.display == DisplayType::InlineBlock) {
            layoutBlock(child, containing_width);
        } else {
            layoutInline(child, containing_width);
        }
    }

    calculateBoxRects(box);
}

void LayoutEngine::layoutBlockChildren(LayoutBoxPtr& box) {
    float content_width = box->content_rect.width;
    float current_y = box->content_rect.y + box->style.padding.top;
    float prev_margin_bottom = 0.0f;

    for (auto& child : box->children) {
        if (child->style.display == DisplayType::None) continue;

        float child_containing_width = content_width - box->style.padding.horizontal();

        if (child->style.display == DisplayType::Block ||
            child->style.display == DisplayType::Flex ||
            child->style.display == DisplayType::Grid ||
            child->style.display == DisplayType::ListItem ||
            child->style.display == DisplayType::Table) {

            // 마진 겹침
            float effective_margin_top = collapseMargins(prev_margin_bottom, child->style.margin.top);
            current_y += effective_margin_top;

            child->content_rect.x = box->content_rect.x + box->style.padding.left + child->style.margin.left;
            child->content_rect.y = current_y;

            layoutBlock(child, child_containing_width);

            current_y = child->margin_rect.bottom();
            prev_margin_bottom = child->style.margin.bottom;
        } else {
            // 인라인 요소를 익명 라인 박스에 배치
            child->content_rect.x = box->content_rect.x + box->style.padding.left;
            child->content_rect.y = current_y;

            layoutInline(child, child_containing_width);

            current_y = child->margin_rect.bottom();
            prev_margin_bottom = 0;
        }
    }

    // 콘텐츠 높이 계산 (명시적 높이가 없는 경우)
    if (box->style.height.isAuto() || box->style.height.isNone()) {
        float content_bottom = current_y + box->style.padding.bottom;
        box->content_rect.height = content_bottom - box->content_rect.y;
    }
}

void LayoutEngine::layoutInlineChildren(LayoutBoxPtr& box) {
    float content_width = box->content_rect.width - box->style.padding.horizontal();
    float current_x = box->content_rect.x + box->style.padding.left;
    float current_y = box->content_rect.y + box->style.padding.top;
    float line_height = 0.0f;
    float max_y = current_y;

    for (auto& child : box->children) {
        if (child->style.display == DisplayType::None) continue;

        // 인라인 블록은 블록으로 레이아웃
        if (child->style.display == DisplayType::InlineBlock) {
            layoutBlock(child, content_width);
        } else {
            layoutInline(child, content_width);
        }

        float child_width = child->margin_rect.width;
        float child_height = child->margin_rect.height;

        // 줄바꿈 필요 여부
        if (current_x + child_width > box->content_rect.x + box->style.padding.left + content_width &&
            current_x != box->content_rect.x + box->style.padding.left) {
            // 다음 줄로
            current_x = box->content_rect.x + box->style.padding.left;
            current_y += line_height;
            line_height = 0.0f;
        }

        child->content_rect.x = current_x + child->style.margin.left;
        child->content_rect.y = current_y + child->style.margin.top;
        calculateBoxRects(child);

        current_x += child_width;
        line_height = std::max(line_height, child_height);
        max_y = std::max(max_y, current_y + line_height);
    }

    // 콘텐츠 높이 갱신
    if (box->style.height.isAuto() || box->style.height.isNone()) {
        box->content_rect.height = (max_y - box->content_rect.y) + box->style.padding.bottom;
    }
}

void LayoutEngine::resolveWidth(LayoutBoxPtr& box, float containing_width) {
    const auto& style = box->style;

    float available_width = containing_width - style.margin.horizontal();

    if (!style.width.isAuto() && !style.width.isNone()) {
        // 명시적 너비
        float w = style.width.toPx(containing_width, style.font_size, root_font_size_,
                                    viewport_.width, viewport_.height);

        // border-box 조정
        if (style.box_sizing == BoxSizing::BorderBox) {
            w -= style.padding.horizontal() + style.border_width.horizontal();
            w = std::max(w, 0.0f);
        }

        box->content_rect.width = w;
    } else {
        // auto — 사용 가능한 전체 너비 사용
        box->content_rect.width = available_width - style.padding.horizontal() -
                                   style.border_width.horizontal();
    }

    // min-width / max-width 적용
    if (!style.min_width.isNone() && !style.min_width.isAuto()) {
        float min_w = style.min_width.toPx(containing_width);
        box->content_rect.width = std::max(box->content_rect.width, min_w);
    }
    if (!style.max_width.isNone() && !style.max_width.isAuto()) {
        float max_w = style.max_width.toPx(containing_width);
        box->content_rect.width = std::min(box->content_rect.width, max_w);
    }
}

void LayoutEngine::resolveHeight(LayoutBoxPtr& box) {
    const auto& style = box->style;

    if (!style.height.isAuto() && !style.height.isNone()) {
        float h = style.height.toPx(0, style.font_size, root_font_size_,
                                     viewport_.width, viewport_.height);

        if (style.box_sizing == BoxSizing::BorderBox) {
            h -= style.padding.vertical() + style.border_width.vertical();
            h = std::max(h, 0.0f);
        }

        box->content_rect.height = h;
    }
    // auto인 경우 자식 레이아웃 후 이미 설정됨

    // min-height / max-height 적용
    if (!style.min_height.isNone() && !style.min_height.isAuto()) {
        float min_h = style.min_height.toPx();
        box->content_rect.height = std::max(box->content_rect.height, min_h);
    }
    if (!style.max_height.isNone() && !style.max_height.isAuto()) {
        float max_h = style.max_height.toPx();
        box->content_rect.height = std::min(box->content_rect.height, max_h);
    }
}

void LayoutEngine::calculateBoxRects(LayoutBoxPtr& box) {
    const auto& style = box->style;
    const auto& content = box->content_rect;

    // 패딩 영역
    box->padding_rect = {
        content.x - style.padding.left,
        content.y - style.padding.top,
        content.width + style.padding.horizontal(),
        content.height + style.padding.vertical()
    };

    // 보더 영역
    box->border_rect = {
        box->padding_rect.x - style.border_width.left,
        box->padding_rect.y - style.border_width.top,
        box->padding_rect.width + style.border_width.horizontal(),
        box->padding_rect.height + style.border_width.vertical()
    };

    // 마진 영역
    box->margin_rect = {
        box->border_rect.x - style.margin.left,
        box->border_rect.y - style.margin.top,
        box->border_rect.width + style.margin.horizontal(),
        box->border_rect.height + style.margin.vertical()
    };
}

float LayoutEngine::collapseMargins(float margin1, float margin2) const {
    // CSS 마진 겹침 규칙
    // 양수끼리: 큰 값
    // 음수끼리: 작은 값 (절댓값이 큰 것)
    // 혼합: 합산
    if (margin1 >= 0 && margin2 >= 0) {
        return std::max(margin1, margin2);
    }
    if (margin1 < 0 && margin2 < 0) {
        return std::min(margin1, margin2);
    }
    return margin1 + margin2;
}

DisplayType LayoutEngine::defaultDisplay(const std::string& tag_name) {
    // 블록 레벨 요소
    static const std::unordered_set<std::string> block_tags = {
        "div", "p", "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li", "table", "form", "header", "footer",
        "main", "section", "article", "aside", "nav", "figure",
        "figcaption", "blockquote", "pre", "hr", "address",
        "details", "summary", "fieldset", "dialog"
    };

    // 인라인 요소
    static const std::unordered_set<std::string> inline_tags = {
        "span", "a", "em", "strong", "b", "i", "u", "s",
        "code", "small", "big", "sub", "sup", "abbr", "cite",
        "q", "mark", "time", "var", "kbd", "samp", "label",
        "input", "button", "select", "textarea", "img", "br",
        "wbr"
    };

    if (block_tags.contains(tag_name)) return DisplayType::Block;
    if (inline_tags.contains(tag_name)) return DisplayType::Inline;

    // li 요소
    if (tag_name == "li") return DisplayType::ListItem;

    // table 관련
    if (tag_name == "tr") return DisplayType::TableRow;
    if (tag_name == "td" || tag_name == "th") return DisplayType::TableCell;

    return DisplayType::Block; // 기본값
}

bool LayoutEngine::selectorMatches(const CssSelector& selector, const ElementPtr& element) const {
    if (!element || selector.parts.empty()) return false;

    // 간단한 매칭: 마지막 단순 셀렉터만 확인
    // (조합자를 포함한 전체 매칭은 복잡하므로 기본 구현)
    for (const auto& part : selector.parts) {
        switch (part.type) {
            case SelectorType::Type:
                if (element->tagName() != part.value) return false;
                break;

            case SelectorType::Id:
                if (element->id() != part.value) return false;
                break;

            case SelectorType::Class: {
                auto classes = element->classList();
                if (std::find(classes.begin(), classes.end(), part.value) == classes.end()) {
                    return false;
                }
                break;
            }

            case SelectorType::Attribute: {
                auto attr = element->getAttribute(part.attr_name);
                if (!attr.has_value()) return false;

                switch (part.attr_op) {
                    case AttributeOp::Exists:
                        break; // 존재만 확인
                    case AttributeOp::Equals:
                        if (attr.value() != part.attr_value) return false;
                        break;
                    case AttributeOp::Contains:
                        if (attr.value().find(part.attr_value) == std::string::npos) return false;
                        break;
                    case AttributeOp::StartsWith:
                        if (attr.value().substr(0, part.attr_value.size()) != part.attr_value) return false;
                        break;
                    case AttributeOp::EndsWith:
                        if (attr.value().size() < part.attr_value.size() ||
                            attr.value().substr(attr.value().size() - part.attr_value.size()) != part.attr_value) return false;
                        break;
                    case AttributeOp::DashMatch:
                        if (attr.value() != part.attr_value &&
                            attr.value().substr(0, part.attr_value.size() + 1) != part.attr_value + "-") return false;
                        break;
                    case AttributeOp::WordMatch: {
                        std::istringstream iss(attr.value());
                        std::string word;
                        bool found = false;
                        while (iss >> word) {
                            if (word == part.attr_value) { found = true; break; }
                        }
                        if (!found) return false;
                        break;
                    }
                }
                break;
            }

            case SelectorType::Universal:
                // 항상 매칭
                break;

            case SelectorType::PseudoClass:
            case SelectorType::PseudoElement:
                // 의사 클래스/요소 — 런타임 상태 없이 기본 매칭 건너뛰기
                break;

            case SelectorType::Combinator:
                // 조합자 — 현재 구현에서는 하위 조합자만 근사적으로 처리
                break;
        }
    }

    return true;
}

} // namespace ordinal::rendering
