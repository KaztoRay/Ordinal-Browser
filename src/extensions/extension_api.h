#pragma once

/**
 * @file extension_api.h
 * @brief 확장 API — V8 바인딩 (browser.* API)
 * 
 * Chrome/Firefox 확장의 browser.* API에 해당하는 V8 바인딩을 제공합니다.
 * tabs, runtime, storage, webRequest API를 지원하며,
 * 비동기 콜백 처리와 권한 기반 접근 제어를 구현합니다.
 */

#include "extension.h"

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <variant>
#include <mutex>

namespace ordinal::extensions {

// 전방 선언
class ExtensionManager;

/**
 * @brief API 호출 결과
 */
struct ApiResult {
    bool success{false};
    std::string data;           ///< JSON 직렬화된 결과 데이터
    std::string error;          ///< 에러 메시지
    std::string error_code;     ///< 에러 코드 (예: "permission_denied")
};

/**
 * @brief 비동기 API 콜백
 */
using ApiCallback = std::function<void(const ApiResult&)>;

/**
 * @brief 탭 정보 구조체
 */
struct TabInfo {
    int id{-1};                     ///< 탭 ID
    int index{0};                   ///< 탭 인덱스 (왼쪽부터)
    int window_id{-1};              ///< 소속 윈도우 ID
    bool active{false};             ///< 활성 탭 여부
    bool pinned{false};             ///< 고정 탭 여부
    bool highlighted{false};        ///< 하이라이트 여부
    bool incognito{false};          ///< 시크릿 모드 여부
    std::string url;                ///< 현재 URL
    std::string title;              ///< 페이지 제목
    std::string fav_icon_url;       ///< 파비콘 URL
    std::string status;             ///< "loading" 또는 "complete"
    bool audible{false};            ///< 소리 재생 중
    bool muted{false};              ///< 음소거 여부

    /// JSON 직렬화
    [[nodiscard]] std::string toJson() const;
};

/**
 * @brief WebRequest 요청 세부 정보
 */
struct WebRequestDetails {
    std::string request_id;         ///< 요청 고유 ID
    std::string url;                ///< 요청 URL
    std::string method;             ///< HTTP 메서드
    int tab_id{-1};                 ///< 요청 발생 탭 ID
    std::string type;               ///< 리소스 타입 ("main_frame", "script", "image" 등)
    std::string initiator;          ///< 요청 발생 출처
    double time_stamp{0.0};         ///< 요청 시간 (밀리초)

    /// 요청 헤더
    std::unordered_map<std::string, std::string> request_headers;
    /// 응답 헤더
    std::unordered_map<std::string, std::string> response_headers;
    /// HTTP 상태 코드
    int status_code{0};

    /// JSON 직렬화
    [[nodiscard]] std::string toJson() const;
};

/**
 * @brief WebRequest 차단 응답
 */
struct BlockingResponse {
    bool cancel{false};                                     ///< 요청 취소
    std::optional<std::string> redirect_url;                ///< 리다이렉트 URL
    std::unordered_map<std::string, std::string> request_headers;    ///< 수정된 요청 헤더
    std::unordered_map<std::string, std::string> response_headers;   ///< 수정된 응답 헤더
};

/**
 * @brief WebRequest 이벤트 필터
 */
struct WebRequestFilter {
    std::vector<std::string> urls;  ///< URL 패턴 목록
    std::vector<std::string> types; ///< 리소스 타입 필터
    int tab_id{-1};                 ///< 특정 탭 필터 (-1이면 모든 탭)
    int window_id{-1};              ///< 특정 윈도우 필터
};

/**
 * @brief WebRequest 이벤트 콜백
 */
using WebRequestCallback = std::function<std::optional<BlockingResponse>(const WebRequestDetails&)>;

/**
 * @brief 확장 API 클래스
 * 
 * browser.tabs, browser.runtime, browser.storage, browser.webRequest
 * API를 V8 엔진에 바인딩합니다. 각 API 호출은 권한을 확인한 후 실행됩니다.
 */
class ExtensionAPI {
public:
    /**
     * @brief 확장에 대한 API 인스턴스 생성
     * @param extension 대상 확장
     * @param manager 확장 관리자 참조
     */
    ExtensionAPI(Extension& extension, ExtensionManager& manager);
    ~ExtensionAPI();

    ExtensionAPI(const ExtensionAPI&) = delete;
    ExtensionAPI& operator=(const ExtensionAPI&) = delete;

    // ============================
    // browser.tabs API
    // ============================

    /**
     * @brief 탭 목록 조회 (browser.tabs.query)
     * @param query_params JSON 쿼리 파라미터 (active, currentWindow, url 등)
     * @param callback 결과 콜백
     */
    void tabsQuery(const std::string& query_params, ApiCallback callback);

    /**
     * @brief 현재 활성 탭 조회 (browser.tabs.getCurrent)
     * @param callback 결과 콜백
     */
    void tabsGetCurrent(ApiCallback callback);

    /**
     * @brief 새 탭 생성 (browser.tabs.create)
     * @param url 열 URL
     * @param active 활성화 여부
     * @param callback 결과 콜백
     */
    void tabsCreate(const std::string& url, bool active, ApiCallback callback);

    /**
     * @brief 탭 업데이트 (browser.tabs.update)
     * @param tab_id 탭 ID
     * @param update_props JSON 업데이트 속성
     * @param callback 결과 콜백
     */
    void tabsUpdate(int tab_id, const std::string& update_props, ApiCallback callback);

    /**
     * @brief 탭 닫기 (browser.tabs.remove)
     * @param tab_ids 닫을 탭 ID 목록
     * @param callback 결과 콜백
     */
    void tabsRemove(const std::vector<int>& tab_ids, ApiCallback callback);

    /**
     * @brief 탭에서 스크립트 실행 (browser.tabs.executeScript)
     * @param tab_id 대상 탭 ID
     * @param code 실행할 JavaScript 코드
     * @param callback 결과 콜백
     */
    void tabsExecuteScript(int tab_id, const std::string& code, ApiCallback callback);

    /**
     * @brief 탭에 CSS 삽입 (browser.tabs.insertCSS)
     * @param tab_id 대상 탭 ID
     * @param css 삽입할 CSS
     * @param callback 결과 콜백
     */
    void tabsInsertCSS(int tab_id, const std::string& css, ApiCallback callback);

    /**
     * @brief 탭에 메시지 전송 (browser.tabs.sendMessage)
     * @param tab_id 대상 탭 ID
     * @param message JSON 메시지
     * @param callback 응답 콜백
     */
    void tabsSendMessage(int tab_id, const std::string& message, ApiCallback callback);

    // ============================
    // browser.runtime API
    // ============================

    /**
     * @brief 확장 ID 조회 (browser.runtime.id)
     */
    [[nodiscard]] std::string runtimeGetId() const;

    /**
     * @brief 확장 URL 생성 (browser.runtime.getURL)
     * @param path 확장 내 상대 경로
     * @return 전체 URL
     */
    [[nodiscard]] std::string runtimeGetURL(const std::string& path) const;

    /**
     * @brief 매니페스트 조회 (browser.runtime.getManifest)
     * @return JSON 직렬화된 매니페스트
     */
    [[nodiscard]] std::string runtimeGetManifest() const;

    /**
     * @brief 메시지 전송 (browser.runtime.sendMessage)
     * @param message JSON 메시지
     * @param callback 응답 콜백
     */
    void runtimeSendMessage(const std::string& message, ApiCallback callback);

    /**
     * @brief 다른 확장에게 메시지 전송 (browser.runtime.sendMessage with extensionId)
     * @param extension_id 대상 확장 ID
     * @param message JSON 메시지
     * @param callback 응답 콜백
     */
    void runtimeSendExternalMessage(const std::string& extension_id,
                                     const std::string& message,
                                     ApiCallback callback);

    /**
     * @brief 메시지 수신 리스너 등록 (browser.runtime.onMessage)
     * @param callback 메시지 수신 콜백
     */
    void runtimeOnMessage(MessageCallback callback);

    /**
     * @brief 확장 설치 이벤트 (browser.runtime.onInstalled)
     * @param callback 설치 이벤트 콜백
     */
    void runtimeOnInstalled(std::function<void(const std::string& reason)> callback);

    // ============================
    // browser.storage API
    // ============================

    /**
     * @brief 로컬 스토리지에서 값 가져오기 (browser.storage.local.get)
     * @param keys 키 목록 (JSON 배열)
     * @param callback 결과 콜백
     */
    void storageLocalGet(const std::vector<std::string>& keys, ApiCallback callback);

    /**
     * @brief 로컬 스토리지에 값 저장 (browser.storage.local.set)
     * @param items JSON 키-값 쌍
     * @param callback 완료 콜백
     */
    void storageLocalSet(const std::unordered_map<std::string, std::string>& items,
                         ApiCallback callback);

    /**
     * @brief 로컬 스토리지에서 값 삭제 (browser.storage.local.remove)
     */
    void storageLocalRemove(const std::vector<std::string>& keys, ApiCallback callback);

    /**
     * @brief 로컬 스토리지 전체 삭제 (browser.storage.local.clear)
     */
    void storageLocalClear(ApiCallback callback);

    /**
     * @brief 동기화 스토리지에서 값 가져오기 (browser.storage.sync.get)
     */
    void storageSyncGet(const std::vector<std::string>& keys, ApiCallback callback);

    /**
     * @brief 동기화 스토리지에 값 저장 (browser.storage.sync.set)
     */
    void storageSyncSet(const std::unordered_map<std::string, std::string>& items,
                        ApiCallback callback);

    /**
     * @brief 스토리지 변경 리스너 (browser.storage.onChanged)
     */
    void storageOnChanged(StorageChangeCallback callback);

    // ============================
    // browser.webRequest API
    // ============================

    /**
     * @brief 요청 전 이벤트 (browser.webRequest.onBeforeRequest)
     * @param filter 필터 조건
     * @param callback 콜백 (BlockingResponse 반환 가능)
     * @param extra_info 추가 정보 요청 ("blocking", "requestBody" 등)
     */
    void webRequestOnBeforeRequest(const WebRequestFilter& filter,
                                    WebRequestCallback callback,
                                    const std::vector<std::string>& extra_info = {});

    /**
     * @brief 요청 헤더 전송 전 이벤트 (browser.webRequest.onBeforeSendHeaders)
     */
    void webRequestOnBeforeSendHeaders(const WebRequestFilter& filter,
                                        WebRequestCallback callback,
                                        const std::vector<std::string>& extra_info = {});

    /**
     * @brief 응답 헤더 수신 이벤트 (browser.webRequest.onHeadersReceived)
     */
    void webRequestOnHeadersReceived(const WebRequestFilter& filter,
                                      WebRequestCallback callback,
                                      const std::vector<std::string>& extra_info = {});

    /**
     * @brief 요청 완료 이벤트 (browser.webRequest.onCompleted)
     */
    void webRequestOnCompleted(const WebRequestFilter& filter,
                                std::function<void(const WebRequestDetails&)> callback);

    /**
     * @brief 요청 오류 이벤트 (browser.webRequest.onErrorOccurred)
     */
    void webRequestOnErrorOccurred(const WebRequestFilter& filter,
                                    std::function<void(const WebRequestDetails&)> callback);

    // ============================
    // 외부 요청 처리 (RequestInterceptor에서 호출)
    // ============================

    /**
     * @brief 네트워크 요청에 대한 확장 필터 실행
     * @param details 요청 세부 정보
     * @return 차단 응답 (nullopt이면 허용)
     */
    [[nodiscard]] std::optional<BlockingResponse> processRequest(const WebRequestDetails& details);

    /**
     * @brief 응답 헤더에 대한 확장 필터 실행
     */
    [[nodiscard]] std::optional<BlockingResponse> processResponseHeaders(const WebRequestDetails& details);

    // ============================
    // V8 바인딩
    // ============================

    /**
     * @brief V8 컨텍스트에 browser.* API 바인딩 등록
     * 
     * V8 Isolate의 전역 객체에 browser.tabs, browser.runtime,
     * browser.storage, browser.webRequest 객체를 등록합니다.
     */
    void registerV8Bindings();

    /**
     * @brief V8 바인딩에서 API 호출 라우팅
     * @param api_name API 이름 (예: "tabs.query", "storage.local.get")
     * @param args JSON 인자
     * @param callback 결과 콜백
     */
    void routeApiCall(const std::string& api_name, const std::string& args, ApiCallback callback);

private:
    Extension& extension_;          ///< 대상 확장 참조
    ExtensionManager& manager_;     ///< 확장 관리자 참조

    /// 권한 확인 헬퍼
    [[nodiscard]] bool checkPermission(Permission required, ApiResult& result) const;

    /// WebRequest 리스너 (이벤트 타입 → 리스너 목록)
    struct WebRequestListener {
        WebRequestFilter filter;
        WebRequestCallback callback;
        std::vector<std::string> extra_info;
        bool is_blocking{false};
    };

    std::unordered_map<std::string, std::vector<WebRequestListener>> web_request_listeners_;
    mutable std::mutex listener_mutex_;

    /// URL 필터 매칭 확인
    [[nodiscard]] bool matchesFilter(const WebRequestFilter& filter, 
                                      const WebRequestDetails& details) const;

    /// 설치 콜백
    std::vector<std::function<void(const std::string&)>> install_callbacks_;
};

} // namespace ordinal::extensions
