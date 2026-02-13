#pragma once

/**
 * @file websocket_client.h
 * @brief WebSocket 클라이언트 — RFC 6455 호환
 * 
 * WebSocket 프로토콜을 통한 양방향 통신을 지원합니다.
 * - ws:// 및 wss:// (SSL/TLS) 프로토콜 지원
 * - 텍스트/바이너리 프레임 송수신
 * - Ping/Pong 하트비트 자동 관리
 * - 자동 재연결 (설정 가능)
 * - SecurityAgent와의 트래픽 분석 통합
 */

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>
#include <optional>
#include <queue>

#include <QObject>
#include <QUrl>
#include <QTimer>
#include <QByteArray>

namespace ordinal::network {

// 전방 선언
class HttpClient;

/**
 * @brief WebSocket 연결 상태
 */
enum class WebSocketState {
    Disconnected,   ///< 연결 안 됨
    Connecting,     ///< 연결 시도 중 (핸드셰이크)
    Connected,      ///< 연결 완료
    Closing,        ///< 종료 핸드셰이크 중
    Error           ///< 에러 발생
};

/**
 * @brief WebSocket 프레임 타입
 */
enum class WebSocketFrameType {
    Text,           ///< 텍스트 프레임 (UTF-8)
    Binary,         ///< 바이너리 프레임
    Ping,           ///< Ping 프레임
    Pong,           ///< Pong 프레임
    Close           ///< 종료 프레임
};

/**
 * @brief WebSocket 종료 코드 (RFC 6455 §7.4.1)
 */
enum class WebSocketCloseCode : uint16_t {
    Normal          = 1000,
    GoingAway       = 1001,
    ProtocolError   = 1002,
    UnsupportedData = 1003,
    NoStatus        = 1005,
    Abnormal        = 1006,
    InvalidPayload  = 1007,
    PolicyViolation = 1008,
    MessageTooBig   = 1009,
    MissingExtension= 1010,
    InternalError   = 1011,
    TLSHandshake    = 1015
};

/**
 * @brief WebSocket 메시지 구조체
 */
struct WebSocketMessage {
    WebSocketFrameType type;            ///< 프레임 타입
    std::string text_data;              ///< 텍스트 데이터 (텍스트 프레임)
    std::vector<uint8_t> binary_data;   ///< 바이너리 데이터 (바이너리 프레임)
    std::chrono::steady_clock::time_point timestamp; ///< 수신 시간
};

/**
 * @brief WebSocket 연결 설정
 */
struct WebSocketConfig {
    /// 자동 재연결 활성화
    bool auto_reconnect{true};
    
    /// 재연결 시도 간격 (초)
    uint32_t reconnect_interval_sec{5};
    
    /// 최대 재연결 시도 횟수 (0 = 무한)
    uint32_t max_reconnect_attempts{10};
    
    /// Ping 간격 (초, 0 = 비활성화)
    uint32_t ping_interval_sec{30};
    
    /// Pong 타임아웃 (초)
    uint32_t pong_timeout_sec{10};
    
    /// 최대 메시지 크기 (바이트, 0 = 무제한)
    size_t max_message_size{16 * 1024 * 1024}; // 16MB
    
    /// SSL/TLS 인증서 검증 활성화
    bool verify_ssl{true};
    
    /// 커스텀 헤더 (핸드셰이크용)
    std::vector<std::pair<std::string, std::string>> custom_headers;
    
    /// 서브프로토콜 목록
    std::vector<std::string> subprotocols;
    
    /// 연결 타임아웃 (초)
    uint32_t connect_timeout_sec{10};

    /// SecurityAgent 트래픽 분석 활성화
    bool enable_security_analysis{true};
};

/**
 * @brief WebSocket 통계
 */
struct WebSocketStats {
    size_t messages_sent{0};            ///< 전송된 메시지 수
    size_t messages_received{0};        ///< 수신된 메시지 수
    size_t bytes_sent{0};               ///< 전송된 바이트 수
    size_t bytes_received{0};           ///< 수신된 바이트 수
    uint32_t reconnect_count{0};        ///< 재연결 횟수
    std::chrono::steady_clock::time_point connected_since; ///< 연결 시작 시간
};

/**
 * @brief WebSocket 클라이언트
 * 
 * RFC 6455 기반 WebSocket 통신을 제공합니다.
 * Qt의 이벤트 루프와 통합되어 비동기 메시지 처리를 지원합니다.
 */
class WebSocketClient : public QObject {
    Q_OBJECT

public:
    // 콜백 타입
    using OnMessageCallback = std::function<void(const WebSocketMessage&)>;
    using OnCloseCallback = std::function<void(WebSocketCloseCode, const std::string&)>;
    using OnErrorCallback = std::function<void(const std::string&)>;
    using OnConnectedCallback = std::function<void()>;

    /**
     * @brief 생성자
     * @param config WebSocket 설정
     * @param parent Qt 부모 객체
     */
    explicit WebSocketClient(
        const WebSocketConfig& config = {},
        QObject* parent = nullptr
    );
    ~WebSocketClient() override;

    // 복사 금지
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    // ---- 연결 관리 ----

    /**
     * @brief WebSocket 서버에 연결
     * @param url WebSocket URL (ws:// 또는 wss://)
     * @return 연결 시도 시작 성공 여부
     */
    bool connect(const std::string& url);

    /**
     * @brief 연결 종료
     * @param code 종료 코드
     * @param reason 종료 사유
     */
    void disconnect(
        WebSocketCloseCode code = WebSocketCloseCode::Normal,
        const std::string& reason = ""
    );

    /**
     * @brief 현재 연결 상태 확인
     */
    [[nodiscard]] WebSocketState state() const;

    /**
     * @brief 연결 여부 확인
     */
    [[nodiscard]] bool isConnected() const;

    // ---- 메시지 송수신 ----

    /**
     * @brief 텍스트 메시지 전송
     * @param text UTF-8 텍스트 데이터
     * @return 전송 성공 여부
     */
    bool sendText(const std::string& text);

    /**
     * @brief 바이너리 메시지 전송
     * @param data 바이너리 데이터
     * @return 전송 성공 여부
     */
    bool sendBinary(const std::vector<uint8_t>& data);

    /**
     * @brief Ping 프레임 전송
     * @param payload Ping 페이로드 (선택)
     */
    void sendPing(const std::string& payload = "");

    // ---- 콜백 등록 ----

    void onMessage(OnMessageCallback callback);
    void onClose(OnCloseCallback callback);
    void onError(OnErrorCallback callback);
    void onConnected(OnConnectedCallback callback);

    // ---- 상태 조회 ----

    /**
     * @brief 연결 통계 조회
     */
    [[nodiscard]] WebSocketStats stats() const;

    /**
     * @brief 현재 연결된 URL 조회
     */
    [[nodiscard]] std::string url() const;

    /**
     * @brief 설정 변경
     */
    void setConfig(const WebSocketConfig& config);

signals:
    void connected();
    void disconnected(uint16_t code, const QString& reason);
    void textMessageReceived(const QString& message);
    void binaryMessageReceived(const QByteArray& data);
    void errorOccurred(const QString& error);
    void stateChanged(WebSocketState newState);

private slots:
    void onPingTimer();
    void onReconnectTimer();

private:
    /// WebSocket 핸드셰이크 수행
    bool performHandshake(const QUrl& url);
    
    /// 프레임 인코딩
    std::vector<uint8_t> encodeFrame(
        WebSocketFrameType type,
        const void* data,
        size_t length,
        bool masked = true
    );
    
    /// 프레임 디코딩
    std::optional<WebSocketMessage> decodeFrame(const std::vector<uint8_t>& raw);
    
    /// 마스킹 키 생성
    static std::array<uint8_t, 4> generateMaskKey();
    
    /// 상태 변경
    void setState(WebSocketState newState);
    
    /// 재연결 시도
    void attemptReconnect();

    /// SecurityAgent에 트래픽 보고
    void reportToSecurityAgent(
        const std::string& direction,
        const WebSocketMessage& message
    );

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ordinal::network
