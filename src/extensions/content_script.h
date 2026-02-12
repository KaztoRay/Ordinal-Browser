#pragma once

/**
 * @file content_script.h
 * @brief 콘텐츠 스크립트 — 페이지 컨텍스트 삽입
 * 
 * 웹 페이지에 JavaScript/CSS를 삽입하여 DOM에 접근합니다.
 * Chrome 확장의 콘텐츠 스크립트와 동일한 기능을 제공합니다.
 * URL 매치 패턴, 실행 시점(run_at), CSS 삽입을 지원합니다.
 */

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <regex>
#include <functional>

namespace ordinal::extensions {

/**
 * @brief 콘텐츠 스크립트 실행 시점
 */
enum class RunAtTiming {
    DocumentStart,  ///< DOM 파싱 시작 직후 (document_start)
    DocumentEnd,    ///< DOM 파싱 완료 직후, 리소스 로드 전 (document_end)
    DocumentIdle    ///< 페이지 로드 완료 후 유휴 시점 (document_idle, 기본값)
};

/**
 * @brief URL 매치 패턴 파서 및 매처
 * 
 * Chrome 확장의 match patterns 사양을 구현합니다.
 * 형식: <scheme>://<host><path>
 * 예: "*://*.google.com/*", "https://example.com/page/*"
 */
class MatchPattern {
public:
    /**
     * @brief 매치 패턴 생성
     * @param pattern 패턴 문자열 (예: "*://*.example.com/*")
     */
    explicit MatchPattern(const std::string& pattern);

    /**
     * @brief URL이 패턴과 일치하는지 확인
     * @param url 검사할 URL
     * @return 일치 여부
     */
    [[nodiscard]] bool matches(const std::string& url) const;

    /**
     * @brief 패턴 유효성 확인
     */
    [[nodiscard]] bool isValid() const { return valid_; }

    /**
     * @brief 원본 패턴 문자열
     */
    [[nodiscard]] const std::string& pattern() const { return pattern_; }

    /**
     * @brief <all_urls> 패턴인지 확인
     */
    [[nodiscard]] bool isAllUrls() const { return all_urls_; }

    /**
     * @brief 패턴의 스킴 부분
     */
    [[nodiscard]] const std::string& scheme() const { return scheme_; }

    /**
     * @brief 패턴의 호스트 부분
     */
    [[nodiscard]] const std::string& host() const { return host_; }

    /**
     * @brief 패턴의 경로 부분
     */
    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string pattern_;       ///< 원본 패턴 문자열
    std::string scheme_;        ///< 스킴 ("http", "https", "*", "file", "ftp")
    std::string host_;          ///< 호스트 ("*.example.com", "example.com", "*")
    std::string path_;          ///< 경로 ("/*", "/page/*")
    bool valid_{false};         ///< 유효한 패턴인지
    bool all_urls_{false};      ///< <all_urls> 패턴인지
    bool host_wildcard_{false}; ///< 호스트에 서브도메인 와일드카드가 있는지

    std::regex compiled_regex_; ///< 컴파일된 정규식

    /// 패턴 파싱
    bool parse();
    /// 정규식 컴파일
    bool compile();
};

/**
 * @brief 삽입할 JavaScript 파일 정보
 */
struct ScriptFile {
    std::string filename;       ///< 파일 이름 (확장 내 상대 경로)
    std::string source_code;    ///< JavaScript 소스 코드
    size_t size_bytes{0};       ///< 파일 크기 (바이트)
};

/**
 * @brief 삽입할 CSS 파일 정보
 */
struct StylesheetFile {
    std::string filename;       ///< 파일 이름
    std::string css_content;    ///< CSS 내용
    size_t size_bytes{0};       ///< 파일 크기 (바이트)
};

/**
 * @brief DOM 접근 결과
 */
struct DomQueryResult {
    bool success{false};
    std::string selector;               ///< 사용된 선택자
    int matched_count{0};               ///< 매칭된 요소 수
    std::vector<std::string> values;    ///< 요소 값 목록
    std::string error;                  ///< 에러 메시지
};

/**
 * @brief CSS 삽입 방식
 */
enum class CssInjectionMethod {
    StyleTag,       ///< <style> 태그로 삽입
    LinkTag,        ///< <link rel="stylesheet"> 태그로 삽입
    CSSOMInsert     ///< CSSOM API (insertRule)로 삽입
};

/**
 * @brief 콘텐츠 스크립트
 * 
 * 웹 페이지 컨텍스트에 JavaScript와 CSS를 삽입합니다.
 * 격리된 월드(isolated world)에서 실행되어 페이지 스크립트와 충돌하지 않습니다.
 */
class ContentScript {
public:
    /**
     * @brief 소속 확장 ID로 생성
     * @param extension_id 확장 프로그램 ID
     */
    explicit ContentScript(const std::string& extension_id);
    ~ContentScript();

    // 이동만 허용
    ContentScript(ContentScript&& other) noexcept;
    ContentScript& operator=(ContentScript&& other) noexcept;
    ContentScript(const ContentScript&) = delete;
    ContentScript& operator=(const ContentScript&) = delete;

    // ============================
    // 매치 패턴 관리
    // ============================

    /**
     * @brief URL 매치 패턴 추가
     * @param pattern 패턴 문자열 (예: "*://*.example.com/*")
     * @return 추가 성공 여부
     */
    bool addMatchPattern(const std::string& pattern);

    /**
     * @brief URL 제외 패턴 추가
     * @param pattern 제외할 URL 패턴
     * @return 추가 성공 여부
     */
    bool addExcludePattern(const std::string& pattern);

    /**
     * @brief URL이 매치 패턴과 일치하는지 확인
     * @param url 검사할 URL
     * @return 매칭 여부 (include 매칭 && !exclude 매칭)
     */
    [[nodiscard]] bool matchesUrl(const std::string& url) const;

    /**
     * @brief 매치 패턴 목록 반환
     */
    [[nodiscard]] const std::vector<MatchPattern>& matchPatterns() const { return match_patterns_; }

    /**
     * @brief 제외 패턴 목록 반환
     */
    [[nodiscard]] const std::vector<MatchPattern>& excludePatterns() const { return exclude_patterns_; }

    // ============================
    // JavaScript 관리
    // ============================

    /**
     * @brief JavaScript 파일 추가
     * @param filename 파일 이름
     * @param source_code JavaScript 소스 코드
     */
    void addJavaScript(const std::string& filename, const std::string& source_code);

    /**
     * @brief 등록된 JavaScript 파일 목록
     */
    [[nodiscard]] const std::vector<ScriptFile>& javaScripts() const { return js_files_; }

    /**
     * @brief 모든 JavaScript를 하나의 번들로 합침
     * @return 합쳐진 JavaScript 코드
     */
    [[nodiscard]] std::string bundledJavaScript() const;

    // ============================
    // CSS 관리
    // ============================

    /**
     * @brief CSS 파일 추가
     * @param filename 파일 이름
     * @param css_content CSS 내용
     */
    void addStylesheet(const std::string& filename, const std::string& css_content);

    /**
     * @brief 등록된 CSS 파일 목록
     */
    [[nodiscard]] const std::vector<StylesheetFile>& stylesheets() const { return css_files_; }

    /**
     * @brief 모든 CSS를 하나의 번들로 합침
     * @return 합쳐진 CSS
     */
    [[nodiscard]] std::string bundledStylesheet() const;

    /**
     * @brief CSS 삽입 방식 설정
     */
    void setCssInjectionMethod(CssInjectionMethod method) { css_method_ = method; }
    [[nodiscard]] CssInjectionMethod cssInjectionMethod() const { return css_method_; }

    // ============================
    // 실행 시점 설정
    // ============================

    /**
     * @brief 실행 시점 설정
     */
    void setRunAt(RunAtTiming timing) { run_at_ = timing; }
    [[nodiscard]] RunAtTiming runAt() const { return run_at_; }

    /**
     * @brief 실행 시점 문자열 반환
     */
    [[nodiscard]] std::string runAtString() const;

    // ============================
    // 프레임 설정
    // ============================

    /**
     * @brief 모든 프레임에 삽입할지 설정
     */
    void setAllFrames(bool all_frames) { all_frames_ = all_frames; }
    [[nodiscard]] bool allFrames() const { return all_frames_; }

    /**
     * @brief about:blank 매칭 설정
     */
    void setMatchAboutBlank(bool match) { match_about_blank_ = match; }
    [[nodiscard]] bool matchAboutBlank() const { return match_about_blank_; }

    // ============================
    // DOM 접근 API
    // ============================

    /**
     * @brief DOM 요소 쿼리 (document.querySelector 래퍼)
     * @param selector CSS 선택자
     * @return 쿼리 결과
     */
    [[nodiscard]] DomQueryResult querySelector(const std::string& selector) const;

    /**
     * @brief DOM 요소 전체 쿼리 (document.querySelectorAll 래퍼)
     * @param selector CSS 선택자
     * @return 쿼리 결과
     */
    [[nodiscard]] DomQueryResult querySelectorAll(const std::string& selector) const;

    /**
     * @brief DOM 요소 텍스트 내용 가져오기
     * @param selector CSS 선택자
     * @return 텍스트 내용 (없으면 nullopt)
     */
    [[nodiscard]] std::optional<std::string> getTextContent(const std::string& selector) const;

    /**
     * @brief DOM 요소 속성 가져오기
     * @param selector CSS 선택자
     * @param attribute 속성 이름
     * @return 속성 값 (없으면 nullopt)
     */
    [[nodiscard]] std::optional<std::string> getAttribute(
        const std::string& selector,
        const std::string& attribute
    ) const;

    /**
     * @brief DOM 요소에 CSS 클래스 추가
     * @param selector CSS 선택자
     * @param class_name 클래스 이름
     * @return 성공 여부
     */
    bool addClass(const std::string& selector, const std::string& class_name);

    /**
     * @brief DOM 요소에서 CSS 클래스 제거
     * @param selector CSS 선택자
     * @param class_name 클래스 이름
     * @return 성공 여부
     */
    bool removeClass(const std::string& selector, const std::string& class_name);

    /**
     * @brief DOM 요소 숨기기
     * @param selector CSS 선택자
     * @return 성공 여부
     */
    bool hideElement(const std::string& selector);

    /**
     * @brief DOM 요소에 이벤트 리스너 등록 (JavaScript 코드 생성)
     * @param selector CSS 선택자
     * @param event_name 이벤트 이름 (예: "click")
     * @param handler_code JavaScript 핸들러 코드
     * @return 생성된 JavaScript 코드
     */
    [[nodiscard]] std::string generateEventListener(
        const std::string& selector,
        const std::string& event_name,
        const std::string& handler_code
    ) const;

    // ============================
    // 페이지 삽입 실행
    // ============================

    /**
     * @brief 페이지에 삽입할 전체 코드 생성
     * 
     * JavaScript와 CSS를 격리된 월드에서 실행할 수 있도록
     * 래핑된 코드를 생성합니다.
     * @return 삽입용 JavaScript 코드
     */
    [[nodiscard]] std::string generateInjectionCode() const;

    /**
     * @brief CSS를 <style> 태그로 삽입하는 JavaScript 코드 생성
     * @return JavaScript 코드
     */
    [[nodiscard]] std::string generateCssInjectionCode() const;

    // ============================
    // 소속 확장 정보
    // ============================

    [[nodiscard]] const std::string& extensionId() const { return extension_id_; }

private:
    std::string extension_id_;          ///< 소속 확장 ID

    // 매치 패턴
    std::vector<MatchPattern> match_patterns_;
    std::vector<MatchPattern> exclude_patterns_;

    // 삽입 파일
    std::vector<ScriptFile> js_files_;
    std::vector<StylesheetFile> css_files_;

    // 실행 설정
    RunAtTiming run_at_{RunAtTiming::DocumentIdle};
    bool all_frames_{false};
    bool match_about_blank_{false};
    CssInjectionMethod css_method_{CssInjectionMethod::StyleTag};

    /// 격리된 월드 ID (페이지 스크립트와 분리)
    int world_id_{0};
    static int s_next_world_id_;
};

} // namespace ordinal::extensions
