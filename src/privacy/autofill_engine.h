#pragma once

/**
 * @file autofill_engine.h
 * @brief 자동입력 엔진
 *
 * 폼 필드 타입 감지 (input name/type/autocomplete 속성 휴리스틱),
 * 프로필 저장 (암호화 JSON), 자동입력 팝업 데이터 제공,
 * 스마트 필드 매칭 (퍼지 이름 매칭), 새 자격증명 저장 프롬프트.
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace ordinal::data {
class DataStore;
}

namespace ordinal::privacy {

// ============================================================
// 필드 타입 열거
// ============================================================

/**
 * @brief 자동입력 필드 타입
 */
enum class FieldType {
    Unknown,            ///< 알 수 없음
    Username,           ///< 사용자명
    Password,           ///< 비밀번호
    NewPassword,        ///< 새 비밀번호 (회원가입/변경)
    Email,              ///< 이메일 주소
    Phone,              ///< 전화번호
    FullName,           ///< 성명
    FirstName,          ///< 이름
    LastName,           ///< 성
    Address,            ///< 주소 (도로명)
    City,               ///< 도시
    State,              ///< 주/도
    ZipCode,            ///< 우편번호
    Country,            ///< 국가
    CardNumber,         ///< 카드 번호
    CardExpiry,         ///< 카드 유효기간
    CardCVC,            ///< 카드 CVC
    CardHolder,         ///< 카드 소유자명
    Organization,       ///< 조직/회사
    OneTimeCode,        ///< OTP / 인증 코드
};

/**
 * @brief 필드 타입 이름 반환
 */
[[nodiscard]] std::string fieldTypeName(FieldType type);

// ============================================================
// 폼 필드 정보
// ============================================================

/**
 * @brief HTML 폼 필드 정보
 */
struct FormField {
    std::string element_id;             ///< DOM 요소 ID
    std::string name;                   ///< name 속성
    std::string type;                   ///< type 속성 (text, password, email 등)
    std::string autocomplete;           ///< autocomplete 속성
    std::string placeholder;            ///< placeholder 텍스트
    std::string label;                  ///< 연결된 label 텍스트
    std::string aria_label;             ///< aria-label 속성
    int tab_index{-1};                  ///< tabindex
    bool is_visible{true};              ///< 화면에 보이는지
    bool is_readonly{false};            ///< 읽기 전용인지

    FieldType detected_type{FieldType::Unknown};  ///< 감지된 타입
    double confidence{0.0};             ///< 감지 신뢰도 (0.0 ~ 1.0)
};

/**
 * @brief 폼 정보 (여러 필드 그룹)
 */
struct FormInfo {
    std::string form_id;                ///< 폼 요소 ID
    std::string action_url;             ///< 폼 action URL
    std::string method;                 ///< POST/GET
    std::string page_url;               ///< 페이지 URL
    std::vector<FormField> fields;      ///< 필드 목록
    bool is_login_form{false};          ///< 로그인 폼인지
    bool is_signup_form{false};         ///< 회원가입 폼인지
    bool is_payment_form{false};        ///< 결제 폼인지
    bool is_address_form{false};        ///< 주소 입력 폼인지
};

// ============================================================
// 자동입력 프로필
// ============================================================

/**
 * @brief 자동입력 프로필 (개인정보)
 */
struct AutofillProfile {
    int64_t id{0};                      ///< DB ID
    std::string label;                  ///< 프로필 라벨 ("기본", "회사" 등)

    // 인적 정보
    std::string full_name;
    std::string first_name;
    std::string last_name;
    std::string email;
    std::string phone;
    std::string organization;

    // 주소 정보
    std::string address;
    std::string city;
    std::string state;
    std::string zip_code;
    std::string country;

    // 카드 정보 (암호화 저장)
    std::string card_number;
    std::string card_expiry;
    std::string card_cvc;
    std::string card_holder;

    // 메타데이터
    std::string created_at;
    std::string modified_at;
    int use_count{0};
    bool is_default{false};
};

/**
 * @brief 자동입력 제안 항목
 */
struct AutofillSuggestion {
    std::string display_text;           ///< 팝업에 표시할 텍스트
    std::string subtitle;               ///< 부제목 (프로필 라벨 등)
    std::string value;                  ///< 실제 입력 값
    FieldType field_type{FieldType::Unknown};
    int64_t profile_id{0};              ///< 출처 프로필 ID
    double relevance{0.0};              ///< 관련도 점수
};

/**
 * @brief 새 자격증명 저장 프롬프트 정보
 */
struct SavePromptInfo {
    std::string origin;                 ///< 사이트 origin
    std::string username;               ///< 감지된 사용자명
    std::string password;               ///< 감지된 비밀번호
    bool is_update{false};              ///< 기존 자격증명 업데이트인지
    int64_t existing_credential_id{0};  ///< 업데이트 시 기존 ID
};

// ============================================================
// 콜백 타입
// ============================================================

/// 저장 프롬프트 콜백: (SavePromptInfo) → 사용자가 수락했는지
using SavePromptCallback = std::function<bool(const SavePromptInfo&)>;

// ============================================================
// AutofillEngine 클래스
// ============================================================

/**
 * @brief 자동입력 엔진
 *
 * HTML 폼 필드를 분석하여 타입을 감지하고,
 * 저장된 프로필 기반으로 자동입력 제안을 제공합니다.
 * 암호화 JSON 형식으로 프로필을 저장/로드합니다.
 */
class AutofillEngine {
public:
    explicit AutofillEngine(std::shared_ptr<ordinal::data::DataStore> store);
    ~AutofillEngine();

    AutofillEngine(const AutofillEngine&) = delete;
    AutofillEngine& operator=(const AutofillEngine&) = delete;

    /**
     * @brief 초기화 (테이블 생성, 프로필 로드)
     * @return 성공 여부
     */
    bool initialize();

    // ============================
    // 필드 타입 감지
    // ============================

    /**
     * @brief 단일 폼 필드의 타입 감지
     * @param field 폼 필드 정보 (결과가 detected_type에 기록됨)
     * @return 감지된 필드 타입
     */
    FieldType detectFieldType(FormField& field) const;

    /**
     * @brief 폼 전체 분석 (모든 필드 타입 감지 + 폼 유형 판별)
     * @param form 폼 정보 (결과가 각 필드와 폼 유형에 기록됨)
     */
    void analyzeForm(FormInfo& form) const;

    // ============================
    // 자동입력 제안
    // ============================

    /**
     * @brief 특정 필드에 대한 자동입력 제안 생성
     * @param field 대상 필드
     * @param page_url 현재 페이지 URL
     * @param max_suggestions 최대 제안 수
     * @return 제안 목록 (관련도 내림차순)
     */
    std::vector<AutofillSuggestion> getSuggestions(
        const FormField& field,
        const std::string& page_url,
        int max_suggestions = 5) const;

    /**
     * @brief 폼 전체를 채울 데이터 생성
     * @param form 분석된 폼 정보
     * @param profile_id 사용할 프로필 ID (0이면 기본 프로필)
     * @return 필드 element_id → 값 매핑
     */
    std::unordered_map<std::string, std::string> getFormFillData(
        const FormInfo& form,
        int64_t profile_id = 0) const;

    // ============================
    // 프로필 CRUD
    // ============================

    /**
     * @brief 프로필 저장
     * @return 저장된 프로필 ID
     */
    int64_t saveProfile(const AutofillProfile& profile);

    /**
     * @brief 프로필 수정
     */
    bool updateProfile(const AutofillProfile& profile);

    /**
     * @brief 프로필 삭제
     */
    bool deleteProfile(int64_t profile_id);

    /**
     * @brief 프로필 조회
     */
    std::optional<AutofillProfile> getProfile(int64_t profile_id) const;

    /**
     * @brief 전체 프로필 목록
     */
    std::vector<AutofillProfile> getAllProfiles() const;

    /**
     * @brief 기본 프로필 조회
     */
    std::optional<AutofillProfile> getDefaultProfile() const;

    /**
     * @brief 기본 프로필 설정
     */
    bool setDefaultProfile(int64_t profile_id);

    // ============================
    // 자격증명 감지 (폼 제출 시)
    // ============================

    /**
     * @brief 폼 제출 시 자격증명 감지
     * @param form 제출된 폼 정보
     * @param field_values 필드 element_id → 입력된 값
     * @return 저장 프롬프트 정보 (자격증명 감지 시)
     */
    std::optional<SavePromptInfo> detectCredentialSubmission(
        const FormInfo& form,
        const std::unordered_map<std::string, std::string>& field_values) const;

    /**
     * @brief 저장 프롬프트 콜백 설정
     */
    void setSavePromptCallback(SavePromptCallback callback);

    // ============================
    // 프로필 사용 기록
    // ============================

    /**
     * @brief 프로필 사용 기록
     */
    void recordProfileUsage(int64_t profile_id);

private:
    std::shared_ptr<ordinal::data::DataStore> store_;
    mutable std::mutex mutex_;
    SavePromptCallback save_prompt_callback_;

    // 캐시된 프로필 목록
    mutable std::vector<AutofillProfile> cached_profiles_;
    mutable bool profiles_dirty_{true};

    /**
     * @brief 프로필 캐시 새로고침
     */
    void refreshProfileCache() const;

    /**
     * @brief DB 행 → AutofillProfile 변환
     */
    AutofillProfile rowToProfile(
        const std::unordered_map<std::string,
            std::variant<std::nullptr_t, int64_t, double, std::string,
                         std::vector<uint8_t>>>& row) const;

    /**
     * @brief 필드 이름/속성에서 타입 추론 (퍼지 매칭)
     */
    FieldType inferFromAttributes(
        const std::string& name,
        const std::string& type,
        const std::string& autocomplete,
        const std::string& placeholder,
        const std::string& label) const;

    /**
     * @brief 퍼지 문자열 매칭 (Levenshtein 거리 기반)
     */
    static double fuzzyMatch(const std::string& a, const std::string& b);

    /**
     * @brief 문자열이 패턴 목록 중 하나에 매칭되는지
     */
    static bool matchesAny(const std::string& text,
                            const std::vector<std::string>& patterns);

    /**
     * @brief 현재 시간 ISO8601
     */
    static std::string nowIso8601();

    /**
     * @brief 프로필의 특정 필드 타입에 대한 값 반환
     */
    static std::string getProfileValueForField(
        const AutofillProfile& profile, FieldType type);
};

} // namespace ordinal::privacy
