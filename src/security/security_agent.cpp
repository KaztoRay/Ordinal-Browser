/**
 * @file security_agent.cpp
 * @brief 보안 에이전트 구현 (중앙 코디네이터)
 */

#include "security_agent.h"
#include "phishing_detector.h"
#include "xss_analyzer.h"
#include "script_analyzer.h"
#include "privacy_tracker.h"
#include "cert_validator.h"

#include <iostream>
#include <algorithm>

namespace ordinal::security {

SecurityAgent& SecurityAgent::instance() {
    static SecurityAgent agent;
    return agent;
}

SecurityAgent::~SecurityAgent() {
    if (initialized_) {
        shutdown();
    }
}

bool SecurityAgent::initialize(const SecurityAgentConfig& config) {
    if (initialized_) {
        std::cerr << "[SecurityAgent] 이미 초기화되었습니다." << std::endl;
        return true;
    }

    config_ = config;
    std::cout << "[SecurityAgent] 보안 에이전트 초기화 시작..." << std::endl;

    // 1. 피싱 탐지기 초기화
    if (config_.enable_phishing_detection) {
        phishing_detector_ = std::make_unique<PhishingDetector>();
        phishing_detector_->initialize();
        std::cout << "[SecurityAgent] ✓ 피싱 탐지기 활성화" << std::endl;
    }

    // 2. XSS 분석기 초기화
    if (config_.enable_xss_detection) {
        xss_analyzer_ = std::make_unique<XssAnalyzer>();
        xss_analyzer_->initialize();
        std::cout << "[SecurityAgent] ✓ XSS 분석기 활성화" << std::endl;
    }

    // 3. 스크립트 분석기 초기화
    if (config_.enable_script_analysis) {
        script_analyzer_ = std::make_unique<ScriptAnalyzer>();
        script_analyzer_->initialize();
        std::cout << "[SecurityAgent] ✓ 스크립트 분석기 활성화" << std::endl;
    }

    // 4. 프라이버시 추적기 차단기 초기화
    if (config_.enable_privacy_tracking) {
        privacy_tracker_ = std::make_unique<PrivacyTracker>();
        privacy_tracker_->initialize();
        std::cout << "[SecurityAgent] ✓ 프라이버시 추적기 차단 활성화" << std::endl;
    }

    // 5. 인증서 검증기 초기화
    if (config_.enable_cert_validation) {
        cert_validator_ = std::make_unique<CertValidator>();
        cert_validator_->initialize();
        std::cout << "[SecurityAgent] ✓ 인증서 검증기 활성화" << std::endl;
    }

    // 6. LLM Agent gRPC 연결 (선택적)
    if (config_.enable_llm_analysis) {
        std::cout << "[SecurityAgent] LLM 분석 활성화 (gRPC: " 
                  << config_.grpc_server_address << ")" << std::endl;
        // TODO: gRPC 클라이언트 초기화
    }

    initialized_ = true;
    std::cout << "[SecurityAgent] 보안 에이전트 초기화 완료!" << std::endl;
    return true;
}

void SecurityAgent::shutdown() {
    if (!initialized_) return;

    std::cout << "[SecurityAgent] 종료 중..." << std::endl;

    phishing_detector_.reset();
    xss_analyzer_.reset();
    script_analyzer_.reset();
    privacy_tracker_.reset();
    cert_validator_.reset();

    initialized_ = false;

    std::cout << "[SecurityAgent] 보안 에이전트 종료 완료" << std::endl;
    std::cout << "[SecurityAgent] 통계 - 스캔: " << total_scanned_.load()
              << ", 위협: " << total_threats_.load()
              << ", 차단: " << total_blocked_.load() << std::endl;
}

// ============================================================
// URL/페이지 분석
// ============================================================

std::vector<ThreatReport> SecurityAgent::analyzeUrl(const std::string& url) {
    total_scanned_.fetch_add(1, std::memory_order_relaxed);
    std::vector<ThreatReport> threats;

    // 1. 피싱 URL 검사
    if (phishing_detector_) {
        auto phishing_result = phishing_detector_->checkUrl(url);
        if (phishing_result.has_value()) {
            threats.push_back(phishing_result.value());
        }
    }

    // 2. 추적기 URL 검사
    if (privacy_tracker_) {
        if (privacy_tracker_->isTracker(url)) {
            ThreatReport report;
            report.category = ThreatCategory::Tracker;
            report.severity = ThreatSeverity::Low;
            report.url = url;
            report.description = "추적기 URL이 감지되었습니다.";
            report.recommendation = "추적기가 차단되었습니다.";
            report.detector_name = "PrivacyTracker";
            report.confidence = 0.9;
            report.detected_at = std::chrono::system_clock::now();
            threats.push_back(report);
        }
    }

    // 위협 발견 시 보고
    for (const auto& threat : threats) {
        reportThreat(threat);
    }

    return threats;
}

std::vector<ThreatReport> SecurityAgent::analyzePage(
    const std::string& url,
    const std::string& html_content,
    const std::vector<std::string>& scripts
) {
    total_scanned_.fetch_add(1, std::memory_order_relaxed);
    std::vector<ThreatReport> threats;

    // 1. URL 분석 (기본)
    auto url_threats = analyzeUrl(url);
    threats.insert(threats.end(), url_threats.begin(), url_threats.end());

    // 2. XSS 분석 (HTML 콘텐츠)
    if (xss_analyzer_) {
        auto xss_threats = xss_analyzer_->analyzeHtml(url, html_content);
        threats.insert(threats.end(), xss_threats.begin(), xss_threats.end());
    }

    // 3. 스크립트 분석 (JavaScript)
    if (script_analyzer_) {
        for (const auto& script : scripts) {
            auto script_threats = script_analyzer_->analyzeScript(url, script);
            threats.insert(threats.end(), script_threats.begin(), script_threats.end());
        }
    }

    // 4. 피싱 콘텐츠 분석
    if (phishing_detector_) {
        auto phishing_threats = phishing_detector_->analyzeContent(url, html_content);
        threats.insert(threats.end(), phishing_threats.begin(), phishing_threats.end());
    }

    // 위협 보고
    for (const auto& threat : threats) {
        reportThreat(threat);
    }

    return threats;
}

bool SecurityAgent::shouldBlockRequest(
    const std::string& url,
    const std::string& referer
) {
    // 1. 추적기 차단
    if (privacy_tracker_ && privacy_tracker_->isTracker(url)) {
        total_blocked_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // 2. 피싱 URL 차단
    if (phishing_detector_) {
        auto result = phishing_detector_->checkUrl(url);
        if (result.has_value() && 
            result->severity >= ThreatSeverity::High) {
            total_blocked_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    // 3. 혼합 콘텐츠 차단 (HTTPS 페이지에서 HTTP 리소스)
    if (!referer.empty() && referer.starts_with("https://") && 
        url.starts_with("http://") && !url.starts_with("https://")) {
        // 혼합 콘텐츠 - 경고 수준에서는 허용, 심각하면 차단
        ThreatReport report;
        report.category = ThreatCategory::MixedContent;
        report.severity = ThreatSeverity::Medium;
        report.url = url;
        report.description = "HTTPS 페이지에서 HTTP 리소스 요청이 감지되었습니다.";
        report.detector_name = "SecurityAgent";
        report.detected_at = std::chrono::system_clock::now();
        reportThreat(report);
        // 혼합 콘텐츠는 기본적으로 차단하지 않음 (경고만)
    }

    return false;
}

// ============================================================
// 위협 알림
// ============================================================

void SecurityAgent::onThreatDetected(ThreatCallback callback) {
    threat_callbacks_.push_back(std::move(callback));
}

std::vector<ThreatReport> SecurityAgent::threatHistory() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return threat_history_;
}

void SecurityAgent::clearThreatHistory() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    threat_history_.clear();
}

void SecurityAgent::reportThreat(const ThreatReport& report) {
    total_threats_.fetch_add(1, std::memory_order_relaxed);

    // 히스토리에 추가
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        threat_history_.push_back(report);

        // 최대 수 초과 시 오래된 항목 제거
        while (static_cast<int>(threat_history_.size()) > config_.max_threat_history) {
            threat_history_.erase(threat_history_.begin());
        }
    }

    // 콜백 호출
    for (const auto& callback : threat_callbacks_) {
        try {
            callback(report);
        } catch (const std::exception& e) {
            std::cerr << "[SecurityAgent] 콜백 오류: " << e.what() << std::endl;
        }
    }

    // 심각한 위협은 콘솔에 출력
    if (report.severity >= ThreatSeverity::High) {
        std::cout << "[SecurityAgent] ⚠️ " << report.severityString() 
                  << " 위협 감지: " << report.categoryString()
                  << " - " << report.description
                  << " (URL: " << report.url << ")" << std::endl;
    }
}

} // namespace ordinal::security
