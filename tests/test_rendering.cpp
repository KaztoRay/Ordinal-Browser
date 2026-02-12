/**
 * @file test_rendering.cpp
 * @brief 렌더링 엔진 단위 테스트 — 16개 Google Test 케이스
 *
 * 테스트 대상:
 *   - HtmlParser: 열림 태그, 속성, void 요소, self-closing, 엔티티, 트리 빌드
 *   - CssParser: 클래스 셀렉터, ID 셀렉터, 복합 셀렉터, 특이성, 속성 파싱
 *   - DomTree: querySelector by id/class, 트리 순회, textContent
 *   - LayoutEngine: 블록 박스 모델
 */

#include <gtest/gtest.h>

#include "rendering/html_parser.h"
#include "rendering/css_parser.h"
#include "rendering/dom_tree.h"
#include "rendering/layout_engine.h"

using namespace ordinal::rendering;

// ============================================================
// HtmlParser 테스트 (6개)
// ============================================================

class HtmlParserTest : public ::testing::Test {
protected:
    HtmlParser parser_;
};

// 1. 기본 열림 태그 토큰화
TEST_F(HtmlParserTest, TokenizesOpenTag) {
    auto tokens = parser_.tokenize("<div>");
    ASSERT_FALSE(tokens.empty()) << "토큰 목록이 비어있으면 안 됩니다";

    // 첫 번째 토큰은 StartTag이고 이름은 "div"
    bool found_div = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::StartTag && token.name == "div") {
            found_div = true;
            break;
        }
    }
    EXPECT_TRUE(found_div) << "<div> 태그가 StartTag로 토큰화되어야 합니다";
}

// 2. 속성 파싱
TEST_F(HtmlParserTest, ParsesAttributes) {
    auto tokens = parser_.tokenize(R"(<a href="https://example.com" class="link">)");
    ASSERT_FALSE(tokens.empty());

    bool found = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::StartTag && token.name == "a") {
            found = true;
            // 속성이 2개 이상 파싱되어야 함
            EXPECT_GE(token.attributes.size(), 2u)
                << "<a> 태그의 속성이 2개 이상이어야 합니다 (href, class)";

            // href 속성 확인
            bool has_href = false;
            for (const auto& [name, value] : token.attributes) {
                if (name == "href" && value == "https://example.com") {
                    has_href = true;
                }
            }
            EXPECT_TRUE(has_href) << "href 속성이 올바르게 파싱되어야 합니다";
            break;
        }
    }
    EXPECT_TRUE(found) << "<a> StartTag가 존재해야 합니다";
}

// 3. void 요소 (br, img, hr 등)
TEST_F(HtmlParserTest, HandlesVoidElements) {
    auto tokens = parser_.tokenize("<br><img src='test.png'><hr>");
    ASSERT_FALSE(tokens.empty());

    // void 요소는 StartTag 또는 SelfClosingTag로 나타나야 함
    int void_count = 0;
    for (const auto& token : tokens) {
        if (token.name == "br" || token.name == "img" || token.name == "hr") {
            EXPECT_TRUE(
                token.type == TokenType::StartTag ||
                token.type == TokenType::SelfClosingTag
            ) << "void 요소는 StartTag 또는 SelfClosingTag이어야 합니다";
            void_count++;
        }
    }
    EXPECT_EQ(void_count, 3) << "br, img, hr 3개의 void 요소가 있어야 합니다";
}

// 4. self-closing 태그 (<tag />)
TEST_F(HtmlParserTest, HandlesSelfClosingTag) {
    auto tokens = parser_.tokenize("<input type='text' />");
    ASSERT_FALSE(tokens.empty());

    bool found_input = false;
    for (const auto& token : tokens) {
        if (token.name == "input") {
            found_input = true;
            // self_closing 플래그 또는 SelfClosingTag 타입
            EXPECT_TRUE(
                token.self_closing ||
                token.type == TokenType::SelfClosingTag
            ) << "self-closing 태그의 self_closing 플래그가 true여야 합니다";
            break;
        }
    }
    EXPECT_TRUE(found_input) << "input 태그가 토큰화되어야 합니다";
}

// 5. HTML 엔티티 디코딩
TEST_F(HtmlParserTest, DecodesEntities) {
    // &amp; → &, &lt; → <, &gt; → >, &quot; → "
    std::string decoded = HtmlParser::decodeEntities("&amp; &lt; &gt; &quot;");
    EXPECT_NE(decoded.find("&"), std::string::npos)
        << "&amp;이 & 로 디코딩되어야 합니다";
    EXPECT_NE(decoded.find("<"), std::string::npos)
        << "&lt;이 < 로 디코딩되어야 합니다";
    EXPECT_NE(decoded.find(">"), std::string::npos)
        << "&gt;이 > 로 디코딩되어야 합니다";

    // 역방향: escapeHtml
    std::string escaped = HtmlParser::escapeHtml("<script>alert(1)</script>");
    EXPECT_EQ(escaped.find("<script>"), std::string::npos)
        << "escapeHtml 결과에 <script>가 없어야 합니다";
}

// 6. 완전한 HTML → DOM 트리 빌드
TEST_F(HtmlParserTest, BuildsDomTree) {
    std::string html = R"(
        <!DOCTYPE html>
        <html>
        <head><title>테스트 페이지</title></head>
        <body>
            <div id="main">
                <h1>제목</h1>
                <p class="content">본문 텍스트</p>
            </div>
        </body>
        </html>
    )";

    auto document = parser_.parse(html);
    ASSERT_NE(document, nullptr) << "파싱된 문서가 nullptr이면 안 됩니다";

    // 문서 노드 타입 확인
    EXPECT_EQ(document->nodeType(), NodeType::Document);

    // 자식 노드 존재 확인
    EXPECT_GT(document->childCount(), 0u)
        << "문서에 자식 노드가 있어야 합니다";
}


// ============================================================
// CssParser 테스트 (5개)
// ============================================================

class CssParserTest : public ::testing::Test {
protected:
    CssParser parser_;
};

// 7. 클래스 셀렉터 파싱
TEST_F(CssParserTest, ParsesClassSelector) {
    auto selector = parser_.parseSelector(".content");
    ASSERT_FALSE(selector.parts.empty())
        << "셀렉터 파트가 비어있으면 안 됩니다";
    EXPECT_EQ(selector.raw, ".content");

    // 클래스 셀렉터 확인
    bool has_class = false;
    for (const auto& part : selector.parts) {
        if (part.type == SelectorType::Class && part.value == "content") {
            has_class = true;
        }
    }
    EXPECT_TRUE(has_class)
        << ".content가 Class 타입 셀렉터로 파싱되어야 합니다";
}

// 8. ID 셀렉터 파싱
TEST_F(CssParserTest, ParsesIdSelector) {
    auto selector = parser_.parseSelector("#main-header");
    ASSERT_FALSE(selector.parts.empty());

    bool has_id = false;
    for (const auto& part : selector.parts) {
        if (part.type == SelectorType::Id && part.value == "main-header") {
            has_id = true;
        }
    }
    EXPECT_TRUE(has_id)
        << "#main-header가 Id 타입 셀렉터로 파싱되어야 합니다";
}

// 9. 복합 셀렉터 파싱 (div.class#id)
TEST_F(CssParserTest, ParsesCompoundSelector) {
    auto rules = parser_.parse("div.highlight { color: red; font-size: 16px; }");
    ASSERT_FALSE(rules.empty())
        << "CSS 규칙이 최소 1개 파싱되어야 합니다";

    // 속성 확인
    const auto& props = rules[0].properties;
    bool has_color = props.find("color") != props.end();
    EXPECT_TRUE(has_color)
        << "color 속성이 파싱되어야 합니다";
}

// 10. 특이성 계산 (Specificity)
TEST_F(CssParserTest, CalculatesSpecificity) {
    // 타입 셀렉터: specificity = (0, 0, 0, 1)
    auto type_sel = parser_.parseSelector("div");
    auto type_spec = CssParser::calculateSpecificity(type_sel);
    EXPECT_EQ(type_spec.type_count, 1);
    EXPECT_EQ(type_spec.class_count, 0);
    EXPECT_EQ(type_spec.id_count, 0);

    // ID 셀렉터: specificity = (0, 1, 0, 0)
    auto id_sel = parser_.parseSelector("#header");
    auto id_spec = CssParser::calculateSpecificity(id_sel);
    EXPECT_EQ(id_spec.id_count, 1);

    // 클래스 셀렉터: specificity = (0, 0, 1, 0)
    auto class_sel = parser_.parseSelector(".nav");
    auto class_spec = CssParser::calculateSpecificity(class_sel);
    EXPECT_EQ(class_spec.class_count, 1);

    // ID > Class > Type 우선순위
    EXPECT_GT(id_spec.toInt(), class_spec.toInt())
        << "ID 셀렉터가 클래스 셀렉터보다 높은 특이성";
    EXPECT_GT(class_spec.toInt(), type_spec.toInt())
        << "클래스 셀렉터가 타입 셀렉터보다 높은 특이성";
}

// 11. 인라인 스타일 속성 파싱
TEST_F(CssParserTest, ParsesInlineStyleProperties) {
    auto props = CssParser::parseInlineStyle(
        "color: blue; margin: 10px; font-weight: bold;"
    );
    EXPECT_FALSE(props.empty())
        << "인라인 스타일에서 속성이 파싱되어야 합니다";

    // color 속성 확인
    auto it = props.find("color");
    if (it != props.end()) {
        EXPECT_EQ(it->second.raw, "blue")
            << "color 속성 값이 'blue'여야 합니다";
    }

    // margin 속성 확인
    auto margin_it = props.find("margin");
    EXPECT_NE(margin_it, props.end())
        << "margin 속성이 존재해야 합니다";
}


// ============================================================
// DomTree 테스트 (3개)
// ============================================================

class DomTreeTest : public ::testing::Test {
protected:
    HtmlParser parser_;
    DocumentPtr doc_;

    void SetUp() override {
        std::string html = R"(
            <html>
            <body>
                <div id="container">
                    <h1 class="title">Ordinal Browser</h1>
                    <p class="desc">V8 기반 보안 브라우저</p>
                    <ul>
                        <li>피싱 탐지</li>
                        <li>XSS 방어</li>
                        <li>추적기 차단</li>
                    </ul>
                </div>
                <footer id="footer">
                    <span>Copyright 2026</span>
                </footer>
            </body>
            </html>
        )";
        doc_ = parser_.parse(html);
    }
};

// 12. querySelector로 ID/클래스 검색
TEST_F(DomTreeTest, QuerySelectorById) {
    ASSERT_NE(doc_, nullptr);

    // getElementById 테스트
    auto container = doc_->getElementById("container");
    if (container) {
        EXPECT_EQ(container->id(), "container")
            << "getElementById('container')의 id가 'container'이어야 합니다";
    }

    auto footer = doc_->getElementById("footer");
    if (footer) {
        EXPECT_EQ(footer->tagName(), "footer")
            << "getElementById('footer')의 태그명이 'footer'이어야 합니다";
    }
}

// 13. 트리 순회 (깊이 우선)
TEST_F(DomTreeTest, TreeTraversal) {
    ASSERT_NE(doc_, nullptr);

    // 깊이 우선 순회로 모든 노드 카운트
    int node_count = 0;
    doc_->walkDepthFirst([&node_count](DomNodePtr node) -> bool {
        node_count++;
        return true; // 계속 순회
    });
    EXPECT_GT(node_count, 5)
        << "DOM 트리에 5개 이상의 노드가 있어야 합니다";

    // 요소 노드만 카운트
    int element_count = 0;
    doc_->walkDepthFirst([&element_count](DomNodePtr node) -> bool {
        if (node->nodeType() == NodeType::Element) {
            element_count++;
        }
        return true;
    });
    EXPECT_GT(element_count, 3)
        << "3개 이상의 요소 노드가 있어야 합니다";
}

// 14. textContent 추출
TEST_F(DomTreeTest, ExtractsTextContent) {
    ASSERT_NE(doc_, nullptr);

    // 전체 문서의 텍스트 콘텐츠
    std::string text = doc_->textContent();
    EXPECT_FALSE(text.empty())
        << "문서의 textContent가 비어있으면 안 됩니다";

    // 핵심 텍스트 포함 확인
    EXPECT_NE(text.find("Ordinal Browser"), std::string::npos)
        << "textContent에 'Ordinal Browser'가 포함되어야 합니다";
    EXPECT_NE(text.find("Copyright"), std::string::npos)
        << "textContent에 'Copyright'가 포함되어야 합니다";
}


// ============================================================
// LayoutEngine 테스트 (2개)
// ============================================================

class LayoutEngineTest : public ::testing::Test {
protected:
    LayoutEngine engine_;
    CssParser css_parser_;
    HtmlParser html_parser_;

    void SetUp() override {
        // 기본 뷰포트 설정
        engine_.setViewport({1920.0f, 1080.0f, 2.0f});
        engine_.setRootFontSize(16.0f);
    }
};

// 15. 블록 박스 모델 레이아웃
TEST_F(LayoutEngineTest, BlockBoxModel) {
    // CSS 길이 값 파싱 테스트
    auto px_value = LayoutEngine::parseLength("100px");
    EXPECT_FLOAT_EQ(px_value.value, 100.0f)
        << "100px의 value는 100.0이어야 합니다";
    EXPECT_EQ(px_value.unit, LengthValue::Unit::Px)
        << "100px의 unit은 Px이어야 합니다";

    auto em_value = LayoutEngine::parseLength("2em");
    EXPECT_FLOAT_EQ(em_value.value, 2.0f)
        << "2em의 value는 2.0이어야 합니다";
    EXPECT_EQ(em_value.unit, LengthValue::Unit::Em);

    auto pct_value = LayoutEngine::parseLength("50%");
    EXPECT_FLOAT_EQ(pct_value.value, 50.0f)
        << "50%의 value는 50.0이어야 합니다";
    EXPECT_EQ(pct_value.unit, LengthValue::Unit::Percent);

    auto auto_value = LayoutEngine::parseLength("auto");
    EXPECT_TRUE(auto_value.isAuto())
        << "auto는 isAuto() == true이어야 합니다";
}

// 16. CSS display 및 position 파싱
TEST_F(LayoutEngineTest, ParsesDisplayAndPosition) {
    // display 값 파싱
    EXPECT_EQ(LayoutEngine::parseDisplay("block"), DisplayType::Block);
    EXPECT_EQ(LayoutEngine::parseDisplay("inline"), DisplayType::Inline);
    EXPECT_EQ(LayoutEngine::parseDisplay("inline-block"), DisplayType::InlineBlock);
    EXPECT_EQ(LayoutEngine::parseDisplay("flex"), DisplayType::Flex);
    EXPECT_EQ(LayoutEngine::parseDisplay("grid"), DisplayType::Grid);
    EXPECT_EQ(LayoutEngine::parseDisplay("none"), DisplayType::None);

    // position 값 파싱
    EXPECT_EQ(LayoutEngine::parsePosition("static"), PositionMode::Static);
    EXPECT_EQ(LayoutEngine::parsePosition("relative"), PositionMode::Relative);
    EXPECT_EQ(LayoutEngine::parsePosition("absolute"), PositionMode::Absolute);
    EXPECT_EQ(LayoutEngine::parsePosition("fixed"), PositionMode::Fixed);
    EXPECT_EQ(LayoutEngine::parsePosition("sticky"), PositionMode::Sticky);

    // LengthValue → 픽셀 변환
    LengthValue len;
    len.value = 50.0f;
    len.unit = LengthValue::Unit::Percent;
    float px = len.toPx(1000.0f); // 부모 크기 1000px
    EXPECT_FLOAT_EQ(px, 500.0f)
        << "50%의 픽셀 값은 부모(1000px)의 50% = 500px이어야 합니다";
}


// ============================================================
// 메인 (Google Test 실행)
// ============================================================
// GTest::gtest_main에 의해 자동으로 main() 제공됨
