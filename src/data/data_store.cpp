/**
 * @file data_store.cpp
 * @brief SQLite 데이터 저장소 구현
 * 
 * SQLite3 래핑, PBKDF2 + AES-256-GCM 암호화, 마이그레이션 시스템,
 * 백업/복원/JSON 내보내기·가져오기 전체 구현.
 */

#include "data_store.h"

#include <sqlite3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <filesystem>

namespace ordinal::data {

// ============================================================
// 생성자 / 소멸자
// ============================================================

DataStore::DataStore() = default;

DataStore::~DataStore() {
    close();
}

// ============================================================
// 초기화 / 종료
// ============================================================

bool DataStore::open(const std::string& db_path, const EncryptionConfig& encryption) {
    std::lock_guard lock(mutex_);

    // 이미 열려있으면 먼저 닫기
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }

    db_path_ = db_path;
    encryption_config_ = encryption;

    // 부모 디렉터리 생성
    auto parent = std::filesystem::path(db_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // SQLite 열기 (WAL 모드)
    int rc = sqlite3_open_v2(
        db_path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    if (rc != SQLITE_OK) {
        setError("sqlite3_open_v2");
        db_ = nullptr;
        return false;
    }

    // WAL 모드 활성화 (성능 향상)
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    // 외래키 제약 활성화
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    // 동기화 모드 (NORMAL — 안전 + 성능 타협)
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    // 바쁜 시 5초 재시도
    sqlite3_busy_timeout(db_, 5000);

    // 암호화 키 파생 (필요 시)
    if (encryption_config_.enabled && !encryption_config_.master_password.empty()) {
        deriveKey();
    }

    return true;
}

void DataStore::close() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    derived_key_.clear();
}

bool DataStore::isOpen() const {
    return db_ != nullptr;
}

std::string DataStore::lastError() const {
    return last_error_;
}

// ============================================================
// 쿼리 실행
// ============================================================

int DataStore::execute(const std::string& sql, const std::vector<DbValue>& params) {
    std::lock_guard lock(mutex_);
    if (!db_) {
        last_error_ = "데이터베이스가 열려있지 않습니다";
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        setError("prepare[execute]");
        return -1;
    }

    if (!bindParams(stmt, params)) {
        sqlite3_finalize(stmt);
        return -1;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        setError("step[execute]");
        return -1;
    }

    return sqlite3_changes(db_);
}

DbResultSet DataStore::query(const std::string& sql, const std::vector<DbValue>& params) {
    std::lock_guard lock(mutex_);
    DbResultSet results;

    if (!db_) {
        last_error_ = "데이터베이스가 열려있지 않습니다";
        return results;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        setError("prepare[query]");
        return results;
    }

    if (!bindParams(stmt, params)) {
        sqlite3_finalize(stmt);
        return results;
    }

    // 행 순회
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        results.push_back(extractRow(stmt));
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        setError("step[query]");
    }

    return results;
}

std::optional<DbValue> DataStore::queryScalar(const std::string& sql,
                                               const std::vector<DbValue>& params) {
    auto rows = query(sql, params);
    if (rows.empty()) return std::nullopt;

    // 첫 번째 행의 첫 번째 열
    auto& row = rows.front();
    if (row.empty()) return std::nullopt;

    return row.begin()->second;
}

int64_t DataStore::lastInsertRowId() const {
    if (!db_) return -1;
    return sqlite3_last_insert_rowid(db_);
}

// ============================================================
// 트랜잭션
// ============================================================

bool DataStore::beginTransaction() {
    return execute("BEGIN TRANSACTION") >= 0;
}

bool DataStore::commit() {
    return execute("COMMIT") >= 0;
}

bool DataStore::rollback() {
    return execute("ROLLBACK") >= 0;
}

bool DataStore::transaction(const std::function<bool()>& func) {
    if (!beginTransaction()) return false;

    try {
        if (func()) {
            return commit();
        } else {
            rollback();
            return false;
        }
    } catch (const std::exception& e) {
        last_error_ = std::string("트랜잭션 예외: ") + e.what();
        rollback();
        return false;
    }
}

// ============================================================
// 마이그레이션
// ============================================================

void DataStore::registerMigration(const Migration& migration) {
    migrations_.push_back(migration);
    // 버전 순서대로 정렬
    std::sort(migrations_.begin(), migrations_.end(),
              [](const Migration& a, const Migration& b) {
                  return a.version < b.version;
              });
}

int DataStore::runMigrations() {
    if (!db_) {
        last_error_ = "데이터베이스가 열려있지 않습니다";
        return -1;
    }

    ensureMigrationTable();

    int current = currentSchemaVersion();
    int applied = 0;

    for (const auto& mig : migrations_) {
        if (mig.version <= current) continue;

        // 트랜잭션 내에서 마이그레이션 적용
        bool ok = transaction([&]() -> bool {
            // SQL 실행 (세미콜론으로 분리된 여러 구문 지원)
            char* err_msg = nullptr;
            int rc = sqlite3_exec(db_, mig.up_sql.c_str(), nullptr, nullptr, &err_msg);
            if (rc != SQLITE_OK) {
                last_error_ = std::string("마이그레이션 ") + std::to_string(mig.version)
                              + " 실패: " + (err_msg ? err_msg : "알 수 없는 오류");
                if (err_msg) sqlite3_free(err_msg);
                return false;
            }

            // 마이그레이션 기록 저장
            std::string insert_sql =
                "INSERT INTO _migrations (version, name, applied_at) VALUES (?, ?, datetime('now'))";
            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, insert_sql.c_str(),
                                     static_cast<int>(insert_sql.size()), &stmt, nullptr);
            if (rc != SQLITE_OK) {
                setError("prepare[migration_record]");
                return false;
            }

            sqlite3_bind_int(stmt, 1, mig.version);
            sqlite3_bind_text(stmt, 2, mig.name.c_str(),
                              static_cast<int>(mig.name.size()), SQLITE_TRANSIENT);

            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            return rc == SQLITE_DONE;
        });

        if (!ok) return -1;
        ++applied;
    }

    return applied;
}

int DataStore::currentSchemaVersion() const {
    if (!db_) return 0;

    // _migrations 테이블 존재 여부 확인
    sqlite3_stmt* stmt = nullptr;
    const char* check_sql =
        "SELECT name FROM sqlite_master WHERE type='table' AND name='_migrations'";
    int rc = sqlite3_prepare_v2(db_, check_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_ROW) return 0;

    // 최대 버전 조회
    const char* ver_sql = "SELECT MAX(version) FROM _migrations";
    rc = sqlite3_prepare_v2(db_, ver_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    rc = sqlite3_step(stmt);
    int version = 0;
    if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return version;
}

bool DataStore::rollbackTo(int target_version) {
    if (!db_) return false;

    int current = currentSchemaVersion();

    // 높은 버전부터 역순으로 롤백
    for (auto it = migrations_.rbegin(); it != migrations_.rend(); ++it) {
        if (it->version <= target_version) break;
        if (it->version > current) continue;

        if (it->down_sql.empty()) {
            last_error_ = "마이그레이션 v" + std::to_string(it->version) + " 롤백 SQL이 없습니다";
            return false;
        }

        bool ok = transaction([&]() -> bool {
            char* err_msg = nullptr;
            int rc = sqlite3_exec(db_, it->down_sql.c_str(), nullptr, nullptr, &err_msg);
            if (rc != SQLITE_OK) {
                last_error_ = std::string("롤백 v") + std::to_string(it->version)
                              + " 실패: " + (err_msg ? err_msg : "");
                if (err_msg) sqlite3_free(err_msg);
                return false;
            }

            // 기록 삭제
            std::string del_sql = "DELETE FROM _migrations WHERE version = ?";
            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, del_sql.c_str(),
                                     static_cast<int>(del_sql.size()), &stmt, nullptr);
            if (rc != SQLITE_OK) return false;

            sqlite3_bind_int(stmt, 1, it->version);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            return rc == SQLITE_DONE;
        });

        if (!ok) return false;
    }

    return true;
}

void DataStore::ensureMigrationTable() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS _migrations (
            version     INTEGER PRIMARY KEY,
            name        TEXT NOT NULL,
            applied_at  TEXT NOT NULL
        )
    )SQL";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

// ============================================================
// 암호화
// ============================================================

void DataStore::deriveKey() const {
    derived_key_.resize(encryption_config_.key_length);

    // 고정 솔트 (DB 파일 경로 기반 해시 — 재현 가능)
    std::vector<uint8_t> salt(encryption_config_.salt_length);
    // SHA-256(db_path)을 솔트로 사용 (고정, 재현 가능)
    unsigned int hash_len = 0;
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(md_ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(md_ctx, db_path_.data(), db_path_.size());
    std::vector<uint8_t> hash(32);
    EVP_DigestFinal_ex(md_ctx, hash.data(), &hash_len);
    EVP_MD_CTX_free(md_ctx);

    std::memcpy(salt.data(), hash.data(),
                std::min(static_cast<size_t>(encryption_config_.salt_length), hash.size()));

    // PBKDF2-HMAC-SHA256
    PKCS5_PBKDF2_HMAC(
        encryption_config_.master_password.c_str(),
        static_cast<int>(encryption_config_.master_password.size()),
        salt.data(), static_cast<int>(salt.size()),
        encryption_config_.pbkdf2_iterations,
        EVP_sha256(),
        encryption_config_.key_length,
        derived_key_.data()
    );
}

std::vector<uint8_t> DataStore::encrypt(const std::vector<uint8_t>& plaintext) const {
    if (derived_key_.empty()) {
        // 키가 없으면 암호화 불가 — 평문 반환
        return plaintext;
    }

    // 랜덤 IV 생성
    std::vector<uint8_t> iv(encryption_config_.iv_length);
    RAND_bytes(iv.data(), encryption_config_.iv_length);

    // 암호문 + 태그 버퍼 할당
    std::vector<uint8_t> ciphertext(plaintext.size() + 16); // AES 블록 여유분
    std::vector<uint8_t> tag(encryption_config_.tag_length);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    int len = 0;
    int ciphertext_len = 0;

    // AES-256-GCM 초기화
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, encryption_config_.iv_length, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, derived_key_.data(), iv.data());

    // 암호화
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                      plaintext.data(), static_cast<int>(plaintext.size()));
    ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;

    // 인증 태그 추출
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, encryption_config_.tag_length, tag.data());
    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(ciphertext_len);

    // 결과: [iv || tag || ciphertext]
    std::vector<uint8_t> result;
    result.reserve(iv.size() + tag.size() + ciphertext.size());
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), tag.begin(), tag.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());

    return result;
}

std::vector<uint8_t> DataStore::decrypt(const std::vector<uint8_t>& data) const {
    if (derived_key_.empty()) {
        return data; // 키 없으면 그대로 반환
    }

    const int iv_len = encryption_config_.iv_length;
    const int tag_len = encryption_config_.tag_length;
    const int header_len = iv_len + tag_len;

    if (static_cast<int>(data.size()) < header_len) {
        return {}; // 데이터 너무 짧음
    }

    // 분리: [iv || tag || ciphertext]
    std::vector<uint8_t> iv(data.begin(), data.begin() + iv_len);
    std::vector<uint8_t> tag(data.begin() + iv_len, data.begin() + header_len);
    std::vector<uint8_t> ciphertext(data.begin() + header_len, data.end());

    std::vector<uint8_t> plaintext(ciphertext.size());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    int len = 0;
    int plaintext_len = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, derived_key_.data(), iv.data());

    EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                      ciphertext.data(), static_cast<int>(ciphertext.size()));
    plaintext_len = len;

    // 인증 태그 설정 및 검증
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len,
                        const_cast<uint8_t*>(tag.data()));

    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        // 인증 실패 (변조 감지)
        return {};
    }

    plaintext_len += len;
    plaintext.resize(plaintext_len);

    return plaintext;
}

std::string DataStore::encryptString(const std::string& plaintext) const {
    std::vector<uint8_t> data(plaintext.begin(), plaintext.end());
    auto encrypted = encrypt(data);
    return base64Encode(encrypted);
}

std::string DataStore::decryptString(const std::string& ciphertext_b64) const {
    auto data = base64Decode(ciphertext_b64);
    auto decrypted = decrypt(data);
    return std::string(decrypted.begin(), decrypted.end());
}

// ============================================================
// 백업 / 내보내기
// ============================================================

bool DataStore::backup(const std::string& backup_path) {
    std::lock_guard lock(mutex_);
    if (!db_) {
        last_error_ = "데이터베이스가 열려있지 않습니다";
        return false;
    }

    sqlite3* backup_db = nullptr;
    int rc = sqlite3_open(backup_path.c_str(), &backup_db);
    if (rc != SQLITE_OK) {
        last_error_ = "백업 DB 열기 실패: " + std::string(sqlite3_errmsg(backup_db));
        sqlite3_close(backup_db);
        return false;
    }

    sqlite3_backup* bk = sqlite3_backup_init(backup_db, "main", db_, "main");
    if (!bk) {
        last_error_ = "백업 초기화 실패: " + std::string(sqlite3_errmsg(backup_db));
        sqlite3_close(backup_db);
        return false;
    }

    // 전체 페이지 복사 (-1 = 한 번에 전부)
    rc = sqlite3_backup_step(bk, -1);
    sqlite3_backup_finish(bk);
    sqlite3_close(backup_db);

    if (rc != SQLITE_DONE) {
        last_error_ = "백업 단계 실패, rc=" + std::to_string(rc);
        return false;
    }

    return true;
}

bool DataStore::restore(const std::string& backup_path) {
    std::lock_guard lock(mutex_);
    if (!db_) {
        last_error_ = "데이터베이스가 열려있지 않습니다";
        return false;
    }

    sqlite3* backup_db = nullptr;
    int rc = sqlite3_open(backup_path.c_str(), &backup_db);
    if (rc != SQLITE_OK) {
        last_error_ = "백업 파일 열기 실패";
        sqlite3_close(backup_db);
        return false;
    }

    // backup_db → 현재 db로 복사 (복원)
    sqlite3_backup* bk = sqlite3_backup_init(db_, "main", backup_db, "main");
    if (!bk) {
        last_error_ = "복원 초기화 실패";
        sqlite3_close(backup_db);
        return false;
    }

    rc = sqlite3_backup_step(bk, -1);
    sqlite3_backup_finish(bk);
    sqlite3_close(backup_db);

    return rc == SQLITE_DONE;
}

bool DataStore::exportToJson(const std::string& output_path,
                              const std::vector<std::string>& tables) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        last_error_ = "출력 파일 열기 실패: " + output_path;
        return false;
    }

    // 내보낼 테이블 결정
    std::vector<std::string> target_tables = tables;
    if (target_tables.empty()) {
        // 모든 사용자 테이블 조회
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT name FROM sqlite_master WHERE type='table' "
                          "AND name NOT LIKE 'sqlite_%' AND name != '_migrations'";
        sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            target_tables.emplace_back(
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    // JSON 수동 구성 (외부 라이브러리 미사용)
    ofs << "{\n";
    for (size_t t = 0; t < target_tables.size(); ++t) {
        const auto& table = target_tables[t];
        ofs << "  \"" << table << "\": [\n";

        std::string select_sql = "SELECT * FROM \"" + table + "\"";
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, select_sql.c_str(), -1, &stmt, nullptr);

        int col_count = sqlite3_column_count(stmt);
        bool first_row = true;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first_row) ofs << ",\n";
            first_row = false;

            ofs << "    {";
            for (int c = 0; c < col_count; ++c) {
                if (c > 0) ofs << ", ";
                const char* col_name = sqlite3_column_name(stmt, c);
                ofs << "\"" << col_name << "\": ";

                int type = sqlite3_column_type(stmt, c);
                switch (type) {
                    case SQLITE_NULL:
                        ofs << "null";
                        break;
                    case SQLITE_INTEGER:
                        ofs << sqlite3_column_int64(stmt, c);
                        break;
                    case SQLITE_FLOAT:
                        ofs << sqlite3_column_double(stmt, c);
                        break;
                    case SQLITE_TEXT: {
                        auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
                        // JSON 이스케이프 (간이)
                        std::string escaped;
                        for (const char* p = text; *p; ++p) {
                            switch (*p) {
                                case '"':  escaped += "\\\""; break;
                                case '\\': escaped += "\\\\"; break;
                                case '\n': escaped += "\\n";  break;
                                case '\r': escaped += "\\r";  break;
                                case '\t': escaped += "\\t";  break;
                                default:   escaped += *p;     break;
                            }
                        }
                        ofs << "\"" << escaped << "\"";
                        break;
                    }
                    case SQLITE_BLOB: {
                        // Base64 인코딩
                        auto blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, c));
                        int blob_size = sqlite3_column_bytes(stmt, c);
                        std::vector<uint8_t> blob_data(blob, blob + blob_size);
                        ofs << "\"" << base64Encode(blob_data) << "\"";
                        break;
                    }
                }
            }
            ofs << "}";
        }

        sqlite3_finalize(stmt);
        ofs << "\n  ]";
        if (t + 1 < target_tables.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "}\n";

    return true;
}

bool DataStore::importFromJson(const std::string& input_path) {
    // JSON 가져오기는 간이 구현 (테이블별 INSERT)
    // 실제로는 JSON 파서가 필요하지만, 간단한 구조만 처리
    std::ifstream ifs(input_path);
    if (!ifs.is_open()) {
        last_error_ = "입력 파일 열기 실패: " + input_path;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    // 간이 JSON 파서: 테이블명과 행 데이터를 추출
    // 본격 파서 없이 단순 구조(exportToJson 출력) 한정 지원
    // 키-값 쌍을 추출하여 INSERT 구문 생성

    // 테이블별 처리 — 중괄호 블록 파싱
    size_t pos = 0;
    while (pos < content.size()) {
        // "table_name": [ 패턴 찾기
        auto quote1 = content.find('"', pos);
        if (quote1 == std::string::npos) break;
        auto quote2 = content.find('"', quote1 + 1);
        if (quote2 == std::string::npos) break;

        std::string table_name = content.substr(quote1 + 1, quote2 - quote1 - 1);

        // [ 찾기
        auto bracket = content.find('[', quote2);
        if (bracket == std::string::npos) break;

        // 대응하는 ] 찾기 (중첩 미고려 — 단순)
        int depth = 1;
        size_t end = bracket + 1;
        while (end < content.size() && depth > 0) {
            if (content[end] == '[') ++depth;
            else if (content[end] == ']') --depth;
            ++end;
        }

        // 각 {} 행 추출 후 INSERT
        size_t row_pos = bracket + 1;
        while (row_pos < end) {
            auto obj_start = content.find('{', row_pos);
            if (obj_start == std::string::npos || obj_start >= end) break;

            auto obj_end = content.find('}', obj_start);
            if (obj_end == std::string::npos) break;

            // 행 내의 키-값 추출
            std::string obj = content.substr(obj_start + 1, obj_end - obj_start - 1);
            std::vector<std::string> columns;
            std::vector<std::string> values;

            size_t kv_pos = 0;
            while (kv_pos < obj.size()) {
                auto kq1 = obj.find('"', kv_pos);
                if (kq1 == std::string::npos) break;
                auto kq2 = obj.find('"', kq1 + 1);
                if (kq2 == std::string::npos) break;

                std::string key = obj.substr(kq1 + 1, kq2 - kq1 - 1);
                columns.push_back("\"" + key + "\"");

                // 콜론 건너뛰기
                auto colon = obj.find(':', kq2);
                if (colon == std::string::npos) break;

                // 값 파싱 (공백 건너뛰기)
                size_t val_start = colon + 1;
                while (val_start < obj.size() && obj[val_start] == ' ') ++val_start;

                if (val_start >= obj.size()) break;

                std::string value;
                if (obj[val_start] == '"') {
                    // 문자열 값
                    auto vq_end = obj.find('"', val_start + 1);
                    // 이스케이프된 따옴표 처리
                    while (vq_end != std::string::npos && vq_end > 0 && obj[vq_end - 1] == '\\') {
                        vq_end = obj.find('"', vq_end + 1);
                    }
                    if (vq_end == std::string::npos) break;
                    value = "'" + obj.substr(val_start + 1, vq_end - val_start - 1) + "'";
                    kv_pos = vq_end + 1;
                } else if (obj.substr(val_start, 4) == "null") {
                    value = "NULL";
                    kv_pos = val_start + 4;
                } else {
                    // 숫자
                    auto num_end = obj.find_first_of(",}", val_start);
                    if (num_end == std::string::npos) num_end = obj.size();
                    value = obj.substr(val_start, num_end - val_start);
                    // 공백 제거
                    while (!value.empty() && value.back() == ' ') value.pop_back();
                    kv_pos = num_end;
                }

                values.push_back(value);

                // 다음 쉼표 건너뛰기
                auto next_comma = obj.find(',', kv_pos);
                if (next_comma == std::string::npos) break;
                kv_pos = next_comma + 1;
            }

            if (!columns.empty()) {
                std::ostringstream sql;
                sql << "INSERT OR REPLACE INTO \"" << table_name << "\" (";
                for (size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) sql << ", ";
                    sql << columns[i];
                }
                sql << ") VALUES (";
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i > 0) sql << ", ";
                    sql << values[i];
                }
                sql << ")";

                execute(sql.str());
            }

            row_pos = obj_end + 1;
        }

        pos = end;
    }

    return true;
}

bool DataStore::vacuum() {
    return execute("VACUUM") >= 0;
}

// ============================================================
// 내부 헬퍼
// ============================================================

bool DataStore::bindParams(sqlite3_stmt* stmt, const std::vector<DbValue>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        int idx = static_cast<int>(i + 1); // SQLite 바인딩은 1-기반

        const auto& val = params[i];
        int rc = SQLITE_OK;

        if (std::holds_alternative<std::nullptr_t>(val)) {
            rc = sqlite3_bind_null(stmt, idx);
        } else if (std::holds_alternative<int64_t>(val)) {
            rc = sqlite3_bind_int64(stmt, idx, std::get<int64_t>(val));
        } else if (std::holds_alternative<double>(val)) {
            rc = sqlite3_bind_double(stmt, idx, std::get<double>(val));
        } else if (std::holds_alternative<std::string>(val)) {
            const auto& s = std::get<std::string>(val);
            rc = sqlite3_bind_text(stmt, idx, s.c_str(),
                                    static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else if (std::holds_alternative<std::vector<uint8_t>>(val)) {
            const auto& blob = std::get<std::vector<uint8_t>>(val);
            rc = sqlite3_bind_blob(stmt, idx, blob.data(),
                                    static_cast<int>(blob.size()), SQLITE_TRANSIENT);
        }

        if (rc != SQLITE_OK) {
            setError("bind[" + std::to_string(idx) + "]");
            return false;
        }
    }
    return true;
}

DbRow DataStore::extractRow(sqlite3_stmt* stmt) {
    DbRow row;
    int col_count = sqlite3_column_count(stmt);

    for (int c = 0; c < col_count; ++c) {
        std::string col_name = sqlite3_column_name(stmt, c);
        int type = sqlite3_column_type(stmt, c);

        DbValue value;
        switch (type) {
            case SQLITE_NULL:
                value = nullptr;
                break;
            case SQLITE_INTEGER:
                value = static_cast<int64_t>(sqlite3_column_int64(stmt, c));
                break;
            case SQLITE_FLOAT:
                value = sqlite3_column_double(stmt, c);
                break;
            case SQLITE_TEXT: {
                auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
                value = std::string(text);
                break;
            }
            case SQLITE_BLOB: {
                auto blob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, c));
                int blob_size = sqlite3_column_bytes(stmt, c);
                value = std::vector<uint8_t>(blob, blob + blob_size);
                break;
            }
            default:
                value = nullptr;
                break;
        }

        row[col_name] = value;
    }

    return row;
}

void DataStore::setError(const std::string& context) {
    if (db_) {
        last_error_ = context + ": " + sqlite3_errmsg(db_);
    } else {
        last_error_ = context + ": (DB 핸들 없음)";
    }
}

// ============================================================
// Base64 인코딩/디코딩
// ============================================================

std::string DataStore::base64Encode(const std::vector<uint8_t>& data) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                      static_cast<uint32_t>(data[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += table[(n >> 6) & 0x3F];
        result += table[n & 0x3F];
        i += 3;
    }

    if (i + 1 == data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += '=';
        result += '=';
    } else if (i + 2 == data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += table[(n >> 6) & 0x3F];
        result += '=';
    }

    return result;
}

std::vector<uint8_t> DataStore::base64Decode(const std::string& encoded) {
    // 디코딩 테이블 구성
    static constexpr int decode_table[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    uint32_t buf = 0;
    int bits = 0;

    for (char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int val = decode_table[static_cast<uint8_t>(c)];
        if (val < 0) continue;

        buf = (buf << 6) | static_cast<uint32_t>(val);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }

    return result;
}

} // namespace ordinal::data
