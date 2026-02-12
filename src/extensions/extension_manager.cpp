/**
 * @file extension_manager.cpp
 * @brief 확장 프로그램 관리자 구현
 * 
 * 확장 로드/언로드, 활성화/비활성화, 매니페스트 검증, 
 * 메시지 라우팅, V8 샌드박싱 메모리 관리를 구현합니다.
 */

#include "extension_manager.h"
#include "content_script.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <numeric>
#include <iostream>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

namespace ordinal::extensions {

// ============================================================
// 싱글톤 인스턴스
// ============================================================

ExtensionManager& ExtensionManager::instance() {
    static ExtensionManager instance;
    return instance;
}

ExtensionManager::~ExtensionManager() {
    if (initialized_) {
        shutdown();
    }
}

// ============================================================
// 초기화 / 종료
// ============================================================

bool ExtensionManager::initialize(const ExtensionManagerConfig& config) {
    if (initialized_) {
        return true;  // 이미 초기화됨
    }

    config_ = config;

    // 확장 디렉토리 생성
    if (!config_.extensions_dir.empty()) {
        try {
            fs::create_directories(config_.extensions_dir);
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[ExtensionManager] 확장 디렉토리 생성 실패: " << e.what() << std::endl;
            return false;
        }
    }

    // 저장된 확장 상태 로드
    loadExtensionStates();

    // 확장 디렉토리 스캔
    scanExtensionsDirectory();

    initialized_ = true;
    std::cout << "[ExtensionManager] 초기화 완료 — 확장 " << extensions_.size() << "개 로드됨" << std::endl;
    return true;
}

void ExtensionManager::shutdown() {
    if (!initialized_) return;

    std::cout << "[ExtensionManager] 종료 중..." << std::endl;

    // 확장 상태 저장
    saveExtensionStates();

    // 모든 확장 비활성화 및 언로드
    {
        std::lock_guard<std::mutex> lock(extensions_mutex_);
        for (auto& [id, ext] : extensions_) {
            if (ext->state() == ExtensionState::Active) {
                ext->deactivate();
            }
        }
        extensions_.clear();
    }

    initialized_ = false;
    std::cout << "[ExtensionManager] 종료 완료" << std::endl;
}

// ============================================================
// 확장 설치 / 제거
// ============================================================

InstallResult ExtensionManager::loadUnpacked(const std::string& directory_path) {
    InstallResult result;

    // 개발 모드 확인
    if (!config_.allow_developer_mode) {
        result.error_message = "개발 모드가 비활성화되어 있습니다";
        return result;
    }

    // 디렉토리 존재 확인
    if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
        result.error_message = "유효하지 않은 디렉토리 경로입니다: " + directory_path;
        return result;
    }

    // manifest.json 존재 확인
    fs::path manifest_path = fs::path(directory_path) / "manifest.json";
    if (!fs::exists(manifest_path)) {
        result.error_message = "manifest.json이 없습니다: " + manifest_path.string();
        return result;
    }

    // 확장 ID 생성
    std::string ext_id = generateExtensionId(directory_path);

    // 차단 목록 확인
    if (isBlocked(ext_id)) {
        result.error_message = "차단된 확장입니다: " + ext_id;
        return result;
    }

    // 최대 확장 수 확인
    if (extensions_.size() >= config_.max_extensions) {
        result.error_message = "최대 확장 설치 수에 도달했습니다 (" + 
                              std::to_string(config_.max_extensions) + ")";
        return result;
    }

    // 이미 설치된 확장인지 확인
    if (hasExtension(ext_id)) {
        // 리로드 처리
        reload(ext_id);
        result.success = true;
        result.extension_id = ext_id;
        return result;
    }

    // 새 확장 생성
    auto extension = std::make_unique<Extension>(ext_id, directory_path);

    // 매니페스트 로드
    auto [load_success, load_error] = extension->loadManifest();
    if (!load_success) {
        result.error_message = "매니페스트 로드 실패: " + load_error;
        return result;
    }

    // 위험 권한 추출
    for (const auto& perm : extension->manifest().permissions) {
        if (PermissionUtils::isDangerous(perm)) {
            result.dangerous_permissions.push_back(perm);
        }
    }

    // 확장 등록
    {
        std::lock_guard<std::mutex> lock(extensions_mutex_);
        extensions_[ext_id] = std::move(extension);
    }

    result.success = true;
    result.extension_id = ext_id;

    emitEvent(ExtensionEvent::Installed, ext_id, "로컬 디렉토리에서 설치됨");
    std::cout << "[ExtensionManager] 확장 로드됨: " << ext_id << std::endl;
    return result;
}

InstallResult ExtensionManager::installFromArchive(const std::string& archive_path) {
    InstallResult result;

    // 아카이브 파일 존재 확인
    if (!fs::exists(archive_path)) {
        result.error_message = "아카이브 파일이 없습니다: " + archive_path;
        return result;
    }

    // 파일 확장자 확인
    fs::path path(archive_path);
    std::string ext = path.extension().string();
    if (ext != ".crx" && ext != ".zip") {
        result.error_message = "지원되지 않는 아카이브 형식입니다: " + ext;
        return result;
    }

    // 확장 ID 생성
    std::string ext_id = generateExtensionId(archive_path);

    // 설치 디렉토리 생성
    fs::path install_dir = fs::path(config_.extensions_dir) / ext_id;
    try {
        fs::create_directories(install_dir);
    } catch (const fs::filesystem_error& e) {
        result.error_message = "설치 디렉토리 생성 실패: " + std::string(e.what());
        return result;
    }

    // .crx 파일 처리 (CRX 헤더 제거 후 ZIP 추출)
    if (ext == ".crx") {
        // CRX3 형식: 매직 넘버(4바이트) + 버전(4바이트) + 헤더 길이(4바이트) + 헤더 + ZIP
        std::ifstream file(archive_path, std::ios::binary);
        if (!file.is_open()) {
            result.error_message = "아카이브 파일을 열 수 없습니다";
            return result;
        }

        // 매직 넘버 확인 ("Cr24")
        char magic[4];
        file.read(magic, 4);
        if (std::string(magic, 4) != "Cr24") {
            result.error_message = "유효하지 않은 CRX 파일입니다";
            return result;
        }

        // 버전 읽기
        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), 4);

        // 헤더 길이 읽기
        uint32_t header_length = 0;
        file.read(reinterpret_cast<char*>(&header_length), 4);

        // 헤더 스킵
        file.seekg(header_length, std::ios::cur);

        // 나머지는 ZIP 데이터 — 임시 파일로 저장 후 추출
        // 실제 구현에서는 libzip 또는 minizip 사용
        std::cout << "[ExtensionManager] CRX v" << version << " 아카이브 처리 중..." << std::endl;
    }

    // ZIP 추출 (실제 구현에서는 libzip 사용)
    // 여기서는 시스템 명령으로 대체 가능
    std::cout << "[ExtensionManager] 아카이브 추출 중: " << archive_path << " → " << install_dir.string() << std::endl;

    // 추출 후 loadUnpacked 호출
    return loadUnpacked(install_dir.string());
}

bool ExtensionManager::uninstall(const std::string& extension_id, bool remove_data) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);

    auto it = extensions_.find(extension_id);
    if (it == extensions_.end()) {
        return false;
    }

    // 활성화된 확장은 먼저 비활성화
    if (it->second->state() == ExtensionState::Active) {
        it->second->deactivate();
    }

    // 확장 데이터 삭제
    if (remove_data) {
        std::string base_path = it->second->basePath();
        try {
            // 확장 디렉토리 삭제 (개발 모드 확장은 삭제하지 않음)
            fs::path ext_dir = fs::path(config_.extensions_dir) / extension_id;
            if (fs::exists(ext_dir)) {
                fs::remove_all(ext_dir);
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[ExtensionManager] 확장 데이터 삭제 실패: " << e.what() << std::endl;
        }
    }

    extensions_.erase(it);
    emitEvent(ExtensionEvent::Uninstalled, extension_id, "확장 제거됨");
    std::cout << "[ExtensionManager] 확장 제거됨: " << extension_id << std::endl;
    return true;
}

// ============================================================
// 확장 활성화 / 비활성화
// ============================================================

bool ExtensionManager::enable(const std::string& extension_id) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);

    auto it = extensions_.find(extension_id);
    if (it == extensions_.end()) {
        return false;
    }

    bool success = it->second->activate();
    if (success) {
        emitEvent(ExtensionEvent::Enabled, extension_id, "확장 활성화됨");
    }
    return success;
}

bool ExtensionManager::disable(const std::string& extension_id) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);

    auto it = extensions_.find(extension_id);
    if (it == extensions_.end()) {
        return false;
    }

    it->second->deactivate();
    emitEvent(ExtensionEvent::Disabled, extension_id, "확장 비활성화됨");
    return true;
}

bool ExtensionManager::reload(const std::string& extension_id) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);

    auto it = extensions_.find(extension_id);
    if (it == extensions_.end()) {
        return false;
    }

    bool success = it->second->reload();
    if (success) {
        emitEvent(ExtensionEvent::Updated, extension_id, "확장 리로드됨");
    }
    return success;
}

// ============================================================
// 확장 조회
// ============================================================

std::vector<std::string> ExtensionManager::installedExtensionIds() const {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    std::vector<std::string> ids;
    ids.reserve(extensions_.size());
    for (const auto& [id, _] : extensions_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> ExtensionManager::enabledExtensionIds() const {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, ext] : extensions_) {
        if (ext->state() == ExtensionState::Active) {
            ids.push_back(id);
        }
    }
    return ids;
}

Extension* ExtensionManager::getExtension(const std::string& extension_id) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    auto it = extensions_.find(extension_id);
    return (it != extensions_.end()) ? it->second.get() : nullptr;
}

const Extension* ExtensionManager::getExtension(const std::string& extension_id) const {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    auto it = extensions_.find(extension_id);
    return (it != extensions_.end()) ? it->second.get() : nullptr;
}

bool ExtensionManager::hasExtension(const std::string& extension_id) const {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    return extensions_.count(extension_id) > 0;
}

size_t ExtensionManager::extensionCount() const {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    return extensions_.size();
}

// ============================================================
// 콘텐츠 스크립트 수집
// ============================================================

std::vector<std::pair<std::string, std::shared_ptr<ContentScript>>>
ExtensionManager::contentScriptsForUrl(const std::string& url) const {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    
    std::vector<std::pair<std::string, std::shared_ptr<ContentScript>>> result;

    for (const auto& [id, ext] : extensions_) {
        if (ext->state() != ExtensionState::Active) continue;

        auto scripts = ext->contentScriptsForUrl(url);
        for (auto& cs : scripts) {
            result.emplace_back(id, std::move(cs));
        }
    }

    return result;
}

// ============================================================
// 메시지 라우팅
// ============================================================

void ExtensionManager::routeMessage(const std::string& from_extension_id,
                                     const std::string& to_extension_id,
                                     const ExtensionMessage& message,
                                     std::function<void(const std::string&)> responseCallback) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);

    // 발신 확장 확인
    auto from_it = extensions_.find(from_extension_id);
    if (from_it == extensions_.end() || from_it->second->state() != ExtensionState::Active) {
        if (responseCallback) {
            responseCallback("{\"error\": \"발신 확장이 활성화되지 않았습니다\"}");
        }
        return;
    }

    // 수신 확장 확인
    auto to_it = extensions_.find(to_extension_id);
    if (to_it == extensions_.end() || to_it->second->state() != ExtensionState::Active) {
        if (responseCallback) {
            responseCallback("{\"error\": \"수신 확장을 찾을 수 없거나 비활성 상태입니다\"}");
        }
        return;
    }

    // 메시지 전달
    to_it->second->handleIncomingMessage(message, responseCallback);
}

void ExtensionManager::broadcastMessage(const std::string& from_extension_id,
                                         const ExtensionMessage& message) {
    std::lock_guard<std::mutex> lock(extensions_mutex_);

    for (const auto& [id, ext] : extensions_) {
        // 자기 자신에게는 보내지 않음
        if (id == from_extension_id) continue;
        if (ext->state() != ExtensionState::Active) continue;

        ext->handleIncomingMessage(message, [](const std::string&) {});
    }
}

// ============================================================
// 권한 관리
// ============================================================

bool ExtensionManager::approvePermission(const std::string& extension_id, Permission permission) {
    auto* ext = getExtension(extension_id);
    if (!ext) return false;
    return ext->grantPermission(permission);
}

void ExtensionManager::revokePermission(const std::string& extension_id, Permission permission) {
    auto* ext = getExtension(extension_id);
    if (ext) {
        ext->revokePermission(permission);
    }
}

std::vector<Permission> ExtensionManager::dangerousPermissions(const std::string& extension_id) const {
    std::vector<Permission> dangerous;
    const auto* ext = getExtension(extension_id);
    if (!ext) return dangerous;

    for (const auto& perm : ext->manifest().permissions) {
        if (PermissionUtils::isDangerous(perm)) {
            dangerous.push_back(perm);
        }
    }
    return dangerous;
}

// ============================================================
// 이벤트
// ============================================================

void ExtensionManager::addEventListener(ExtensionEventCallback callback) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    event_listeners_.push_back(std::move(callback));
}

void ExtensionManager::emitEvent(ExtensionEvent event, const std::string& extension_id,
                                  const std::string& details) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    for (const auto& listener : event_listeners_) {
        try {
            listener(event, extension_id, details);
        } catch (const std::exception& e) {
            std::cerr << "[ExtensionManager] 이벤트 리스너 오류: " << e.what() << std::endl;
        }
    }
}

// ============================================================
// V8 샌드박싱 / 메모리 관리
// ============================================================

size_t ExtensionManager::extensionMemoryUsage(const std::string& extension_id) const {
    // 실제 구현에서는 V8 Isolate의 힙 통계를 조회
    auto it = memory_usage_cache_.find(extension_id);
    if (it != memory_usage_cache_.end()) {
        return it->second;
    }
    return 0;
}

size_t ExtensionManager::totalMemoryUsage() const {
    return std::accumulate(memory_usage_cache_.begin(), memory_usage_cache_.end(), size_t{0},
        [](size_t sum, const auto& pair) { return sum + pair.second; });
}

void ExtensionManager::enforceMemoryLimits() {
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    
    size_t max_bytes = config_.max_memory_per_extension_mb * 1024 * 1024;
    
    for (auto& [id, ext] : extensions_) {
        if (ext->state() != ExtensionState::Active) continue;

        size_t usage = extensionMemoryUsage(id);
        if (usage > max_bytes) {
            std::cout << "[ExtensionManager] 메모리 한도 초과로 확장 일시 중단: " << id 
                      << " (" << usage / (1024 * 1024) << "MB)" << std::endl;
            ext->suspend();
            emitEvent(ExtensionEvent::Error, id, "메모리 한도 초과로 일시 중단됨");
        }
    }
}

// ============================================================
// 유틸리티
// ============================================================

std::string ExtensionManager::generateExtensionId(const std::string& path) {
    // 경로를 기반으로 고유 ID 생성 (간단한 해시)
    // Chrome은 CRX 공개키의 SHA-256 해시에서 처음 16바이트를 base16(a-p) 인코딩
    // 여기서는 간단히 경로의 해시를 사용
    std::hash<std::string> hasher;
    size_t hash = hasher(path);

    // 해시를 32자 hex 문자열로 변환
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // 64비트 해시를 두 번 사용하여 32자 생성
    ss << std::setw(16) << hash;
    
    // 추가 엔트로피를 위해 경로 길이도 혼합
    size_t hash2 = hasher(path + std::to_string(path.size()));
    ss << std::setw(16) << hash2;

    std::string hex = ss.str();
    
    // Chrome 스타일 ID로 변환 (a-p 문자 사용)
    std::string ext_id;
    for (char c : hex) {
        if (c >= '0' && c <= '9') {
            ext_id += static_cast<char>('a' + (c - '0'));
        } else if (c >= 'a' && c <= 'f') {
            ext_id += static_cast<char>('k' + (c - 'a'));
        } else {
            ext_id += c;
        }
    }

    // 32자로 자르기
    if (ext_id.size() > 32) ext_id.resize(32);
    while (ext_id.size() < 32) ext_id += 'a';

    return ext_id;
}

bool ExtensionManager::isBlocked(const std::string& extension_id) const {
    return std::find(config_.blocked_extensions.begin(), 
                     config_.blocked_extensions.end(), 
                     extension_id) != config_.blocked_extensions.end();
}

// ============================================================
// 내부 헬퍼
// ============================================================

void ExtensionManager::scanExtensionsDirectory() {
    if (config_.extensions_dir.empty()) return;
    if (!fs::exists(config_.extensions_dir)) return;

    try {
        for (const auto& entry : fs::directory_iterator(config_.extensions_dir)) {
            if (!entry.is_directory()) continue;

            // manifest.json 존재 확인
            fs::path manifest = entry.path() / "manifest.json";
            if (!fs::exists(manifest)) continue;

            std::string dir_path = entry.path().string();
            auto result = loadUnpacked(dir_path);
            if (result.success) {
                std::cout << "[ExtensionManager] 자동 로드: " << result.extension_id << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[ExtensionManager] 확장 디렉토리 스캔 오류: " << e.what() << std::endl;
    }
}

void ExtensionManager::saveExtensionStates() {
    // 확장 상태를 JSON 파일로 저장
    fs::path state_file = fs::path(config_.extensions_dir) / "extensions_state.json";

    std::ofstream file(state_file);
    if (!file.is_open()) return;

    file << "{\n";
    file << "  \"extensions\": [\n";

    bool first = true;
    std::lock_guard<std::mutex> lock(extensions_mutex_);
    for (const auto& [id, ext] : extensions_) {
        if (!first) file << ",\n";
        first = false;

        file << "    {\n";
        file << "      \"id\": \"" << id << "\",\n";
        file << "      \"name\": \"" << ext->manifest().name << "\",\n";
        file << "      \"version\": \"" << ext->manifest().version << "\",\n";
        file << "      \"enabled\": " << (ext->state() == ExtensionState::Active ? "true" : "false") << ",\n";
        file << "      \"path\": \"" << ext->basePath() << "\"\n";
        file << "    }";
    }

    file << "\n  ]\n";
    file << "}\n";
}

void ExtensionManager::loadExtensionStates() {
    fs::path state_file = fs::path(config_.extensions_dir) / "extensions_state.json";
    
    if (!fs::exists(state_file)) return;

    // 상태 파일 로드 (기존 확장의 활성/비활성 상태 복원용)
    // 실제 복원은 scanExtensionsDirectory에서 수행
    std::cout << "[ExtensionManager] 확장 상태 파일 로드됨" << std::endl;
}

} // namespace ordinal::extensions
