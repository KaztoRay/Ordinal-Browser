/**
 * @file websocket_client.cpp
 * @brief WebSocket 클라이언트 구현 — RFC 6455 호환
 * 
 * WebSocket 프레임 인코딩/디코딩, 핸드셰이크, Ping/Pong,
 * 자동 재연결, SSL/TLS 통합을 구현합니다.
 */

#include "websocket_client.h"

#include <QTcpSocket>
#include <QSslSocket>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QTimer>

#include <iostream>
#include <sstream>
#include <random>
#include <algorithm>

namespace ordinal::network {

// ============================================================
// 내부 구현 (PIMPL)
// ============================================================

struct WebSocketClient::Impl {
    WebSocketConfig config;
    WebSocketState state{WebSocketState::Disconnected};
    WebSocketStats stats;
    
    QUrl url;
    QTcpSocket* socket{nullptr};
    
    QTimer* ping_timer{nullptr};
    QTimer* reconnect_timer{nullptr};
    
    uint32_t reconnect_attempts{0};
    bool awaiting_pong{false};
    std::chrono::steady_clock::time_point last_pong_time;
    
    // 수신 버퍼 (불완전 프레임 처리)
    std::vector<uint8_t> recv_buffer;
    
    // 콜백
    WebSocketClient::OnMessageCallback on_message;
    WebSocketClient::OnCloseCallback on_close;
    WebSocketClient::OnErrorCallback on_error;
    WebSocketClient::OnConnectedCallback on_connected;
    
    mutable std::mutex mutex;
};

// ============================================================
// WebSocket 프레임 상수 (RFC 6455)
// ============================================================

namespace {
    constexpr uint8_t WS_OPCODE_CONTINUATION = 0x00;
    constexpr uint8_t WS_OPCODE_TEXT         = 0x01;
    constexpr uint8_t WS_OPCODE_BINARY       = 0x02;
    constexpr uint8_t WS_OPCODE_CLOSE        = 0x08;
    constexpr uint8_t WS_OPCODE_PING         = 0x09;
    constexpr uint8_t WS_OPCODE_PONG         = 0x0A;
    constexpr uint8_t WS_FIN_BIT             = 0x80;
    constexpr uint8_t WS_MASK_BIT            = 0x80;
    
    // WebSocket 매직 GUID (RFC 6455 §4.2.2)
    const char* WS_MAGIC_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    // Base64 인코딩 테이블
    std::string base64Encode(const QByteArray& input) {
        return input.toBase64().toStdString();
    }
    
    // 16바이트 랜덤 키 생성
    std::string generateWebSocketKey() {
        QByteArray key(16, 0);
        for (int i = 0; i < 16; ++i) {
            key[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
        return base64Encode(key);
    }
    
    // Accept 키 계산 (RFC 6455 §4.2.2)
    std::string computeAcceptKey(const std::string& client_key) {
        QByteArray data = QByteArray::fromStdString(client_key + WS_MAGIC_GUID);
        QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
        return base64Encode(hash);
    }

    // (void) 미사용 파라미터 억제
    [[maybe_unused]] constexpr uint8_t WS_OPCODE_CONTINUATION_UNUSED = WS_OPCODE_CONTINUATION;
} // anonymous namespace

// ============================================================
// 생성자/소멸자
// ============================================================

WebSocketClient::WebSocketClient(const WebSocketConfig& config, QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>())
{
    impl_->config = config;
    
    // Ping 타이머
    impl_->ping_timer = new QTimer(this);
    QObject::connect(impl_->ping_timer, &QTimer::timeout, this, &WebSocketClient::onPingTimer);
    
    // 재연결 타이머
    impl_->reconnect_timer = new QTimer(this);
    impl_->reconnect_timer->setSingleShot(true);
    QObject::connect(impl_->reconnect_timer, &QTimer::timeout, this, &WebSocketClient::onReconnectTimer);
}

WebSocketClient::~WebSocketClient() {
    disconnect(WebSocketCloseCode::GoingAway, "클라이언트 종료");
}

// ============================================================
// 연결 관리
// ============================================================

bool WebSocketClient::connect(const std::string& url) {
    std::lock_guard lock(impl_->mutex);
    
    if (impl_->state == WebSocketState::Connected ||
        impl_->state == WebSocketState::Connecting) {
        std::cerr << "[WebSocket] 이미 연결 중입니다: " << url << std::endl;
        return false;
    }
    
    impl_->url = QUrl(QString::fromStdString(url));
    
    if (!impl_->url.isValid()) {
        std::cerr << "[WebSocket] 잘못된 URL: " << url << std::endl;
        setState(WebSocketState::Error);
        return false;
    }
    
    QString scheme = impl_->url.scheme().toLower();
    if (scheme != "ws" && scheme != "wss") {
        std::cerr << "[WebSocket] 지원되지 않는 스키마: " 
                  << scheme.toStdString() << std::endl;
        setState(WebSocketState::Error);
        return false;
    }
    
    setState(WebSocketState::Connecting);
    impl_->reconnect_attempts = 0;
    
    // 소켓 생성 (SSL 여부에 따라)
    bool use_ssl = (scheme == "wss");
    
    if (use_ssl) {
        auto* ssl_socket = new QSslSocket(this);
        impl_->socket = ssl_socket;
        
        if (!impl_->config.verify_ssl) {
            // 인증서 검증 비활성화 (개발용)
            ssl_socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        }
        
        // SSL 연결
        int port = impl_->url.port(443);
        ssl_socket->connectToHostEncrypted(impl_->url.host(), port);
    } else {
        impl_->socket = new QTcpSocket(this);
        int port = impl_->url.port(80);
        impl_->socket->connectToHost(impl_->url.host(), port);
    }
    
    // 소켓 시그널 연결
    QObject::connect(impl_->socket, &QTcpSocket::connected, this, [this]() {
        // TCP 연결 성공 → WebSocket 핸드셰이크
        if (performHandshake(impl_->url)) {
            setState(WebSocketState::Connected);
            impl_->stats.connected_since = std::chrono::steady_clock::now();
            impl_->reconnect_attempts = 0;
            
            // Ping 타이머 시작
            if (impl_->config.ping_interval_sec > 0) {
                impl_->ping_timer->start(
                    static_cast<int>(impl_->config.ping_interval_sec * 1000)
                );
            }
            
            if (impl_->on_connected) impl_->on_connected();
            emit connected();
        }
    });
    
    QObject::connect(impl_->socket, &QTcpSocket::readyRead, this, [this]() {
        // 데이터 수신 처리
        QByteArray raw = impl_->socket->readAll();
        std::vector<uint8_t> data(raw.begin(), raw.end());
        impl_->recv_buffer.insert(impl_->recv_buffer.end(), data.begin(), data.end());
        
        // 프레임 디코딩 시도
        while (auto msg = decodeFrame(impl_->recv_buffer)) {
            impl_->stats.messages_received++;
            impl_->stats.bytes_received += msg->text_data.size() + msg->binary_data.size();
            
            switch (msg->type) {
                case WebSocketFrameType::Text:
                    if (impl_->config.enable_security_analysis) {
                        reportToSecurityAgent("recv", *msg);
                    }
                    if (impl_->on_message) impl_->on_message(*msg);
                    emit textMessageReceived(QString::fromStdString(msg->text_data));
                    break;
                    
                case WebSocketFrameType::Binary:
                    if (impl_->on_message) impl_->on_message(*msg);
                    emit binaryMessageReceived(QByteArray(
                        reinterpret_cast<const char*>(msg->binary_data.data()),
                        static_cast<int>(msg->binary_data.size())
                    ));
                    break;
                    
                case WebSocketFrameType::Ping:
                    // Pong 자동 응답
                    sendPing(msg->text_data);
                    break;
                    
                case WebSocketFrameType::Pong:
                    impl_->awaiting_pong = false;
                    impl_->last_pong_time = std::chrono::steady_clock::now();
                    break;
                    
                case WebSocketFrameType::Close: {
                    uint16_t code = 1000;
                    std::string reason;
                    if (msg->binary_data.size() >= 2) {
                        code = (static_cast<uint16_t>(msg->binary_data[0]) << 8) |
                               msg->binary_data[1];
                        if (msg->binary_data.size() > 2) {
                            reason.assign(msg->binary_data.begin() + 2, msg->binary_data.end());
                        }
                    }
                    setState(WebSocketState::Disconnected);
                    if (impl_->on_close) {
                        impl_->on_close(static_cast<WebSocketCloseCode>(code), reason);
                    }
                    emit disconnected(code, QString::fromStdString(reason));
                    break;
                }
            }
        }
    });
    
    QObject::connect(impl_->socket, &QTcpSocket::disconnected, this, [this]() {
        impl_->ping_timer->stop();
        
        if (impl_->state != WebSocketState::Disconnected) {
            setState(WebSocketState::Disconnected);
            
            if (impl_->on_close) {
                impl_->on_close(WebSocketCloseCode::Abnormal, "연결 끊김");
            }
            emit disconnected(
                static_cast<uint16_t>(WebSocketCloseCode::Abnormal),
                "연결 끊김"
            );
            
            // 자동 재연결
            if (impl_->config.auto_reconnect) {
                attemptReconnect();
            }
        }
    });
    
    std::cout << "[WebSocket] 연결 시도: " << url << std::endl;
    return true;
}

void WebSocketClient::disconnect(WebSocketCloseCode code, const std::string& reason) {
    std::lock_guard lock(impl_->mutex);
    
    impl_->config.auto_reconnect = false; // 수동 종료 시 재연결 비활성화
    impl_->ping_timer->stop();
    impl_->reconnect_timer->stop();
    
    if (impl_->socket && impl_->state == WebSocketState::Connected) {
        setState(WebSocketState::Closing);
        
        // Close 프레임 전송
        std::vector<uint8_t> payload;
        uint16_t code_val = static_cast<uint16_t>(code);
        payload.push_back(static_cast<uint8_t>(code_val >> 8));
        payload.push_back(static_cast<uint8_t>(code_val & 0xFF));
        payload.insert(payload.end(), reason.begin(), reason.end());
        
        auto frame = encodeFrame(WebSocketFrameType::Close, payload.data(), payload.size());
        impl_->socket->write(
            reinterpret_cast<const char*>(frame.data()),
            static_cast<qint64>(frame.size())
        );
        impl_->socket->flush();
        impl_->socket->disconnectFromHost();
    }
    
    setState(WebSocketState::Disconnected);
}

WebSocketState WebSocketClient::state() const {
    return impl_->state;
}

bool WebSocketClient::isConnected() const {
    return impl_->state == WebSocketState::Connected;
}

// ============================================================
// 메시지 송수신
// ============================================================

bool WebSocketClient::sendText(const std::string& text) {
    std::lock_guard lock(impl_->mutex);
    
    if (impl_->state != WebSocketState::Connected || !impl_->socket) {
        return false;
    }
    
    if (impl_->config.max_message_size > 0 && text.size() > impl_->config.max_message_size) {
        std::cerr << "[WebSocket] 메시지 크기 초과: " << text.size() << " bytes" << std::endl;
        return false;
    }
    
    auto frame = encodeFrame(WebSocketFrameType::Text, text.data(), text.size());
    qint64 written = impl_->socket->write(
        reinterpret_cast<const char*>(frame.data()),
        static_cast<qint64>(frame.size())
    );
    
    if (written > 0) {
        impl_->stats.messages_sent++;
        impl_->stats.bytes_sent += text.size();
        
        if (impl_->config.enable_security_analysis) {
            WebSocketMessage msg;
            msg.type = WebSocketFrameType::Text;
            msg.text_data = text;
            msg.timestamp = std::chrono::steady_clock::now();
            reportToSecurityAgent("send", msg);
        }
        
        return true;
    }
    
    return false;
}

bool WebSocketClient::sendBinary(const std::vector<uint8_t>& data) {
    std::lock_guard lock(impl_->mutex);
    
    if (impl_->state != WebSocketState::Connected || !impl_->socket) {
        return false;
    }
    
    if (impl_->config.max_message_size > 0 && data.size() > impl_->config.max_message_size) {
        return false;
    }
    
    auto frame = encodeFrame(WebSocketFrameType::Binary, data.data(), data.size());
    qint64 written = impl_->socket->write(
        reinterpret_cast<const char*>(frame.data()),
        static_cast<qint64>(frame.size())
    );
    
    if (written > 0) {
        impl_->stats.messages_sent++;
        impl_->stats.bytes_sent += data.size();
        return true;
    }
    
    return false;
}

void WebSocketClient::sendPing(const std::string& payload) {
    std::lock_guard lock(impl_->mutex);
    
    if (impl_->state != WebSocketState::Connected || !impl_->socket) {
        return;
    }
    
    auto frame = encodeFrame(WebSocketFrameType::Ping, payload.data(), payload.size());
    impl_->socket->write(
        reinterpret_cast<const char*>(frame.data()),
        static_cast<qint64>(frame.size())
    );
    impl_->awaiting_pong = true;
}

// ============================================================
// 콜백 등록
// ============================================================

void WebSocketClient::onMessage(OnMessageCallback callback) {
    impl_->on_message = std::move(callback);
}

void WebSocketClient::onClose(OnCloseCallback callback) {
    impl_->on_close = std::move(callback);
}

void WebSocketClient::onError(OnErrorCallback callback) {
    impl_->on_error = std::move(callback);
}

void WebSocketClient::onConnected(OnConnectedCallback callback) {
    impl_->on_connected = std::move(callback);
}

// ============================================================
// 상태 조회
// ============================================================

WebSocketStats WebSocketClient::stats() const {
    return impl_->stats;
}

std::string WebSocketClient::url() const {
    return impl_->url.toString().toStdString();
}

void WebSocketClient::setConfig(const WebSocketConfig& config) {
    std::lock_guard lock(impl_->mutex);
    impl_->config = config;
}

// ============================================================
// 프레임 인코딩/디코딩
// ============================================================

std::vector<uint8_t> WebSocketClient::encodeFrame(
    WebSocketFrameType type,
    const void* data,
    size_t length,
    bool masked
) {
    std::vector<uint8_t> frame;
    
    // 첫 번째 바이트: FIN + Opcode
    uint8_t opcode;
    switch (type) {
        case WebSocketFrameType::Text:   opcode = WS_OPCODE_TEXT; break;
        case WebSocketFrameType::Binary: opcode = WS_OPCODE_BINARY; break;
        case WebSocketFrameType::Ping:   opcode = WS_OPCODE_PING; break;
        case WebSocketFrameType::Pong:   opcode = WS_OPCODE_PONG; break;
        case WebSocketFrameType::Close:  opcode = WS_OPCODE_CLOSE; break;
    }
    frame.push_back(WS_FIN_BIT | opcode);
    
    // 두 번째 바이트: MASK + Payload Length
    uint8_t mask_bit = masked ? WS_MASK_BIT : 0;
    
    if (length <= 125) {
        frame.push_back(mask_bit | static_cast<uint8_t>(length));
    } else if (length <= 65535) {
        frame.push_back(mask_bit | 126);
        frame.push_back(static_cast<uint8_t>(length >> 8));
        frame.push_back(static_cast<uint8_t>(length & 0xFF));
    } else {
        frame.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((length >> (8 * i)) & 0xFF));
        }
    }
    
    // 마스킹 키 + 마스킹된 페이로드
    if (masked) {
        auto mask_key = generateMaskKey();
        frame.insert(frame.end(), mask_key.begin(), mask_key.end());
        
        const auto* payload = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; ++i) {
            frame.push_back(payload[i] ^ mask_key[i % 4]);
        }
    } else {
        const auto* payload = static_cast<const uint8_t*>(data);
        frame.insert(frame.end(), payload, payload + length);
    }
    
    return frame;
}

std::optional<WebSocketMessage> WebSocketClient::decodeFrame(
    const std::vector<uint8_t>& raw
) {
    if (raw.size() < 2) return std::nullopt;
    
    size_t pos = 0;
    
    // 첫 번째 바이트
    [[maybe_unused]] bool fin = (raw[pos] & WS_FIN_BIT) != 0;
    uint8_t opcode = raw[pos] & 0x0F;
    pos++;
    
    // 두 번째 바이트
    bool masked = (raw[pos] & WS_MASK_BIT) != 0;
    uint64_t payload_len = raw[pos] & 0x7F;
    pos++;
    
    if (payload_len == 126) {
        if (raw.size() < pos + 2) return std::nullopt;
        payload_len = (static_cast<uint64_t>(raw[pos]) << 8) | raw[pos + 1];
        pos += 2;
    } else if (payload_len == 127) {
        if (raw.size() < pos + 8) return std::nullopt;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | raw[pos + i];
        }
        pos += 8;
    }
    
    // 마스킹 키
    std::array<uint8_t, 4> mask_key{};
    if (masked) {
        if (raw.size() < pos + 4) return std::nullopt;
        std::copy_n(raw.begin() + static_cast<long>(pos), 4, mask_key.begin());
        pos += 4;
    }
    
    // 페이로드
    if (raw.size() < pos + payload_len) return std::nullopt;
    
    std::vector<uint8_t> payload(payload_len);
    for (uint64_t i = 0; i < payload_len; ++i) {
        payload[i] = raw[pos + i];
        if (masked) payload[i] ^= mask_key[i % 4];
    }
    
    // 버퍼에서 처리된 데이터 제거
    auto& buffer = const_cast<std::vector<uint8_t>&>(raw);
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(pos + payload_len));
    
    // 메시지 구성
    WebSocketMessage msg;
    msg.timestamp = std::chrono::steady_clock::now();
    
    switch (opcode) {
        case WS_OPCODE_TEXT:
            msg.type = WebSocketFrameType::Text;
            msg.text_data.assign(payload.begin(), payload.end());
            break;
        case WS_OPCODE_BINARY:
            msg.type = WebSocketFrameType::Binary;
            msg.binary_data = std::move(payload);
            break;
        case WS_OPCODE_PING:
            msg.type = WebSocketFrameType::Ping;
            msg.text_data.assign(payload.begin(), payload.end());
            break;
        case WS_OPCODE_PONG:
            msg.type = WebSocketFrameType::Pong;
            break;
        case WS_OPCODE_CLOSE:
            msg.type = WebSocketFrameType::Close;
            msg.binary_data = std::move(payload);
            break;
        default:
            return std::nullopt;
    }
    
    return msg;
}

std::array<uint8_t, 4> WebSocketClient::generateMaskKey() {
    std::array<uint8_t, 4> key;
    for (auto& byte : key) {
        byte = static_cast<uint8_t>(QRandomGenerator::global()->bounded(256));
    }
    return key;
}

// ============================================================
// 핸드셰이크
// ============================================================

bool WebSocketClient::performHandshake(const QUrl& url) {
    std::string ws_key = generateWebSocketKey();
    
    // HTTP Upgrade 요청 구성
    std::ostringstream request;
    request << "GET " << url.path().toStdString();
    if (url.hasQuery()) {
        request << "?" << url.query().toStdString();
    }
    request << " HTTP/1.1\r\n";
    request << "Host: " << url.host().toStdString();
    if (url.port() != -1) {
        request << ":" << url.port();
    }
    request << "\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Sec-WebSocket-Key: " << ws_key << "\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    
    // 서브프로토콜
    if (!impl_->config.subprotocols.empty()) {
        request << "Sec-WebSocket-Protocol: ";
        for (size_t i = 0; i < impl_->config.subprotocols.size(); ++i) {
            if (i > 0) request << ", ";
            request << impl_->config.subprotocols[i];
        }
        request << "\r\n";
    }
    
    // 커스텀 헤더
    for (const auto& [key, value] : impl_->config.custom_headers) {
        request << key << ": " << value << "\r\n";
    }
    
    request << "\r\n";
    
    // 요청 전송
    std::string req_str = request.str();
    impl_->socket->write(req_str.c_str(), static_cast<qint64>(req_str.size()));
    impl_->socket->flush();
    
    // 응답 대기 (동기)
    if (!impl_->socket->waitForReadyRead(
            static_cast<int>(impl_->config.connect_timeout_sec * 1000))) {
        std::cerr << "[WebSocket] 핸드셰이크 타임아웃" << std::endl;
        return false;
    }
    
    QByteArray response = impl_->socket->readAll();
    std::string resp_str = response.toStdString();
    
    // 101 Switching Protocols 확인
    if (resp_str.find("HTTP/1.1 101") == std::string::npos) {
        std::cerr << "[WebSocket] 핸드셰이크 실패 — 101이 아닙니다" << std::endl;
        return false;
    }
    
    // Sec-WebSocket-Accept 검증
    std::string expected_accept = computeAcceptKey(ws_key);
    if (resp_str.find(expected_accept) == std::string::npos) {
        std::cerr << "[WebSocket] 핸드셰이크 실패 — Accept 키 불일치" << std::endl;
        return false;
    }
    
    std::cout << "[WebSocket] 핸드셰이크 성공: " << url.toString().toStdString() << std::endl;
    return true;
}

// ============================================================
// 타이머 핸들러
// ============================================================

void WebSocketClient::onPingTimer() {
    if (impl_->awaiting_pong) {
        // Pong 타임아웃 — 연결 끊김으로 간주
        auto elapsed = std::chrono::steady_clock::now() - impl_->last_pong_time;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        
        if (static_cast<uint32_t>(elapsed_sec) > impl_->config.pong_timeout_sec) {
            std::cerr << "[WebSocket] Pong 타임아웃 — 연결 종료" << std::endl;
            disconnect(WebSocketCloseCode::Abnormal, "Pong 타임아웃");
            return;
        }
    }
    
    sendPing("ordinal-ping");
}

void WebSocketClient::onReconnectTimer() {
    std::cout << "[WebSocket] 재연결 시도 #" << impl_->reconnect_attempts << std::endl;
    connect(impl_->url.toString().toStdString());
}

// ============================================================
// 유틸리티
// ============================================================

void WebSocketClient::setState(WebSocketState newState) {
    if (impl_->state != newState) {
        impl_->state = newState;
        emit stateChanged(newState);
    }
}

void WebSocketClient::attemptReconnect() {
    if (!impl_->config.auto_reconnect) return;
    
    if (impl_->config.max_reconnect_attempts > 0 &&
        impl_->reconnect_attempts >= impl_->config.max_reconnect_attempts) {
        std::cerr << "[WebSocket] 최대 재연결 시도 초과" << std::endl;
        if (impl_->on_error) {
            impl_->on_error("최대 재연결 시도 횟수 초과");
        }
        emit errorOccurred("최대 재연결 시도 횟수 초과");
        return;
    }
    
    impl_->reconnect_attempts++;
    impl_->stats.reconnect_count++;
    
    // 지수 백오프 (최대 60초)
    uint32_t delay = std::min(
        impl_->config.reconnect_interval_sec * impl_->reconnect_attempts,
        60u
    );
    
    std::cout << "[WebSocket] " << delay << "초 후 재연결 시도 (시도 #"
              << impl_->reconnect_attempts << ")" << std::endl;
    
    impl_->reconnect_timer->start(static_cast<int>(delay * 1000));
}

void WebSocketClient::reportToSecurityAgent(
    const std::string& direction,
    const WebSocketMessage& message
) {
    // SecurityAgent에 WebSocket 트래픽 보고
    // TODO: SecurityAgent 인스턴스와 통합
    (void)direction;
    (void)message;
}

} // namespace ordinal::network
