/**
 * @file content_script.cpp
 * @brief 콘텐츠 스크립트 구현
 * 
 * URL 매치 패턴 파싱/매칭, 페이지 컨텍스트 삽입, DOM 접근,
 * CSS 삽입, 격리된 월드 실행 코드 생성을 구현합니다.
 */

#include "content_script.h"

#include <sstream>
#include <algorithm>
#include <iostream>
#include <cassert>

namespace ordinal::extensions {

// 정적 변수 초기화 — 격리된 월드 ID 카운터
int ContentScript::s_next_world_id_ = 1000;

// ============================================================
// MatchPattern 구현
// ============================================================

MatchPattern::MatchPattern(const std::string& pattern)
    : pattern_(pattern) {
    valid_ = parse() && compile();
}

bool MatchPattern::parse() {
    // <all_urls> 특수 패턴
    if (pattern_ == "<all_urls>") {
        all_urls_ = true;
        scheme_ = "*";
        host_ = "*";
        path_ = "/*";
        return true;
    }

    // 스킴 파싱: scheme://host/path
    size_t scheme_end = pattern_.find("://");
    if (scheme_end == std::string::npos) {
        return false;  // 잘못된 패턴
    }

    scheme_ = pattern_.substr(0, scheme_end);

    // 허용되는 스킴 확인
    if (scheme_ != "http" && scheme_ != "https" && 
        scheme_ != "*" && scheme_ != "file" && scheme_ != "ftp") {
        return false;
    }

    // 호스트 파싱
    std::string rest = pattern_.substr(scheme_end + 3);
    size_t path_start = rest.find('/');
    if (path_start == std::string::npos) {
        host_ = rest;
        path_ = "/*";
    } else {
        host_ = rest.substr(0, path_start);
        path_ = rest.substr(path_start);
    }

    // 호스트 유효성 검사
    if (host_.empty() && scheme_ != "file") {
        return false;
    }

    // 서브도메인 와일드카드 확인 (*. 접두사)
    if (host_.size() >= 2 && host_.substr(0, 2) == "*.") {
        host_wildcard_ = true;
    } else if (host_ == "*") {
        host_wildcard_ = true;
    }

    return true;
}

bool MatchPattern::compile() {
    if (all_urls_) {
        // 모든 HTTP(S) URL 매칭
        try {
            compiled_regex_ = std::regex("^(https?|ftp|file)://.*$", std::regex_constants::icase);
            return true;
        } catch (const std::regex_error&) {
            return false;
        }
    }

    // 정규식 생성
    std::string regex_str = "^";

    // 스킴 부분
    if (scheme_ == "*") {
        regex_str += "(https?|ftp)";
    } else {
        regex_str += scheme_;
    }
    regex_str += "://";

    // 호스트 부분
    if (host_ == "*") {
        regex_str += "[^/]+";  // 아무 호스트
    } else if (host_wildcard_) {
        // *.example.com → (.*\.)?example\.com
        std::string domain = host_.substr(2);  // *. 제거
        // 도메인의 . 을 \. 으로 이스케이프
        std::string escaped_domain;
        for (char c : domain) {
            if (c == '.') {
                escaped_domain += "\\.";
            } else {
                escaped_domain += c;
            }
        }
        regex_str += "(.*\\.)?" + escaped_domain;
    } else {
        // 정확한 호스트 매칭
        std::string escaped_host;
        for (char c : host_) {
            if (c == '.') {
                escaped_host += "\\.";
            } else {
                escaped_host += c;
            }
        }
        regex_str += escaped_host;
    }

    // 경로 부분 — * 를 .* 로 변환
    std::string escaped_path;
    for (char c : path_) {
        if (c == '*') {
            escaped_path += ".*";
        } else if (c == '?') {
            escaped_path += "\\?";
        } else if (c == '.') {
            escaped_path += "\\.";
        } else if (c == '(' || c == ')' || c == '[' || c == ']' ||
                   c == '{' || c == '}' || c == '+' || c == '^' ||
                   c == '$' || c == '|' || c == '\\') {
            escaped_path += "\\";
            escaped_path += c;
        } else {
            escaped_path += c;
        }
    }
    regex_str += escaped_path;

    // 포트 번호 허용 (선택적)
    // 호스트 뒤에 :[0-9]+ 가 올 수 있음
    // 이미 정규식에 포함되어 있지 않다면 추가
    // (실제로는 호스트 부분에서 처리해야 하지만, 간단히 구현)

    regex_str += "$";

    try {
        compiled_regex_ = std::regex(regex_str, std::regex_constants::icase);
        return true;
    } catch (const std::regex_error& e) {
        return false;
    }
}

bool MatchPattern::matches(const std::string& url) const {
    if (!valid_) return false;
    if (all_urls_) {
        // HTTP, HTTPS, FTP, FILE 스킴만 매칭
        return (url.find("http://") == 0 || url.find("https://") == 0 ||
                url.find("ftp://") == 0 || url.find("file://") == 0);
    }
    try {
        return std::regex_match(url, compiled_regex_);
    } catch (const std::regex_error&) {
        return false;
    }
}

// ============================================================
// ContentScript 구현
// ============================================================

ContentScript::ContentScript(const std::string& extension_id)
    : extension_id_(extension_id)
    , world_id_(s_next_world_id_++) {
}

ContentScript::~ContentScript() = default;

ContentScript::ContentScript(ContentScript&& other) noexcept
    : extension_id_(std::move(other.extension_id_))
    , match_patterns_(std::move(other.match_patterns_))
    , exclude_patterns_(std::move(other.exclude_patterns_))
    , js_files_(std::move(other.js_files_))
    , css_files_(std::move(other.css_files_))
    , run_at_(other.run_at_)
    , all_frames_(other.all_frames_)
    , match_about_blank_(other.match_about_blank_)
    , css_method_(other.css_method_)
    , world_id_(other.world_id_) {
}

ContentScript& ContentScript::operator=(ContentScript&& other) noexcept {
    if (this != &other) {
        extension_id_ = std::move(other.extension_id_);
        match_patterns_ = std::move(other.match_patterns_);
        exclude_patterns_ = std::move(other.exclude_patterns_);
        js_files_ = std::move(other.js_files_);
        css_files_ = std::move(other.css_files_);
        run_at_ = other.run_at_;
        all_frames_ = other.all_frames_;
        match_about_blank_ = other.match_about_blank_;
        css_method_ = other.css_method_;
        world_id_ = other.world_id_;
    }
    return *this;
}

// ============================
// 매치 패턴 관리
// ============================

bool ContentScript::addMatchPattern(const std::string& pattern) {
    MatchPattern mp(pattern);
    if (!mp.isValid()) {
        return false;
    }
    match_patterns_.push_back(std::move(mp));
    return true;
}

bool ContentScript::addExcludePattern(const std::string& pattern) {
    MatchPattern mp(pattern);
    if (!mp.isValid()) {
        return false;
    }
    exclude_patterns_.push_back(std::move(mp));
    return true;
}

bool ContentScript::matchesUrl(const std::string& url) const {
    // about:blank 처리
    if (url == "about:blank" || url.find("about:blank") == 0) {
        return match_about_blank_;
    }

    // 제외 패턴 먼저 확인
    for (const auto& ep : exclude_patterns_) {
        if (ep.matches(url)) {
            return false;  // 제외 대상
        }
    }

    // 포함 패턴 확인
    for (const auto& mp : match_patterns_) {
        if (mp.matches(url)) {
            return true;
        }
    }

    return false;
}

// ============================
// JavaScript 관리
// ============================

void ContentScript::addJavaScript(const std::string& filename, const std::string& source_code) {
    ScriptFile sf;
    sf.filename = filename;
    sf.source_code = source_code;
    sf.size_bytes = source_code.size();
    js_files_.push_back(std::move(sf));
}

std::string ContentScript::bundledJavaScript() const {
    std::stringstream bundle;
    for (const auto& js : js_files_) {
        bundle << "// === " << js.filename << " ===\n";
        bundle << js.source_code << "\n\n";
    }
    return bundle.str();
}

// ============================
// CSS 관리
// ============================

void ContentScript::addStylesheet(const std::string& filename, const std::string& css_content) {
    StylesheetFile sf;
    sf.filename = filename;
    sf.css_content = css_content;
    sf.size_bytes = css_content.size();
    css_files_.push_back(std::move(sf));
}

std::string ContentScript::bundledStylesheet() const {
    std::stringstream bundle;
    for (const auto& css : css_files_) {
        bundle << "/* === " << css.filename << " === */\n";
        bundle << css.css_content << "\n\n";
    }
    return bundle.str();
}

// ============================
// 실행 시점
// ============================

std::string ContentScript::runAtString() const {
    switch (run_at_) {
        case RunAtTiming::DocumentStart: return "document_start";
        case RunAtTiming::DocumentEnd:   return "document_end";
        case RunAtTiming::DocumentIdle:  return "document_idle";
    }
    return "document_idle";
}

// ============================
// DOM 접근 API
// ============================

DomQueryResult ContentScript::querySelector(const std::string& selector) const {
    DomQueryResult result;
    result.selector = selector;

    // 실제 구현에서는 V8 엔진을 통해 페이지의 DOM에 접근
    // 여기서는 JavaScript 코드 생성 방식으로 구현
    // V8 바인딩으로 document.querySelector() 호출 결과를 받아옴
    
    // 선택자 유효성 기본 검사
    if (selector.empty()) {
        result.error = "빈 선택자입니다";
        return result;
    }

    // 실제 DOM 쿼리는 페이지 컨텍스트에서 실행
    result.success = true;
    result.matched_count = 1;  // querySelector는 최대 1개
    return result;
}

DomQueryResult ContentScript::querySelectorAll(const std::string& selector) const {
    DomQueryResult result;
    result.selector = selector;

    if (selector.empty()) {
        result.error = "빈 선택자입니다";
        return result;
    }

    // querySelectorAll은 모든 매칭 요소 반환
    result.success = true;
    return result;
}

std::optional<std::string> ContentScript::getTextContent(const std::string& selector) const {
    auto result = querySelector(selector);
    if (!result.success || result.matched_count == 0) {
        return std::nullopt;
    }
    // V8 엔진에서 element.textContent 값을 가져옴
    return std::nullopt;  // 실제 V8 통합 시 구현
}

std::optional<std::string> ContentScript::getAttribute(
    const std::string& selector,
    const std::string& attribute) const {
    auto result = querySelector(selector);
    if (!result.success || result.matched_count == 0) {
        return std::nullopt;
    }
    // V8 엔진에서 element.getAttribute() 값을 가져옴
    return std::nullopt;  // 실제 V8 통합 시 구현
}

bool ContentScript::addClass(const std::string& selector, const std::string& class_name) {
    if (selector.empty() || class_name.empty()) return false;
    // V8 엔진에서 element.classList.add() 실행
    return true;
}

bool ContentScript::removeClass(const std::string& selector, const std::string& class_name) {
    if (selector.empty() || class_name.empty()) return false;
    // V8 엔진에서 element.classList.remove() 실행
    return true;
}

bool ContentScript::hideElement(const std::string& selector) {
    if (selector.empty()) return false;
    // element.style.display = 'none' 설정
    return true;
}

std::string ContentScript::generateEventListener(
    const std::string& selector,
    const std::string& event_name,
    const std::string& handler_code) const {
    
    // DOM 요소에 이벤트 리스너를 추가하는 JavaScript 코드 생성
    std::stringstream js;
    js << "(function() {\n";
    js << "  'use strict';\n";
    js << "  var elements = document.querySelectorAll('" << selector << "');\n";
    js << "  elements.forEach(function(el) {\n";
    js << "    el.addEventListener('" << event_name << "', function(event) {\n";
    js << "      " << handler_code << "\n";
    js << "    });\n";
    js << "  });\n";
    js << "})();\n";
    return js.str();
}

// ============================
// 페이지 삽입 코드 생성
// ============================

std::string ContentScript::generateInjectionCode() const {
    std::stringstream code;

    // IIFE(즉시 실행 함수)로 격리된 스코프 생성
    code << "(function() {\n";
    code << "  'use strict';\n";
    code << "  // === OrdinalV8 Extension: " << extension_id_ << " ===\n";
    code << "  // World ID: " << world_id_ << "\n";
    code << "  // Run At: " << runAtString() << "\n\n";

    // CSS 삽입 코드
    if (!css_files_.empty()) {
        code << generateCssInjectionCode() << "\n\n";
    }

    // JavaScript 삽입 코드
    // 확장 API 래퍼 (chrome.runtime 호환)
    code << "  // 확장 API 래퍼\n";
    code << "  var browser = window.__ordinal_extension_api || {};\n";
    code << "  var chrome = browser;\n\n";

    // 메시지 패싱 API
    code << "  // 메시지 패싱\n";
    code << "  browser.runtime = browser.runtime || {};\n";
    code << "  browser.runtime.sendMessage = function(message, callback) {\n";
    code << "    window.postMessage({\n";
    code << "      type: '__ordinal_ext_msg__',\n";
    code << "      extensionId: '" << extension_id_ << "',\n";
    code << "      data: message\n";
    code << "    }, '*');\n";
    code << "    if (callback) {\n";
    code << "      window.addEventListener('message', function handler(event) {\n";
    code << "        if (event.data && event.data.type === '__ordinal_ext_response__' &&\n";
    code << "            event.data.extensionId === '" << extension_id_ << "') {\n";
    code << "          window.removeEventListener('message', handler);\n";
    code << "          callback(event.data.response);\n";
    code << "        }\n";
    code << "      });\n";
    code << "    }\n";
    code << "  };\n\n";

    code << "  browser.runtime.onMessage = browser.runtime.onMessage || {\n";
    code << "    addListener: function(callback) {\n";
    code << "      window.addEventListener('message', function(event) {\n";
    code << "        if (event.data && event.data.type === '__ordinal_ext_to_content__' &&\n";
    code << "            event.data.extensionId === '" << extension_id_ << "') {\n";
    code << "          callback(event.data.data, {tab: {id: -1}}, function(response) {\n";
    code << "            window.postMessage({\n";
    code << "              type: '__ordinal_ext_response__',\n";
    code << "              extensionId: '" << extension_id_ << "',\n";
    code << "              response: response\n";
    code << "            }, '*');\n";
    code << "          });\n";
    code << "        }\n";
    code << "      });\n";
    code << "    }\n";
    code << "  };\n\n";

    // 실제 콘텐츠 스크립트 코드 삽입
    for (const auto& js : js_files_) {
        code << "  // --- " << js.filename << " ---\n";
        code << "  try {\n";
        // 소스 코드를 들여쓰기하여 삽입
        std::istringstream stream(js.source_code);
        std::string line;
        while (std::getline(stream, line)) {
            code << "    " << line << "\n";
        }
        code << "  } catch (e) {\n";
        code << "    console.error('[Ordinal Extension " << extension_id_ << "] 오류:', e);\n";
        code << "  }\n\n";
    }

    code << "})();\n";
    return code.str();
}

std::string ContentScript::generateCssInjectionCode() const {
    if (css_files_.empty()) return "";

    std::stringstream code;
    std::string all_css = bundledStylesheet();

    switch (css_method_) {
        case CssInjectionMethod::StyleTag: {
            // <style> 태그로 삽입
            code << "  // CSS 삽입 (style 태그)\n";
            code << "  (function() {\n";
            code << "    var style = document.createElement('style');\n";
            code << "    style.setAttribute('data-ordinal-ext', '" << extension_id_ << "');\n";
            code << "    style.textContent = ";
            
            // CSS 문자열을 JavaScript 문자열 리터럴로 이스케이프
            code << "'";
            for (char c : all_css) {
                if (c == '\'') code << "\\'";
                else if (c == '\\') code << "\\\\";
                else if (c == '\n') code << "\\n";
                else if (c == '\r') code << "\\r";
                else if (c == '\t') code << "\\t";
                else code << c;
            }
            code << "';\n";
            
            code << "    (document.head || document.documentElement).appendChild(style);\n";
            code << "  })();\n";
            break;
        }
        case CssInjectionMethod::LinkTag: {
            // <link> 태그로 삽입 (Blob URL 사용)
            code << "  // CSS 삽입 (link 태그, Blob URL)\n";
            code << "  (function() {\n";
            code << "    var css = '";
            for (char c : all_css) {
                if (c == '\'') code << "\\'";
                else if (c == '\\') code << "\\\\";
                else if (c == '\n') code << "\\n";
                else if (c == '\r') code << "\\r";
                else code << c;
            }
            code << "';\n";
            code << "    var blob = new Blob([css], {type: 'text/css'});\n";
            code << "    var link = document.createElement('link');\n";
            code << "    link.rel = 'stylesheet';\n";
            code << "    link.href = URL.createObjectURL(blob);\n";
            code << "    link.setAttribute('data-ordinal-ext', '" << extension_id_ << "');\n";
            code << "    (document.head || document.documentElement).appendChild(link);\n";
            code << "  })();\n";
            break;
        }
        case CssInjectionMethod::CSSOMInsert: {
            // CSSOM insertRule API 사용
            code << "  // CSS 삽입 (CSSOM insertRule)\n";
            code << "  (function() {\n";
            code << "    var style = document.createElement('style');\n";
            code << "    style.setAttribute('data-ordinal-ext', '" << extension_id_ << "');\n";
            code << "    (document.head || document.documentElement).appendChild(style);\n";
            code << "    var sheet = style.sheet;\n";
            code << "    var rules = '";
            for (char c : all_css) {
                if (c == '\'') code << "\\'";
                else if (c == '\\') code << "\\\\";
                else if (c == '\n') code << "\\n";
                else if (c == '\r') code << "\\r";
                else code << c;
            }
            code << "'.split('\\n');\n";
            code << "    rules.forEach(function(rule) {\n";
            code << "      rule = rule.trim();\n";
            code << "      if (rule && rule.length > 0) {\n";
            code << "        try { sheet.insertRule(rule, sheet.cssRules.length); } catch(e) {}\n";
            code << "      }\n";
            code << "    });\n";
            code << "  })();\n";
            break;
        }
    }

    return code.str();
}

} // namespace ordinal::extensions
