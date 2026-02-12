/**
 * @file extension.cpp
 * @brief 확장 프로그램 클래스 구현
 * 
 * 확장 매니페스트 파싱, 권한 관리, 메시지 패싱, 스토리지 API,
 * 콘텐츠 스크립트 관리 등 확장의 전체 수명 주기를 구현합니다.
 */

#include "extension.h"
#include "content_script.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <regex>
#include <iostream>

namespace fs = std::filesystem;

namespace ordinal::extensions {

// ============================================================
// PermissionUtils 구현
// ============================================================

std::optional<Permission> PermissionUtils::fromString(const std::string& name) {
    // 권한 이름 → 열거형 매핑 테이블
    static const std::unordered_map<std::string, Permission> kPermMap = {
        {"tabs",                Permission::Tabs},
        {"activeTab",           Permission::ActiveTab},
        {"storage",             Permission::Storage},
        {"webRequest",          Permission::WebRequest},
        {"webRequestBlocking",  Permission::WebRequestBlocking},
        {"cookies",             Permission::Cookies},
        {"downloads",           Permission::Downloads},
        {"history",             Permission::History},
        {"bookmarks",           Permission::Bookmarks},
        {"notifications",       Permission::Notifications},
        {"contextMenus",        Permission::ContextMenus},
        {"clipboardRead",       Permission::ClipboardRead},
        {"clipboardWrite",      Permission::ClipboardWrite},
        {"identity",            Permission::Identity},
        {"management",          Permission::Management},
        {"webNavigation",       Permission::WebNavigation},
        {"alarms",              Permission::Alarms},
        {"unlimitedStorage",    Permission::UnlimitedStorage}
    };

    auto it = kPermMap.find(name);
    if (it != kPermMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string PermissionUtils::toString(Permission perm) {
    // 열거형 → 권한 이름 변환
    switch (perm) {
        case Permission::Tabs:               return "tabs";
        case Permission::ActiveTab:          return "activeTab";
        case Permission::Storage:            return "storage";
        case Permission::WebRequest:         return "webRequest";
        case Permission::WebRequestBlocking: return "webRequestBlocking";
        case Permission::Cookies:            return "cookies";
        case Permission::Downloads:          return "downloads";
        case Permission::History:            return "history";
        case Permission::Bookmarks:          return "bookmarks";
        case Permission::Notifications:      return "notifications";
        case Permission::ContextMenus:       return "contextMenus";
        case Permission::ClipboardRead:      return "clipboardRead";
        case Permission::ClipboardWrite:     return "clipboardWrite";
        case Permission::Identity:           return "identity";
        case Permission::Management:         return "management";
        case Permission::WebNavigation:      return "webNavigation";
        case Permission::Alarms:             return "alarms";
        case Permission::UnlimitedStorage:   return "unlimitedStorage";
    }
    return "unknown";
}

bool PermissionUtils::isDangerous(Permission perm) {
    // 사용자 데이터에 접근하거나 네트워크를 가로챌 수 있는 위험 권한
    switch (perm) {
        case Permission::Tabs:
        case Permission::WebRequest:
        case Permission::WebRequestBlocking:
        case Permission::Cookies:
        case Permission::History:
        case Permission::Bookmarks:
        case Permission::ClipboardRead:
        case Permission::Identity:
        case Permission::Management:
            return true;
        default:
            return false;
    }
}

// ============================================================
// ExtensionManifest 구현
// ============================================================

bool ExtensionManifest::isValid() const {
    return validate().empty();
}

std::vector<std::string> ExtensionManifest::validate() const {
    std::vector<std::string> errors;

    // 필수 필드 검사
    if (name.empty()) {
        errors.push_back("매니페스트에 'name' 필드가 필요합니다");
    }
    if (version.empty()) {
        errors.push_back("매니페스트에 'version' 필드가 필요합니다");
    }
    // 매니페스트 버전 검사 (2 또는 3만 허용)
    if (manifest_version != 2 && manifest_version != 3) {
        errors.push_back("manifest_version은 2 또는 3이어야 합니다");
    }
    // MV3에서는 background_scripts 대신 service_worker 사용
    if (manifest_version == 3 && !background_scripts.empty()) {
        errors.push_back("MV3에서는 background.scripts 대신 background.service_worker를 사용하세요");
    }
    // MV2에서는 service_worker 미지원
    if (manifest_version == 2 && !background_service_worker.empty()) {
        errors.push_back("MV2에서는 background.service_worker가 지원되지 않습니다");
    }
    // 콘텐츠 스크립트 매치 패턴 검사
    for (size_t i = 0; i < content_scripts.size(); ++i) {
        const auto& cs = content_scripts[i];
        if (cs.matches.empty()) {
            errors.push_back("content_scripts[" + std::to_string(i) + "]에 'matches' 패턴이 필요합니다");
        }
        if (cs.js.empty() && cs.css.empty()) {
            errors.push_back("content_scripts[" + std::to_string(i) + "]에 js 또는 css 파일이 필요합니다");
        }
        // run_at 유효성 검사
        if (cs.run_at != "document_start" && 
            cs.run_at != "document_end" && 
            cs.run_at != "document_idle") {
            errors.push_back("content_scripts[" + std::to_string(i) + "]의 run_at 값이 잘못되었습니다: " + cs.run_at);
        }
    }

    return errors;
}

// ============================================================
// Extension 구현
// ============================================================

Extension::Extension(const std::string& id, const std::string& base_path)
    : id_(id)
    , base_path_(base_path) {
}

Extension::~Extension() {
    // 활성 상태면 비활성화
    if (state_.load() == ExtensionState::Active) {
        deactivate();
    }
}

Extension::Extension(Extension&& other) noexcept
    : id_(std::move(other.id_))
    , base_path_(std::move(other.base_path_))
    , manifest_(std::move(other.manifest_))
    , state_(other.state_.load())
    , last_error_(std::move(other.last_error_))
    , granted_optional_permissions_(std::move(other.granted_optional_permissions_))
    , content_scripts_(std::move(other.content_scripts_))
    , message_listeners_(std::move(other.message_listeners_))
    , external_message_listeners_(std::move(other.external_message_listeners_))
    , storage_data_(std::move(other.storage_data_))
    , storage_change_callbacks_(std::move(other.storage_change_callbacks_))
    , logs_(std::move(other.logs_)) {
    other.state_.store(ExtensionState::Unloaded);
}

Extension& Extension::operator=(Extension&& other) noexcept {
    if (this != &other) {
        if (state_.load() == ExtensionState::Active) {
            deactivate();
        }
        id_ = std::move(other.id_);
        base_path_ = std::move(other.base_path_);
        manifest_ = std::move(other.manifest_);
        state_.store(other.state_.load());
        last_error_ = std::move(other.last_error_);
        granted_optional_permissions_ = std::move(other.granted_optional_permissions_);
        content_scripts_ = std::move(other.content_scripts_);
        message_listeners_ = std::move(other.message_listeners_);
        external_message_listeners_ = std::move(other.external_message_listeners_);
        storage_data_ = std::move(other.storage_data_);
        storage_change_callbacks_ = std::move(other.storage_change_callbacks_);
        logs_ = std::move(other.logs_);
        other.state_.store(ExtensionState::Unloaded);
    }
    return *this;
}

std::string Extension::stateString() const {
    switch (state_.load()) {
        case ExtensionState::Unloaded:  return "미로드";
        case ExtensionState::Loading:   return "로딩중";
        case ExtensionState::Active:    return "활성";
        case ExtensionState::Disabled:  return "비활성";
        case ExtensionState::Error:     return "오류";
        case ExtensionState::Suspended: return "일시중단";
    }
    return "알 수 없음";
}

// ============================
// 수명 주기
// ============================

std::pair<bool, std::string> Extension::loadManifest() {
    // manifest.json 경로 구성
    fs::path manifest_path = fs::path(base_path_) / "manifest.json";

    if (!fs::exists(manifest_path)) {
        std::string err = "manifest.json을 찾을 수 없습니다: " + manifest_path.string();
        last_error_ = err;
        return {false, err};
    }

    // 파일 읽기
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        std::string err = "manifest.json을 열 수 없습니다: " + manifest_path.string();
        last_error_ = err;
        return {false, err};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();

    // 간단한 JSON 파서로 매니페스트 필드 추출
    // 실제 프로덕션에서는 nlohmann/json 또는 rapidjson 사용 권장
    // 여기서는 기본 필드를 정규식으로 파싱
    auto extractString = [&](const std::string& key) -> std::string {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(json_content, match, re)) {
            return match[1].str();
        }
        return "";
    };

    auto extractInt = [&](const std::string& key) -> int {
        std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
        std::smatch match;
        if (std::regex_search(json_content, match, re)) {
            return std::stoi(match[1].str());
        }
        return 0;
    };

    auto extractStringArray = [&](const std::string& key) -> std::vector<std::string> {
        std::vector<std::string> result;
        // "key": ["val1", "val2"] 패턴 매칭
        std::regex re("\"" + key + "\"\\s*:\\s*\\[([^\\]]*?)\\]");
        std::smatch match;
        if (std::regex_search(json_content, match, re)) {
            std::string arr_content = match[1].str();
            std::regex item_re("\"([^\"]+)\"");
            auto begin = std::sregex_iterator(arr_content.begin(), arr_content.end(), item_re);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                result.push_back((*it)[1].str());
            }
        }
        return result;
    };

    // 기본 필드 파싱
    manifest_.manifest_version = extractInt("manifest_version");
    if (manifest_.manifest_version == 0) manifest_.manifest_version = 3;

    manifest_.name = extractString("name");
    manifest_.version = extractString("version");
    manifest_.description = extractString("description");
    manifest_.author = extractString("author");
    manifest_.homepage_url = extractString("homepage_url");

    // 백그라운드 스크립트 파싱
    manifest_.background_service_worker = extractString("service_worker");
    manifest_.background_scripts = extractStringArray("scripts");

    // 권한 파싱
    auto perm_strings = extractStringArray("permissions");
    for (const auto& perm_str : perm_strings) {
        auto perm = PermissionUtils::fromString(perm_str);
        if (perm.has_value()) {
            manifest_.permissions.insert(perm.value());
        }
    }

    // 선택적 권한 파싱
    auto opt_perm_strings = extractStringArray("optional_permissions");
    for (const auto& perm_str : opt_perm_strings) {
        auto perm = PermissionUtils::fromString(perm_str);
        if (perm.has_value()) {
            manifest_.optional_permissions.insert(perm.value());
        }
    }

    // 호스트 권한 파싱
    manifest_.host_permissions = extractStringArray("host_permissions");

    // 팝업 HTML 파싱
    manifest_.popup_html = extractString("default_popup");

    // 옵션 페이지 파싱
    manifest_.options_page = extractString("options_page");

    // CSP 파싱
    manifest_.content_security_policy = extractString("content_security_policy");

    // 매니페스트 유효성 검사
    auto errors = manifest_.validate();
    if (!errors.empty()) {
        std::string combined_errors;
        for (const auto& err : errors) {
            if (!combined_errors.empty()) combined_errors += "; ";
            combined_errors += err;
        }
        last_error_ = combined_errors;
        return {false, combined_errors};
    }

    appendLog("매니페스트 로드 완료: " + manifest_.name + " v" + manifest_.version);
    return {true, ""};
}

bool Extension::activate() {
    auto current_state = state_.load();
    if (current_state == ExtensionState::Active) {
        return true;  // 이미 활성화됨
    }

    state_.store(ExtensionState::Loading);
    appendLog("확장 활성화 시작: " + manifest_.name);

    // 콘텐츠 스크립트 로드
    if (!loadContentScripts()) {
        state_.store(ExtensionState::Error);
        return false;
    }

    // 백그라운드 스크립트 초기화
    if (!initializeBackgroundScript()) {
        state_.store(ExtensionState::Error);
        return false;
    }

    state_.store(ExtensionState::Active);
    appendLog("확장 활성화 완료: " + manifest_.name);
    return true;
}

void Extension::deactivate() {
    if (state_.load() == ExtensionState::Unloaded) return;

    appendLog("확장 비활성화: " + manifest_.name);

    // 콘텐츠 스크립트 정리
    content_scripts_.clear();

    // 메시지 리스너 정리
    {
        std::lock_guard<std::mutex> lock(message_mutex_);
        message_listeners_.clear();
        external_message_listeners_.clear();
    }

    state_.store(ExtensionState::Disabled);
}

bool Extension::reload() {
    appendLog("확장 리로드: " + manifest_.name);

    // 비활성화 후 재활성화
    deactivate();

    auto [success, error] = loadManifest();
    if (!success) {
        last_error_ = "리로드 실패 (매니페스트): " + error;
        state_.store(ExtensionState::Error);
        return false;
    }

    return activate();
}

void Extension::suspend() {
    if (state_.load() != ExtensionState::Active) return;

    appendLog("확장 일시 중단: " + manifest_.name);
    state_.store(ExtensionState::Suspended);
    // V8 Isolate의 메모리를 해제하지만 상태는 보존
}

void Extension::resume() {
    if (state_.load() != ExtensionState::Suspended) return;

    appendLog("확장 재개: " + manifest_.name);
    state_.store(ExtensionState::Active);
}

// ============================
// 권한 관리
// ============================

bool Extension::hasPermission(Permission perm) const {
    // 매니페스트에 선언된 권한 확인
    if (manifest_.permissions.count(perm) > 0) {
        return true;
    }
    // 동적으로 부여된 선택적 권한 확인
    std::lock_guard<std::mutex> lock(permission_mutex_);
    return granted_optional_permissions_.count(perm) > 0;
}

bool Extension::hasHostPermission(const std::string& url) const {
    // 호스트 권한 패턴 매칭
    for (const auto& pattern : manifest_.host_permissions) {
        // <all_urls> 패턴
        if (pattern == "<all_urls>") return true;

        // 패턴을 정규식으로 변환
        // 예: "*://*.google.com/*" → "^(https?|\\*)://([^/]*\\.)?google\\.com/.*$"
        std::string regex_pattern = pattern;

        // 스킴 부분의 * 처리
        size_t scheme_end = regex_pattern.find("://");
        if (scheme_end != std::string::npos) {
            std::string scheme = regex_pattern.substr(0, scheme_end);
            if (scheme == "*") {
                regex_pattern = "(https?)" + regex_pattern.substr(scheme_end);
            }
        }

        // 호스트 부분의 *. 처리 (서브도메인 와일드카드)
        std::string escaped;
        for (size_t i = 0; i < regex_pattern.size(); ++i) {
            char c = regex_pattern[i];
            if (c == '.') {
                escaped += "\\.";
            } else if (c == '*') {
                escaped += ".*";
            } else {
                escaped += c;
            }
        }

        try {
            std::regex re("^" + escaped + "$", std::regex_constants::icase);
            if (std::regex_match(url, re)) {
                return true;
            }
        } catch (const std::regex_error&) {
            // 잘못된 정규식은 무시
            continue;
        }
    }
    return false;
}

bool Extension::grantPermission(Permission perm) {
    // 선택적 권한 목록에 있는지 확인
    if (manifest_.optional_permissions.count(perm) == 0) {
        last_error_ = "해당 권한은 optional_permissions에 선언되지 않았습니다: " + PermissionUtils::toString(perm);
        return false;
    }

    std::lock_guard<std::mutex> lock(permission_mutex_);
    granted_optional_permissions_.insert(perm);
    appendLog("권한 부여됨: " + PermissionUtils::toString(perm));
    return true;
}

void Extension::revokePermission(Permission perm) {
    std::lock_guard<std::mutex> lock(permission_mutex_);
    granted_optional_permissions_.erase(perm);
    appendLog("권한 회수됨: " + PermissionUtils::toString(perm));
}

std::unordered_set<Permission> Extension::grantedPermissions() const {
    std::lock_guard<std::mutex> lock(permission_mutex_);
    // 매니페스트 권한 + 동적 부여 권한 합집합
    auto result = manifest_.permissions;
    result.insert(granted_optional_permissions_.begin(), 
                  granted_optional_permissions_.end());
    return result;
}

// ============================
// 메시지 패싱
// ============================

void Extension::sendMessage(const ExtensionMessage& message,
                            std::function<void(const std::string&)> responseCallback) {
    ExtensionMessage msg = message;
    msg.extension_id = id_;
    msg.message_id = next_message_id_.fetch_add(1);
    msg.timestamp = std::chrono::system_clock::now();

    appendLog("메시지 전송: type=" + msg.type + ", context=" + msg.sender_context);

    // 내부 리스너에게 전달
    std::lock_guard<std::mutex> lock(message_mutex_);
    for (const auto& listener : message_listeners_) {
        try {
            listener(msg, responseCallback ? responseCallback : [](const std::string&) {});
        } catch (const std::exception& e) {
            appendLog("메시지 리스너 오류: " + std::string(e.what()));
        }
    }
}

void Extension::sendExternalMessage(const std::string& target_extension_id,
                                     const ExtensionMessage& message,
                                     std::function<void(const std::string&)> responseCallback) {
    ExtensionMessage msg = message;
    msg.extension_id = id_;
    msg.message_id = next_message_id_.fetch_add(1);
    msg.timestamp = std::chrono::system_clock::now();

    appendLog("외부 메시지 전송: target=" + target_extension_id + ", type=" + msg.type);

    // ExtensionManager를 통해 대상 확장에 라우팅됨
    // 여기서는 메시지 객체만 준비하고, Manager가 handleIncomingMessage를 호출
    // 라우팅은 ExtensionManager에서 처리
}

void Extension::onMessage(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(message_mutex_);
    message_listeners_.push_back(std::move(callback));
}

void Extension::onExternalMessage(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(message_mutex_);
    external_message_listeners_.push_back(std::move(callback));
}

void Extension::handleIncomingMessage(const ExtensionMessage& message,
                                       std::function<void(const std::string&)> sendResponse) {
    std::lock_guard<std::mutex> lock(message_mutex_);

    // 외부 메시지인 경우 (다른 확장에서 온 메시지)
    if (message.extension_id != id_) {
        for (const auto& listener : external_message_listeners_) {
            try {
                listener(message, sendResponse);
            } catch (const std::exception& e) {
                appendLog("외부 메시지 처리 오류: " + std::string(e.what()));
            }
        }
    } else {
        // 내부 메시지
        for (const auto& listener : message_listeners_) {
            try {
                listener(message, sendResponse);
            } catch (const std::exception& e) {
                appendLog("메시지 처리 오류: " + std::string(e.what()));
            }
        }
    }
}

// ============================
// 스토리지 API
// ============================

std::unordered_map<std::string, std::string> Extension::storageGet(
    StorageArea area,
    const std::vector<std::string>& keys) {
    
    // 스토리지 권한 확인
    if (!hasPermission(Permission::Storage)) {
        appendLog("스토리지 접근 거부: storage 권한 없음");
        return {};
    }

    std::lock_guard<std::mutex> lock(storage_mutex_);
    std::unordered_map<std::string, std::string> result;

    auto area_it = storage_data_.find(area);
    if (area_it == storage_data_.end()) {
        return result;  // 해당 영역에 데이터 없음
    }

    for (const auto& key : keys) {
        auto it = area_it->second.find(key);
        if (it != area_it->second.end()) {
            result[key] = it->second;
        }
    }

    return result;
}

void Extension::storageSet(StorageArea area,
                           const std::unordered_map<std::string, std::string>& items) {
    if (!hasPermission(Permission::Storage)) {
        appendLog("스토리지 쓰기 거부: storage 권한 없음");
        return;
    }

    std::vector<StorageChange> changes;

    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        auto& area_data = storage_data_[area];

        for (const auto& [key, value] : items) {
            StorageChange change;
            change.key = key;

            auto it = area_data.find(key);
            if (it != area_data.end()) {
                change.old_value = it->second;
            }
            change.new_value = value;

            area_data[key] = value;
            changes.push_back(std::move(change));
        }
    }

    // 변경 알림
    notifyStorageChange(changes, area);
}

void Extension::storageRemove(StorageArea area, const std::vector<std::string>& keys) {
    if (!hasPermission(Permission::Storage)) {
        appendLog("스토리지 삭제 거부: storage 권한 없음");
        return;
    }

    std::vector<StorageChange> changes;

    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        auto area_it = storage_data_.find(area);
        if (area_it == storage_data_.end()) return;

        for (const auto& key : keys) {
            auto it = area_it->second.find(key);
            if (it != area_it->second.end()) {
                StorageChange change;
                change.key = key;
                change.old_value = it->second;
                // new_value는 nullopt (삭제)
                changes.push_back(std::move(change));
                area_it->second.erase(it);
            }
        }
    }

    notifyStorageChange(changes, area);
}

void Extension::storageClear(StorageArea area) {
    if (!hasPermission(Permission::Storage)) return;

    std::vector<StorageChange> changes;

    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        auto area_it = storage_data_.find(area);
        if (area_it == storage_data_.end()) return;

        for (const auto& [key, value] : area_it->second) {
            StorageChange change;
            change.key = key;
            change.old_value = value;
            changes.push_back(std::move(change));
        }
        area_it->second.clear();
    }

    notifyStorageChange(changes, area);
}

size_t Extension::storageUsage(StorageArea area) const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    auto area_it = storage_data_.find(area);
    if (area_it == storage_data_.end()) return 0;

    size_t total = 0;
    for (const auto& [key, value] : area_it->second) {
        total += key.size() + value.size();
    }
    return total;
}

void Extension::onStorageChanged(StorageChangeCallback callback) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    storage_change_callbacks_.push_back(std::move(callback));
}

void Extension::notifyStorageChange(const std::vector<StorageChange>& changes, StorageArea area) {
    if (changes.empty()) return;

    std::lock_guard<std::mutex> lock(storage_mutex_);
    for (const auto& callback : storage_change_callbacks_) {
        try {
            callback(changes, area);
        } catch (const std::exception& e) {
            appendLog("스토리지 변경 콜백 오류: " + std::string(e.what()));
        }
    }
}

// ============================
// 콘텐츠 스크립트 관리
// ============================

const std::vector<std::shared_ptr<ContentScript>>& Extension::contentScripts() const {
    return content_scripts_;
}

std::vector<std::shared_ptr<ContentScript>> Extension::contentScriptsForUrl(
    const std::string& url) const {
    
    std::vector<std::shared_ptr<ContentScript>> matched;
    for (const auto& cs : content_scripts_) {
        if (cs->matchesUrl(url)) {
            matched.push_back(cs);
        }
    }
    return matched;
}

bool Extension::loadContentScripts() {
    content_scripts_.clear();

    for (const auto& cs_def : manifest_.content_scripts) {
        // 콘텐츠 스크립트 생성
        auto cs = std::make_shared<ContentScript>(id_);

        // 매치 패턴 설정
        for (const auto& pattern : cs_def.matches) {
            cs->addMatchPattern(pattern);
        }
        for (const auto& pattern : cs_def.exclude_matches) {
            cs->addExcludePattern(pattern);
        }

        // 실행 시점 설정
        if (cs_def.run_at == "document_start") {
            cs->setRunAt(RunAtTiming::DocumentStart);
        } else if (cs_def.run_at == "document_end") {
            cs->setRunAt(RunAtTiming::DocumentEnd);
        } else {
            cs->setRunAt(RunAtTiming::DocumentIdle);
        }

        cs->setAllFrames(cs_def.all_frames);
        cs->setMatchAboutBlank(cs_def.match_about_blank);

        // JavaScript 파일 로드
        for (const auto& js_file : cs_def.js) {
            fs::path full_path = fs::path(base_path_) / js_file;
            auto content = readFileInternal(full_path.string());
            if (!content.empty()) {
                cs->addJavaScript(js_file, content);
            } else {
                appendLog("콘텐츠 스크립트 JS 파일 로드 실패: " + js_file);
            }
        }

        // CSS 파일 로드
        for (const auto& css_file : cs_def.css) {
            fs::path full_path = fs::path(base_path_) / css_file;
            auto content = readFileInternal(full_path.string());
            if (!content.empty()) {
                cs->addStylesheet(css_file, content);
            } else {
                appendLog("콘텐츠 스크립트 CSS 파일 로드 실패: " + css_file);
            }
        }

        content_scripts_.push_back(std::move(cs));
    }

    appendLog("콘텐츠 스크립트 " + std::to_string(content_scripts_.size()) + "개 로드됨");
    return true;
}

bool Extension::initializeBackgroundScript() {
    // MV3: service_worker, MV2: background scripts
    std::string bg_script;

    if (!manifest_.background_service_worker.empty()) {
        fs::path full_path = fs::path(base_path_) / manifest_.background_service_worker;
        bg_script = readFileInternal(full_path.string());
    } else if (!manifest_.background_scripts.empty()) {
        // MV2: 여러 스크립트를 순서대로 합침
        for (const auto& script_file : manifest_.background_scripts) {
            fs::path full_path = fs::path(base_path_) / script_file;
            auto content = readFileInternal(full_path.string());
            if (!content.empty()) {
                bg_script += content + "\n";
            }
        }
    }

    if (bg_script.empty()) {
        // 백그라운드 스크립트가 없어도 되는 확장도 있음
        appendLog("백그라운드 스크립트 없음 (선택 사항)");
        return true;
    }

    // V8 Isolate에서 백그라운드 스크립트 실행
    // 실제 구현에서는 별도 스레드의 V8 Isolate에서 실행
    appendLog("백그라운드 스크립트 초기화됨 (" + std::to_string(bg_script.size()) + " bytes)");
    return true;
}

// ============================
// 팝업 UI
// ============================

std::optional<std::string> Extension::popupHtmlPath() const {
    if (manifest_.popup_html.empty()) return std::nullopt;
    return (fs::path(base_path_) / manifest_.popup_html).string();
}

std::optional<std::string> Extension::popupHtmlContent() const {
    auto path = popupHtmlPath();
    if (!path.has_value()) return std::nullopt;
    
    auto content = readFileInternal(path.value());
    if (content.empty()) return std::nullopt;
    return content;
}

// ============================
// 리소스 접근
// ============================

std::optional<std::string> Extension::readFile(const std::string& relative_path) const {
    fs::path full_path = fs::path(base_path_) / relative_path;
    
    // 경로 탈출 방지 (디렉토리 트래버설 차단)
    auto canonical_base = fs::weakly_canonical(fs::path(base_path_));
    auto canonical_target = fs::weakly_canonical(full_path);
    
    std::string base_str = canonical_base.string();
    std::string target_str = canonical_target.string();
    
    if (target_str.substr(0, base_str.size()) != base_str) {
        appendLog("보안 위반: 경로 탈출 시도 감지 — " + relative_path);
        return std::nullopt;
    }

    auto content = readFileInternal(full_path.string());
    if (content.empty()) return std::nullopt;
    return content;
}

bool Extension::isWebAccessibleResource(const std::string& resource_path,
                                          const std::string& page_url) const {
    for (const auto& war : manifest_.web_accessible_resources) {
        // 리소스 경로 매칭
        bool resource_match = false;
        for (const auto& pattern : war.resources) {
            if (pattern == resource_path || pattern == "*") {
                resource_match = true;
                break;
            }
            // 와일드카드 패턴 매칭 (간단 구현)
            if (pattern.find('*') != std::string::npos) {
                std::string regex_str = pattern;
                // *를 .*로 변환
                size_t pos = 0;
                while ((pos = regex_str.find('*', pos)) != std::string::npos) {
                    regex_str.replace(pos, 1, ".*");
                    pos += 2;
                }
                try {
                    std::regex re(regex_str);
                    if (std::regex_match(resource_path, re)) {
                        resource_match = true;
                        break;
                    }
                } catch (const std::regex_error&) {
                    continue;
                }
            }
        }

        if (!resource_match) continue;

        // 페이지 URL 매칭 (MV3: matches 필드 확인)
        if (war.matches.empty()) return true;  // 매칭 제한 없음

        for (const auto& match_pattern : war.matches) {
            if (match_pattern == "<all_urls>") return true;
            // URL 패턴 매칭 (ContentScript의 matchesUrl과 유사)
            // 간단히 도메인 포함 여부로 판단
            if (page_url.find(match_pattern) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// ============================
// 로깅
// ============================

void Extension::appendLog(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&time_t));

    std::string log_entry = "[" + std::string(time_buf) + "] [" + id_ + "] " + message;

    std::lock_guard<std::mutex> lock(log_mutex_);
    logs_.push_back(log_entry);

    // 로그 최대 크기 제한 (1000개)
    if (logs_.size() > 1000) {
        logs_.erase(logs_.begin(), logs_.begin() + 500);
    }
}

std::string Extension::readFileInternal(const std::string& full_path) const {
    std::ifstream file(full_path);
    if (!file.is_open()) return "";

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace ordinal::extensions
