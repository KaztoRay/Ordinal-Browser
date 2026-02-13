#pragma once

/**
 * @file history_manager.h
 * @brief 방문 기록 관리자
 * 
 * 웹 방문 기록을 SQLite에 저장하고, 텍스트/날짜 범위 검색,
 * 자주 방문한 사이트, 최근 닫은 탭, 시간대별 삭제를 지원합니다.
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <mutex>
#include <functional>
#include <cstdint>

namespace ordinal::data {

class DataStore;

// ============================================================
// 방문 기록 항목
// ============================================================

/**
 * @brief 방문 기록 항목
 */
struct VisitRecord {
    int64_t id{0};                                      ///< 고유 ID
    std::string url;                                    ///< 방문 URL
    std::string title;                                  ///< 페이지 제목
    std::chrono::system_clock::time_point visit_time;   ///< 방문 시각
    int duration_seconds{0};                            ///< 체류 시간 (초)
    int visit_count{1};                                 ///< 방문 횟수 (URL 기준 누적)
    std::string favicon_url;                            ///< 파비콘 URL
    std::string referrer_url;                           ///< 참조 URL
    bool is_typed{false};                               ///< 주소창 직접 입력 여부
};

/**
 * @brief 자주 방문한 사이트 정보
 */
struct FrequentSite {
    std::string url;
    std::string title;
    std::string favicon_url;
    int total_visits{0};
    int total_duration_seconds{0};
    std::chrono::system_clock::time_point last_visit;
};

/**
 * @brief 최근 닫은 탭 정보
 */
struct ClosedTab {
    int64_t id{0};
    std::string url;
    std::string title;
    std::chrono::system_clock::time_point closed_at;
    int tab_index{0};       ///< 닫힌 당시의 탭 인덱스
};

/**
 * @brief 삭제 시간 범위
 */
enum class ClearTimeRange {
    LastHour,       ///< 최근 1시간
    Last24Hours,    ///< 최근 24시간
    Last7Days,      ///< 최근 7일
    Last30Days,     ///< 최근 30일
    AllTime         ///< 전체
};

/**
 * @brief 검색 필터
 */
struct HistorySearchFilter {
    std::string text_query;                                     ///< 텍스트 검색어
    std::optional<std::chrono::system_clock::time_point> from;  ///< 시작 일시
    std::optional<std::chrono::system_clock::time_point> to;    ///< 종료 일시
    int max_results{100};                                       ///< 최대 결과 수
    int offset{0};                                              ///< 오프셋 (페이징)
    bool typed_only{false};                                     ///< 주소창 입력만
};

// ============================================================
// HistoryManager
// ============================================================

/**
 * @brief 방문 기록 관리자
 * 
 * 방문 기록 저장/검색/삭제 + 자주 방문한 사이트 + 최근 닫은 탭 관리.
 */
class HistoryManager {
public:
    explicit HistoryManager(std::shared_ptr<DataStore> store);
    ~HistoryManager();

    /**
     * @brief 초기화 (테이블 생성)
     */
    bool initialize();

    // ============================
    // 방문 기록
    // ============================

    /**
     * @brief 방문 기록 추가
     * @param url 방문 URL
     * @param title 페이지 제목
     * @param referrer 참조 URL
     * @param is_typed 주소창 직접 입력 여부
     * @return 기록 ID
     */
    int64_t recordVisit(const std::string& url, const std::string& title,
                        const std::string& referrer = "", bool is_typed = false);

    /**
     * @brief 체류 시간 업데이트 (탭 전환/닫기 시 호출)
     * @param record_id 기록 ID
     * @param duration_seconds 체류 시간 (초)
     */
    bool updateDuration(int64_t record_id, int duration_seconds);

    /**
     * @brief URL의 제목 업데이트 (페이지 로드 완료 후)
     */
    bool updateTitle(int64_t record_id, const std::string& title);

    // ============================
    // 검색
    // ============================

    /**
     * @brief 텍스트 검색 (제목 + URL)
     * @param query 검색어
     * @param max_results 최대 결과 수
     * @return 방문 기록 목록
     */
    std::vector<VisitRecord> search(const std::string& query, int max_results = 100) const;

    /**
     * @brief 필터 기반 고급 검색
     */
    std::vector<VisitRecord> searchWithFilter(const HistorySearchFilter& filter) const;

    /**
     * @brief 날짜 범위 조회
     * @param from 시작 일시
     * @param to 종료 일시
     */
    std::vector<VisitRecord> getByDateRange(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) const;

    /**
     * @brief 최근 방문 기록 조회
     * @param count 조회할 수
     */
    std::vector<VisitRecord> getRecent(int count = 50) const;

    /**
     * @brief URL 존재 여부 확인
     */
    [[nodiscard]] bool hasVisited(const std::string& url) const;

    /**
     * @brief URL별 총 방문 횟수
     */
    [[nodiscard]] int getVisitCount(const std::string& url) const;

    // ============================
    // 자주 방문한 사이트
    // ============================

    /**
     * @brief 자주 방문한 사이트 상위 N개
     * @param count 개수
     */
    std::vector<FrequentSite> getMostVisited(int count = 10) const;

    // ============================
    // 최근 닫은 탭
    // ============================

    /**
     * @brief 닫은 탭 기록
     */
    void recordClosedTab(const std::string& url, const std::string& title, int tab_index);

    /**
     * @brief 최근 닫은 탭 목록
     * @param count 개수
     */
    std::vector<ClosedTab> getRecentlyClosed(int count = 20) const;

    /**
     * @brief 최근 닫은 탭 기록 초기화
     */
    void clearClosedTabs();

    // ============================
    // 삭제
    // ============================

    /**
     * @brief 시간 범위별 기록 삭제
     * @param range 삭제 시간 범위
     * @return 삭제된 행 수
     */
    int clearByTimeRange(ClearTimeRange range);

    /**
     * @brief 특정 URL의 기록 삭제
     */
    bool removeByUrl(const std::string& url);

    /**
     * @brief 특정 ID의 기록 삭제
     */
    bool removeById(int64_t id);

    /**
     * @brief 전체 기록 삭제
     */
    bool clearAll();

    // ============================
    // 통계
    // ============================

    /**
     * @brief 전체 방문 기록 수
     */
    [[nodiscard]] int64_t totalRecords() const;

    /**
     * @brief 오늘 방문한 고유 URL 수
     */
    [[nodiscard]] int todayUniqueVisits() const;

private:
    std::shared_ptr<DataStore> store_;
    mutable std::mutex mutex_;

    /**
     * @brief DB 행 → VisitRecord 변환
     */
    VisitRecord rowToRecord(
        const std::unordered_map<std::string,
            std::variant<std::nullptr_t, int64_t, double, std::string, std::vector<uint8_t>>>& row) const;

    /**
     * @brief 시간 범위 → SQL WHERE 절
     */
    std::string timeRangeToSql(ClearTimeRange range) const;

    /**
     * @brief 현재 시간 문자열 (SQLite 형식)
     */
    static std::string nowString();

    /**
     * @brief time_point → SQLite 문자열
     */
    static std::string timeToString(std::chrono::system_clock::time_point tp);
};

} // namespace ordinal::data
