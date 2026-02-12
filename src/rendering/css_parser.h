#pragma once

/**
 * @file css_parser.h
 * @brief CSS 파서
 * 
 * CSS 토크나이저, 셀렉터 파싱, 속성 맵, 특이성 계산,
 * @media 쿼리를 구현합니다.
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>
#include <optional>

namespace ordinal::rendering {

/**
 * @brief CSS 특이성 (Specificity) 구조체
 * 
 * 형식: (inline, id, class+attribute+pseudo, type+pseudo-element)
 */
struct Specificity {
    int inline_style{0};   ///< 인라인 스타일 (가장 높은 우선순위)
    int id_count{0};       ///< ID 셀렉터 수
    int class_count{0};    ///< 클래스/속성/의사 클래스 수
    int type_count{0};     ///< 타입/의사 요소 수

    [[nodiscard]] int toInt() const {
        return inline_style * 1000 + id_count * 100 + class_count * 10 + type_count;
    }

    bool operator<(const Specificity& other) const { return toInt() < other.toInt(); }
    bool operator>(const Specificity& other) const { return toInt() > other.toInt(); }
    bool operator==(const Specificity& other) const { return toInt() == other.toInt(); }
    bool operator<=(const Specificity& other) const { return toInt() <= other.toInt(); }
    bool operator>=(const Specificity& other) const { return toInt() >= other.toInt(); }
};

/**
 * @brief 셀렉터 타입 열거형
 */
enum class SelectorType {
    Type,           ///< 타입 셀렉터 (div, p, span)
    Class,          ///< 클래스 셀렉터 (.class)
    Id,             ///< ID 셀렉터 (#id)
    Attribute,      ///< 속성 셀렉터 ([attr=value])
    PseudoClass,    ///< 의사 클래스 (:hover, :first-child)
    PseudoElement,  ///< 의사 요소 (::before, ::after)
    Universal,      ///< 전체 셀렉터 (*)
    Combinator      ///< 조합자 (>, +, ~, 공백)
};

/**
 * @brief 속성 셀렉터 연산자
 */
enum class AttributeOp {
    Exists,     ///< [attr] — 존재 여부
    Equals,     ///< [attr=value] — 정확히 일치
    Contains,   ///< [attr*=value] — 포함
    StartsWith, ///< [attr^=value] — 시작
    EndsWith,   ///< [attr$=value] — 끝
    DashMatch,  ///< [attr|=value] — 하이픈 일치
    WordMatch   ///< [attr~=value] — 단어 일치
};

/**
 * @brief 개별 셀렉터 부분
 */
struct SelectorPart {
    SelectorType type;
    std::string value;              ///< 셀렉터 값
    AttributeOp attr_op{AttributeOp::Exists};  ///< 속성 연산자
    std::string attr_name;          ///< 속성 이름 (속성 셀렉터용)
    std::string attr_value;         ///< 속성 값 (속성 셀렉터용)
    char combinator{' '};           ///< 조합자 문자 (>, +, ~)
};

/**
 * @brief 완전한 CSS 셀렉터
 */
struct CssSelector {
    std::vector<SelectorPart> parts;  ///< 셀렉터 구성 요소
    std::string raw;                  ///< 원본 셀렉터 문자열
    Specificity specificity;          ///< 계산된 특이성

    [[nodiscard]] std::string toString() const { return raw; }
};

/**
 * @brief CSS 속성 값
 */
struct CssValue {
    std::string raw;          ///< 원본 값 문자열
    bool important{false};    ///< !important 여부

    CssValue() = default;
    explicit CssValue(std::string val, bool imp = false)
        : raw(std::move(val)), important(imp) {}
};

/**
 * @brief CSS 속성 맵 (속성명 → 값)
 */
using PropertyMap = std::unordered_map<std::string, CssValue>;

/**
 * @brief CSS 규칙 (셀렉터 + 속성)
 */
struct CssRule {
    CssSelector selector;     ///< 셀렉터
    PropertyMap properties;   ///< 속성 맵
};

/**
 * @brief @media 조건
 */
struct MediaCondition {
    std::string feature;      ///< 미디어 기능 (width, color 등)
    std::string value;        ///< 기능 값
    bool is_min{false};       ///< min- 접두사
    bool is_max{false};       ///< max- 접두사
};

/**
 * @brief @media 쿼리
 */
struct MediaQuery {
    std::string media_type;   ///< 미디어 타입 (all, screen, print)
    std::vector<MediaCondition> conditions;  ///< 미디어 조건
    std::vector<CssRule> rules;              ///< 포함된 규칙
    bool negate{false};       ///< not 키워드
};

/**
 * @brief CSS 토큰 타입
 */
enum class CssTokenType {
    Ident,          ///< 식별자
    String,         ///< 문자열 리터럴
    Number,         ///< 숫자
    Hash,           ///< #hash
    Dot,            ///< .
    Colon,          ///< :
    DoubleColon,    ///< ::
    Semicolon,      ///< ;
    OpenBrace,      ///< {
    CloseBrace,     ///< }
    OpenBracket,    ///< [
    CloseBracket,   ///< ]
    OpenParen,      ///< (
    CloseParen,     ///< )
    Comma,          ///< ,
    Star,           ///< *
    Greater,        ///< >
    Plus,           ///< +
    Tilde,          ///< ~
    At,             ///< @
    Whitespace,     ///< 공백
    Delim,          ///< 기타 구분자
    EndOfFile       ///< 파일 끝
};

/**
 * @brief CSS 토큰
 */
struct CssToken {
    CssTokenType type;
    std::string value;
};

/**
 * @brief CSS 파서
 * 
 * CSS 소스를 파싱하여 규칙, 셀렉터, 속성 맵을 생성합니다.
 */
class CssParser {
public:
    CssParser();
    ~CssParser() = default;

    /**
     * @brief CSS 소스 코드 파싱
     * @param css CSS 소스
     * @return 파싱된 CSS 규칙 목록
     */
    [[nodiscard]] std::vector<CssRule> parse(const std::string& css);

    /**
     * @brief CSS를 토큰 목록으로 변환
     */
    [[nodiscard]] std::vector<CssToken> tokenize(const std::string& css);

    /**
     * @brief 셀렉터 문자열 파싱
     * @param selector_str 셀렉터 문자열
     * @return 파싱된 셀렉터
     */
    [[nodiscard]] CssSelector parseSelector(const std::string& selector_str);

    /**
     * @brief 특이성 계산
     */
    [[nodiscard]] static Specificity calculateSpecificity(const CssSelector& selector);

    /**
     * @brief @media 쿼리 파싱
     */
    [[nodiscard]] std::vector<MediaQuery> parseMediaQueries(const std::string& css);

    /**
     * @brief 인라인 스타일 파싱 (style 속성)
     * @param style_str 스타일 문자열
     * @return 속성 맵
     */
    [[nodiscard]] static PropertyMap parseInlineStyle(const std::string& style_str);

    /**
     * @brief 파싱 에러 목록
     */
    [[nodiscard]] const std::vector<std::string>& errors() const { return errors_; }

private:
    // 토크나이저 내부
    void runTokenizer(const std::string& css);
    char consume();
    [[nodiscard]] char peek() const;
    [[nodiscard]] bool atEnd() const;
    void skipWhitespace();
    [[nodiscard]] std::string readIdent();
    [[nodiscard]] std::string readString(char quote);
    [[nodiscard]] std::string readNumber();

    // 파서 내부
    [[nodiscard]] CssRule parseRule(const std::vector<CssToken>& tokens, size_t& pos);
    [[nodiscard]] PropertyMap parseDeclarationBlock(const std::vector<CssToken>& tokens, size_t& pos);
    [[nodiscard]] CssSelector parseSelectorTokens(const std::vector<CssToken>& tokens, size_t& pos);
    [[nodiscard]] SelectorPart parseAttributeSelector(const std::vector<CssToken>& tokens, size_t& pos);

    // 미디어 쿼리 내부
    [[nodiscard]] MediaQuery parseMediaQuery(const std::vector<CssToken>& tokens, size_t& pos);
    [[nodiscard]] MediaCondition parseMediaCondition(const std::vector<CssToken>& tokens, size_t& pos);

    // 토크나이저 상태
    std::string input_;
    size_t pos_{0};
    std::vector<CssToken> tokens_;

    // 에러 목록
    std::vector<std::string> errors_;
};

} // namespace ordinal::rendering
