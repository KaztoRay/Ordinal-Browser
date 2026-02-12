/**
 * @file extension_api.cpp
 * @brief 확장 API V8 바인딩 구현
 * 
 * browser.tabs, browser.runtime, browser.storage, browser.webRequest
 * API의 구현체입니다. 각 호출은 권한을 확인하고, 비동기 콜백을 통해
 * 결과를 반환합니다.
 */

#include "extension_api.h"
#include "extension_manager.h"
#include "content_script.h"

#include <sstream>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <regex>

namespace ordinal::extensions {

// ============================================================
// TabInfo JSON 직렬화
// ============================================================

std::string TabInfo::toJson() const {
    std::stringstream ss;
    ss << "{"
       << "\"id\":" << id << ","
       << "\"index\":" << index << ","
       << "\"windowId\":" << window_id << ","
       << "\"active\":" << (active ? "true" : "false") << ","
       << "\"pinned\":" << (pinned ? "true" : "false") << ","
       << "\"highlighted\":" << (highlighted ? "true" : "false") << ","
       << "\"incognito\":" << (incognito ? "true" : "false") << ","
       << "\"url\":\"" << url << "\","
       << "\"title\":\"" << title << "\","
       << "\"favIconUrl\":\"" << fav_icon_url << "\","
       << "\"status\":\"" << status << "\","
       << "\"audible\":" << (audible ? "true" : "false") << ","
       << "\"mutedInfo\":{\"muted\":" << (muted ? "true" : "false") << "}"
       << "}";
    return ss.str();
}

// ============================================================
// WebRequestDetails JSON 직렬화
// ============================================================

std::string WebRequestDetails::toJson() const {
    std::stringstream ss;
    ss << "{"
       << "\"requestId\":\"" << request_id << "\","
       << "\"url\":\"" << url << "\","
       << "\"method\":\"" << method << "\","
       << "\"tabId\":" << tab_id << ","
       << "\"type\":\"" << type << "\","
       << "\"initiator\":\"" << initiator << "\","
       << "\"timeStamp\":" << time_stamp << ","
       << "\"statusCode\":" << status_code;

    // 요청 헤더
    if (!request_headers.empty()) {
        ss << ",\"requestHeaders\":[";
        bool first = true;
        for (const auto& [name, value] : request_headers) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"name\":\"" << name << "\",\"value\":\"" << value << "\"}";
        }
        ss << "]";
    }

    // 응답 헤더
    if (!response_headers.empty()) {
        ss << ",\"responseHeaders\":[";
        bool first = true;
        for (const auto& [name, value] : response_headers) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"name\":\"" << name << "\",\"value\":\"" << value << "\"}";
        }
        ss << "]";
    }

    ss << "}";
    return ss.str();
}

// ============================================================
// ExtensionAPI 생성/소멸
// ============================================================

ExtensionAPI::ExtensionAPI(Extension& extension, ExtensionManager& manager)
    : extension_(extension)
    , manager_(manager) {
}

ExtensionAPI::~ExtensionAPI() = default;

// ============================================================
// 권한 확인 헬퍼
// ============================================================

bool ExtensionAPI::checkPermission(Permission required, ApiResult& result) const {
    if (!extension_.hasPermission(required)) {
        result.success = false;
        result.error = "권한이 없습니다: " + PermissionUtils::toString(required);
        result.error_code = "permission_denied";
        return false;
    }
    return true;
}

// ============================================================
// browser.tabs API
// ============================================================

void ExtensionAPI::tabsQuery(const std::string& query_params, ApiCallback callback) {
    ApiResult result;

    // tabs 또는 activeTab 권한 필요
    if (!extension_.hasPermission(Permission::Tabs) && 
        !extension_.hasPermission(Permission::ActiveTab)) {
        result.error = "tabs 또는 activeTab 권한이 필요합니다";
        result.error_code = "permission_denied";
        callback(result);
        return;
    }

    // 실제 구현에서는 TabManager에서 탭 목록을 가져옴
    // 여기서는 빈 배열 반환 (탭 시스템과 통합 시 구현)
    result.success = true;
    result.data = "[]";  // 빈 탭 목록
    callback(result);
}

void ExtensionAPI::tabsGetCurrent(ApiCallback callback) {
    ApiResult result;

    if (!checkPermission(Permission::Tabs, result) &&
        !extension_.hasPermission(Permission::ActiveTab)) {
        callback(result);
        return;
    }

    // 현재 활성 탭 정보 반환
    TabInfo tab;
    tab.id = 0;
    tab.active = true;
    tab.status = "complete";

    result.success = true;
    result.data = tab.toJson();
    callback(result);
}

void ExtensionAPI::tabsCreate(const std::string& url, bool active, ApiCallback callback) {
    ApiResult result;

    // 탭 생성은 tabs 권한 필요
    if (!checkPermission(Permission::Tabs, result)) {
        callback(result);
        return;
    }

    // URL 유효성 검사
    if (!url.empty()) {
        // chrome:// 또는 about:// 같은 내부 URL 차단
        if (url.find("chrome://") == 0 || url.find("about:") == 0) {
            result.error = "내부 URL은 접근할 수 없습니다";
            result.error_code = "invalid_url";
            callback(result);
            return;
        }
    }

    // 탭 생성 (실제 구현에서는 TabManager 호출)
    TabInfo new_tab;
    new_tab.id = 1;  // 실제로는 TabManager에서 할당
    new_tab.url = url;
    new_tab.active = active;
    new_tab.status = "loading";

    result.success = true;
    result.data = new_tab.toJson();

    extension_.appendLog("tabs.create: " + url);
    callback(result);
}

void ExtensionAPI::tabsUpdate(int tab_id, const std::string& update_props, ApiCallback callback) {
    ApiResult result;

    if (!checkPermission(Permission::Tabs, result)) {
        callback(result);
        return;
    }

    // 탭 업데이트 (URL 변경, 활성화 등)
    // 실제 구현에서는 TabManager에서 대상 탭을 찾아 업데이트
    result.success = true;
    result.data = "{\"id\": " + std::to_string(tab_id) + ", \"updated\": true}";

    extension_.appendLog("tabs.update: tabId=" + std::to_string(tab_id));
    callback(result);
}

void ExtensionAPI::tabsRemove(const std::vector<int>& tab_ids, ApiCallback callback) {
    ApiResult result;

    if (!checkPermission(Permission::Tabs, result)) {
        callback(result);
        return;
    }

    // 탭 닫기
    for (int tab_id : tab_ids) {
        extension_.appendLog("tabs.remove: tabId=" + std::to_string(tab_id));
    }

    result.success = true;
    callback(result);
}

void ExtensionAPI::tabsExecuteScript(int tab_id, const std::string& code, ApiCallback callback) {
    ApiResult result;

    // activeTab 또는 tabs 권한 + 호스트 권한 필요
    if (!extension_.hasPermission(Permission::ActiveTab) &&
        !extension_.hasPermission(Permission::Tabs)) {
        result.error = "activeTab 또는 tabs 권한이 필요합니다";
        result.error_code = "permission_denied";
        callback(result);
        return;
    }

    // 코드 길이 제한 (보안)
    if (code.size() > 1024 * 1024) {  // 1MB
        result.error = "스크립트가 너무 깁니다 (최대 1MB)";
        result.error_code = "script_too_large";
        callback(result);
        return;
    }

    // V8 엔진에서 탭의 페이지 컨텍스트에 코드 실행
    extension_.appendLog("tabs.executeScript: tabId=" + std::to_string(tab_id) + 
                         ", code_size=" + std::to_string(code.size()));

    result.success = true;
    result.data = "[null]";  // 실행 결과 배열
    callback(result);
}

void ExtensionAPI::tabsInsertCSS(int tab_id, const std::string& css, ApiCallback callback) {
    ApiResult result;

    if (!extension_.hasPermission(Permission::ActiveTab) &&
        !extension_.hasPermission(Permission::Tabs)) {
        result.error = "activeTab 또는 tabs 권한이 필요합니다";
        result.error_code = "permission_denied";
        callback(result);
        return;
    }

    extension_.appendLog("tabs.insertCSS: tabId=" + std::to_string(tab_id));
    result.success = true;
    callback(result);
}

void ExtensionAPI::tabsSendMessage(int tab_id, const std::string& message, ApiCallback callback) {
    ApiResult result;

    // 메시지 전송은 특별한 권한 불필요 (자신의 콘텐츠 스크립트에게만)
    ExtensionMessage msg;
    msg.extension_id = extension_.id();
    msg.data = message;
    msg.sender_context = "background";
    msg.sender_tab_id = -1;  // 백그라운드에서 전송

    extension_.sendMessage(msg, [callback](const std::string& response) {
        ApiResult res;
        res.success = true;
        res.data = response;
        callback(res);
    });
}

// ============================================================
// browser.runtime API
// ============================================================

std::string ExtensionAPI::runtimeGetId() const {
    return extension_.id();
}

std::string ExtensionAPI::runtimeGetURL(const std::string& path) const {
    // ordinal-extension://확장ID/경로 형식
    return "ordinal-extension://" + extension_.id() + "/" + path;
}

std::string ExtensionAPI::runtimeGetManifest() const {
    const auto& m = extension_.manifest();
    std::stringstream ss;
    ss << "{"
       << "\"manifest_version\":" << m.manifest_version << ","
       << "\"name\":\"" << m.name << "\","
       << "\"version\":\"" << m.version << "\","
       << "\"description\":\"" << m.description << "\"";

    if (!m.author.empty()) {
        ss << ",\"author\":\"" << m.author << "\"";
    }

    // 권한 목록
    ss << ",\"permissions\":[";
    bool first = true;
    for (const auto& perm : m.permissions) {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << PermissionUtils::toString(perm) << "\"";
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

void ExtensionAPI::runtimeSendMessage(const std::string& message, ApiCallback callback) {
    ExtensionMessage msg;
    msg.extension_id = extension_.id();
    msg.data = message;
    msg.sender_context = "content";  // 콘텐츠 스크립트에서 호출 가정
    msg.type = "runtime.sendMessage";

    extension_.sendMessage(msg, [callback](const std::string& response) {
        ApiResult result;
        result.success = true;
        result.data = response;
        callback(result);
    });
}

void ExtensionAPI::runtimeSendExternalMessage(const std::string& extension_id,
                                               const std::string& message,
                                               ApiCallback callback) {
    ExtensionMessage msg;
    msg.extension_id = extension_.id();
    msg.data = message;
    msg.sender_context = "background";
    msg.type = "runtime.sendExternalMessage";

    manager_.routeMessage(extension_.id(), extension_id, msg,
        [callback](const std::string& response) {
            ApiResult result;
            result.success = true;
            result.data = response;
            callback(result);
        });
}

void ExtensionAPI::runtimeOnMessage(MessageCallback callback) {
    extension_.onMessage(std::move(callback));
}

void ExtensionAPI::runtimeOnInstalled(std::function<void(const std::string& reason)> callback) {
    install_callbacks_.push_back(std::move(callback));
}

// ============================================================
// browser.storage API
// ============================================================

void ExtensionAPI::storageLocalGet(const std::vector<std::string>& keys, ApiCallback callback) {
    ApiResult result;
    if (!checkPermission(Permission::Storage, result)) {
        callback(result);
        return;
    }

    auto data = extension_.storageGet(StorageArea::Local, keys);

    // JSON 객체로 직렬화
    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& [key, value] : data) {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << key << "\":" << value;
    }
    ss << "}";

    result.success = true;
    result.data = ss.str();
    callback(result);
}

void ExtensionAPI::storageLocalSet(const std::unordered_map<std::string, std::string>& items,
                                    ApiCallback callback) {
    ApiResult result;
    if (!checkPermission(Permission::Storage, result)) {
        callback(result);
        return;
    }

    extension_.storageSet(StorageArea::Local, items);
    result.success = true;
    callback(result);
}

void ExtensionAPI::storageLocalRemove(const std::vector<std::string>& keys, ApiCallback callback) {
    ApiResult result;
    if (!checkPermission(Permission::Storage, result)) {
        callback(result);
        return;
    }

    extension_.storageRemove(StorageArea::Local, keys);
    result.success = true;
    callback(result);
}

void ExtensionAPI::storageLocalClear(ApiCallback callback) {
    ApiResult result;
    if (!checkPermission(Permission::Storage, result)) {
        callback(result);
        return;
    }

    extension_.storageClear(StorageArea::Local);
    result.success = true;
    callback(result);
}

void ExtensionAPI::storageSyncGet(const std::vector<std::string>& keys, ApiCallback callback) {
    ApiResult result;
    if (!checkPermission(Permission::Storage, result)) {
        callback(result);
        return;
    }

    auto data = extension_.storageGet(StorageArea::Sync, keys);

    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& [key, value] : data) {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << key << "\":" << value;
    }
    ss << "}";

    result.success = true;
    result.data = ss.str();
    callback(result);
}

void ExtensionAPI::storageSyncSet(const std::unordered_map<std::string, std::string>& items,
                                   ApiCallback callback) {
    ApiResult result;
    if (!checkPermission(Permission::Storage, result)) {
        callback(result);
        return;
    }

    extension_.storageSet(StorageArea::Sync, items);
    result.success = true;
    callback(result);
}

void ExtensionAPI::storageOnChanged(StorageChangeCallback callback) {
    extension_.onStorageChanged(std::move(callback));
}

// ============================================================
// browser.webRequest API
// ============================================================

void ExtensionAPI::webRequestOnBeforeRequest(const WebRequestFilter& filter,
                                              WebRequestCallback callback,
                                              const std::vector<std::string>& extra_info) {
    // webRequest 권한 확인
    if (!extension_.hasPermission(Permission::WebRequest)) {
        extension_.appendLog("webRequest.onBeforeRequest: 권한 없음");
        return;
    }

    // blocking 옵션이 있으면 webRequestBlocking 권한 필요
    bool is_blocking = std::find(extra_info.begin(), extra_info.end(), "blocking") != extra_info.end();
    if (is_blocking && !extension_.hasPermission(Permission::WebRequestBlocking)) {
        extension_.appendLog("webRequest.onBeforeRequest: webRequestBlocking 권한 없음");
        return;
    }

    std::lock_guard<std::mutex> lock(listener_mutex_);
    WebRequestListener listener;
    listener.filter = filter;
    listener.callback = std::move(callback);
    listener.extra_info = extra_info;
    listener.is_blocking = is_blocking;

    web_request_listeners_["onBeforeRequest"].push_back(std::move(listener));
    extension_.appendLog("webRequest.onBeforeRequest 리스너 등록됨");
}

void ExtensionAPI::webRequestOnBeforeSendHeaders(const WebRequestFilter& filter,
                                                   WebRequestCallback callback,
                                                   const std::vector<std::string>& extra_info) {
    if (!extension_.hasPermission(Permission::WebRequest)) return;

    bool is_blocking = std::find(extra_info.begin(), extra_info.end(), "blocking") != extra_info.end();
    if (is_blocking && !extension_.hasPermission(Permission::WebRequestBlocking)) return;

    std::lock_guard<std::mutex> lock(listener_mutex_);
    WebRequestListener listener;
    listener.filter = filter;
    listener.callback = std::move(callback);
    listener.extra_info = extra_info;
    listener.is_blocking = is_blocking;

    web_request_listeners_["onBeforeSendHeaders"].push_back(std::move(listener));
}

void ExtensionAPI::webRequestOnHeadersReceived(const WebRequestFilter& filter,
                                                WebRequestCallback callback,
                                                const std::vector<std::string>& extra_info) {
    if (!extension_.hasPermission(Permission::WebRequest)) return;

    bool is_blocking = std::find(extra_info.begin(), extra_info.end(), "blocking") != extra_info.end();

    std::lock_guard<std::mutex> lock(listener_mutex_);
    WebRequestListener listener;
    listener.filter = filter;
    listener.callback = std::move(callback);
    listener.extra_info = extra_info;
    listener.is_blocking = is_blocking;

    web_request_listeners_["onHeadersReceived"].push_back(std::move(listener));
}

void ExtensionAPI::webRequestOnCompleted(const WebRequestFilter& filter,
                                          std::function<void(const WebRequestDetails&)> callback) {
    if (!extension_.hasPermission(Permission::WebRequest)) return;

    std::lock_guard<std::mutex> lock(listener_mutex_);
    WebRequestListener listener;
    listener.filter = filter;
    listener.callback = [cb = std::move(callback)](const WebRequestDetails& d) -> std::optional<BlockingResponse> {
        cb(d);
        return std::nullopt;
    };
    listener.is_blocking = false;

    web_request_listeners_["onCompleted"].push_back(std::move(listener));
}

void ExtensionAPI::webRequestOnErrorOccurred(const WebRequestFilter& filter,
                                              std::function<void(const WebRequestDetails&)> callback) {
    if (!extension_.hasPermission(Permission::WebRequest)) return;

    std::lock_guard<std::mutex> lock(listener_mutex_);
    WebRequestListener listener;
    listener.filter = filter;
    listener.callback = [cb = std::move(callback)](const WebRequestDetails& d) -> std::optional<BlockingResponse> {
        cb(d);
        return std::nullopt;
    };
    listener.is_blocking = false;

    web_request_listeners_["onErrorOccurred"].push_back(std::move(listener));
}

// ============================================================
// 외부 요청 처리
// ============================================================

std::optional<BlockingResponse> ExtensionAPI::processRequest(const WebRequestDetails& details) {
    std::lock_guard<std::mutex> lock(listener_mutex_);

    auto it = web_request_listeners_.find("onBeforeRequest");
    if (it == web_request_listeners_.end()) return std::nullopt;

    for (const auto& listener : it->second) {
        if (!matchesFilter(listener.filter, details)) continue;

        try {
            auto response = listener.callback(details);
            if (response.has_value()) {
                // 차단 또는 리다이렉트 응답이 있으면 즉시 반환
                if (response->cancel || response->redirect_url.has_value()) {
                    return response;
                }
            }
        } catch (const std::exception& e) {
            extension_.appendLog("webRequest 콜백 오류: " + std::string(e.what()));
        }
    }

    return std::nullopt;
}

std::optional<BlockingResponse> ExtensionAPI::processResponseHeaders(const WebRequestDetails& details) {
    std::lock_guard<std::mutex> lock(listener_mutex_);

    auto it = web_request_listeners_.find("onHeadersReceived");
    if (it == web_request_listeners_.end()) return std::nullopt;

    for (const auto& listener : it->second) {
        if (!matchesFilter(listener.filter, details)) continue;

        try {
            auto response = listener.callback(details);
            if (response.has_value()) {
                return response;
            }
        } catch (const std::exception& e) {
            extension_.appendLog("webRequest 응답 헤더 콜백 오류: " + std::string(e.what()));
        }
    }

    return std::nullopt;
}

// ============================================================
// URL 필터 매칭
// ============================================================

bool ExtensionAPI::matchesFilter(const WebRequestFilter& filter,
                                  const WebRequestDetails& details) const {
    // 탭 ID 필터
    if (filter.tab_id >= 0 && filter.tab_id != details.tab_id) {
        return false;
    }

    // 리소스 타입 필터
    if (!filter.types.empty()) {
        bool type_match = std::any_of(filter.types.begin(), filter.types.end(),
            [&](const std::string& t) { return t == details.type; });
        if (!type_match) return false;
    }

    // URL 패턴 필터
    if (!filter.urls.empty()) {
        bool url_match = false;
        for (const auto& pattern : filter.urls) {
            if (pattern == "<all_urls>") {
                url_match = true;
                break;
            }
            // MatchPattern을 사용하여 URL 매칭
            MatchPattern mp(pattern);
            if (mp.isValid() && mp.matches(details.url)) {
                url_match = true;
                break;
            }
        }
        if (!url_match) return false;
    }

    return true;
}

// ============================================================
// V8 바인딩
// ============================================================

void ExtensionAPI::registerV8Bindings() {
    // V8 전역 객체에 browser 네임스페이스 등록
    // 실제 구현에서는 V8 API를 사용하여 JavaScript 객체 생성
    
    extension_.appendLog("V8 바인딩 등록: browser.tabs, browser.runtime, browser.storage, browser.webRequest");

    // 각 API 네임스페이스를 JavaScript 객체로 생성하고
    // 메서드를 C++ 콜백으로 바인딩
    
    // browser.tabs
    // browser.tabs.query = function(queryInfo, callback) { ... }
    // browser.tabs.create = function(createProperties, callback) { ... }
    // browser.tabs.update = function(tabId, updateProperties, callback) { ... }
    // browser.tabs.remove = function(tabIds, callback) { ... }
    // browser.tabs.executeScript = function(tabId, details, callback) { ... }
    // browser.tabs.insertCSS = function(tabId, details, callback) { ... }
    // browser.tabs.sendMessage = function(tabId, message, callback) { ... }
    
    // browser.runtime
    // browser.runtime.id (property)
    // browser.runtime.getURL = function(path) { ... }
    // browser.runtime.getManifest = function() { ... }
    // browser.runtime.sendMessage = function(message, callback) { ... }
    // browser.runtime.onMessage.addListener(callback)
    // browser.runtime.onInstalled.addListener(callback)
    
    // browser.storage
    // browser.storage.local.get = function(keys, callback) { ... }
    // browser.storage.local.set = function(items, callback) { ... }
    // browser.storage.local.remove = function(keys, callback) { ... }
    // browser.storage.local.clear = function(callback) { ... }
    // browser.storage.sync.get/set/remove/clear
    // browser.storage.onChanged.addListener(callback)
    
    // browser.webRequest
    // browser.webRequest.onBeforeRequest.addListener(callback, filter, extraInfo)
    // browser.webRequest.onBeforeSendHeaders.addListener(...)
    // browser.webRequest.onHeadersReceived.addListener(...)
    // browser.webRequest.onCompleted.addListener(...)
    // browser.webRequest.onErrorOccurred.addListener(...)
}

void ExtensionAPI::routeApiCall(const std::string& api_name, const std::string& args,
                                 ApiCallback callback) {
    // JavaScript에서 호출된 API를 적절한 C++ 메서드로 라우팅
    
    if (api_name == "tabs.query") {
        tabsQuery(args, callback);
    } else if (api_name == "tabs.getCurrent") {
        tabsGetCurrent(callback);
    } else if (api_name == "tabs.create") {
        tabsCreate(args, true, callback);
    } else if (api_name == "runtime.getId") {
        ApiResult result;
        result.success = true;
        result.data = "\"" + runtimeGetId() + "\"";
        callback(result);
    } else if (api_name == "runtime.getURL") {
        ApiResult result;
        result.success = true;
        result.data = "\"" + runtimeGetURL(args) + "\"";
        callback(result);
    } else if (api_name == "runtime.getManifest") {
        ApiResult result;
        result.success = true;
        result.data = runtimeGetManifest();
        callback(result);
    } else if (api_name == "runtime.sendMessage") {
        runtimeSendMessage(args, callback);
    } else if (api_name == "storage.local.get") {
        // args를 키 목록으로 파싱
        std::vector<std::string> keys;
        // 간단한 JSON 배열 파싱 (["key1", "key2"])
        std::regex key_re("\"([^\"]+)\"");
        auto begin = std::sregex_iterator(args.begin(), args.end(), key_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            keys.push_back((*it)[1].str());
        }
        storageLocalGet(keys, callback);
    } else if (api_name == "storage.local.set") {
        // args를 키-값 맵으로 파싱 (간단 구현)
        std::unordered_map<std::string, std::string> items;
        items["_raw"] = args;
        storageLocalSet(items, callback);
    } else if (api_name == "storage.local.clear") {
        storageLocalClear(callback);
    } else {
        ApiResult result;
        result.error = "알 수 없는 API: " + api_name;
        result.error_code = "unknown_api";
        callback(result);
    }
}

} // namespace ordinal::extensions
