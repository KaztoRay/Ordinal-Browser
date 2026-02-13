#pragma once

/**
 * @file bookmark_manager.h
 * @brief 북마크 관리자
 * 
 * 트리 구조(폴더 + 북마크) CRUD, 가져오기/내보내기(HTML/Chrome JSON),
 * 검색, 태그, 정렬(날짜/이름/빈도), SQLite 저장.
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <cstdint>

namespace ordinal::data {

class DataStore;

// ============================================================
// 북마크 노드 타입
// ============================================================

/**
 * @brief 북마크 노드 유형
 */
enum class BookmarkNodeType {
    Folder,     ///< 폴더
    Bookmark,   ///< 북마크 (URL)
    Separator   ///< 구분선
};

/**
 * @brief 정렬 기준
 */
enum class BookmarkSortBy {
    Name,           ///< 이름순
    DateAdded,      ///< 추가일순
    DateModified,   ///< 수정일순
    VisitCount,     ///< 방문 빈도순
    Url             ///< URL 순
};

/**
 * @brief 정렬 방향
 */
enum class SortOrder {
    Ascending,  ///< 오름차순
    Descending  ///< 내림차순
};

// ============================================================
// 북마크 노드
// ============================================================

/**
 * @brief 북마크 트리 노드
 * 
 * 폴더와 북마크를 트리 구조로 표현합니다.
 */
struct BookmarkNode {
    int64_t id{0};                      ///< 고유 ID (DB)
    int64_t parent_id{0};               ///< 부모 폴더 ID (0이면 루트)
    BookmarkNodeType type{BookmarkNodeType::Bookmark};

    std::string title;                  ///< 제목
    std::string url;                    ///< URL (폴더는 비어있음)
    std::string description;            ///< 설명
    std::string favicon_url;            ///< 파비콘 URL

    std::vector<std::string> tags;      ///< 태그 목록

    int position{0};                    ///< 같은 폴더 내 순서
    int visit_count{0};                 ///< 방문 횟수

    std::chrono::system_clock::time_point date_added;      ///< 추가 일시
    std::chrono::system_clock::time_point date_modified;   ///< 수정 일시

    /// 자식 노드 (폴더일 때만 사용)
    std::vector<std::shared_ptr<BookmarkNode>> children;
};

// ============================================================
// 내보내기 형식
// ============================================================

/**
 * @brief 내보내기/가져오기 형식
 */
enum class BookmarkFormat {
    HTML,       ///< Netscape Bookmark HTML 형식
    ChromeJson  ///< Chrome JSON 형식
};

// ============================================================
// BookmarkManager
// ============================================================

/**
 * @brief 북마크 관리자
 * 
 * 트리 구조 북마크를 SQLite에 저장하고, 검색/태그/정렬/내보내기 기능을 제공합니다.
 */
class BookmarkManager {
public:
    /**
     * @brief 생성자
     * @param store 데이터 저장소 (공유)
     */
    explicit BookmarkManager(std::shared_ptr<DataStore> store);
    ~BookmarkManager();

    /**
     * @brief 초기화 (테이블 생성 + 기본 폴더)
     */
    bool initialize();

    // ============================
    // CRUD 작업
    // ============================

    /**
     * @brief 북마크 추가
     * @param parent_id 부모 폴더 ID
     * @param title 제목
     * @param url URL
     * @param tags 태그 목록
     * @return 생성된 노드 ID, 실패 시 -1
     */
    int64_t addBookmark(int64_t parent_id, const std::string& title,
                        const std::string& url,
                        const std::vector<std::string>& tags = {});

    /**
     * @brief 폴더 추가
     * @param parent_id 부모 폴더 ID
     * @param title 폴더 이름
     * @return 생성된 폴더 ID, 실패 시 -1
     */
    int64_t addFolder(int64_t parent_id, const std::string& title);

    /**
     * @brief 구분선 추가
     * @param parent_id 부모 폴더 ID
     * @return 생성된 ID, 실패 시 -1
     */
    int64_t addSeparator(int64_t parent_id);

    /**
     * @brief 노드 수정 (제목, URL, 설명, 태그)
     * @param id 노드 ID
     * @param title 새 제목 (비어있으면 변경 안 함)
     * @param url 새 URL
     * @param description 새 설명
     * @return 성공 여부
     */
    bool updateNode(int64_t id, const std::string& title = "",
                    const std::string& url = "",
                    const std::string& description = "");

    /**
     * @brief 노드 삭제 (폴더면 하위 전부 삭제)
     * @param id 노드 ID
     * @return 성공 여부
     */
    bool removeNode(int64_t id);

    /**
     * @brief 노드 이동 (다른 폴더로)
     * @param id 이동할 노드 ID
     * @param new_parent_id 새 부모 폴더 ID
     * @param position 위치 (-1이면 끝에 추가)
     * @return 성공 여부
     */
    bool moveNode(int64_t id, int64_t new_parent_id, int position = -1);

    // ============================
    // 조회
    // ============================

    /**
     * @brief ID로 노드 조회
     */
    std::shared_ptr<BookmarkNode> getNode(int64_t id) const;

    /**
     * @brief 폴더의 자식 노드 조회
     * @param folder_id 폴더 ID (0이면 루트)
     */
    std::vector<std::shared_ptr<BookmarkNode>> getChildren(int64_t folder_id) const;

    /**
     * @brief 전체 트리 조회 (재귀)
     * @return 루트 노드 (가상 루트)
     */
    std::shared_ptr<BookmarkNode> getTree() const;

    /**
     * @brief 전체 북마크 수
     */
    [[nodiscard]] int64_t totalCount() const;

    /**
     * @brief 방문 횟수 증가
     */
    bool incrementVisitCount(int64_t id);

    // ============================
    // 태그
    // ============================

    /**
     * @brief 태그 추가
     */
    bool addTag(int64_t bookmark_id, const std::string& tag);

    /**
     * @brief 태그 제거
     */
    bool removeTag(int64_t bookmark_id, const std::string& tag);

    /**
     * @brief 노드의 태그 목록 조회
     */
    std::vector<std::string> getTags(int64_t bookmark_id) const;

    /**
     * @brief 모든 사용 중인 태그 조회 (사용 횟수 포함)
     */
    std::vector<std::pair<std::string, int>> getAllTags() const;

    /**
     * @brief 태그로 북마크 검색
     */
    std::vector<std::shared_ptr<BookmarkNode>> findByTag(const std::string& tag) const;

    // ============================
    // 검색 & 정렬
    // ============================

    /**
     * @brief 텍스트 검색 (제목, URL, 설명, 태그)
     * @param query 검색어
     * @param max_results 최대 결과 수 (0이면 제한 없음)
     * @return 매칭 북마크 목록
     */
    std::vector<std::shared_ptr<BookmarkNode>> search(
        const std::string& query, int max_results = 0) const;

    /**
     * @brief 정렬된 자식 노드 조회
     * @param folder_id 폴더 ID
     * @param sort_by 정렬 기준
     * @param order 정렬 방향
     */
    std::vector<std::shared_ptr<BookmarkNode>> getChildrenSorted(
        int64_t folder_id, BookmarkSortBy sort_by,
        SortOrder order = SortOrder::Ascending) const;

    // ============================
    // 가져오기 / 내보내기
    // ============================

    /**
     * @brief 북마크 파일 내보내기
     * @param file_path 출력 파일 경로
     * @param format 내보내기 형식
     * @return 성공 여부
     */
    bool exportBookmarks(const std::string& file_path, BookmarkFormat format) const;

    /**
     * @brief 북마크 파일 가져오기
     * @param file_path 입력 파일 경로
     * @param format 가져오기 형식
     * @param target_folder_id 가져올 폴더 ID (0이면 루트)
     * @return 가져온 북마크 수, 실패 시 -1
     */
    int importBookmarks(const std::string& file_path, BookmarkFormat format,
                        int64_t target_folder_id = 0);

    /**
     * @brief URL이 이미 북마크에 있는지 확인
     */
    [[nodiscard]] bool isBookmarked(const std::string& url) const;

    /**
     * @brief URL로 북마크 검색
     */
    std::shared_ptr<BookmarkNode> findByUrl(const std::string& url) const;

private:
    std::shared_ptr<DataStore> store_;  ///< 데이터 저장소
    mutable std::mutex mutex_;          ///< 스레드 안전 뮤텍스

    // 기본 폴더 ID
    int64_t toolbar_folder_id_{0};      ///< 북마크 바 폴더
    int64_t menu_folder_id_{0};         ///< 메뉴 폴더
    int64_t other_folder_id_{0};        ///< 기타 북마크 폴더

    /**
     * @brief 기본 폴더 생성 (북마크 바, 메뉴, 기타)
     */
    void createDefaultFolders();

    /**
     * @brief DB 행에서 BookmarkNode 구성
     */
    std::shared_ptr<BookmarkNode> rowToNode(
        const std::unordered_map<std::string, 
            std::variant<std::nullptr_t, int64_t, double, std::string, std::vector<uint8_t>>>& row) const;

    /**
     * @brief 폴더 내 다음 위치 번호
     */
    int nextPosition(int64_t parent_id) const;

    /**
     * @brief 트리 재귀 빌드
     */
    void buildTreeRecursive(std::shared_ptr<BookmarkNode>& node) const;

    /**
     * @brief 하위 전부 삭제 (재귀)
     */
    void removeChildrenRecursive(int64_t folder_id);

    /**
     * @brief HTML 내보내기 (Netscape 형식)
     */
    bool exportToHtml(const std::string& file_path) const;

    /**
     * @brief Chrome JSON 내보내기
     */
    bool exportToJson(const std::string& file_path) const;

    /**
     * @brief HTML 가져오기
     */
    int importFromHtml(const std::string& file_path, int64_t target_folder_id);

    /**
     * @brief Chrome JSON 가져오기
     */
    int importFromJson(const std::string& file_path, int64_t target_folder_id);

    /**
     * @brief 태그 문자열 → 벡터
     */
    static std::vector<std::string> parseTags(const std::string& tag_str);

    /**
     * @brief 벡터 → 태그 문자열
     */
    static std::string joinTags(const std::vector<std::string>& tags);
};

} // namespace ordinal::data
