#pragma once

/**
 * @file data_store.h
 * @brief SQLite 데이터 저장소 래퍼
 * 
 * SQLite3 래핑 + 마이그레이션 시스템 + AES-256 암호화 + 백업/내보내기.
 * 모든 데이터 매니저가 이 클래스를 통해 DB에 접근합니다.
 */

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <variant>
#include <cstdint>

// 전방 선언 (sqlite3)
struct sqlite3;
struct sqlite3_stmt;

namespace ordinal::data {

// ============================================================
// 쿼리 결과 타입
// ============================================================

/// 단일 셀 값 (NULL, 정수, 실수, 문자열, BLOB)
using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string, std::vector<uint8_t>>;

/// 단일 행 (열 이름 → 값)
using DbRow = std::unordered_map<std::string, DbValue>;

/// 쿼리 결과 집합
using DbResultSet = std::vector<DbRow>;

// ============================================================
// 마이그레이션
// ============================================================

/**
 * @brief 스키마 마이그레이션 항목
 */
struct Migration {
    int version;            ///< 마이그레이션 버전 번호
    std::string name;       ///< 마이그레이션 이름
    std::string up_sql;     ///< 적용 SQL
    std::string down_sql;   ///< 롤백 SQL (빈 문자열이면 롤백 불가)
};

// ============================================================
// 암호화 설정
// ============================================================

/**
 * @brief 암호화 설정 (AES-256)
 */
struct EncryptionConfig {
    bool enabled{false};                ///< 암호화 활성화 여부
    std::string master_password;        ///< 마스터 비밀번호
    int pbkdf2_iterations{100000};      ///< PBKDF2 반복 횟수
    int key_length{32};                 ///< 키 길이 (바이트, 256비트)
    int salt_length{16};                ///< 솔트 길이
    int iv_length{12};                  ///< IV(Nonce) 길이 (GCM)
    int tag_length{16};                 ///< 인증 태그 길이
};

// ============================================================
// DataStore 클래스
// ============================================================

/**
 * @brief SQLite 데이터 저장소
 * 
 * 스레드 안전한 SQLite 래퍼로, 마이그레이션 시스템과
 * 선택적 AES-256-GCM 암호화를 제공합니다.
 */
class DataStore {
public:
    DataStore();
    ~DataStore();

    // 복사 금지
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    // ============================
    // 초기화 / 종료
    // ============================

    /**
     * @brief 데이터베이스 열기
     * @param db_path 데이터베이스 파일 경로
     * @param encryption 암호화 설정 (선택)
     * @return 성공 여부
     */
    bool open(const std::string& db_path, const EncryptionConfig& encryption = {});

    /**
     * @brief 데이터베이스 닫기
     */
    void close();

    /**
     * @brief 열려있는지 확인
     */
    [[nodiscard]] bool isOpen() const;

    /**
     * @brief 마지막 에러 메시지
     */
    [[nodiscard]] std::string lastError() const;

    // ============================
    // 쿼리 실행
    // ============================

    /**
     * @brief SQL 실행 (결과 없음 — INSERT, UPDATE, DELETE, DDL)
     * @param sql SQL 문
     * @param params 바인딩 파라미터 (순서대로 ?에 바인딩)
     * @return 영향받은 행 수, 실패 시 -1
     */
    int execute(const std::string& sql, const std::vector<DbValue>& params = {});

    /**
     * @brief SQL 조회 (SELECT)
     * @param sql SQL 문
     * @param params 바인딩 파라미터
     * @return 결과 행 집합
     */
    DbResultSet query(const std::string& sql, const std::vector<DbValue>& params = {});

    /**
     * @brief 단일 스칼라 값 조회
     * @param sql SQL 문
     * @param params 바인딩 파라미터
     * @return 첫 번째 행의 첫 번째 열 값
     */
    std::optional<DbValue> queryScalar(const std::string& sql, const std::vector<DbValue>& params = {});

    /**
     * @brief 마지막 INSERT의 ROWID
     */
    [[nodiscard]] int64_t lastInsertRowId() const;

    // ============================
    // 트랜잭션
    // ============================

    /**
     * @brief 트랜잭션 시작
     */
    bool beginTransaction();

    /**
     * @brief 트랜잭션 커밋
     */
    bool commit();

    /**
     * @brief 트랜잭션 롤백
     */
    bool rollback();

    /**
     * @brief RAII 트랜잭션 실행
     * @param func 트랜잭션 내에서 실행할 함수 (false 반환 시 롤백)
     * @return func의 반환값 (성공/실패)
     */
    bool transaction(const std::function<bool()>& func);

    // ============================
    // 마이그레이션
    // ============================

    /**
     * @brief 마이그레이션 등록
     * @param migration 마이그레이션 항목
     */
    void registerMigration(const Migration& migration);

    /**
     * @brief 등록된 마이그레이션 일괄 적용
     * @return 적용된 마이그레이션 수, 실패 시 -1
     */
    int runMigrations();

    /**
     * @brief 현재 스키마 버전 조회
     */
    [[nodiscard]] int currentSchemaVersion() const;

    /**
     * @brief 특정 버전으로 롤백
     * @param target_version 대상 버전 (0이면 전부 롤백)
     * @return 성공 여부
     */
    bool rollbackTo(int target_version);

    // ============================
    // 암호화
    // ============================

    /**
     * @brief 바이트 배열 AES-256-GCM 암호화
     * @param plaintext 평문 데이터
     * @return 암호문 (salt + iv + tag + ciphertext)
     */
    [[nodiscard]] std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext) const;

    /**
     * @brief 바이트 배열 AES-256-GCM 복호화
     * @param ciphertext 암호문 (salt + iv + tag + ciphertext)
     * @return 평문 데이터, 실패 시 빈 벡터
     */
    [[nodiscard]] std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext) const;

    /**
     * @brief 문자열 암호화 (Base64 인코딩 반환)
     */
    [[nodiscard]] std::string encryptString(const std::string& plaintext) const;

    /**
     * @brief 문자열 복호화 (Base64 디코딩 → 복호화)
     */
    [[nodiscard]] std::string decryptString(const std::string& ciphertext_b64) const;

    // ============================
    // 백업 / 내보내기
    // ============================

    /**
     * @brief 데이터베이스 백업 생성
     * @param backup_path 백업 파일 경로
     * @return 성공 여부
     */
    bool backup(const std::string& backup_path);

    /**
     * @brief 백업으로부터 복원
     * @param backup_path 백업 파일 경로
     * @return 성공 여부
     */
    bool restore(const std::string& backup_path);

    /**
     * @brief JSON 형태로 전체 데이터 내보내기
     * @param output_path 출력 파일 경로
     * @param tables 내보낼 테이블 목록 (비어있으면 전체)
     * @return 성공 여부
     */
    bool exportToJson(const std::string& output_path,
                      const std::vector<std::string>& tables = {});

    /**
     * @brief JSON 파일에서 데이터 가져오기
     * @param input_path 입력 파일 경로
     * @return 성공 여부
     */
    bool importFromJson(const std::string& input_path);

    /**
     * @brief 데이터베이스 VACUUM (최적화)
     */
    bool vacuum();

private:
    sqlite3* db_{nullptr};                      ///< SQLite3 핸들
    std::string db_path_;                       ///< DB 파일 경로
    EncryptionConfig encryption_config_;         ///< 암호화 설정
    std::string last_error_;                    ///< 마지막 에러 메시지
    mutable std::mutex mutex_;                  ///< 스레드 안전 뮤텍스
    std::vector<Migration> migrations_;         ///< 등록된 마이그레이션 목록

    // 파생 키 캐시 (PBKDF2 결과)
    mutable std::vector<uint8_t> derived_key_;  ///< 파생 암호화 키

    /**
     * @brief 마이그레이션 관리 테이블 생성
     */
    void ensureMigrationTable();

    /**
     * @brief PBKDF2로 마스터 비밀번호에서 키 파생
     */
    void deriveKey() const;

    /**
     * @brief 파라미터 바인딩
     */
    bool bindParams(sqlite3_stmt* stmt, const std::vector<DbValue>& params);

    /**
     * @brief stmt에서 행 추출
     */
    DbRow extractRow(sqlite3_stmt* stmt);

    /**
     * @brief 에러 설정
     */
    void setError(const std::string& context);

    /**
     * @brief Base64 인코딩
     */
    static std::string base64Encode(const std::vector<uint8_t>& data);

    /**
     * @brief Base64 디코딩
     */
    static std::vector<uint8_t> base64Decode(const std::string& encoded);
};

} // namespace ordinal::data
