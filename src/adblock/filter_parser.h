#pragma once

/**
 * @file filter_parser.h
 * @brief ABP/uBlock 필터 구문 파서
 *
 * || 앵커, | 시작/끝 앵커, ^ 구분자, * 와일드카드 패턴 파싱,
 * 옵션 파싱 ($script,$image,$third-party,$domain=...),
 * 도메인별 룰 맵 컴파일, 정규식 룰 지원, 주석/메타데이터 처리.
 */

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <cstdint>

namespace ordinal::adblock {

// ============================================================
// 리소스 타입
// ============================================================

/**
 * @brief 네트워크 요청 리소스 타입
 */
enum class ResourceType : uint32_t {
    None        = 0,
    Script      = 1 << 0,      ///< JavaScript
    Image       = 1 << 1,      ///< 이미지
    Stylesheet  = 1 << 2,      ///< CSS 스타일시트
    Object      = 1 << 3,      ///< 플러그인 오브젝트
    XmlHttp     = 1 << 4,      ///< XMLHttpRequest / fetch
    SubDocument = 1 << 5,      ///< iframe / frame
    Font        = 1 << 6,      ///< 웹 폰트
    Media       = 1 << 7,      ///< 미디어 (video, audio)
    WebSocket   = 1 << 8,      ///< WebSocket
    Popup       = 1 << 9,      ///< 팝업 창
    Document    = 1 << 10,     ///< 최상위 문서
    Other       = 1 << 11,     ///< 기타
    All         = 0xFFFFFFFF,  ///< 모든 타입
};

/// 비트 OR 연산자
inline ResourceType operator|(ResourceType a, ResourceType b) {
    return static_cast<ResourceType>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// 비트 AND 연산자
inline ResourceType operator&(ResourceType a, ResourceType b) {
    return static_cast<ResourceType>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// 비트 NOT 연산자
inline ResourceType operator~(ResourceType a) {
    return static_cast<ResourceType>(~static_cast<uint32_t>(a));
}

/**
 * @brief 문자열 → ResourceType 변환
 */
ResourceType resourceTypeFromString(const std::string& str);

// ============================================================
// 필터 룰 구조
// ============================================================

/**
 * @brief 파싱된 네트워크 필터 룰
 */
struct NetworkRule {
    int64_t id{0};                              ///< 룰 ID

    // 원본 텍스트
    std::string raw_text;                       ///< 원본 필터 문자열

    // 매칭 패턴
    std::string pattern;                        ///< 변환된 패턴 (와일드카드 → 정규식)
    std::regex compiled_regex;                  ///< 컴파일된 정규식
    bool has_regex{false};                      ///< 정규식 패턴인지

    // 앵커
    bool anchor_start{false};                   ///< | 시작 앵커
    bool anchor_end{false};                     ///< | 끝 앵커
    bool anchor_domain{false};                  ///< || 도메인 앵커

    // 옵션
    ResourceType resource_types{ResourceType::All};   ///< 적용 리소스 타입
    bool third_party{false};                    ///< $third-party
    bool first_party{false};                    ///< $~third-party (퍼스트파티 전용)
    bool is_exception{false};                   ///< @@ 예외 룰인지

    // 도메인 제한
    std::vector<std::string> include_domains;   ///< $domain= 적용 도메인
    std::vector<std::string> exclude_domains;   ///< $domain=~... 제외 도메인

    // 메타데이터
    std::string source_list;                    ///< 출처 필터 리스트 이름
};

/**
 * @brief 필터 리스트 메타데이터
 */
struct FilterListMeta {
    std::string title;                          ///< 리스트 제목
    std::string homepage;                       ///< 홈페이지 URL
    std::string version;                        ///< 버전
    int64_t last_modified{0};                   ///< 마지막 수정 시간
    int64_t expires_hours{72};                  ///< 갱신 주기 (시간)
    int rule_count{0};                          ///< 룰 수
};

// ============================================================
// FilterParser 클래스
// ============================================================

/**
 * @brief ABP/uBlock 필터 구문 파서
 *
 * 필터 리스트 텍스트를 파싱하여 NetworkRule 목록으로 변환합니다.
 * 도메인별 빠른 룩업을 위한 인덱스도 구축합니다.
 */
class FilterParser {
public:
    FilterParser();
    ~FilterParser();

    /**
     * @brief 필터 리스트 텍스트 파싱
     * @param text 필터 리스트 전체 텍스트
     * @param source_name 필터 리스트 이름
     * @return 파싱된 네트워크 룰 목록
     */
    std::vector<NetworkRule> parseFilterList(
        const std::string& text,
        const std::string& source_name = "") const;

    /**
     * @brief 단일 필터 라인 파싱
     * @param line 필터 라인
     * @return 파싱된 룰 (코멘트/빈 줄이면 nullopt)
     */
    std::optional<NetworkRule> parseLine(const std::string& line) const;

    /**
     * @brief 필터 리스트 메타데이터 추출
     * @param text 필터 리스트 텍스트
     * @return 메타데이터
     */
    FilterListMeta parseMetadata(const std::string& text) const;

    /**
     * @brief 코스메틱 필터인지 확인 (## 또는 #@#)
     */
    static bool isCosmeticFilter(const std::string& line);

    /**
     * @brief 주석/메타데이터 라인인지 확인
     */
    static bool isComment(const std::string& line);

    /**
     * @brief 도메인별 룰 인덱스 구축 (빠른 룩업용)
     * @param rules 룰 목록
     * @return 도메인 → 룰 인덱스 맵
     */
    static std::unordered_map<std::string, std::vector<size_t>> buildDomainIndex(
        const std::vector<NetworkRule>& rules);

private:
    /**
     * @brief 필터 패턴을 정규식으로 변환
     * @param pattern ABP 패턴 문자열
     * @return 정규식 문자열
     */
    static std::string patternToRegex(const std::string& pattern);

    /**
     * @brief 옵션 문자열 파싱 ($... 부분)
     * @param options_str 옵션 문자열
     * @param rule 결과를 저장할 룰
     */
    void parseOptions(const std::string& options_str, NetworkRule& rule) const;

    /**
     * @brief 도메인 옵션 파싱 ($domain=...)
     * @param domain_str 도메인 문자열 (|로 구분)
     * @param rule 결과를 저장할 룰
     */
    static void parseDomainOption(const std::string& domain_str, NetworkRule& rule);

    /// 다음 룰 ID
    mutable int64_t next_id_{1};
};

} // namespace ordinal::adblock
