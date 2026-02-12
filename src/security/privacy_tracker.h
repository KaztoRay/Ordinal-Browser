#pragma once

/**
 * @file privacy_tracker.h
 * @brief 프라이버시 추적기 차단 시스템
 * 
 * 웹 추적기(트래커), 핑거프린팅, 쿠키 추적, 픽셀 트래커 등을
 * 탐지하고 차단합니다. EasyList/EasyPrivacy 호환 블록리스트를 지원합니다.
 */

#include "security_agent.h"

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <regex>
#include <mutex>
#include <atomic>

namespace ordinal::security {

/**
 * @brief 추적기 유형
 */
enum class TrackerType {
    Unknown,            ///< 알 수 없음
    AnalyticsTracker,   ///< 분석 추적기 (Google Analytics 등)
    AdTracker,          ///< 광고 추적기
    SocialTracker,      ///< 소셜 미디어 추적기
    CookieTracker,      ///< 서드파티 쿠키 추적
    PixelTracker,       ///< 1x1 픽셀 트래커
    Fingerprinter,      ///< 브라우저 핑거프린팅
    SessionReplay,      ///< 세션 리플레이 (FullStory, Hotjar 등)
    CryptoMiner,        ///< 암호화폐 채굴
    Malicious           ///< 악성 추적기
};

/**
 * @brief 추적기 탐지 결과
 */
struct TrackerInfo {
    TrackerType type{TrackerType::Unknown};
    std::string domain;                 ///< 추적기 도메인
    std::string company;                ///< 운영 회사명
    std::string category;               ///< 카테고리
    int frequency{0};                   ///< 차단 횟수
    bool is_blocked{false};             ///< 차단 여부
};

/**
 * @brief 핑거프린트 기법 유형
 */
enum class FingerprintMethod {
    Canvas,             ///< Canvas 핑거프린팅
    WebGL,              ///< WebGL 핑거프린팅
    AudioContext,       ///< Audio API 핑거프린팅
    FontEnumeration,    ///< 폰트 목록 수집
    ScreenResolution,   ///< 화면 해상도
    TimezoneOffset,     ///< 시간대 정보
    NavigatorProperties,///< navigator 속성 수집
    BatteryAPI,         ///< 배터리 상태
    MediaDevices,       ///< 미디어 디바이스 열거
    WebRTC              ///< WebRTC IP 유출
};

/**
 * @brief 프라이버시 보호 설정
 */
struct PrivacyConfig {
    bool block_trackers{true};              ///< 추적기 차단
    bool block_third_party_cookies{true};   ///< 서드파티 쿠키 차단
    bool block_fingerprinting{true};        ///< 핑거프린팅 차단
    bool block_pixel_trackers{true};        ///< 픽셀 트래커 차단
    bool block_crypto_miners{true};         ///< 암호화폐 채굴 차단
    bool block_session_replay{false};       ///< 세션 리플레이 차단 (기본 비활성)
    bool strict_mode{false};                ///< 엄격 모드 (더 많은 차단, 사이트 오작동 가능)
    std::string custom_blocklist_path;      ///< 사용자 정의 블록리스트 경로
};

/**
 * @brief 프라이버시 통계
 */
struct PrivacyStats {
    std::atomic<uint64_t> total_blocked{0};          ///< 총 차단 수
    std::atomic<uint64_t> trackers_blocked{0};       ///< 추적기 차단 수
    std::atomic<uint64_t> ads_blocked{0};            ///< 광고 차단 수
    std::atomic<uint64_t> fingerprints_blocked{0};   ///< 핑거프린팅 차단 수
    std::atomic<uint64_t> cookies_blocked{0};        ///< 쿠키 차단 수
    std::atomic<uint64_t> miners_blocked{0};         ///< 채굴 차단 수
};

/**
 * @brief 프라이버시 추적기 차단 시스템
 * 
 * 도메인 기반 블록리스트, URL 패턴 매칭, 핑거프린팅 탐지를 통해
 * 사용자의 프라이버시를 보호합니다.
 */
class PrivacyTracker {
public:
    PrivacyTracker();
    ~PrivacyTracker();

    /**
     * @brief 초기화 (블록리스트 로드)
     */
    bool initialize(const PrivacyConfig& config = {});

    /**
     * @brief URL이 추적기인지 확인
     * @param url 확인할 URL
     * @return 추적기 여부
     */
    [[nodiscard]] bool isTracker(const std::string& url) const;

    /**
     * @brief URL의 추적기 유형 판별
     * @param url 확인할 URL
     * @return 추적기 정보 (추적기가 아니면 type=Unknown)
     */
    [[nodiscard]] TrackerInfo identifyTracker(const std::string& url) const;

    /**
     * @brief 요청 차단 여부 판단
     * @param request_url 요청 URL
     * @param page_url 현재 페이지 URL
     * @param resource_type 리소스 타입 (script, image, iframe 등)
     * @return 차단 여부
     */
    [[nodiscard]] bool shouldBlock(
        const std::string& request_url,
        const std::string& page_url,
        const std::string& resource_type = ""
    );

    /**
     * @brief JavaScript 코드에서 핑거프린팅 시도 탐지
     * @param script JavaScript 소스
     * @return 감지된 핑거프린팅 기법 목록
     */
    [[nodiscard]] std::vector<FingerprintMethod> detectFingerprinting(
        const std::string& script
    ) const;

    // ============================
    // 블록리스트 관리
    // ============================

    /**
     * @brief 도메인을 블록리스트에 추가
     */
    void addBlockedDomain(const std::string& domain);

    /**
     * @brief 도메인을 허용목록에 추가
     */
    void addAllowedDomain(const std::string& domain);

    /**
     * @brief 블록리스트에서 도메인 제거
     */
    void removeBlockedDomain(const std::string& domain);

    /**
     * @brief 허용목록에서 도메인 제거
     */
    void removeAllowedDomain(const std::string& domain);

    /**
     * @brief 외부 블록리스트 파일 로드 (EasyList 형식)
     */
    bool loadBlocklist(const std::string& filepath);

    // ============================
    // 통계
    // ============================

    [[nodiscard]] const PrivacyStats& stats() const { return stats_; }

    /**
     * @brief 페이지별 차단 추적기 목록
     */
    [[nodiscard]] std::vector<TrackerInfo> getBlockedTrackersForPage(
        const std::string& page_url
    ) const;

    /**
     * @brief 통계 초기화
     */
    void resetStats();

private:
    /**
     * @brief URL에서 도메인 추출
     */
    [[nodiscard]] std::string extractDomain(const std::string& url) const;

    /**
     * @brief 서드파티 요청인지 확인
     */
    [[nodiscard]] bool isThirdParty(
        const std::string& request_url,
        const std::string& page_url
    ) const;

    /**
     * @brief 등록 가능 도메인 (eTLD+1) 추출
     */
    [[nodiscard]] std::string getRegistrableDomain(const std::string& domain) const;

    /**
     * @brief 1x1 픽셀 트래커 여부 확인
     */
    [[nodiscard]] bool isPixelTracker(
        const std::string& url,
        const std::string& resource_type
    ) const;

    PrivacyConfig config_;

    // 블록리스트 데이터
    std::unordered_set<std::string> blocked_domains_;       ///< 차단 도메인
    std::unordered_set<std::string> allowed_domains_;       ///< 허용 도메인
    std::vector<std::regex> blocked_url_patterns_;          ///< 차단 URL 패턴
    std::unordered_map<std::string, TrackerType> domain_types_; ///< 도메인별 추적기 유형
    std::unordered_map<std::string, std::string> domain_companies_; ///< 도메인별 회사명

    // 핑거프린팅 탐지 패턴
    std::unordered_map<FingerprintMethod, std::vector<std::string>> fingerprint_apis_;

    // 페이지별 차단 기록
    mutable std::mutex page_trackers_mutex_;
    std::unordered_map<std::string, std::vector<TrackerInfo>> page_trackers_;

    // 통계
    PrivacyStats stats_;
};

} // namespace ordinal::security
