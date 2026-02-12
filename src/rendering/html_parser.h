#pragma once

/**
 * @file html_parser.h
 * @brief HTML5 파서
 * 
 * HTML5 토크나이저와 트리 생성기를 구현합니다.
 * Data, TagOpen, TagName, Attribute 상태 머신 기반 파싱을 수행합니다.
 */

#include "dom_tree.h"

#include <string>
#include <vector>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace ordinal::rendering {

/**
 * @brief HTML 토큰 타입
 */
enum class TokenType {
    Doctype,        ///< <!DOCTYPE ...>
    StartTag,       ///< <tag>
    EndTag,         ///< </tag>
    SelfClosingTag, ///< <tag />
    Text,           ///< 텍스트 콘텐츠
    Comment,        ///< <!-- ... -->
    EndOfFile       ///< 파일 끝
};

/**
 * @brief HTML 토큰 구조체
 */
struct HtmlToken {
    TokenType type;
    std::string name;        ///< 태그 이름 또는 DOCTYPE 값
    std::string data;        ///< 텍스트/주석 내용
    std::vector<std::pair<std::string, std::string>> attributes;  ///< 속성 목록
    bool self_closing{false};  ///< 자기 닫힘 태그 여부
};

/**
 * @brief 토크나이저 상태 열거형
 */
enum class TokenizerState {
    Data,               ///< 일반 텍스트 상태
    TagOpen,            ///< '<' 발견 후
    EndTagOpen,         ///< '</' 발견 후
    TagName,            ///< 태그 이름 읽는 중
    BeforeAttributeName,///< 속성 이름 전 공백
    AttributeName,      ///< 속성 이름 읽는 중
    AfterAttributeName, ///< 속성 이름 후
    BeforeAttributeValue, ///< '=' 후 값 전
    AttributeValueDoubleQuoted, ///< 큰따옴표 내 값
    AttributeValueSingleQuoted, ///< 작은따옴표 내 값
    AttributeValueUnquoted,     ///< 따옴표 없는 값
    AfterAttributeValue,        ///< 속성 값 후
    SelfClosingStartTag,        ///< '/' 발견 (자기 닫힘)
    BogusComment,       ///< 잘못된 주석
    MarkupDeclarationOpen, ///< '<!' 발견 후
    CommentStart,       ///< '<!--' 시작
    CommentStartDash,   ///< '<!-' 상태
    Comment,            ///< 주석 내용 읽는 중
    CommentEndDash,     ///< '--' 가능성
    CommentEnd          ///< '-->' 종료 대기
};

/**
 * @brief HTML5 파서
 * 
 * HTML 소스 코드를 토큰화하고 DOM 트리를 생성합니다.
 */
class HtmlParser {
public:
    HtmlParser();
    ~HtmlParser() = default;

    /**
     * @brief HTML 문자열을 파싱하여 DOM 트리 생성
     * @param html HTML 소스 코드
     * @return 파싱된 문서 노드
     */
    [[nodiscard]] DocumentPtr parse(const std::string& html);

    /**
     * @brief HTML을 토큰 목록으로 변환
     * @param html HTML 소스 코드
     * @return 토큰 목록
     */
    [[nodiscard]] std::vector<HtmlToken> tokenize(const std::string& html);

    /**
     * @brief 파싱 에러 목록 조회
     */
    [[nodiscard]] const std::vector<std::string>& errors() const { return errors_; }

    /**
     * @brief HTML 엔티티 디코딩
     * @param text 엔티티가 포함된 텍스트
     * @return 디코딩된 텍스트
     */
    [[nodiscard]] static std::string decodeEntities(const std::string& text);

    /**
     * @brief HTML 특수문자 이스케이프
     * @param text 원본 텍스트
     * @return 이스케이프된 텍스트
     */
    [[nodiscard]] static std::string escapeHtml(const std::string& text);

private:
    // ============================
    // 토크나이저 내부 메서드
    // ============================

    /**
     * @brief 토크나이저 상태 머신 실행
     */
    void runTokenizer(const std::string& html);

    /**
     * @brief 현재 문자 소비 및 전진
     */
    char consume();

    /**
     * @brief 다음 문자 미리보기
     */
    [[nodiscard]] char peek() const;

    /**
     * @brief 입력 끝 확인
     */
    [[nodiscard]] bool atEnd() const;

    /**
     * @brief 토큰 발행
     */
    void emitToken(HtmlToken token);

    /**
     * @brief 현재 토큰에 속성 추가
     */
    void addAttribute();

    // ============================
    // 트리 생성 내부 메서드
    // ============================

    /**
     * @brief 토큰을 DOM 트리에 반영
     */
    void processToken(const HtmlToken& token);

    /**
     * @brief 요소 스택에서 매칭되는 태그 찾기
     */
    [[nodiscard]] bool hasInScope(const std::string& tag_name) const;

    /**
     * @brief 포맷팅 요소 처리 (b, i, em, strong 등)
     */
    void adoptionAgency(const std::string& tag_name);

    /**
     * @brief 암시적 태그 자동 생성 (html, head, body)
     */
    void ensureImplicitElements();

    /**
     * @brief void 요소 여부 확인
     */
    [[nodiscard]] static bool isVoidElement(const std::string& tag);

    /**
     * @brief 포맷팅 요소 여부 확인
     */
    [[nodiscard]] static bool isFormattingElement(const std::string& tag);

    // 토크나이저 상태
    TokenizerState state_{TokenizerState::Data};
    std::string input_;
    size_t pos_{0};

    // 현재 토큰 생성 중
    HtmlToken current_token_;
    std::string current_attr_name_;
    std::string current_attr_value_;

    // 생성된 토큰 목록
    std::vector<HtmlToken> tokens_;

    // 트리 생성 상태
    DocumentPtr document_;
    std::stack<ElementPtr> open_elements_;  ///< 열린 요소 스택
    bool head_inserted_{false};
    bool body_inserted_{false};

    // 에러 목록
    std::vector<std::string> errors_;

    // HTML 엔티티 매핑 테이블
    static const std::unordered_map<std::string, std::string> entity_map_;
};

} // namespace ordinal::rendering
