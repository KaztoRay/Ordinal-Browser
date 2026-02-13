/**
 * @file autofill_engine.cpp
 * @brief 자동입력 엔진 전체 구현
 *
 * 폼 필드 타입 감지 (input name/type/autocomplete 속성 휴리스틱),
 * 프로필 암호화 저장/조회, 자동입력 제안 생성, 퍼지 이름 매칭,
 * 폼 제출 시 자격증명 감지 및 저장 프롬프트 트리거.
 */

#include "autofill_engine.h"
#include "../data/data_store.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <regex>
#include <sstream>

namespace ordinal::privacy {

using namespace ordinal::data;
using Clock = std::chrono::system_clock;

// ============================================================
// 필드 타입 감지 패턴 테이블
// ============================================================

namespace {

/// 필드 타입별 매칭 패턴 (이름, placeholder, label 등에서 검색)
struct FieldPattern {
    FieldType type;
    std::vector<std::string> name_patterns;         ///< name 속성 패턴
    std::vector<std::string> autocomplete_values;    ///< autocomplete 속성 값
    std::vector<std::string> placeholder_patterns;   ///< placeholder 패턴
};

const std::vector<FieldPattern> FIELD_PATTERNS = {
    // 사용자명
    {FieldType::Username,
     {"username", "user_name", "userid", "user_id", "login", "login_id",
      "account", "acct", "member_id", "nickname", "nick"},
     {"username"},
     {"사용자명", "아이디", "username", "user name", "login id", "account"}},

    // 비밀번호
    {FieldType::Password,
     {"password", "passwd", "pass", "pw", "user_password", "login_password",
      "current_password", "current-password"},
     {"current-password"},
     {"비밀번호", "password", "패스워드", "암호"}},

    // 새 비밀번호
    {FieldType::NewPassword,
     {"new_password", "new-password", "newpasswd", "password_new",
      "confirm_password", "password_confirm", "retype_password"},
     {"new-password"},
     {"새 비밀번호", "new password", "비밀번호 확인", "confirm password"}},

    // 이메일
    {FieldType::Email,
     {"email", "e-mail", "mail", "user_email", "email_address",
      "emailaddress", "contact_email"},
     {"email"},
     {"이메일", "email", "e-mail", "메일 주소", "email address"}},

    // 전화번호
    {FieldType::Phone,
     {"phone", "telephone", "tel", "mobile", "cell", "phone_number",
      "phonenumber", "contact_phone", "hp"},
     {"tel", "tel-national"},
     {"전화번호", "phone", "연락처", "핸드폰", "mobile", "telephone"}},

    // 성명
    {FieldType::FullName,
     {"fullname", "full_name", "name", "your_name", "real_name",
      "customer_name", "display_name"},
     {"name"},
     {"성명", "이름", "full name", "your name", "name"}},

    // 이름 (first name)
    {FieldType::FirstName,
     {"firstname", "first_name", "fname", "given_name", "givenname"},
     {"given-name"},
     {"이름", "first name", "given name"}},

    // 성 (last name)
    {FieldType::LastName,
     {"lastname", "last_name", "lname", "family_name", "familyname", "surname"},
     {"family-name"},
     {"성", "last name", "family name", "surname"}},

    // 주소
    {FieldType::Address,
     {"address", "address1", "address_line1", "street", "street_address",
      "addr", "address_line", "road"},
     {"address-line1", "street-address"},
     {"주소", "address", "도로명 주소", "street address"}},

    // 도시
    {FieldType::City,
     {"city", "town", "locality", "address_city"},
     {"address-level2"},
     {"도시", "시", "city", "town"}},

    // 주/도
    {FieldType::State,
     {"state", "province", "region", "address_state", "prefecture"},
     {"address-level1"},
     {"주", "도", "state", "province", "region"}},

    // 우편번호
    {FieldType::ZipCode,
     {"zip", "zipcode", "zip_code", "postal", "postal_code", "postcode"},
     {"postal-code"},
     {"우편번호", "zip code", "postal code"}},

    // 국가
    {FieldType::Country,
     {"country", "country_code", "nation"},
     {"country", "country-name"},
     {"국가", "나라", "country"}},

    // 카드 번호
    {FieldType::CardNumber,
     {"card_number", "cardnumber", "cc_number", "ccnumber", "credit_card",
      "creditcard", "card_num", "pan"},
     {"cc-number"},
     {"카드 번호", "card number", "credit card", "신용카드"}},

    // 카드 유효기간
    {FieldType::CardExpiry,
     {"card_expiry", "expiry", "expiration", "exp_date", "cc_exp",
      "card_exp", "expiry_date", "exp_month_year"},
     {"cc-exp"},
     {"유효기간", "expiry", "expiration date", "만료일"}},

    // 카드 CVC
    {FieldType::CardCVC,
     {"cvc", "cvv", "csc", "security_code", "card_cvc", "cc_cvc",
      "card_verification", "cvv2"},
     {"cc-csc"},
     {"CVC", "CVV", "보안 코드", "security code"}},

    // 카드 소유자명
    {FieldType::CardHolder,
     {"card_holder", "cardholder", "cc_name", "card_name", "name_on_card"},
     {"cc-name"},
     {"카드 소유자", "cardholder", "name on card"}},

    // 조직/회사
    {FieldType::Organization,
     {"organization", "company", "org", "company_name", "employer"},
     {"organization"},
     {"회사", "조직", "company", "organization"}},

    // OTP
    {FieldType::OneTimeCode,
     {"otp", "one_time_code", "verification_code", "auth_code",
      "security_code", "mfa_code", "totp"},
     {"one-time-code"},
     {"인증 코드", "OTP", "verification code", "인증번호"}},
};

/// 소문자 변환 헬퍼
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

} // anonymous namespace

// ============================================================
// fieldTypeName 유틸리티
// ============================================================

std::string fieldTypeName(FieldType type) {
    switch (type) {
        case FieldType::Unknown:      return "Unknown";
        case FieldType::Username:     return "Username";
        case FieldType::Password:     return "Password";
        case FieldType::NewPassword:  return "NewPassword";
        case FieldType::Email:        return "Email";
        case FieldType::Phone:        return "Phone";
        case FieldType::FullName:     return "FullName";
        case FieldType::FirstName:    return "FirstName";
        case FieldType::LastName:     return "LastName";
        case FieldType::Address:      return "Address";
        case FieldType::City:         return "City";
        case FieldType::State:        return "State";
        case FieldType::ZipCode:      return "ZipCode";
        case FieldType::Country:      return "Country";
        case FieldType::CardNumber:   return "CardNumber";
        case FieldType::CardExpiry:   return "CardExpiry";
        case FieldType::CardCVC:      return "CardCVC";
        case FieldType::CardHolder:   return "CardHolder";
        case FieldType::Organization: return "Organization";
        case FieldType::OneTimeCode:  return "OneTimeCode";
    }
    return "Unknown";
}

// ============================================================
// 생성자 / 소멸자
// ============================================================

AutofillEngine::AutofillEngine(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {
}

AutofillEngine::~AutofillEngine() = default;

// ============================================================
// 초기화
// ============================================================

bool AutofillEngine::initialize() {
    std::lock_guard lock(mutex_);

    if (!store_ || !store_->isOpen()) {
        std::cerr << "[AutofillEngine] DataStore가 열려있지 않습니다." << std::endl;
        return false;
    }

    // 프로필 테이블 생성
    const std::string create_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS autofill_profiles (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            label           TEXT NOT NULL DEFAULT '기본',
            full_name       TEXT DEFAULT '',
            first_name      TEXT DEFAULT '',
            last_name       TEXT DEFAULT '',
            email           TEXT DEFAULT '',
            phone           TEXT DEFAULT '',
            organization    TEXT DEFAULT '',
            address         TEXT DEFAULT '',
            city            TEXT DEFAULT '',
            state           TEXT DEFAULT '',
            zip_code        TEXT DEFAULT '',
            country         TEXT DEFAULT '',
            card_number     TEXT DEFAULT '',
            card_expiry     TEXT DEFAULT '',
            card_cvc        TEXT DEFAULT '',
            card_holder     TEXT DEFAULT '',
            created_at      TEXT NOT NULL,
            modified_at     TEXT NOT NULL,
            use_count       INTEGER NOT NULL DEFAULT 0,
            is_default      INTEGER NOT NULL DEFAULT 0
        );
    )SQL";

    if (store_->execute(create_sql) < 0) {
        std::cerr << "[AutofillEngine] 프로필 테이블 생성 실패: "
                  << store_->lastError() << std::endl;
        return false;
    }

    // 인덱스 생성
    store_->execute(
        "CREATE INDEX IF NOT EXISTS idx_autofill_default "
        "ON autofill_profiles(is_default);");

    profiles_dirty_ = true;
    std::cout << "[AutofillEngine] 초기화 완료" << std::endl;
    return true;
}

// ============================================================
// 필드 타입 감지
// ============================================================

FieldType AutofillEngine::detectFieldType(FormField& field) const {
    // 1순위: autocomplete 속성 (표준 값)
    if (!field.autocomplete.empty()) {
        std::string ac = toLower(field.autocomplete);
        for (const auto& pattern : FIELD_PATTERNS) {
            for (const auto& acv : pattern.autocomplete_values) {
                if (ac == acv || ac.find(acv) != std::string::npos) {
                    field.detected_type = pattern.type;
                    field.confidence = 0.95;
                    return pattern.type;
                }
            }
        }
    }

    // 2순위: input type 속성
    std::string type_lower = toLower(field.type);
    if (type_lower == "password") {
        // password 필드 — 이름으로 새 비밀번호 vs 기존 비밀번호 구분
        std::string name_lower = toLower(field.name);
        if (name_lower.find("new") != std::string::npos ||
            name_lower.find("confirm") != std::string::npos ||
            name_lower.find("retype") != std::string::npos ||
            name_lower.find("register") != std::string::npos) {
            field.detected_type = FieldType::NewPassword;
            field.confidence = 0.90;
            return FieldType::NewPassword;
        }
        field.detected_type = FieldType::Password;
        field.confidence = 0.95;
        return FieldType::Password;
    }
    if (type_lower == "email") {
        field.detected_type = FieldType::Email;
        field.confidence = 0.90;
        return FieldType::Email;
    }
    if (type_lower == "tel") {
        field.detected_type = FieldType::Phone;
        field.confidence = 0.90;
        return FieldType::Phone;
    }

    // 3순위: name/placeholder/label 속성 기반 퍼지 매칭
    FieldType result = inferFromAttributes(
        field.name, field.type, field.autocomplete,
        field.placeholder, field.label);

    if (result != FieldType::Unknown) {
        field.detected_type = result;
        field.confidence = 0.75;
        return result;
    }

    // aria-label 확인
    if (!field.aria_label.empty()) {
        result = inferFromAttributes(
            "", "", "", field.aria_label, "");
        if (result != FieldType::Unknown) {
            field.detected_type = result;
            field.confidence = 0.65;
            return result;
        }
    }

    field.detected_type = FieldType::Unknown;
    field.confidence = 0.0;
    return FieldType::Unknown;
}

// ============================================================
// 폼 전체 분석
// ============================================================

void AutofillEngine::analyzeForm(FormInfo& form) const {
    // 각 필드 타입 감지
    for (auto& field : form.fields) {
        detectFieldType(field);
    }

    // 폼 유형 판별 (감지된 필드 타입 기반)
    bool has_username = false;
    bool has_password = false;
    bool has_new_password = false;
    bool has_email = false;
    bool has_card = false;
    bool has_address = false;
    int password_count = 0;

    for (const auto& field : form.fields) {
        switch (field.detected_type) {
            case FieldType::Username:    has_username = true; break;
            case FieldType::Password:    has_password = true; password_count++; break;
            case FieldType::NewPassword: has_new_password = true; password_count++; break;
            case FieldType::Email:       has_email = true; break;
            case FieldType::CardNumber:  has_card = true; break;
            case FieldType::Address:
            case FieldType::City:
            case FieldType::ZipCode:     has_address = true; break;
            default: break;
        }
    }

    // 로그인 폼: 사용자명/이메일 + 비밀번호 1개
    form.is_login_form = (has_username || has_email) &&
                          has_password && password_count == 1;

    // 회원가입 폼: 비밀번호 2개 이상 또는 새 비밀번호 필드
    form.is_signup_form = has_new_password || password_count >= 2;

    // 결제 폼: 카드 번호 필드
    form.is_payment_form = has_card;

    // 주소 폼: 주소 관련 필드
    form.is_address_form = has_address && !has_card;
}

// ============================================================
// 자동입력 제안
// ============================================================

std::vector<AutofillSuggestion> AutofillEngine::getSuggestions(
    const FormField& field,
    const std::string& /*page_url*/,
    int max_suggestions) const {

    std::lock_guard lock(mutex_);

    if (field.detected_type == FieldType::Unknown) {
        return {};
    }

    refreshProfileCache();

    std::vector<AutofillSuggestion> suggestions;

    for (const auto& profile : cached_profiles_) {
        std::string value = getProfileValueForField(profile, field.detected_type);
        if (value.empty()) {
            continue;
        }

        AutofillSuggestion suggestion;
        suggestion.display_text = value;
        suggestion.subtitle = profile.label;
        suggestion.value = value;
        suggestion.field_type = field.detected_type;
        suggestion.profile_id = profile.id;

        // 관련도 점수: 기본 프로필 우선 + 사용 빈도
        suggestion.relevance = static_cast<double>(profile.use_count) * 0.1;
        if (profile.is_default) {
            suggestion.relevance += 10.0;
        }

        suggestions.push_back(suggestion);
    }

    // 관련도 내림차순 정렬
    std::sort(suggestions.begin(), suggestions.end(),
        [](const AutofillSuggestion& a, const AutofillSuggestion& b) {
            return a.relevance > b.relevance;
        });

    // 최대 수 제한
    if (static_cast<int>(suggestions.size()) > max_suggestions) {
        suggestions.resize(static_cast<size_t>(max_suggestions));
    }

    return suggestions;
}

// ============================================================
// 폼 전체 자동입력 데이터
// ============================================================

std::unordered_map<std::string, std::string> AutofillEngine::getFormFillData(
    const FormInfo& form,
    int64_t profile_id) const {

    std::lock_guard lock(mutex_);

    // 프로필 가져오기
    std::optional<AutofillProfile> profile;
    if (profile_id > 0) {
        profile = getProfile(profile_id);
    } else {
        profile = getDefaultProfile();
    }

    if (!profile) {
        return {};
    }

    std::unordered_map<std::string, std::string> fill_data;

    for (const auto& field : form.fields) {
        if (field.detected_type == FieldType::Unknown ||
            field.is_readonly || !field.is_visible) {
            continue;
        }

        std::string value = getProfileValueForField(*profile, field.detected_type);
        if (!value.empty()) {
            std::string key = field.element_id.empty() ? field.name : field.element_id;
            fill_data[key] = value;
        }
    }

    return fill_data;
}

// ============================================================
// 프로필 CRUD
// ============================================================

int64_t AutofillEngine::saveProfile(const AutofillProfile& profile) {
    std::lock_guard lock(mutex_);

    std::string now = nowIso8601();

    // 카드 정보는 암호화하여 저장
    std::string enc_card_num = profile.card_number;
    std::string enc_card_exp = profile.card_expiry;
    std::string enc_card_cvc = profile.card_cvc;

    if (store_) {
        if (!enc_card_num.empty())
            enc_card_num = store_->encryptString(enc_card_num);
        if (!enc_card_exp.empty())
            enc_card_exp = store_->encryptString(enc_card_exp);
        if (!enc_card_cvc.empty())
            enc_card_cvc = store_->encryptString(enc_card_cvc);
    }

    const std::string sql = R"SQL(
        INSERT INTO autofill_profiles (
            label, full_name, first_name, last_name, email, phone,
            organization, address, city, state, zip_code, country,
            card_number, card_expiry, card_cvc, card_holder,
            created_at, modified_at, use_count, is_default
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";

    int result = store_->execute(sql, {
        profile.label,
        profile.full_name, profile.first_name, profile.last_name,
        profile.email, profile.phone, profile.organization,
        profile.address, profile.city, profile.state,
        profile.zip_code, profile.country,
        enc_card_num, enc_card_exp, enc_card_cvc, profile.card_holder,
        now, now,
        static_cast<int64_t>(profile.use_count),
        static_cast<int64_t>(profile.is_default ? 1 : 0),
    });

    if (result < 0) {
        std::cerr << "[AutofillEngine] 프로필 저장 실패: "
                  << store_->lastError() << std::endl;
        return -1;
    }

    profiles_dirty_ = true;
    int64_t id = store_->lastInsertRowId();
    std::cout << "[AutofillEngine] 프로필 저장 완료 (ID: " << id
              << ", 라벨: " << profile.label << ")" << std::endl;
    return id;
}

bool AutofillEngine::updateProfile(const AutofillProfile& profile) {
    std::lock_guard lock(mutex_);

    std::string now = nowIso8601();

    // 카드 정보 암호화
    std::string enc_card_num = profile.card_number;
    std::string enc_card_exp = profile.card_expiry;
    std::string enc_card_cvc = profile.card_cvc;

    if (store_) {
        if (!enc_card_num.empty())
            enc_card_num = store_->encryptString(enc_card_num);
        if (!enc_card_exp.empty())
            enc_card_exp = store_->encryptString(enc_card_exp);
        if (!enc_card_cvc.empty())
            enc_card_cvc = store_->encryptString(enc_card_cvc);
    }

    const std::string sql = R"SQL(
        UPDATE autofill_profiles SET
            label = ?, full_name = ?, first_name = ?, last_name = ?,
            email = ?, phone = ?, organization = ?,
            address = ?, city = ?, state = ?, zip_code = ?, country = ?,
            card_number = ?, card_expiry = ?, card_cvc = ?, card_holder = ?,
            modified_at = ?, is_default = ?
        WHERE id = ?;
    )SQL";

    int result = store_->execute(sql, {
        profile.label,
        profile.full_name, profile.first_name, profile.last_name,
        profile.email, profile.phone, profile.organization,
        profile.address, profile.city, profile.state,
        profile.zip_code, profile.country,
        enc_card_num, enc_card_exp, enc_card_cvc, profile.card_holder,
        now,
        static_cast<int64_t>(profile.is_default ? 1 : 0),
        profile.id,
    });

    profiles_dirty_ = true;
    return result > 0;
}

bool AutofillEngine::deleteProfile(int64_t profile_id) {
    std::lock_guard lock(mutex_);

    int result = store_->execute(
        "DELETE FROM autofill_profiles WHERE id = ?;",
        {profile_id});

    profiles_dirty_ = true;
    return result > 0;
}

std::optional<AutofillProfile> AutofillEngine::getProfile(int64_t profile_id) const {
    auto rows = store_->query(
        "SELECT * FROM autofill_profiles WHERE id = ?;",
        {profile_id});

    if (rows.empty()) {
        return std::nullopt;
    }
    return rowToProfile(rows[0]);
}

std::vector<AutofillProfile> AutofillEngine::getAllProfiles() const {
    std::lock_guard lock(mutex_);
    refreshProfileCache();
    return cached_profiles_;
}

std::optional<AutofillProfile> AutofillEngine::getDefaultProfile() const {
    auto rows = store_->query(
        "SELECT * FROM autofill_profiles WHERE is_default = 1 LIMIT 1;");

    if (rows.empty()) {
        // 기본 프로필이 없으면 첫 번째 프로필
        rows = store_->query(
            "SELECT * FROM autofill_profiles ORDER BY use_count DESC LIMIT 1;");
    }

    if (rows.empty()) {
        return std::nullopt;
    }
    return rowToProfile(rows[0]);
}

bool AutofillEngine::setDefaultProfile(int64_t profile_id) {
    std::lock_guard lock(mutex_);

    // 기존 기본 프로필 해제
    store_->execute("UPDATE autofill_profiles SET is_default = 0;");

    // 새 기본 프로필 설정
    int result = store_->execute(
        "UPDATE autofill_profiles SET is_default = 1 WHERE id = ?;",
        {profile_id});

    profiles_dirty_ = true;
    return result > 0;
}

// ============================================================
// 자격증명 감지 (폼 제출 시)
// ============================================================

std::optional<SavePromptInfo> AutofillEngine::detectCredentialSubmission(
    const FormInfo& form,
    const std::unordered_map<std::string, std::string>& field_values) const {

    // 로그인/회원가입 폼이 아니면 무시
    if (!form.is_login_form && !form.is_signup_form) {
        return std::nullopt;
    }

    std::string username;
    std::string password;

    for (const auto& field : form.fields) {
        std::string key = field.element_id.empty() ? field.name : field.element_id;
        auto it = field_values.find(key);
        if (it == field_values.end() || it->second.empty()) {
            continue;
        }

        switch (field.detected_type) {
            case FieldType::Username:
            case FieldType::Email:
                if (username.empty()) {
                    username = it->second;
                }
                break;
            case FieldType::Password:
            case FieldType::NewPassword:
                if (password.empty()) {
                    password = it->second;
                }
                break;
            default:
                break;
        }
    }

    // 비밀번호가 없으면 자격증명이 아님
    if (password.empty()) {
        return std::nullopt;
    }

    SavePromptInfo info;
    info.origin = form.page_url;
    info.username = username;
    info.password = password;
    info.is_update = false; // 기존 자격증명 확인은 PasswordManager에서

    return info;
}

void AutofillEngine::setSavePromptCallback(SavePromptCallback callback) {
    std::lock_guard lock(mutex_);
    save_prompt_callback_ = std::move(callback);
}

// ============================================================
// 프로필 사용 기록
// ============================================================

void AutofillEngine::recordProfileUsage(int64_t profile_id) {
    std::lock_guard lock(mutex_);

    store_->execute(
        "UPDATE autofill_profiles SET use_count = use_count + 1 WHERE id = ?;",
        {profile_id});

    profiles_dirty_ = true;
}

// ============================================================
// Private: 프로필 캐시 새로고침
// ============================================================

void AutofillEngine::refreshProfileCache() const {
    if (!profiles_dirty_) {
        return;
    }

    auto rows = store_->query(
        "SELECT * FROM autofill_profiles ORDER BY use_count DESC;");

    cached_profiles_.clear();
    cached_profiles_.reserve(rows.size());

    for (const auto& row : rows) {
        cached_profiles_.push_back(rowToProfile(row));
    }

    profiles_dirty_ = false;
}

// ============================================================
// Private: DB 행 → AutofillProfile 변환
// ============================================================

AutofillProfile AutofillEngine::rowToProfile(const DbRow& row) const {
    AutofillProfile p;

    auto getStr = [&](const std::string& key) -> std::string {
        auto it = row.find(key);
        if (it != row.end()) {
            if (auto* v = std::get_if<std::string>(&it->second)) return *v;
        }
        return {};
    };
    auto getInt = [&](const std::string& key) -> int64_t {
        auto it = row.find(key);
        if (it != row.end()) {
            if (auto* v = std::get_if<int64_t>(&it->second)) return *v;
        }
        return 0;
    };

    p.id = getInt("id");
    p.label = getStr("label");
    p.full_name = getStr("full_name");
    p.first_name = getStr("first_name");
    p.last_name = getStr("last_name");
    p.email = getStr("email");
    p.phone = getStr("phone");
    p.organization = getStr("organization");
    p.address = getStr("address");
    p.city = getStr("city");
    p.state = getStr("state");
    p.zip_code = getStr("zip_code");
    p.country = getStr("country");
    p.card_holder = getStr("card_holder");
    p.created_at = getStr("created_at");
    p.modified_at = getStr("modified_at");
    p.use_count = static_cast<int>(getInt("use_count"));
    p.is_default = (getInt("is_default") != 0);

    // 카드 정보 복호화
    std::string enc_num = getStr("card_number");
    std::string enc_exp = getStr("card_expiry");
    std::string enc_cvc = getStr("card_cvc");

    if (store_) {
        if (!enc_num.empty()) p.card_number = store_->decryptString(enc_num);
        if (!enc_exp.empty()) p.card_expiry = store_->decryptString(enc_exp);
        if (!enc_cvc.empty()) p.card_cvc = store_->decryptString(enc_cvc);
    }

    return p;
}

// ============================================================
// Private: 속성 기반 타입 추론
// ============================================================

FieldType AutofillEngine::inferFromAttributes(
    const std::string& name,
    const std::string& type,
    const std::string& autocomplete,
    const std::string& placeholder,
    const std::string& label) const {

    // 모든 텍스트를 소문자로 합치기
    std::string combined = toLower(name) + " " + toLower(placeholder) + " " +
                           toLower(label) + " " + toLower(autocomplete);

    // 각 패턴 확인 (점수 기반)
    FieldType best_type = FieldType::Unknown;
    double best_score = 0.0;

    for (const auto& pattern : FIELD_PATTERNS) {
        double score = 0.0;

        // name 속성 매칭 (가장 높은 가중치)
        std::string name_lower = toLower(name);
        for (const auto& np : pattern.name_patterns) {
            if (name_lower == np) {
                score = std::max(score, 1.0);
            } else if (name_lower.find(np) != std::string::npos) {
                score = std::max(score, 0.8);
            } else if (fuzzyMatch(name_lower, np) > 0.7) {
                score = std::max(score, 0.6);
            }
        }

        // placeholder 매칭
        for (const auto& pp : pattern.placeholder_patterns) {
            std::string pp_lower = toLower(pp);
            if (toLower(placeholder).find(pp_lower) != std::string::npos) {
                score = std::max(score, 0.7);
            }
        }

        // label 매칭
        for (const auto& pp : pattern.placeholder_patterns) {
            std::string pp_lower = toLower(pp);
            if (toLower(label).find(pp_lower) != std::string::npos) {
                score = std::max(score, 0.65);
            }
        }

        if (score > best_score) {
            best_score = score;
            best_type = pattern.type;
        }
    }

    // 최소 임계값
    if (best_score >= 0.5) {
        return best_type;
    }

    return FieldType::Unknown;
}

// ============================================================
// Private: 퍼지 문자열 매칭 (Levenshtein)
// ============================================================

double AutofillEngine::fuzzyMatch(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;

    size_t len_a = a.size();
    size_t len_b = b.size();

    // Levenshtein 거리 계산 (DP)
    std::vector<std::vector<int>> dp(len_a + 1, std::vector<int>(len_b + 1, 0));

    for (size_t i = 0; i <= len_a; ++i) dp[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= len_b; ++j) dp[0][j] = static_cast<int>(j);

    for (size_t i = 1; i <= len_a; ++i) {
        for (size_t j = 1; j <= len_b; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,          // 삭제
                dp[i][j - 1] + 1,           // 삽입
                dp[i - 1][j - 1] + cost     // 대체
            });
        }
    }

    int max_len = static_cast<int>(std::max(len_a, len_b));
    return 1.0 - static_cast<double>(dp[len_a][len_b]) / max_len;
}

// ============================================================
// Private: 패턴 매칭 헬퍼
// ============================================================

bool AutofillEngine::matchesAny(
    const std::string& text,
    const std::vector<std::string>& patterns) {

    std::string lower = toLower(text);
    for (const auto& p : patterns) {
        if (lower.find(toLower(p)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ============================================================
// Private: 시간 유틸리티
// ============================================================

std::string AutofillEngine::nowIso8601() {
    auto now = Clock::now();
    auto time_t = Clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time_t, &tm_buf);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

// ============================================================
// Private: 프로필 필드 값 반환
// ============================================================

std::string AutofillEngine::getProfileValueForField(
    const AutofillProfile& profile, FieldType type) {

    switch (type) {
        case FieldType::Username:     return profile.email; // 이메일을 사용자명으로
        case FieldType::Email:        return profile.email;
        case FieldType::Phone:        return profile.phone;
        case FieldType::FullName:     return profile.full_name;
        case FieldType::FirstName:    return profile.first_name;
        case FieldType::LastName:     return profile.last_name;
        case FieldType::Address:      return profile.address;
        case FieldType::City:         return profile.city;
        case FieldType::State:        return profile.state;
        case FieldType::ZipCode:      return profile.zip_code;
        case FieldType::Country:      return profile.country;
        case FieldType::CardNumber:   return profile.card_number;
        case FieldType::CardExpiry:   return profile.card_expiry;
        case FieldType::CardCVC:      return profile.card_cvc;
        case FieldType::CardHolder:   return profile.card_holder;
        case FieldType::Organization: return profile.organization;
        default: return {};
    }
}

} // namespace ordinal::privacy
