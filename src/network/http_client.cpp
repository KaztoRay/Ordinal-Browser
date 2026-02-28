/**
 * @file http_client.cpp
 * @brief HTTP/HTTPS 클라이언트 구현 (libcurl 래퍼)
 */

#include "http_client.h"

#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <mutex>
#include <thread>

namespace ordinal::network {

/**
 * @brief HttpClient 내부 구현 (PIMPL)
 */
struct HttpClient::Impl {
    CURL* curl{nullptr};
    std::string user_agent{"OrdinalV8/0.1"};
    std::unordered_map<std::string, std::string> default_headers;
    std::optional<std::string> proxy_url;
    bool ssl_verify{true};
    std::mutex mutex;  // 스레드 안전을 위한 뮤텍스
};

// ============================================================
// libcurl 콜백 함수들
// ============================================================

/**
 * @brief 응답 데이터 수신 콜백
 */
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response_body = static_cast<std::string*>(userdata);
    size_t total_size = size * nmemb;
    response_body->append(ptr, total_size);
    return total_size;
}

/**
 * @brief 응답 헤더 수신 콜백
 */
static size_t headerCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::unordered_map<std::string, std::string>*>(userdata);
    size_t total_size = size * nmemb;
    std::string header_line(ptr, total_size);

    // 줄바꿈 제거
    while (!header_line.empty() && 
           (header_line.back() == '\r' || header_line.back() == '\n')) {
        header_line.pop_back();
    }

    // 헤더 파싱 (Key: Value 형식)
    auto colon_pos = header_line.find(':');
    if (colon_pos != std::string::npos) {
        std::string key = header_line.substr(0, colon_pos);
        std::string value = header_line.substr(colon_pos + 1);
        
        // 앞뒤 공백 제거
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        while (!value.empty() && value.back() == ' ') value.pop_back();

        // 소문자로 정규화
        std::string lower_key;
        for (char c : key) lower_key += static_cast<char>(std::tolower(c));
        
        (*headers)[lower_key] = value;
    }

    return total_size;
}

/**
 * @brief 진행률 콜백 래퍼
 */
struct ProgressData {
    ProgressCallback callback;
};

static int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* data = static_cast<ProgressData*>(clientp);
    if (data && data->callback) {
        bool should_continue = data->callback(
            static_cast<size_t>(dlnow),
            static_cast<size_t>(dltotal)
        );
        return should_continue ? 0 : 1;  // 0=계속, 1=중단
    }
    return 0;
}

// ============================================================
// 전역 초기화/정리
// ============================================================

void HttpClient::globalInit() {
    curl_global_init(CURL_GLOBAL_ALL);
    std::cout << "[HttpClient] libcurl 전역 초기화 완료" << std::endl;
}

void HttpClient::globalCleanup() {
    curl_global_cleanup();
    std::cout << "[HttpClient] libcurl 전역 정리 완료" << std::endl;
}

// ============================================================
// 생성자/소멸자
// ============================================================

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {
    impl_->curl = curl_easy_init();
    if (!impl_->curl) {
        std::cerr << "[HttpClient] CURL 핸들 생성 실패!" << std::endl;
    }
}

HttpClient::~HttpClient() {
    if (impl_ && impl_->curl) {
        curl_easy_cleanup(impl_->curl);
    }
}

HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

// ============================================================
// 요청 실행
// ============================================================

HttpResponse HttpClient::send(
    const HttpRequest& request,
    ProgressCallback progress_cb
) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    HttpResponse response;

    if (!impl_->curl) {
        response.success = false;
        response.error_message = "CURL 핸들이 초기화되지 않았습니다.";
        return response;
    }

    // CURL 옵션 초기화
    curl_easy_reset(impl_->curl);

    // URL 설정
    curl_easy_setopt(impl_->curl, CURLOPT_URL, request.url.c_str());

    // HTTP 메서드 설정
    switch (request.method) {
        case HttpMethod::GET:
            curl_easy_setopt(impl_->curl, CURLOPT_HTTPGET, 1L);
            break;
        case HttpMethod::POST:
            curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
            curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, 
                             static_cast<long>(request.body.size()));
            break;
        case HttpMethod::PUT:
            curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, request.body.c_str());
            break;
        case HttpMethod::DELETE_:
            curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
        case HttpMethod::PATCH:
            curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, request.body.c_str());
            break;
        case HttpMethod::HEAD:
            curl_easy_setopt(impl_->curl, CURLOPT_NOBODY, 1L);
            break;
        case HttpMethod::OPTIONS:
            curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
            break;
    }

    // User-Agent 설정
    curl_easy_setopt(impl_->curl, CURLOPT_USERAGENT, impl_->user_agent.c_str());

    // 헤더 설정
    struct curl_slist* header_list = nullptr;
    
    // 기본 헤더 추가
    for (const auto& [key, value] : impl_->default_headers) {
        std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }

    // 요청별 헤더 추가
    for (const auto& [key, value] : request.headers) {
        std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }

    // Content-Type 헤더
    if (!request.content_type.empty()) {
        std::string ct = "Content-Type: " + request.content_type;
        header_list = curl_slist_append(header_list, ct.c_str());
    }

    if (header_list) {
        curl_easy_setopt(impl_->curl, CURLOPT_HTTPHEADER, header_list);
    }

    // 응답 데이터 콜백
    std::string response_body;
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &response_body);

    // 응답 헤더 콜백
    std::unordered_map<std::string, std::string> response_headers;
    curl_easy_setopt(impl_->curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(impl_->curl, CURLOPT_HEADERDATA, &response_headers);

    // 타임아웃 설정
    curl_easy_setopt(impl_->curl, CURLOPT_CONNECTTIMEOUT,
                     static_cast<long>(request.connect_timeout.count()));
    curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT,
                     static_cast<long>(request.transfer_timeout.count()));

    // SSL 검증 설정
    bool verify_ssl = request.verify_ssl && impl_->ssl_verify;
    curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
    curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);

    // CA 인증서 경로
    if (request.ca_cert_path.has_value()) {
        curl_easy_setopt(impl_->curl, CURLOPT_CAINFO, 
                         request.ca_cert_path->c_str());
    }

    // 리다이렉션 설정
    curl_easy_setopt(impl_->curl, CURLOPT_FOLLOWLOCATION, 
                     request.follow_redirects ? 1L : 0L);
    curl_easy_setopt(impl_->curl, CURLOPT_MAXREDIRS, 
                     static_cast<long>(request.max_redirects));

    // 프록시 설정
    auto proxy = request.proxy_url.value_or(
        impl_->proxy_url.value_or("")
    );
    if (!proxy.empty()) {
        curl_easy_setopt(impl_->curl, CURLOPT_PROXY, proxy.c_str());
    }

    // 쿠키 설정
    if (request.enable_cookies) {
        curl_easy_setopt(impl_->curl, CURLOPT_COOKIEFILE, "");
        if (request.cookie_jar_path.has_value()) {
            curl_easy_setopt(impl_->curl, CURLOPT_COOKIEJAR, 
                             request.cookie_jar_path->c_str());
        }
    }

    // 진행률 콜백
    ProgressData prog_data{progress_cb};
    if (progress_cb) {
        curl_easy_setopt(impl_->curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(impl_->curl, CURLOPT_XFERINFODATA, &prog_data);
        curl_easy_setopt(impl_->curl, CURLOPT_NOPROGRESS, 0L);
    }

    // SSL 인증서 정보 수집 활성화
    curl_easy_setopt(impl_->curl, CURLOPT_CERTINFO, 1L);

    // 요청 실행
    CURLcode res = curl_easy_perform(impl_->curl);

    // 응답 처리
    if (res == CURLE_OK) {
        response.success = true;

        // 상태 코드
        long http_code = 0;
        curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);

        // 최종 URL (리다이렉션 후)
        char* effective_url = nullptr;
        curl_easy_getinfo(impl_->curl, CURLINFO_EFFECTIVE_URL, &effective_url);
        if (effective_url) response.effective_url = effective_url;

        // 성능 정보
        curl_easy_getinfo(impl_->curl, CURLINFO_TOTAL_TIME, &response.total_time_seconds);
        curl_easy_getinfo(impl_->curl, CURLINFO_NAMELOOKUP_TIME, &response.dns_time_seconds);
        curl_easy_getinfo(impl_->curl, CURLINFO_CONNECT_TIME, &response.connect_time_seconds);
        curl_easy_getinfo(impl_->curl, CURLINFO_APPCONNECT_TIME, &response.tls_time_seconds);

        curl_off_t dl_bytes = 0;
        curl_easy_getinfo(impl_->curl, CURLINFO_SIZE_DOWNLOAD_T, &dl_bytes);
        response.bytes_downloaded = static_cast<size_t>(dl_bytes);

        // SSL 인증서 정보 수집
        struct curl_certinfo* cert_info = nullptr;
        curl_easy_getinfo(impl_->curl, CURLINFO_CERTINFO, &cert_info);
        if (cert_info && cert_info->num_of_certs > 0) {
            CertificateInfo cert;
            cert.is_valid = true;
            // 첫 번째 인증서 (서버 인증서) 정보 파싱
            for (struct curl_slist* slist = cert_info->certinfo[0]; 
                 slist; slist = slist->next) {
                std::string data = slist->data;
                if (data.starts_with("Subject:")) {
                    cert.subject = data.substr(8);
                } else if (data.starts_with("Issuer:")) {
                    cert.issuer = data.substr(7);
                } else if (data.starts_with("Start date:")) {
                    cert.valid_from = data.substr(11);
                } else if (data.starts_with("Expire date:")) {
                    cert.valid_until = data.substr(12);
                }
            }
            response.certificate = cert;
        }
    } else {
        response.success = false;
        response.curl_error_code = static_cast<int>(res);
        response.error_message = curl_easy_strerror(res);
        std::cerr << "[HttpClient] 요청 실패: " << response.error_message 
                  << " (URL: " << request.url << ")" << std::endl;
    }

    response.body = std::move(response_body);
    response.headers = std::move(response_headers);

    // 헤더 리스트 정리
    if (header_list) {
        curl_slist_free_all(header_list);
    }

    return response;
}

HttpResponse HttpClient::get(const std::string& url) {
    HttpRequest req;
    req.method = HttpMethod::GET;
    req.url = url;
    return send(req);
}

HttpResponse HttpClient::post(
    const std::string& url,
    const std::string& body,
    const std::string& content_type
) {
    HttpRequest req;
    req.method = HttpMethod::POST;
    req.url = url;
    req.body = body;
    req.content_type = content_type;
    return send(req);
}

// ============================================================
// 비동기 요청
// ============================================================

std::future<HttpResponse> HttpClient::sendAsync(const HttpRequest& request) {
    return std::async(std::launch::async, [this, request]() {
        // 비동기 실행을 위해 별도 HttpClient 사용
        HttpClient async_client;
        async_client.setUserAgent(impl_->user_agent);
        if (impl_->proxy_url.has_value()) {
            async_client.setProxy(impl_->proxy_url.value());
        }
        return async_client.send(request);
    });
}

// ============================================================
// 설정
// ============================================================

void HttpClient::setUserAgent(const std::string& user_agent) {
    impl_->user_agent = user_agent;
}

void HttpClient::setDefaultHeader(const std::string& key, const std::string& value) {
    impl_->default_headers[key] = value;
}

void HttpClient::setProxy(const std::string& proxy_url) {
    impl_->proxy_url = proxy_url;
}

void HttpClient::setSslVerification(bool enable) {
    impl_->ssl_verify = enable;
}

} // namespace ordinal::network
