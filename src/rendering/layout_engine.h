#pragma once

/**
 * @file layout_engine.h
 * @brief 레이아웃 엔진
 * 
 * 박스 모델 (margin/border/padding), 블록/인라인 컨텍스트,
 * 너비/높이 해석, 위치 지정 모드를 구현합니다.
 */

#include "dom_tree.h"
#include "css_parser.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <cmath>

namespace ordinal::rendering {

/**
 * @brief 디스플레이 타입
 */
enum class DisplayType {
    Block,          ///< 블록 레벨 요소
    Inline,         ///< 인라인 요소
    InlineBlock,    ///< 인라인 블록
    Flex,           ///< 플렉스 컨테이너
    Grid,           ///< 그리드 컨테이너
    None,           ///< 표시 안 함
    Table,          ///< 테이블
    TableRow,       ///< 테이블 행
    TableCell,      ///< 테이블 셀
    ListItem        ///< 리스트 아이템
};

/**
 * @brief 위치 지정 모드
 */
enum class PositionMode {
    Static,         ///< 정적 위치 (기본)
    Relative,       ///< 상대 위치
    Absolute,       ///< 절대 위치
    Fixed,          ///< 고정 위치
    Sticky          ///< 고정+상대 위치
};

/**
 * @brief 오버플로 모드
 */
enum class OverflowMode {
    Visible,        ///< 기본 — 넘침 표시
    Hidden,         ///< 넘침 숨김
    Scroll,         ///< 항상 스크롤바
    Auto            ///< 필요시 스크롤바
};

/**
 * @brief 박스 사이징 모드
 */
enum class BoxSizing {
    ContentBox,     ///< 표준 박스 모델
    BorderBox       ///< 대체 박스 모델
};

/**
 * @brief 플로트 타입
 */
enum class FloatType {
    None,           ///< 플로트 없음
    Left,           ///< 왼쪽 플로트
    Right           ///< 오른쪽 플로트
};

/**
 * @brief 길이 값 (CSS 단위 포함)
 */
struct LengthValue {
    enum class Unit {
        Px,         ///< 픽셀
        Em,         ///< em 단위
        Rem,        ///< 루트 em
        Percent,    ///< 백분율
        Vw,         ///< 뷰포트 너비 %
        Vh,         ///< 뷰포트 높이 %
        Auto,       ///< 자동
        None        ///< 값 없음
    };

    float value{0.0f};
    Unit unit{Unit::None};

    [[nodiscard]] bool isAuto() const { return unit == Unit::Auto; }
    [[nodiscard]] bool isNone() const { return unit == Unit::None; }

    /**
     * @brief 길이를 픽셀 값으로 변환
     * @param parent_size 부모 요소의 해당 축 크기 (% 계산용)
     * @param font_size 현재 글꼴 크기 (em 계산용)
     * @param root_font_size 루트 글꼴 크기 (rem 계산용)
     * @param viewport_width 뷰포트 너비 (vw 계산용)
     * @param viewport_height 뷰포트 높이 (vh 계산용)
     */
    [[nodiscard]] float toPx(float parent_size = 0.0f, float font_size = 16.0f,
                              float root_font_size = 16.0f,
                              float viewport_width = 1920.0f,
                              float viewport_height = 1080.0f) const;
};

/**
 * @brief 사각형 영역
 */
struct Rect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};

    [[nodiscard]] float right() const { return x + width; }
    [[nodiscard]] float bottom() const { return y + height; }
    [[nodiscard]] bool contains(float px, float py) const {
        return px >= x && px <= right() && py >= y && py <= bottom();
    }
};

/**
 * @brief 네 방향 값 (margin, padding, border)
 */
struct EdgeValues {
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
    float left{0.0f};

    [[nodiscard]] float horizontal() const { return left + right; }
    [[nodiscard]] float vertical() const { return top + bottom; }
};

/**
 * @brief 계산된 스타일 (Computed Style)
 */
struct ComputedStyle {
    DisplayType display{DisplayType::Block};
    PositionMode position{PositionMode::Static};
    BoxSizing box_sizing{BoxSizing::ContentBox};
    FloatType float_type{FloatType::None};
    OverflowMode overflow_x{OverflowMode::Visible};
    OverflowMode overflow_y{OverflowMode::Visible};

    // 크기
    LengthValue width;
    LengthValue height;
    LengthValue min_width;
    LengthValue min_height;
    LengthValue max_width;
    LengthValue max_height;

    // 박스 모델
    EdgeValues margin;
    EdgeValues padding;
    EdgeValues border_width;

    // 위치 오프셋 (position이 static이 아닐 때)
    LengthValue top;
    LengthValue right_offset;
    LengthValue bottom_offset;
    LengthValue left_offset;

    // 텍스트
    float font_size{16.0f};
    float line_height{1.2f};
    std::string font_family{"sans-serif"};
    int font_weight{400};

    // Z 인덱스
    int z_index{0};

    // 가시성
    bool visible{true};
    float opacity{1.0f};
};

/**
 * @brief 레이아웃 박스
 * 
 * DOM 노드에 대응하는 레이아웃 정보를 담습니다.
 */
struct LayoutBox {
    // 참조하는 DOM 노드
    DomNodePtr node;

    // 계산된 스타일
    ComputedStyle style;

    // 레이아웃 영역
    Rect content_rect;      ///< 콘텐츠 영역
    Rect padding_rect;      ///< 패딩 포함
    Rect border_rect;       ///< 보더 포함
    Rect margin_rect;       ///< 마진 포함

    // 자식 레이아웃 박스
    std::vector<std::shared_ptr<LayoutBox>> children;

    // 부모 참조
    std::weak_ptr<LayoutBox> parent;

    // 라인 박스 (인라인 컨텍스트용)
    bool is_line_box{false};
    float baseline{0.0f};
};

using LayoutBoxPtr = std::shared_ptr<LayoutBox>;

/**
 * @brief 뷰포트 정보
 */
struct Viewport {
    float width{1920.0f};
    float height{1080.0f};
    float device_pixel_ratio{2.0f};
};

/**
 * @brief 레이아웃 엔진
 * 
 * DOM 트리와 CSS 규칙을 받아 레이아웃 트리를 생성합니다.
 */
class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine() = default;

    /**
     * @brief 뷰포트 설정
     */
    void setViewport(const Viewport& vp) { viewport_ = vp; }
    [[nodiscard]] const Viewport& viewport() const { return viewport_; }

    /**
     * @brief 루트 글꼴 크기 설정
     */
    void setRootFontSize(float size) { root_font_size_ = size; }

    /**
     * @brief 레이아웃 계산
     * @param document DOM 문서 노드
     * @param rules CSS 규칙 목록
     * @return 루트 레이아웃 박스
     */
    [[nodiscard]] LayoutBoxPtr layout(const DocumentPtr& document,
                                       const std::vector<CssRule>& rules);

    /**
     * @brief 특정 좌표에 있는 레이아웃 박스 찾기 (히트 테스트)
     */
    [[nodiscard]] LayoutBoxPtr hitTest(const LayoutBoxPtr& root, float x, float y) const;

    /**
     * @brief CSS 값 문자열을 LengthValue로 파싱
     */
    [[nodiscard]] static LengthValue parseLength(const std::string& value);

    /**
     * @brief CSS display 값 파싱
     */
    [[nodiscard]] static DisplayType parseDisplay(const std::string& value);

    /**
     * @brief CSS position 값 파싱
     */
    [[nodiscard]] static PositionMode parsePosition(const std::string& value);

private:
    /**
     * @brief DOM 노드에서 레이아웃 트리 생성
     */
    LayoutBoxPtr buildLayoutTree(const DomNodePtr& node, const std::vector<CssRule>& rules);

    /**
     * @brief 스타일 계산 — CSS 규칙 매칭 및 캐스케이딩
     */
    ComputedStyle computeStyle(const ElementPtr& element, const std::vector<CssRule>& rules);

    /**
     * @brief CSS 속성 맵에서 ComputedStyle 생성
     */
    ComputedStyle resolveProperties(const PropertyMap& props, const ComputedStyle& inherited);

    /**
     * @brief 블록 레이아웃 계산
     */
    void layoutBlock(LayoutBoxPtr& box, float containing_width);

    /**
     * @brief 인라인 레이아웃 계산
     */
    void layoutInline(LayoutBoxPtr& box, float containing_width);

    /**
     * @brief 자식 블록 요소들의 수직 레이아웃
     */
    void layoutBlockChildren(LayoutBoxPtr& box);

    /**
     * @brief 자식 인라인 요소들의 수평 레이아웃 (라인 박스)
     */
    void layoutInlineChildren(LayoutBoxPtr& box);

    /**
     * @brief 너비 해석
     */
    void resolveWidth(LayoutBoxPtr& box, float containing_width);

    /**
     * @brief 높이 해석
     */
    void resolveHeight(LayoutBoxPtr& box);

    /**
     * @brief 박스 모델 영역 계산
     */
    void calculateBoxRects(LayoutBoxPtr& box);

    /**
     * @brief 마진 겹침 (margin collapsing) 처리
     */
    [[nodiscard]] float collapseMargins(float margin1, float margin2) const;

    /**
     * @brief 기본 display 값 결정 (태그별)
     */
    [[nodiscard]] static DisplayType defaultDisplay(const std::string& tag_name);

    /**
     * @brief 셀렉터가 요소에 매칭되는지 확인
     */
    [[nodiscard]] bool selectorMatches(const CssSelector& selector, const ElementPtr& element) const;

    // 뷰포트 정보
    Viewport viewport_;
    float root_font_size_{16.0f};
};

} // namespace ordinal::rendering
