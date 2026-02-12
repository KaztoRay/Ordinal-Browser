/**
 * @file html_parser.cpp
 * @brief HTML5 파서 구현
 * 
 * HTML5 사양에 기반한 토크나이저 상태 머신과
 * 트리 생성 알고리즘을 구현합니다.
 */

#include "html_parser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace ordinal::rendering {

// ============================================================
// HTML 엔티티 매핑 테이블
// ============================================================

const std::unordered_map<std::string, std::string> HtmlParser::entity_map_ = {
    {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""},
    {"apos", "'"}, {"nbsp", "\xC2\xA0"}, {"copy", "\xC2\xA9"},
    {"reg", "\xC2\xAE"}, {"trade", "\xE2\x84\xA2"},
    {"laquo", "\xC2\xAB"}, {"raquo", "\xC2\xBB"},
    {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"},
    {"bull", "\xE2\x80\xA2"}, {"hellip", "\xE2\x80\xA6"},
    {"prime", "\xE2\x80\xB2"}, {"Prime", "\xE2\x80\xB3"},
    {"lsquo", "\xE2\x80\x98"}, {"rsquo", "\xE2\x80\x99"},
    {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"},
    {"euro", "\xE2\x82\xAC"}, {"pound", "\xC2\xA3"},
    {"yen", "\xC2\xA5"}, {"cent", "\xC2\xA2"},
    {"times", "\xC3\x97"}, {"divide", "\xC3\xB7"},
    {"para", "\xC2\xB6"}, {"sect", "\xC2\xA7"},
    {"deg", "\xC2\xB0"}, {"micro", "\xC2\xB5"},
    {"frac12", "\xC2\xBD"}, {"frac14", "\xC2\xBC"},
    {"frac34", "\xC2\xBE"}, {"iexcl", "\xC2\xA1"},
    {"iquest", "\xC2\xBF"}, {"lArr", "\xE2\x87\x90"},
    {"rArr", "\xE2\x87\x92"}, {"larr", "\xE2\x86\x90"},
    {"rarr", "\xE2\x86\x92"}, {"uarr", "\xE2\x86\x91"},
    {"darr", "\xE2\x86\x93"}, {"hearts", "\xE2\x99\xA5"},
    {"diams", "\xE2\x99\xA6"}, {"clubs", "\xE2\x99\xA3"},
    {"spades", "\xE2\x99\xA0"},
};

// ============================================================
// 생성자
// ============================================================

HtmlParser::HtmlParser() = default;

// ============================================================
// 공개 메서드
// ============================================================

DocumentPtr HtmlParser::parse(const std::string& html) {
    // 초기화
    document_ = std::make_shared<DocumentNode>();
    while (!open_elements_.empty()) open_elements_.pop();
    head_inserted_ = false;
    body_inserted_ = false;
    errors_.clear();

    // 토큰화
    auto tokens = tokenize(html);

    // 트리 생성 — 토큰을 순서대로 처리
    for (const auto& token : tokens) {
        processToken(token);
    }

    // 열린 태그 정리 — 닫히지 않은 태그 경고
    while (!open_elements_.empty()) {
        errors_.push_back("닫히지 않은 태그: <" + open_elements_.top()->tagName() + ">");
        open_elements_.pop();
    }

    return document_;
}

std::vector<HtmlToken> HtmlParser::tokenize(const std::string& html) {
    // 상태 초기화
    state_ = TokenizerState::Data;
    input_ = html;
    pos_ = 0;
    tokens_.clear();
    current_token_ = {};
    current_attr_name_.clear();
    current_attr_value_.clear();

    // 토크나이저 상태 머신 실행
    runTokenizer(html);

    return tokens_;
}

std::string HtmlParser::decodeEntities(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '&') {
            // 엔티티 시작
            size_t semicolon = text.find(';', i + 1);
            if (semicolon != std::string::npos && semicolon - i < 12) {
                std::string entity_name = text.substr(i + 1, semicolon - i - 1);
                
                // 숫자 엔티티: &#123; 또는 &#x1F;
                if (!entity_name.empty() && entity_name[0] == '#') {
                    uint32_t code_point = 0;
                    bool valid = false;
                    
                    if (entity_name.size() > 1 && (entity_name[1] == 'x' || entity_name[1] == 'X')) {
                        // 16진수 엔티티
                        try {
                            code_point = static_cast<uint32_t>(
                                std::stoul(entity_name.substr(2), nullptr, 16));
                            valid = true;
                        } catch (...) {}
                    } else {
                        // 10진수 엔티티
                        try {
                            code_point = static_cast<uint32_t>(
                                std::stoul(entity_name.substr(1), nullptr, 10));
                            valid = true;
                        } catch (...) {}
                    }
                    
                    if (valid && code_point > 0) {
                        // UTF-8 인코딩
                        if (code_point < 0x80) {
                            result += static_cast<char>(code_point);
                        } else if (code_point < 0x800) {
                            result += static_cast<char>(0xC0 | (code_point >> 6));
                            result += static_cast<char>(0x80 | (code_point & 0x3F));
                        } else if (code_point < 0x10000) {
                            result += static_cast<char>(0xE0 | (code_point >> 12));
                            result += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (code_point & 0x3F));
                        } else if (code_point < 0x110000) {
                            result += static_cast<char>(0xF0 | (code_point >> 18));
                            result += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
                            result += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (code_point & 0x3F));
                        }
                        i = semicolon + 1;
                        continue;
                    }
                }
                
                // 이름 엔티티
                auto it = entity_map_.find(entity_name);
                if (it != entity_map_.end()) {
                    result += it->second;
                    i = semicolon + 1;
                    continue;
                }
            }
            
            // 인식할 수 없는 엔티티 — 그대로 출력
            result += '&';
            ++i;
        } else {
            result += text[i];
            ++i;
        }
    }
    
    return result;
}

std::string HtmlParser::escapeHtml(const std::string& text) {
    std::string result;
    result.reserve(text.size() * 1.2);
    
    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default:   result += c; break;
        }
    }
    
    return result;
}

// ============================================================
// 토크나이저 상태 머신
// ============================================================

void HtmlParser::runTokenizer(const std::string& /*html*/) {
    while (!atEnd()) {
        char c = consume();
        
        switch (state_) {
            // ----------------------------------------
            // Data 상태 — 일반 텍스트
            // ----------------------------------------
            case TokenizerState::Data: {
                if (c == '<') {
                    // 축적된 텍스트 발행
                    if (!current_token_.data.empty()) {
                        current_token_.type = TokenType::Text;
                        current_token_.data = decodeEntities(current_token_.data);
                        emitToken(current_token_);
                        current_token_ = {};
                    }
                    state_ = TokenizerState::TagOpen;
                } else {
                    current_token_.data += c;
                }
                break;
            }
            
            // ----------------------------------------
            // TagOpen 상태 — '<' 후
            // ----------------------------------------
            case TokenizerState::TagOpen: {
                if (c == '/') {
                    state_ = TokenizerState::EndTagOpen;
                } else if (c == '!') {
                    state_ = TokenizerState::MarkupDeclarationOpen;
                } else if (std::isalpha(static_cast<unsigned char>(c))) {
                    current_token_ = {};
                    current_token_.type = TokenType::StartTag;
                    current_token_.name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    state_ = TokenizerState::TagName;
                } else {
                    // 잘못된 태그 — 텍스트로 처리
                    errors_.push_back("잘못된 태그 시작 문자: " + std::string(1, c));
                    current_token_.data += '<';
                    current_token_.data += c;
                    state_ = TokenizerState::Data;
                }
                break;
            }
            
            // ----------------------------------------
            // EndTagOpen 상태 — '</' 후
            // ----------------------------------------
            case TokenizerState::EndTagOpen: {
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    current_token_ = {};
                    current_token_.type = TokenType::EndTag;
                    current_token_.name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    state_ = TokenizerState::TagName;
                } else {
                    errors_.push_back("잘못된 종료 태그");
                    state_ = TokenizerState::BogusComment;
                    current_token_ = {};
                    current_token_.data += c;
                }
                break;
            }
            
            // ----------------------------------------
            // TagName 상태 — 태그 이름 읽기
            // ----------------------------------------
            case TokenizerState::TagName: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    state_ = TokenizerState::BeforeAttributeName;
                } else if (c == '/') {
                    state_ = TokenizerState::SelfClosingStartTag;
                } else if (c == '>') {
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    current_token_.name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                break;
            }
            
            // ----------------------------------------
            // BeforeAttributeName — 속성 이름 시작 전
            // ----------------------------------------
            case TokenizerState::BeforeAttributeName: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    // 공백 무시
                } else if (c == '/' || c == '>') {
                    // 태그 종료
                    if (c == '/') {
                        state_ = TokenizerState::SelfClosingStartTag;
                    } else {
                        emitToken(current_token_);
                        current_token_ = {};
                        state_ = TokenizerState::Data;
                    }
                } else {
                    // 속성 이름 시작
                    current_attr_name_.clear();
                    current_attr_value_.clear();
                    current_attr_name_ += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    state_ = TokenizerState::AttributeName;
                }
                break;
            }
            
            // ----------------------------------------
            // AttributeName — 속성 이름 읽기
            // ----------------------------------------
            case TokenizerState::AttributeName: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    state_ = TokenizerState::AfterAttributeName;
                } else if (c == '=') {
                    state_ = TokenizerState::BeforeAttributeValue;
                } else if (c == '/' || c == '>') {
                    // 값 없는 속성 추가
                    addAttribute();
                    if (c == '/') {
                        state_ = TokenizerState::SelfClosingStartTag;
                    } else {
                        emitToken(current_token_);
                        current_token_ = {};
                        state_ = TokenizerState::Data;
                    }
                } else {
                    current_attr_name_ += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                break;
            }
            
            // ----------------------------------------
            // AfterAttributeName — 속성 이름 후
            // ----------------------------------------
            case TokenizerState::AfterAttributeName: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    // 공백 무시
                } else if (c == '=') {
                    state_ = TokenizerState::BeforeAttributeValue;
                } else if (c == '/' || c == '>') {
                    addAttribute();
                    if (c == '/') {
                        state_ = TokenizerState::SelfClosingStartTag;
                    } else {
                        emitToken(current_token_);
                        current_token_ = {};
                        state_ = TokenizerState::Data;
                    }
                } else {
                    // 이전 속성 저장하고 새 속성 시작
                    addAttribute();
                    current_attr_name_.clear();
                    current_attr_value_.clear();
                    current_attr_name_ += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    state_ = TokenizerState::AttributeName;
                }
                break;
            }
            
            // ----------------------------------------
            // BeforeAttributeValue — '=' 후 값 시작 전
            // ----------------------------------------
            case TokenizerState::BeforeAttributeValue: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    // 공백 무시
                } else if (c == '"') {
                    state_ = TokenizerState::AttributeValueDoubleQuoted;
                } else if (c == '\'') {
                    state_ = TokenizerState::AttributeValueSingleQuoted;
                } else if (c == '>') {
                    addAttribute();
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    current_attr_value_ += c;
                    state_ = TokenizerState::AttributeValueUnquoted;
                }
                break;
            }
            
            // ----------------------------------------
            // 따옴표 안 속성 값 읽기
            // ----------------------------------------
            case TokenizerState::AttributeValueDoubleQuoted: {
                if (c == '"') {
                    addAttribute();
                    state_ = TokenizerState::AfterAttributeValue;
                } else {
                    current_attr_value_ += c;
                }
                break;
            }
            
            case TokenizerState::AttributeValueSingleQuoted: {
                if (c == '\'') {
                    addAttribute();
                    state_ = TokenizerState::AfterAttributeValue;
                } else {
                    current_attr_value_ += c;
                }
                break;
            }
            
            case TokenizerState::AttributeValueUnquoted: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    addAttribute();
                    state_ = TokenizerState::BeforeAttributeName;
                } else if (c == '>') {
                    addAttribute();
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    current_attr_value_ += c;
                }
                break;
            }
            
            // ----------------------------------------
            // AfterAttributeValue — 속성 값 후
            // ----------------------------------------
            case TokenizerState::AfterAttributeValue: {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    state_ = TokenizerState::BeforeAttributeName;
                } else if (c == '/') {
                    state_ = TokenizerState::SelfClosingStartTag;
                } else if (c == '>') {
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    errors_.push_back("속성 값 후 예상치 못한 문자");
                    current_attr_name_.clear();
                    current_attr_value_.clear();
                    current_attr_name_ += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    state_ = TokenizerState::AttributeName;
                }
                break;
            }
            
            // ----------------------------------------
            // SelfClosingStartTag — 자기 닫힘 태그
            // ----------------------------------------
            case TokenizerState::SelfClosingStartTag: {
                if (c == '>') {
                    current_token_.self_closing = true;
                    current_token_.type = TokenType::SelfClosingTag;
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    errors_.push_back("자기 닫힘 태그에서 예상치 못한 문자");
                    state_ = TokenizerState::BeforeAttributeName;
                }
                break;
            }
            
            // ----------------------------------------
            // MarkupDeclarationOpen — '<!' 후
            // ----------------------------------------
            case TokenizerState::MarkupDeclarationOpen: {
                if (c == '-' && !atEnd() && peek() == '-') {
                    consume(); // 두 번째 '-' 소비
                    current_token_ = {};
                    current_token_.type = TokenType::Comment;
                    state_ = TokenizerState::Comment;
                } else {
                    // DOCTYPE 또는 기타 선언
                    std::string rest;
                    rest += c;
                    
                    // "DOCTYPE" 확인 (대소문자 무시)
                    size_t saved = pos_;
                    while (!atEnd() && rest.size() < 7) {
                        rest += consume();
                    }
                    
                    std::string upper_rest = rest;
                    std::transform(upper_rest.begin(), upper_rest.end(), upper_rest.begin(),
                                   [](unsigned char ch) { return std::toupper(ch); });
                    
                    if (upper_rest == "DOCTYPE") {
                        // DOCTYPE 읽기
                        std::string doctype_value;
                        while (!atEnd()) {
                            char dc = consume();
                            if (dc == '>') break;
                            doctype_value += dc;
                        }
                        // 공백 트리밍
                        size_t start = doctype_value.find_first_not_of(" \t\n\r");
                        if (start != std::string::npos) {
                            doctype_value = doctype_value.substr(start);
                        }
                        
                        HtmlToken doctype_token;
                        doctype_token.type = TokenType::Doctype;
                        doctype_token.name = doctype_value;
                        emitToken(doctype_token);
                        state_ = TokenizerState::Data;
                    } else {
                        // 인식할 수 없는 선언 — bogus comment로 처리
                        pos_ = saved;
                        current_token_ = {};
                        current_token_.data = rest;
                        state_ = TokenizerState::BogusComment;
                    }
                }
                break;
            }
            
            // ----------------------------------------
            // Comment 상태
            // ----------------------------------------
            case TokenizerState::Comment: {
                if (c == '-') {
                    state_ = TokenizerState::CommentEndDash;
                } else {
                    current_token_.data += c;
                }
                break;
            }
            
            case TokenizerState::CommentEndDash: {
                if (c == '-') {
                    state_ = TokenizerState::CommentEnd;
                } else {
                    current_token_.data += '-';
                    current_token_.data += c;
                    state_ = TokenizerState::Comment;
                }
                break;
            }
            
            case TokenizerState::CommentEnd: {
                if (c == '>') {
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    current_token_.data += "--";
                    current_token_.data += c;
                    state_ = TokenizerState::Comment;
                }
                break;
            }
            
            // ----------------------------------------
            // BogusComment — 잘못된 주석
            // ----------------------------------------
            case TokenizerState::BogusComment: {
                if (c == '>') {
                    current_token_.type = TokenType::Comment;
                    emitToken(current_token_);
                    current_token_ = {};
                    state_ = TokenizerState::Data;
                } else {
                    current_token_.data += c;
                }
                break;
            }
            
            default:
                // 알 수 없는 상태 — Data로 복귀
                state_ = TokenizerState::Data;
                break;
        }
    }
    
    // 남은 텍스트 토큰 발행
    if (!current_token_.data.empty() && state_ == TokenizerState::Data) {
        current_token_.type = TokenType::Text;
        current_token_.data = decodeEntities(current_token_.data);
        emitToken(current_token_);
    }
    
    // EOF 토큰
    HtmlToken eof;
    eof.type = TokenType::EndOfFile;
    emitToken(eof);
}

char HtmlParser::consume() {
    if (pos_ < input_.size()) {
        return input_[pos_++];
    }
    return '\0';
}

char HtmlParser::peek() const {
    if (pos_ < input_.size()) {
        return input_[pos_];
    }
    return '\0';
}

bool HtmlParser::atEnd() const {
    return pos_ >= input_.size();
}

void HtmlParser::emitToken(HtmlToken token) {
    tokens_.push_back(std::move(token));
}

void HtmlParser::addAttribute() {
    if (!current_attr_name_.empty()) {
        // 엔티티 디코딩된 속성 값
        std::string decoded_value = decodeEntities(current_attr_value_);
        current_token_.attributes.emplace_back(current_attr_name_, decoded_value);
        current_attr_name_.clear();
        current_attr_value_.clear();
    }
}

// ============================================================
// 트리 생성
// ============================================================

void HtmlParser::processToken(const HtmlToken& token) {
    switch (token.type) {
        case TokenType::Doctype: {
            document_->setDoctype(token.name);
            break;
        }
        
        case TokenType::StartTag:
        case TokenType::SelfClosingTag: {
            // 암시적 요소 보장
            ensureImplicitElements();
            
            // 요소 생성
            auto element = std::make_shared<ElementNode>(token.name);
            
            // 속성 설정
            for (const auto& [name, value] : token.attributes) {
                element->setAttribute(name, value);
            }
            
            // title 태그 처리
            if (token.name == "title") {
                // title 내용은 다음 텍스트 토큰에서 설정
            }
            
            // 현재 부모에 추가
            if (!open_elements_.empty()) {
                open_elements_.top()->appendChild(element);
            } else {
                document_->appendChild(element);
            }
            
            // head/body 추적
            if (token.name == "head") head_inserted_ = true;
            if (token.name == "body") body_inserted_ = true;
            
            // void 요소 또는 자기 닫힘이 아니면 스택에 푸시
            if (!isVoidElement(token.name) && token.type != TokenType::SelfClosingTag) {
                open_elements_.push(element);
            }
            
            break;
        }
        
        case TokenType::EndTag: {
            // 매칭되는 시작 태그 찾기
            if (!open_elements_.empty()) {
                if (open_elements_.top()->tagName() == token.name) {
                    // 직접 매칭 — 스택에서 제거
                    open_elements_.pop();
                } else if (hasInScope(token.name)) {
                    // 스택에서 매칭되는 태그까지 팝
                    while (!open_elements_.empty() &&
                           open_elements_.top()->tagName() != token.name) {
                        errors_.push_back("암시적으로 닫힘: <" + 
                                         open_elements_.top()->tagName() + ">");
                        open_elements_.pop();
                    }
                    if (!open_elements_.empty()) {
                        open_elements_.pop();
                    }
                } else {
                    errors_.push_back("매칭되지 않는 종료 태그: </" + token.name + ">");
                }
            } else {
                errors_.push_back("스택이 비어있는 상태에서 종료 태그: </" + token.name + ">");
            }
            break;
        }
        
        case TokenType::Text: {
            // 텍스트 노드 생성
            auto text_node = std::make_shared<TextNode>(token.data);
            
            if (!open_elements_.empty()) {
                // title 태그 안이면 문서 제목 설정
                if (open_elements_.top()->tagName() == "title") {
                    document_->setTitle(token.data);
                }
                open_elements_.top()->appendChild(text_node);
            } else if (document_) {
                // 루트 레벨 텍스트 (보통 공백)
                if (token.data.find_first_not_of(" \t\n\r") != std::string::npos) {
                    ensureImplicitElements();
                    if (!open_elements_.empty()) {
                        open_elements_.top()->appendChild(text_node);
                    }
                }
            }
            break;
        }
        
        case TokenType::Comment: {
            auto comment_node = std::make_shared<CommentNode>(token.data);
            
            if (!open_elements_.empty()) {
                open_elements_.top()->appendChild(comment_node);
            } else {
                document_->appendChild(comment_node);
            }
            break;
        }
        
        case TokenType::EndOfFile:
            // 파싱 완료
            break;
    }
}

bool HtmlParser::hasInScope(const std::string& tag_name) const {
    // 열린 요소 스택에서 태그 이름 검색
    auto temp = open_elements_;
    while (!temp.empty()) {
        if (temp.top()->tagName() == tag_name) {
            return true;
        }
        // 범위 경계 요소 확인
        const auto& tn = temp.top()->tagName();
        if (tn == "html" || tn == "table" || tn == "td" || tn == "th" ||
            tn == "caption" || tn == "marquee" || tn == "object" ||
            tn == "template") {
            break;
        }
        temp.pop();
    }
    return false;
}

void HtmlParser::adoptionAgency(const std::string& tag_name) {
    // 간단한 포맷팅 요소 처리
    // 전체 알고리즘은 복잡하므로 기본적인 팝 방식 사용
    if (!open_elements_.empty() && open_elements_.top()->tagName() == tag_name) {
        open_elements_.pop();
    }
}

void HtmlParser::ensureImplicitElements() {
    // <html> 요소가 없으면 생성
    if (!document_->documentElement()) {
        auto html = std::make_shared<ElementNode>("html");
        document_->appendChild(html);
        open_elements_.push(html);
    }
    
    // <head>가 없고 아직 삽입되지 않았으면 생성
    if (!head_inserted_) {
        auto head = std::make_shared<ElementNode>("head");
        if (!open_elements_.empty()) {
            open_elements_.top()->appendChild(head);
        }
        head_inserted_ = true;
    }
    
    // <body>가 없고 아직 삽입되지 않았으면 생성
    if (!body_inserted_) {
        auto body = std::make_shared<ElementNode>("body");
        // html 요소 찾기
        auto html = document_->documentElement();
        if (html) {
            html->appendChild(body);
            open_elements_.push(body);
        }
        body_inserted_ = true;
    }
}

bool HtmlParser::isVoidElement(const std::string& tag) {
    // HTML5 void 요소 목록
    static const std::unordered_set<std::string> void_elements = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    return void_elements.contains(tag);
}

bool HtmlParser::isFormattingElement(const std::string& tag) {
    // 포맷팅 요소 목록
    static const std::unordered_set<std::string> formatting = {
        "a", "b", "big", "code", "em", "font", "i", "nobr",
        "s", "small", "strike", "strong", "tt", "u"
    };
    return formatting.contains(tag);
}

} // namespace ordinal::rendering
