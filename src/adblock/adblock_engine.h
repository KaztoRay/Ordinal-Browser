#pragma once

/**
 * @file adblock_engine.h
 * @brief 광고 차단 엔진
 *
 * 필터 리스트 로드 (URL/파일), 네트워크 요청 매칭,
 * CSS 요소 숨김 룰 추출, 예외 룰 (@@), $third-party/$~third-party,
 * 구독 관리 (리스트 URL, 자동 갱신, 마지막 업데이트),
 * 차단 통계 (총 차단, 도메인별, 페이지별).
 */

#include "filter_parser.h"
#include "cosmetic_filter.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <cstdint>

namespace ordinal::adblock {

// ============================================================
// 구독 (필터 리스트)
// ============================================================

/**
 * @brief 필터 리스트 구독 정보
 */
struct FilterSubscription {
    std::string url;                            ///< 다운로드 URL
    std::string title;                          ///< 리스트 제목
    std::string local_path;                     ///< 로컬 캐시 경로
    int64_t last_updated{0};                    ///< 마지막 업데이트 (Unix epoch)
    int64_t update_interval_hours{72};          ///< 자동 갱신 주기 (시간)
    int rule_count{0};                          ///< 룰 수
    bool enabled{true};                         ///< 활성화 여부
    bool builtin{false};                        ///< 내장 리스트 여부
};

// ============================================================
// 차단 통계
// ============================================================

/**
 * @brief 차단 통계
 */
struct BlockingStats {
    std::atomic<uint64_t> total_blocked{0};     ///< 총 차단 수
    std::atomic<uint64_t> total_checked{0};     ///< 총 검사 수

    /// 도메인별 차단 수
    std::unordered_map<std::string, uint64_t> blocked_per_domain;
    std::mutex domain_mutex;

    /// 페이지별 차단 수
    std::unordered_map<std::string, uint64_t> blocked_per_page;
    std::mutex page_mutex;
};

/**
 * @brief 차단 결과
 */
struct BlockResult {
    bool blocked{false};                        ///< 차단 여부
    bool exception{false};                      ///< 예외 적용 여부
    std::string matched_rule;                   ///< 매칭된 룰 원본 텍스트
    std::string filter_list;                    ///< 출처 필터 리스트
};

// ============================================================
// 콜백 타입
// ============================================================

/// 필터 리스트 다운로드 콜백: (url) → content
using DownloadCallback = std::function<std::string(const std::string& url)>;

// ============================================================
// AdBlockEngine 클래스
// ============================================================

/**
 * @brief 광고 차단 엔진
 *
 * 필터 리스트를 로드/파싱하여 네트워크 요청 차단 및
 * CSS 요소 숨김을 수행합니다. ABP/uBlock 호환.
 */
class AdBlockEngine {
public:
    AdBlockEngine();
    ~AdBlockEngine();

    AdBlockEngine(const AdBlockEngine&) = delete;
    AdBlockEngine& operator=(const AdBlockEngine&) = delete;

    /**
     * @brief 초기화 (기본 필터 리스트 로드)
     * @param data_dir 데이터 저장 디렉토리
     * @return 성공 여부
     */
    bool initialize(const std::string& data_dir);

    /**
     * @brief 활성화/비활성화
     */
    void setEnabled(bool enabled);

    /**
     * @brief 활성화 상태 확인
     */
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    // ============================
    // 네트워크 요청 차단
    // ============================

    /**
     * @brief 요청 차단 여부 확인
     * @param url 요청 URL
     * @param page_url 현재 페이지 URL
     * @param resource_type 리소스 타입 문자열 ("script", "image" 등)
     * @return 차단 여부
     */
    [[nodiscard]] bool shouldBlock(
        const std::string& url,
        const std::string& page_url,
        const std::string& resource_type = "") const;

    /**
     * @brief 상세 차단 결과 확인
     */
    [[nodiscard]] BlockResult checkRequest(
        const std::string& url,
        const std::string& page_url,
        const std::string& resource_type = "") const;

    // ============================
    // CSS 요소 숨김
    // ============================

    /**
     * @brief 도메인에 적용할 CSS 스타일시트 생성
     * @param domain 현재 페이지 도메인
     * @return CSS 스타일시트 문자열
     */
    [[nodiscard]] std::string getCosmeticStylesheet(
        const std::string& domain) const;

    /**
     * @brief 도메인에 적용할 CSS 셀렉터 목록
     */
    [[nodiscard]] std::vector<std::string> getCosmeticSelectors(
        const std::string& domain) const;

    // ============================
    // 필터 리스트 관리
    // ============================

    /**
     * @brief 필터 리스트 파일에서 로드
     * @param file_path 파일 경로
     * @param name 리스트 이름
     * @return 로드된 룰 수
     */
    int loadFilterFile(const std::string& file_path,
                       const std::string& name = "");

    /**
     * @brief 필터 리스트 텍스트에서 로드
     * @param text 필터 텍스트
     * @param name 리스트 이름
     * @return 로드된 룰 수
     */
    int loadFilterText(const std::string& text,
                       const std::string& name = "");

    /**
     * @brief 구독 추가
     * @param url 필터 리스트 URL
     * @param title 리스트 제목
     */
    void addSubscription(const std::string& url,
                         const std::string& title = "");

    /**
     * @brief 구독 제거
     * @param url 필터 리스트 URL
     */
    void removeSubscription(const std::string& url);

    /**
     * @brief 구독 활성화/비활성화
     */
    void setSubscriptionEnabled(const std::string& url, bool enabled);

    /**
     * @brief 모든 구독 목록 조회
     */
    [[nodiscard]] std::vector<FilterSubscription> getSubscriptions() const;

    /**
     * @brief 모든 구독 업데이트 (다운로드 + 리로드)
     * @return 업데이트된 리스트 수
     */
    int updateAllSubscriptions();

    /**
     * @brief 다운로드 콜백 설정
     */
    void setDownloadCallback(DownloadCallback callback);

    // ============================
    // 통계
    // ============================

    /**
     * @brief 총 차단 수
     */
    [[nodiscard]] uint64_t totalBlocked() const;

    /**
     * @brief 도메인별 차단 수
     */
    [[nodiscard]] std::unordered_map<std::string, uint64_t> blockedPerDomain() const;

    /**
     * @brief 페이지별 차단 수
     */
    [[nodiscard]] std::unordered_map<std::string, uint64_t> blockedPerPage() const;

    /**
     * @brief 총 네트워크 룰 수
     */
    [[nodiscard]] int totalNetworkRuleCount() const;

    /**
     * @brief 총 코스메틱 룰 수
     */
    [[nodiscard]] int totalCosmeticRuleCount() const;

    /**
     * @brief 통계 초기화
     */
    void resetStats();

    /**
     * @brief 페이지별 차단 수 기록
     */
    void recordPageBlock(const std::string& page_url) const;

private:
    bool enabled_{true};
    std::string data_dir_;

    // 파서
    FilterParser parser_;
    CosmeticFilter cosmetic_filter_;

    // 네트워크 룰
    mutable std::mutex rules_mutex_;
    std::vector<NetworkRule> network_rules_;         ///< 차단 룰
    std::vector<NetworkRule> exception_rules_;        ///< 예외 룰 (@@)

    // 도메인 인덱스 (빠른 룩업)
    std::unordered_map<std::string, std::vector<size_t>> domain_index_;
    std::unordered_map<std::string, std::vector<size_t>> exception_domain_index_;

    // 구독 목록
    mutable std::mutex subscriptions_mutex_;
    std::vector<FilterSubscription> subscriptions_;

    // 통계
    mutable BlockingStats stats_;

    // 콜백
    DownloadCallback download_callback_;

    /**
     * @brief URL에서 도메인 추출
     */
    static std::string extractDomain(const std::string& url);

    /**
     * @brief 서드파티 요청인지 확인
     */
    static bool isThirdParty(const std::string& request_url,
                              const std::string& page_url);

    /**
     * @brief 등록 가능 도메인 추출
     */
    static std::string getRegistrableDomain(const std::string& domain);

    /**
     * @brief 룰이 요청에 매칭되는지 확인
     */
    static bool ruleMatches(const NetworkRule& rule,
                             const std::string& url,
                             const std::string& page_domain,
                             ResourceType res_type,
                             bool is_third_party);

    /**
     * @brief 인덱스 재구축
     */
    void rebuildIndices();

    /**
     * @brief 내장 필터 리스트 로드
     */
    void loadBuiltinRules();
};

} // namespace ordinal::adblock
