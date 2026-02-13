#pragma once

/**
 * @file password_manager.h
 * @brief 비밀번호 관리자
 *
 * AES-256-GCM 암호화 자격증명 저장소, PBKDF2 키 파생,
 * 자동입력 매칭, 비밀번호 생성기, 유출 확인(k-Anonymity).
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace ordinal::data {
class DataStore;
}

namespace ordinal::privacy {

// ============================================================
// 자격증명 구조
// ============================================================

/**
 * @brief 저장된 자격증명 (비밀번호 항목)
 */
struct Credential {
    int64_t id{0};                          ///< DB ID
    std::string origin;                     ///< 출처 URL (scheme + host)
    std::string action_url;                 ///< 로그인 폼 action URL
    std::string username_field;             ///< 사용자명 필드 이름
    std::string password_field;             ///< 비밀번호 필드 이름
    std::string username;                   ///< 사용자명 (평문)
    std::string password;                   ///< 비밀번호 (복호화된 평문)
    std::string note;                       ///< 메모
    std::string created_at;                 ///< 생성 시간 (ISO8601)
    std::string modified_at;                ///< 수정 시간
    std::string last_used_at;              ///< 마지막 사용 시간
    int use_count{0};                       ///< 사용 횟수
    bool compromised{false};                ///< 유출 여부
    bool weak{false};                       ///< 약한 비밀번호 여부
    bool reused{false};                     ///< 재사용 여부
};

/**
 * @brief 비밀번호 강도 등급
 */
enum class PasswordStrength {
    VeryWeak,       ///< 매우 약함 (0-20)
    Weak,           ///< 약함 (21-40)
    Fair,           ///< 보통 (41-60)
    Strong,         ///< 강함 (61-80)
    VeryStrong      ///< 매우 강함 (81-100)
};

/**
 * @brief 비밀번호 강도 분석 결과
 */
struct PasswordStrengthResult {
    int score{0};                           ///< 점수 (0-100)
    PasswordStrength grade{PasswordStrength::VeryWeak};
    std::string feedback;                   ///< 피드백 메시지
    double entropy_bits{0.0};               ///< 엔트로피 (비트)
    int estimated_crack_seconds{0};         ///< 크랙 예상 시간 (초)
    bool has_uppercase{false};
    bool has_lowercase{false};
    bool has_digits{false};
    bool has_symbols{false};
    bool is_common{false};                  ///< 흔한 비밀번호 여부
    int length{0};
};

/**
 * @brief 비밀번호 생성 옵션
 */
struct PasswordGeneratorOptions {
    int length{16};                         ///< 길이
    bool use_uppercase{true};               ///< 대문자 포함
    bool use_lowercase{true};               ///< 소문자 포함
    bool use_digits{true};                  ///< 숫자 포함
    bool use_symbols{true};                 ///< 특수문자 포함
    std::string exclude_chars;              ///< 제외할 문자
    bool exclude_ambiguous{false};          ///< 모호한 문자 제외 (l, 1, I, O, 0)
    int min_uppercase{1};                   ///< 최소 대문자 수
    int min_lowercase{1};                   ///< 최소 소문자 수
    int min_digits{1};                      ///< 최소 숫자 수
    int min_symbols{1};                     ///< 최소 특수문자 수
};

/**
 * @brief 유출 확인 결과
 */
struct BreachCheckResult {
    bool compromised{false};                ///< 유출 여부
    int breach_count{0};                    ///< 유출 횟수
    std::string message;                    ///< 결과 메시지
};

// ============================================================
// 콜백 타입
// ============================================================

/// 마스터 비밀번호 요청 콜백: () → password
using MasterPasswordCallback = std::function<std::string()>;

// ============================================================
// PasswordManager
// ============================================================

/**
 * @brief 비밀번호 관리자
 *
 * AES-256-GCM으로 비밀번호를 암호화하여 SQLite에 저장하고,
 * URL 매칭으로 자동입력, 비밀번호 생성, HIBP API로 유출 확인.
 */
class PasswordManager {
public:
    explicit PasswordManager(std::shared_ptr<ordinal::data::DataStore> store);
    ~PasswordManager();

    PasswordManager(const PasswordManager&) = delete;
    PasswordManager& operator=(const PasswordManager&) = delete;

    /**
     * @brief 초기화 (테이블 생성, 마스터 키 설정)
     * @param master_password 마스터 비밀번호
     * @return 성공 여부
     */
    bool initialize(const std::string& master_password);

    /**
     * @brief 마스터 비밀번호 변경
     * @param old_password 기존 비밀번호
     * @param new_password 새 비밀번호
     * @return 성공 여부
     */
    bool changeMasterPassword(const std::string& old_password,
                               const std::string& new_password);

    /**
     * @brief 마스터 비밀번호 검증
     */
    bool verifyMasterPassword(const std::string& password) const;

    /**
     * @brief 잠금 상태 확인
     */
    [[nodiscard]] bool isLocked() const;

    /**
     * @brief 잠금 해제
     */
    bool unlock(const std::string& master_password);

    /**
     * @brief 잠금 (메모리에서 키 제거)
     */
    void lock();

    // ============================
    // CRUD
    // ============================

    /**
     * @brief 자격증명 저장
     * @return 저장된 Credential ID
     */
    int64_t saveCredential(const Credential& cred);

    /**
     * @brief 자격증명 수정
     */
    bool updateCredential(const Credential& cred);

    /**
     * @brief 자격증명 삭제
     */
    bool deleteCredential(int64_t id);

    /**
     * @brief ID로 자격증명 조회
     */
    std::optional<Credential> getCredential(int64_t id) const;

    /**
     * @brief 전체 자격증명 목록
     */
    std::vector<Credential> getAllCredentials() const;

    // ============================
    // 자동입력 매칭
    // ============================

    /**
     * @brief URL에 매칭되는 자격증명 검색
     * @param url 현재 페이지 URL
     * @return 매칭된 자격증명 목록 (사용 빈도 내림차순)
     */
    std::vector<Credential> findMatchingCredentials(const std::string& url) const;

    /**
     * @brief 자격증명 사용 기록 업데이트
     */
    void recordUsage(int64_t credential_id);

    // ============================
    // 비밀번호 생성
    // ============================

    /**
     * @brief 강력한 비밀번호 생성
     * @param options 생성 옵션
     * @return 생성된 비밀번호
     */
    static std::string generatePassword(
        const PasswordGeneratorOptions& options = {});

    /**
     * @brief 기억하기 쉬운 비밀번호(패스프레이즈) 생성
     * @param word_count 단어 수
     * @param separator 구분자
     * @return 생성된 패스프레이즈
     */
    static std::string generatePassphrase(int word_count = 4,
                                           const std::string& separator = "-");

    // ============================
    // 비밀번호 분석
    // ============================

    /**
     * @brief 비밀번호 강도 분석
     */
    static PasswordStrengthResult analyzeStrength(const std::string& password);

    /**
     * @brief 유출 확인 (HIBP k-Anonymity API)
     *
     * SHA-1 해시의 처음 5자리만 전송하여 프라이버시 보호
     * @param password 확인할 비밀번호
     * @return 유출 확인 결과
     */
    static BreachCheckResult checkBreach(const std::string& password);

    /**
     * @brief 전체 자격증명 유출 일괄 확인
     * @return 유출된 자격증명 수
     */
    int checkAllBreaches();

    /**
     * @brief 재사용된 비밀번호 탐지
     * @return 재사용 그룹 (같은 비밀번호를 쓰는 origin 목록)
     */
    std::vector<std::vector<Credential>> findReusedPasswords() const;

    /**
     * @brief 약한 비밀번호 탐지
     * @return 약한 비밀번호 목록
     */
    std::vector<Credential> findWeakPasswords() const;

    // ============================
    // 내보내기 / 가져오기
    // ============================

    /**
     * @brief CSV로 내보내기 (Chrome 호환 형식)
     */
    bool exportToCsv(const std::string& file_path) const;

    /**
     * @brief CSV에서 가져오기
     * @return 가져온 자격증명 수
     */
    int importFromCsv(const std::string& file_path);

    // ============================
    // 콜백
    // ============================

    /**
     * @brief 마스터 비밀번호 요청 콜백 설정
     */
    void setMasterPasswordCallback(MasterPasswordCallback callback);

private:
    std::shared_ptr<ordinal::data::DataStore> store_;
    mutable std::mutex mutex_;

    // 마스터 키 (PBKDF2 파생)
    std::vector<uint8_t> master_key_;
    std::vector<uint8_t> master_salt_;
    std::string master_hash_;       ///< 마스터 비밀번호 검증용 해시
    bool locked_{true};

    MasterPasswordCallback master_pw_callback_;

    // PBKDF2 파라미터
    static constexpr int PBKDF2_ITERATIONS = 600000;
    static constexpr int KEY_LENGTH = 32;       // 256비트
    static constexpr int SALT_LENGTH = 32;
    static constexpr int IV_LENGTH = 12;        // GCM nonce
    static constexpr int TAG_LENGTH = 16;

    /**
     * @brief PBKDF2로 마스터 키 파생
     */
    void deriveKey(const std::string& password, const std::vector<uint8_t>& salt);

    /**
     * @brief AES-256-GCM 암호화
     */
    std::string encryptField(const std::string& plaintext) const;

    /**
     * @brief AES-256-GCM 복호화
     */
    std::string decryptField(const std::string& ciphertext_b64) const;

    /**
     * @brief URL에서 origin 추출 (scheme://host)
     */
    static std::string extractOrigin(const std::string& url);

    /**
     * @brief URL eTLD+1 추출 (도메인 매칭용)
     */
    static std::string extractDomain(const std::string& url);

    /**
     * @brief SHA-1 해시 (HIBP용)
     */
    static std::string sha1Hex(const std::string& input);

    /**
     * @brief SHA-256 해시
     */
    static std::string sha256Hex(const std::string& input);

    /**
     * @brief 현재 시간 ISO8601
     */
    static std::string nowIso8601();

    /**
     * @brief Base64 인코딩
     */
    static std::string base64Encode(const std::vector<uint8_t>& data);

    /**
     * @brief Base64 디코딩
     */
    static std::vector<uint8_t> base64Decode(const std::string& encoded);
};

} // namespace ordinal::privacy
