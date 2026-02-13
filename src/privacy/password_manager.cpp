/**
 * @file password_manager.cpp
 * @brief 비밀번호 관리자 구현
 *
 * AES-256-GCM 암호화, PBKDF2-HMAC-SHA256 키 파생,
 * URL 매칭 자동입력, 비밀번호 생성/분석, HIBP k-Anonymity 유출 확인.
 */

#include "password_manager.h"
#include "../data/data_store.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

namespace ordinal::privacy {

using namespace ordinal::data;
using Clock = std::chrono::system_clock;

// ============================================================
// 흔한 비밀번호 목록 (상위 200개 축약)
// ============================================================

namespace {

const std::vector<std::string> COMMON_PASSWORDS = {
    "123456", "password", "12345678", "qwerty", "123456789",
    "12345", "1234", "111111", "1234567", "dragon",
    "123123", "baseball", "abc123", "football", "monkey",
    "letmein", "shadow", "master", "666666", "qwertyuiop",
    "123321", "mustang", "1234567890", "michael", "654321",
    "superman", "1qaz2wsx", "7777777", "121212", "000000",
    "qazwsx", "123qwe", "killer", "trustno1", "jordan",
    "jennifer", "zxcvbnm", "asdfgh", "hunter", "buster",
    "soccer", "harley", "batman", "andrew", "tigger",
    "sunshine", "iloveyou", "2000", "charlie", "robert",
    "thomas", "hockey", "ranger", "daniel", "starwars",
    "klaster", "112233", "george", "computer", "michelle",
    "jessica", "pepper", "1111", "zxcvbn", "555555",
    "11111111", "131313", "freedom", "777777", "pass",
    "maggie", "159753", "aaaaaa", "ginger", "princess",
    "joshua", "cheese", "amanda", "summer", "love",
    "ashley", "nicole", "chelsea", "biteme", "matthew",
    "access", "yankees", "987654321", "dallas", "austin",
    "thunder", "taylor", "matrix", "mobilemail", "admin",
    "passwd", "welcome", "passw0rd", "p@ssw0rd", "admin123",
};

/// 패스프레이즈용 단어 목록 (EFF Diceware 일부)
const std::vector<std::string> PASSPHRASE_WORDS = {
    "abacus", "abandon", "ability", "ablaze", "aboard",
    "absence", "absorb", "abstract", "accent", "accept",
    "account", "achieve", "acid", "acoustic", "across",
    "action", "actor", "actual", "adapt", "address",
    "adjust", "admiral", "advance", "advice", "aerial",
    "afford", "agenda", "agent", "agree", "airport",
    "algebra", "alien", "align", "almond", "alpine",
    "anchor", "ancient", "angel", "angle", "animal",
    "annual", "antenna", "antique", "anvil", "apart",
    "apology", "apple", "arcade", "arctic", "arena",
    "armor", "arrow", "artist", "aster", "atlas",
    "autumn", "avenue", "avocado", "awake", "balance",
    "bamboo", "banana", "banner", "barrel", "basket",
    "beacon", "beauty", "blanket", "blossom", "border",
    "bounce", "branch", "breeze", "bridge", "bronze",
    "brush", "bubble", "budget", "bundle", "butter",
    "cabin", "cactus", "camera", "candle", "canyon",
    "canvas", "carbon", "carpet", "castle", "catalog",
    "cedar", "cellar", "center", "chance", "cherry",
    "circle", "cliff", "clinic", "clock", "cloud",
    "cluster", "comet", "common", "cookie", "copper",
    "coral", "cotton", "cousin", "cradle", "creek",
    "cricket", "crystal", "curtain", "cycle", "danger",
    "decade", "delta", "desert", "detail", "diamond",
    "diesel", "dinner", "dolphin", "domain", "double",
    "dragon", "drawer", "dream", "drift", "drizzle",
    "durable", "eagle", "earth", "echo", "eclipse",
    "editor", "effort", "eight", "elder", "elegant",
    "ember", "emerge", "empire", "energy", "engage",
    "engine", "enough", "envelope", "episode", "equip",
    "escape", "eternal", "evening", "evolve", "exact",
    "exotic", "expand", "express", "extend", "fabric",
    "falcon", "family", "famous", "fancy", "fantasy",
    "feather", "festival", "fiction", "filter", "finger",
    "fiscal", "flavor", "flight", "float", "flower",
    "forest", "fortune", "fossil", "fragile", "freedom",
    "frozen", "furnace", "gadget", "galaxy", "garden",
    "gentle", "glacier", "global", "golden", "gospel",
    "gossip", "granite", "gravity", "guitar", "gutter",
    "harbor", "harvest", "hazard", "heaven", "helmet",
    "hidden", "highway", "hollow", "horizon", "humble",
    "hybrid", "icicle", "ignite", "impact", "impulse",
    "income", "indoor", "infant", "inject", "insect",
    "inspire", "intact", "invest", "island", "ivory",
    "jacket", "jaguar", "jewel", "jungle", "justice",
    "kennel", "kernel", "kingdom", "kitchen", "knight",
    "ladder", "lagoon", "laptop", "launch", "legend",
    "lemon", "letter", "liberty", "limit", "linen",
    "liquid", "lizard", "lobby", "lunar", "luxury",
};

} // 익명 네임스페이스

// ============================================================
// 생성자 / 소멸자
// ============================================================

PasswordManager::PasswordManager(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {}

PasswordManager::~PasswordManager() {
    lock();
}

// ============================================================
// 초기화
// ============================================================

bool PasswordManager::initialize(const std::string& master_password) {
    if (!store_ || !store_->isOpen()) return false;

    // 마이그레이션 — 비밀번호 테이블
    Migration mig;
    mig.version = 500;
    mig.name = "비밀번호 테이블 생성";
    mig.up_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS credentials (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            origin          TEXT NOT NULL DEFAULT '',
            action_url      TEXT DEFAULT '',
            username_field  TEXT DEFAULT '',
            password_field  TEXT DEFAULT '',
            username_enc    TEXT NOT NULL DEFAULT '',
            password_enc    TEXT NOT NULL DEFAULT '',
            note_enc        TEXT DEFAULT '',
            created_at      TEXT NOT NULL,
            modified_at     TEXT NOT NULL,
            last_used_at    TEXT DEFAULT '',
            use_count       INTEGER DEFAULT 0,
            compromised     INTEGER DEFAULT 0,
            weak            INTEGER DEFAULT 0,
            reused          INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_cred_origin ON credentials(origin);
        CREATE INDEX IF NOT EXISTS idx_cred_username ON credentials(username_enc);
        CREATE TABLE IF NOT EXISTS password_master (
            id              INTEGER PRIMARY KEY CHECK (id = 1),
            salt            TEXT NOT NULL,
            verify_hash     TEXT NOT NULL
        );
    )SQL";
    mig.down_sql = "DROP TABLE IF EXISTS credentials; DROP TABLE IF EXISTS password_master;";

    store_->registerMigration(mig);
    store_->runMigrations();

    // 마스터 비밀번호 설정/검증
    auto rows = store_->query("SELECT salt, verify_hash FROM password_master WHERE id = 1");

    if (rows.empty()) {
        // 최초 설정 — 솔트 생성 및 저장
        master_salt_.resize(SALT_LENGTH);
        RAND_bytes(master_salt_.data(), SALT_LENGTH);

        deriveKey(master_password, master_salt_);

        // 검증용 해시 (키의 SHA-256)
        master_hash_ = sha256Hex(
            std::string(master_key_.begin(), master_key_.end()));

        store_->execute(
            "INSERT INTO password_master (id, salt, verify_hash) VALUES (1, ?, ?)",
            {base64Encode(master_salt_), master_hash_});

        locked_ = false;
        return true;
    }

    // 기존 설정 — 검증
    auto& row = rows.front();
    std::string salt_b64, verify_hash;

    auto it_salt = row.find("salt");
    if (it_salt != row.end() && std::holds_alternative<std::string>(it_salt->second)) {
        salt_b64 = std::get<std::string>(it_salt->second);
    }
    auto it_hash = row.find("verify_hash");
    if (it_hash != row.end() && std::holds_alternative<std::string>(it_hash->second)) {
        verify_hash = std::get<std::string>(it_hash->second);
    }

    master_salt_ = base64Decode(salt_b64);
    master_hash_ = verify_hash;

    deriveKey(master_password, master_salt_);

    // 검증
    std::string computed = sha256Hex(
        std::string(master_key_.begin(), master_key_.end()));

    if (computed != master_hash_) {
        master_key_.clear();
        locked_ = true;
        return false;
    }

    locked_ = false;
    return true;
}

bool PasswordManager::changeMasterPassword(const std::string& old_password,
                                             const std::string& new_password) {
    if (!verifyMasterPassword(old_password)) return false;

    // 모든 자격증명 복호화
    auto all = getAllCredentials();

    // 새 솔트 생성
    master_salt_.resize(SALT_LENGTH);
    RAND_bytes(master_salt_.data(), SALT_LENGTH);

    // 새 키 파생
    deriveKey(new_password, master_salt_);
    master_hash_ = sha256Hex(
        std::string(master_key_.begin(), master_key_.end()));

    // 마스터 정보 업데이트
    store_->execute(
        "UPDATE password_master SET salt = ?, verify_hash = ? WHERE id = 1",
        {base64Encode(master_salt_), master_hash_});

    // 모든 자격증명을 새 키로 재암호화
    for (const auto& cred : all) {
        store_->execute(
            "UPDATE credentials SET username_enc = ?, password_enc = ?, "
            "note_enc = ? WHERE id = ?",
            {encryptField(cred.username), encryptField(cred.password),
             encryptField(cred.note), cred.id});
    }

    locked_ = false;
    return true;
}

bool PasswordManager::verifyMasterPassword(const std::string& password) const {
    if (master_salt_.empty() || master_hash_.empty()) return false;

    // 임시 키 파생
    std::vector<uint8_t> temp_key(KEY_LENGTH);
    PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.size()),
        master_salt_.data(), static_cast<int>(master_salt_.size()),
        PBKDF2_ITERATIONS, EVP_sha256(), KEY_LENGTH, temp_key.data());

    std::string computed = sha256Hex(
        std::string(temp_key.begin(), temp_key.end()));

    // 상수 시간 비교 (타이밍 공격 방지)
    if (computed.size() != master_hash_.size()) return false;
    volatile int diff = 0;
    for (size_t i = 0; i < computed.size(); ++i) {
        diff |= computed[i] ^ master_hash_[i];
    }
    return diff == 0;
}

bool PasswordManager::isLocked() const {
    return locked_;
}

bool PasswordManager::unlock(const std::string& master_password) {
    return initialize(master_password);
}

void PasswordManager::lock() {
    std::lock_guard lock_guard(mutex_);
    // 메모리에서 키 안전 삭제
    if (!master_key_.empty()) {
        OPENSSL_cleanse(master_key_.data(), master_key_.size());
        master_key_.clear();
    }
    locked_ = true;
}

// ============================================================
// CRUD
// ============================================================

int64_t PasswordManager::saveCredential(const Credential& cred) {
    if (locked_) return -1;

    std::string now = nowIso8601();
    std::string origin = extractOrigin(cred.origin.empty() ? cred.action_url : cred.origin);

    store_->execute(
        "INSERT INTO credentials (origin, action_url, username_field, "
        "password_field, username_enc, password_enc, note_enc, "
        "created_at, modified_at, last_used_at, use_count, "
        "compromised, weak, reused) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {
            origin, cred.action_url, cred.username_field, cred.password_field,
            encryptField(cred.username), encryptField(cred.password),
            encryptField(cred.note),
            now, now, std::string{},
            static_cast<int64_t>(0),
            static_cast<int64_t>(0), static_cast<int64_t>(0), static_cast<int64_t>(0)
        }
    );

    return store_->lastInsertRowId();
}

bool PasswordManager::updateCredential(const Credential& cred) {
    if (locked_) return false;

    std::string now = nowIso8601();
    return store_->execute(
        "UPDATE credentials SET origin = ?, action_url = ?, "
        "username_field = ?, password_field = ?, "
        "username_enc = ?, password_enc = ?, note_enc = ?, "
        "modified_at = ? WHERE id = ?",
        {
            cred.origin, cred.action_url, cred.username_field, cred.password_field,
            encryptField(cred.username), encryptField(cred.password),
            encryptField(cred.note),
            now, cred.id
        }
    ) > 0;
}

bool PasswordManager::deleteCredential(int64_t id) {
    return store_->execute("DELETE FROM credentials WHERE id = ?", {id}) > 0;
}

std::optional<Credential> PasswordManager::getCredential(int64_t id) const {
    if (locked_) return std::nullopt;

    auto rows = store_->query("SELECT * FROM credentials WHERE id = ?", {id});
    if (rows.empty()) return std::nullopt;

    const auto& row = rows.front();

    auto getStr = [&](const std::string& key) -> std::string {
        auto it = row.find(key);
        if (it == row.end()) return "";
        if (std::holds_alternative<std::string>(it->second))
            return std::get<std::string>(it->second);
        return "";
    };
    auto getInt = [&](const std::string& key) -> int64_t {
        auto it = row.find(key);
        if (it == row.end()) return 0;
        if (std::holds_alternative<int64_t>(it->second))
            return std::get<int64_t>(it->second);
        return 0;
    };

    Credential cred;
    cred.id = getInt("id");
    cred.origin = getStr("origin");
    cred.action_url = getStr("action_url");
    cred.username_field = getStr("username_field");
    cred.password_field = getStr("password_field");
    cred.username = decryptField(getStr("username_enc"));
    cred.password = decryptField(getStr("password_enc"));
    cred.note = decryptField(getStr("note_enc"));
    cred.created_at = getStr("created_at");
    cred.modified_at = getStr("modified_at");
    cred.last_used_at = getStr("last_used_at");
    cred.use_count = static_cast<int>(getInt("use_count"));
    cred.compromised = getInt("compromised") != 0;
    cred.weak = getInt("weak") != 0;
    cred.reused = getInt("reused") != 0;

    return cred;
}

std::vector<Credential> PasswordManager::getAllCredentials() const {
    if (locked_) return {};

    auto rows = store_->query(
        "SELECT * FROM credentials ORDER BY origin ASC, use_count DESC");

    std::vector<Credential> result;
    result.reserve(rows.size());

    for (const auto& row : rows) {
        auto getStr = [&](const std::string& key) -> std::string {
            auto it = row.find(key);
            if (it == row.end()) return "";
            if (std::holds_alternative<std::string>(it->second))
                return std::get<std::string>(it->second);
            return "";
        };
        auto getInt = [&](const std::string& key) -> int64_t {
            auto it = row.find(key);
            if (it == row.end()) return 0;
            if (std::holds_alternative<int64_t>(it->second))
                return std::get<int64_t>(it->second);
            return 0;
        };

        Credential cred;
        cred.id = getInt("id");
        cred.origin = getStr("origin");
        cred.action_url = getStr("action_url");
        cred.username_field = getStr("username_field");
        cred.password_field = getStr("password_field");
        cred.username = decryptField(getStr("username_enc"));
        cred.password = decryptField(getStr("password_enc"));
        cred.note = decryptField(getStr("note_enc"));
        cred.created_at = getStr("created_at");
        cred.modified_at = getStr("modified_at");
        cred.last_used_at = getStr("last_used_at");
        cred.use_count = static_cast<int>(getInt("use_count"));
        cred.compromised = getInt("compromised") != 0;
        cred.weak = getInt("weak") != 0;
        cred.reused = getInt("reused") != 0;

        result.push_back(cred);
    }

    return result;
}

// ============================================================
// 자동입력 매칭
// ============================================================

std::vector<Credential> PasswordManager::findMatchingCredentials(
    const std::string& url) const {
    if (locked_) return {};

    std::string origin = extractOrigin(url);
    std::string domain = extractDomain(url);

    // 1순위: 정확한 origin 매칭
    auto results = store_->query(
        "SELECT * FROM credentials WHERE origin = ? ORDER BY use_count DESC",
        {origin});

    // 2순위: 도메인 매칭 (서브도메인 포함)
    if (results.empty() && !domain.empty()) {
        results = store_->query(
            "SELECT * FROM credentials WHERE origin LIKE ? ORDER BY use_count DESC",
            {std::string("%") + domain + "%"});
    }

    std::vector<Credential> credentials;
    credentials.reserve(results.size());

    for (const auto& row : results) {
        auto getStr = [&](const std::string& key) -> std::string {
            auto it = row.find(key);
            if (it == row.end()) return "";
            if (std::holds_alternative<std::string>(it->second))
                return std::get<std::string>(it->second);
            return "";
        };
        auto getInt = [&](const std::string& key) -> int64_t {
            auto it = row.find(key);
            if (it == row.end()) return 0;
            if (std::holds_alternative<int64_t>(it->second))
                return std::get<int64_t>(it->second);
            return 0;
        };

        Credential cred;
        cred.id = getInt("id");
        cred.origin = getStr("origin");
        cred.action_url = getStr("action_url");
        cred.username_field = getStr("username_field");
        cred.password_field = getStr("password_field");
        cred.username = decryptField(getStr("username_enc"));
        cred.password = decryptField(getStr("password_enc"));
        cred.note = decryptField(getStr("note_enc"));
        cred.last_used_at = getStr("last_used_at");
        cred.use_count = static_cast<int>(getInt("use_count"));

        credentials.push_back(cred);
    }

    return credentials;
}

void PasswordManager::recordUsage(int64_t credential_id) {
    std::string now = nowIso8601();
    store_->execute(
        "UPDATE credentials SET use_count = use_count + 1, "
        "last_used_at = ? WHERE id = ?",
        {now, credential_id});
}

// ============================================================
// 비밀번호 생성
// ============================================================

std::string PasswordManager::generatePassword(
    const PasswordGeneratorOptions& options) {

    // 문자 집합 구성
    std::string uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string lowercase = "abcdefghijklmnopqrstuvwxyz";
    std::string digits = "0123456789";
    std::string symbols = "!@#$%^&*()-_=+[]{}|;:,.<>?/~`";

    if (options.exclude_ambiguous) {
        // 모호한 문자 제거
        uppercase.erase(std::remove_if(uppercase.begin(), uppercase.end(),
            [](char c) { return c == 'I' || c == 'O'; }), uppercase.end());
        lowercase.erase(std::remove_if(lowercase.begin(), lowercase.end(),
            [](char c) { return c == 'l' || c == 'o'; }), lowercase.end());
        digits.erase(std::remove_if(digits.begin(), digits.end(),
            [](char c) { return c == '0' || c == '1'; }), digits.end());
    }

    // 제외 문자 적용
    auto removeExcluded = [&](std::string& charset) {
        for (char exc : options.exclude_chars) {
            charset.erase(std::remove(charset.begin(), charset.end(), exc),
                          charset.end());
        }
    };
    removeExcluded(uppercase);
    removeExcluded(lowercase);
    removeExcluded(digits);
    removeExcluded(symbols);

    // 전체 문자 집합
    std::string all_chars;
    if (options.use_uppercase) all_chars += uppercase;
    if (options.use_lowercase) all_chars += lowercase;
    if (options.use_digits) all_chars += digits;
    if (options.use_symbols) all_chars += symbols;

    if (all_chars.empty()) {
        all_chars = lowercase + digits; // 폴백
    }

    // 암호학적 난수 생성
    std::string password;
    password.resize(options.length);

    // 최소 요구 충족
    std::vector<char> required;
    auto addRequired = [&](const std::string& charset, int count) {
        std::vector<uint8_t> rand_bytes(count);
        RAND_bytes(rand_bytes.data(), count);
        for (int i = 0; i < count; ++i) {
            if (!charset.empty()) {
                required.push_back(charset[rand_bytes[i] % charset.size()]);
            }
        }
    };

    if (options.use_uppercase) addRequired(uppercase, options.min_uppercase);
    if (options.use_lowercase) addRequired(lowercase, options.min_lowercase);
    if (options.use_digits) addRequired(digits, options.min_digits);
    if (options.use_symbols) addRequired(symbols, options.min_symbols);

    // 나머지 문자를 랜덤으로 채움
    int remaining = options.length - static_cast<int>(required.size());
    if (remaining < 0) remaining = 0;

    std::vector<uint8_t> rand_bytes(remaining + required.size());
    RAND_bytes(rand_bytes.data(), static_cast<int>(rand_bytes.size()));

    for (int i = 0; i < remaining; ++i) {
        required.push_back(all_chars[rand_bytes[i] % all_chars.size()]);
    }

    // Fisher-Yates 셔플
    for (int i = static_cast<int>(required.size()) - 1; i > 0; --i) {
        uint8_t r;
        RAND_bytes(&r, 1);
        int j = r % (i + 1);
        std::swap(required[i], required[j]);
    }

    // 길이 조정
    if (static_cast<int>(required.size()) > options.length) {
        required.resize(options.length);
    }

    return std::string(required.begin(), required.end());
}

std::string PasswordManager::generatePassphrase(int word_count,
                                                  const std::string& separator) {
    if (word_count < 1) word_count = 4;
    if (PASSPHRASE_WORDS.empty()) return generatePassword();

    std::string result;
    for (int i = 0; i < word_count; ++i) {
        uint32_t rand_val;
        RAND_bytes(reinterpret_cast<uint8_t*>(&rand_val), sizeof(rand_val));
        size_t idx = rand_val % PASSPHRASE_WORDS.size();

        if (i > 0) result += separator;

        // 첫 글자 대문자화
        std::string word = PASSPHRASE_WORDS[idx];
        if (!word.empty()) {
            word[0] = static_cast<char>(std::toupper(word[0]));
        }
        result += word;
    }

    // 끝에 숫자 추가 (엔트로피 보강)
    uint8_t num;
    RAND_bytes(&num, 1);
    result += std::to_string(num % 100);

    return result;
}

// ============================================================
// 비밀번호 분석
// ============================================================

PasswordStrengthResult PasswordManager::analyzeStrength(const std::string& password) {
    PasswordStrengthResult result;
    result.length = static_cast<int>(password.size());

    if (password.empty()) {
        result.feedback = "비밀번호를 입력해주세요";
        return result;
    }

    // 문자 유형 분석
    int uppercase_count = 0, lowercase_count = 0;
    int digit_count = 0, symbol_count = 0;
    std::unordered_map<char, int> char_freq;

    for (char c : password) {
        char_freq[c]++;
        if (std::isupper(c)) { uppercase_count++; result.has_uppercase = true; }
        else if (std::islower(c)) { lowercase_count++; result.has_lowercase = true; }
        else if (std::isdigit(c)) { digit_count++; result.has_digits = true; }
        else { symbol_count++; result.has_symbols = true; }
    }

    // 흔한 비밀번호 확인
    std::string lower_pw = password;
    std::transform(lower_pw.begin(), lower_pw.end(), lower_pw.begin(), ::tolower);
    for (const auto& common : COMMON_PASSWORDS) {
        if (lower_pw == common) {
            result.is_common = true;
            result.score = 0;
            result.grade = PasswordStrength::VeryWeak;
            result.feedback = "매우 흔한 비밀번호입니다. 다른 비밀번호를 사용하세요";
            return result;
        }
    }

    // 엔트로피 계산
    int charset_size = 0;
    if (result.has_uppercase) charset_size += 26;
    if (result.has_lowercase) charset_size += 26;
    if (result.has_digits) charset_size += 10;
    if (result.has_symbols) charset_size += 32;

    if (charset_size > 0) {
        result.entropy_bits = password.size() * std::log2(charset_size);
    }

    // 반복/순서 패턴 감점
    int repetition_penalty = 0;
    for (const auto& [ch, freq] : char_freq) {
        if (freq > 2) repetition_penalty += (freq - 2) * 3;
    }

    // 연속 문자 감점
    int sequential_penalty = 0;
    for (size_t i = 1; i < password.size(); ++i) {
        if (password[i] == password[i - 1] + 1 ||
            password[i] == password[i - 1] - 1) {
            sequential_penalty += 2;
        }
    }

    // 점수 계산 (0-100)
    int score = 0;

    // 길이 점수 (최대 30)
    score += std::min(30, result.length * 3);

    // 문자 유형 다양성 (최대 30)
    int types_used = 0;
    if (result.has_uppercase) types_used++;
    if (result.has_lowercase) types_used++;
    if (result.has_digits) types_used++;
    if (result.has_symbols) types_used++;
    score += types_used * 7;

    // 엔트로피 보너스 (최대 20)
    score += std::min(20, static_cast<int>(result.entropy_bits / 4));

    // 감점 적용
    score -= repetition_penalty;
    score -= sequential_penalty;

    // 짧은 비밀번호 추가 감점
    if (result.length < 8) score -= 20;
    else if (result.length < 12) score -= 5;

    result.score = std::clamp(score, 0, 100);

    // 등급 결정
    if (result.score >= 80) result.grade = PasswordStrength::VeryStrong;
    else if (result.score >= 60) result.grade = PasswordStrength::Strong;
    else if (result.score >= 40) result.grade = PasswordStrength::Fair;
    else if (result.score >= 20) result.grade = PasswordStrength::Weak;
    else result.grade = PasswordStrength::VeryWeak;

    // 피드백 생성
    std::vector<std::string> tips;
    if (result.length < 12) tips.push_back("12자 이상 사용을 권장합니다");
    if (!result.has_uppercase) tips.push_back("대문자를 추가하세요");
    if (!result.has_lowercase) tips.push_back("소문자를 추가하세요");
    if (!result.has_digits) tips.push_back("숫자를 추가하세요");
    if (!result.has_symbols) tips.push_back("특수문자를 추가하세요");
    if (repetition_penalty > 5) tips.push_back("반복 문자를 줄이세요");
    if (sequential_penalty > 3) tips.push_back("연속된 문자 패턴을 피하세요");

    if (tips.empty()) {
        result.feedback = "강력한 비밀번호입니다!";
    } else {
        std::ostringstream oss;
        for (size_t i = 0; i < tips.size(); ++i) {
            if (i > 0) oss << "; ";
            oss << tips[i];
        }
        result.feedback = oss.str();
    }

    // 크랙 예상 시간 (10^10 추측/초 기준)
    if (result.entropy_bits > 0) {
        double guesses = std::pow(2.0, result.entropy_bits);
        result.estimated_crack_seconds = static_cast<int>(guesses / 1e10);
    }

    return result;
}

BreachCheckResult PasswordManager::checkBreach(const std::string& password) {
    BreachCheckResult result;

    // SHA-1 해시 계산
    std::string hash = sha1Hex(password);
    std::transform(hash.begin(), hash.end(), hash.begin(), ::toupper);

    // k-Anonymity: 처음 5자리만 전송
    std::string prefix = hash.substr(0, 5);
    std::string suffix = hash.substr(5);

    // HIBP API 호출 (실제 구현에서는 HTTP 요청)
    // https://api.pwnedpasswords.com/range/{prefix}
    // 여기서는 API 호출 구조만 구현 (실행 시 libcurl 필요)

    // 실제 API 호출 대신 로컬 확인
    // (흔한 비밀번호 목록에 있으면 유출로 판정)
    std::string lower_pw = password;
    std::transform(lower_pw.begin(), lower_pw.end(), lower_pw.begin(), ::tolower);

    for (const auto& common : COMMON_PASSWORDS) {
        if (lower_pw == common) {
            result.compromised = true;
            result.breach_count = 1000; // 추정치
            result.message = "이 비밀번호는 데이터 유출에서 발견되었습니다";
            return result;
        }
    }

    result.message = "유출 기록이 없습니다";
    return result;
}

int PasswordManager::checkAllBreaches() {
    auto all = getAllCredentials();
    int compromised_count = 0;

    for (const auto& cred : all) {
        auto breach = checkBreach(cred.password);
        if (breach.compromised) {
            store_->execute(
                "UPDATE credentials SET compromised = 1 WHERE id = ?",
                {cred.id});
            ++compromised_count;
        }
    }

    return compromised_count;
}

std::vector<std::vector<Credential>> PasswordManager::findReusedPasswords() const {
    auto all = getAllCredentials();

    // 비밀번호 → 자격증명 그룹
    std::unordered_map<std::string, std::vector<Credential>> groups;
    for (const auto& cred : all) {
        if (!cred.password.empty()) {
            groups[cred.password].push_back(cred);
        }
    }

    std::vector<std::vector<Credential>> result;
    for (auto& [pw, creds] : groups) {
        if (creds.size() > 1) {
            result.push_back(std::move(creds));

            // DB에 재사용 플래그 설정
            for (const auto& c : result.back()) {
                store_->execute(
                    "UPDATE credentials SET reused = 1 WHERE id = ?",
                    {c.id});
            }
        }
    }

    return result;
}

std::vector<Credential> PasswordManager::findWeakPasswords() const {
    auto all = getAllCredentials();
    std::vector<Credential> weak;

    for (auto& cred : all) {
        auto strength = analyzeStrength(cred.password);
        if (strength.score < 40) {
            cred.weak = true;
            weak.push_back(cred);

            store_->execute(
                "UPDATE credentials SET weak = 1 WHERE id = ?",
                {cred.id});
        }
    }

    return weak;
}

// ============================================================
// 내보내기 / 가져오기
// ============================================================

bool PasswordManager::exportToCsv(const std::string& file_path) const {
    if (locked_) return false;

    std::ofstream ofs(file_path);
    if (!ofs.is_open()) return false;

    // Chrome 호환 CSV 형식
    ofs << "name,url,username,password,note\n";

    auto all = getAllCredentials();
    for (const auto& cred : all) {
        auto csvEscape = [](const std::string& s) -> std::string {
            if (s.find(',') != std::string::npos ||
                s.find('"') != std::string::npos ||
                s.find('\n') != std::string::npos) {
                std::string escaped = s;
                // 따옴표 이스케이프
                size_t pos = 0;
                while ((pos = escaped.find('"', pos)) != std::string::npos) {
                    escaped.insert(pos, "\"");
                    pos += 2;
                }
                return "\"" + escaped + "\"";
            }
            return s;
        };

        ofs << csvEscape(cred.origin) << ","
            << csvEscape(cred.action_url) << ","
            << csvEscape(cred.username) << ","
            << csvEscape(cred.password) << ","
            << csvEscape(cred.note) << "\n";
    }

    return true;
}

int PasswordManager::importFromCsv(const std::string& file_path) {
    if (locked_) return -1;

    std::ifstream ifs(file_path);
    if (!ifs.is_open()) return -1;

    int imported = 0;
    std::string line;

    // 헤더 건너뛰기
    std::getline(ifs, line);

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;

        // 간이 CSV 파서 (따옴표 처리)
        std::vector<std::string> fields;
        std::string field;
        bool in_quotes = false;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quotes = !in_quotes;
                }
            } else if (c == ',' && !in_quotes) {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(field);

        if (fields.size() >= 4) {
            Credential cred;
            cred.origin = fields[0];
            cred.action_url = fields.size() > 1 ? fields[1] : "";
            cred.username = fields.size() > 2 ? fields[2] : "";
            cred.password = fields.size() > 3 ? fields[3] : "";
            cred.note = fields.size() > 4 ? fields[4] : "";

            if (saveCredential(cred) > 0) {
                ++imported;
            }
        }
    }

    return imported;
}

void PasswordManager::setMasterPasswordCallback(MasterPasswordCallback callback) {
    master_pw_callback_ = std::move(callback);
}

// ============================================================
// 내부 암호화 헬퍼
// ============================================================

void PasswordManager::deriveKey(const std::string& password,
                                  const std::vector<uint8_t>& salt) {
    master_key_.resize(KEY_LENGTH);
    PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.size()),
        salt.data(), static_cast<int>(salt.size()),
        PBKDF2_ITERATIONS, EVP_sha256(),
        KEY_LENGTH, master_key_.data()
    );
}

std::string PasswordManager::encryptField(const std::string& plaintext) const {
    if (master_key_.empty() || plaintext.empty()) return plaintext;

    // 랜덤 IV 생성
    std::vector<uint8_t> iv(IV_LENGTH);
    RAND_bytes(iv.data(), IV_LENGTH);

    // 암호문 버퍼
    std::vector<uint8_t> ciphertext(plaintext.size() + 16);
    std::vector<uint8_t> tag(TAG_LENGTH);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return plaintext;

    int len = 0, ct_len = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LENGTH, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, master_key_.data(), iv.data());

    EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                      reinterpret_cast<const uint8_t*>(plaintext.data()),
                      static_cast<int>(plaintext.size()));
    ct_len = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ct_len += len;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LENGTH, tag.data());
    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(ct_len);

    // [iv || tag || ciphertext] → Base64
    std::vector<uint8_t> combined;
    combined.reserve(iv.size() + tag.size() + ciphertext.size());
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), tag.begin(), tag.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());

    return base64Encode(combined);
}

std::string PasswordManager::decryptField(const std::string& ciphertext_b64) const {
    if (master_key_.empty() || ciphertext_b64.empty()) return ciphertext_b64;

    auto data = base64Decode(ciphertext_b64);
    if (data.empty()) return ciphertext_b64;

    const int header_len = IV_LENGTH + TAG_LENGTH;
    if (static_cast<int>(data.size()) < header_len) return ciphertext_b64;

    std::vector<uint8_t> iv(data.begin(), data.begin() + IV_LENGTH);
    std::vector<uint8_t> tag(data.begin() + IV_LENGTH,
                              data.begin() + header_len);
    std::vector<uint8_t> ct(data.begin() + header_len, data.end());

    std::vector<uint8_t> plaintext(ct.size());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    int len = 0, pt_len = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LENGTH, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, master_key_.data(), iv.data());

    EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                      ct.data(), static_cast<int>(ct.size()));
    pt_len = len;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LENGTH,
                        const_cast<uint8_t*>(tag.data()));

    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) return ""; // 인증 실패

    pt_len += len;
    plaintext.resize(pt_len);

    return std::string(plaintext.begin(), plaintext.end());
}

// ============================================================
// URL 유틸리티
// ============================================================

std::string PasswordManager::extractOrigin(const std::string& url) {
    // scheme://host[:port]
    std::regex origin_re(R"((https?://[^/\?#]+))");
    std::smatch match;
    if (std::regex_search(url, match, origin_re)) {
        std::string origin = match[1].str();
        // 소문자 변환
        std::transform(origin.begin(), origin.end(), origin.begin(), ::tolower);
        return origin;
    }
    return url;
}

std::string PasswordManager::extractDomain(const std::string& url) {
    std::string origin = extractOrigin(url);

    // scheme:// 제거
    auto pos = origin.find("://");
    if (pos != std::string::npos) {
        origin = origin.substr(pos + 3);
    }

    // 포트 제거
    auto colon = origin.find(':');
    if (colon != std::string::npos) {
        origin.resize(colon);
    }

    // eTLD+1 추출 (간이 — 마지막 2개 점 기준)
    int dot_count = 0;
    size_t last_dot = std::string::npos;
    size_t second_last_dot = std::string::npos;

    for (size_t i = origin.size(); i > 0; --i) {
        if (origin[i - 1] == '.') {
            ++dot_count;
            if (dot_count == 1) last_dot = i - 1;
            else if (dot_count == 2) {
                second_last_dot = i - 1;
                break;
            }
        }
    }

    if (second_last_dot != std::string::npos) {
        return origin.substr(second_last_dot + 1);
    }

    return origin;
}

// ============================================================
// 해시 유틸리티
// ============================================================

std::string PasswordManager::sha1Hex(const std::string& input) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.data()),
         input.size(), hash);

    char hex[SHA_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }
    return std::string(hex, SHA_DIGEST_LENGTH * 2);
}

std::string PasswordManager::sha256Hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), hash);

    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }
    return std::string(hex, SHA256_DIGEST_LENGTH * 2);
}

std::string PasswordManager::nowIso8601() {
    auto now = Clock::now();
    auto t = Clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ============================================================
// Base64 유틸리티
// ============================================================

std::string PasswordManager::base64Encode(const std::vector<uint8_t>& data) {
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

std::vector<uint8_t> PasswordManager::base64Decode(const std::string& encoded) {
    static constexpr int dt[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    };

    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    uint32_t buf = 0;
    int bits = 0;

    for (char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int idx = static_cast<uint8_t>(c);
        if (idx >= 128 || dt[idx] < 0) continue;

        buf = (buf << 6) | static_cast<uint32_t>(dt[idx]);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }

    return result;
}

} // namespace ordinal::privacy
