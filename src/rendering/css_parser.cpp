/**
 * @file css_parser.cpp
 * @brief CSS 파서 구현
 * 
 * CSS 토크나이저, 셀렉터 파싱, 속성 맵 생성,
 * 특이성 계산, @media 쿼리를 구현합니다.
 */

#include "css_parser.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ordinal::rendering {

// ============================================================
// 생성자
// ============================================================

CssParser::CssParser() = default;

// ============================================================
// 공개 메서드
// ============================================================

std::vector<CssRule> CssParser::parse(const std::string& css) {
    errors_.clear();
    auto tokens = tokenize(css);
    
    std::vector<CssRule> rules;
    size_t pos = 0;
    
    while (pos < tokens.size() && tokens[pos].type != CssTokenType::EndOfFile) {
        // 공백 건너뛰기
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) {
            ++pos;
        }
        
        if (pos >= tokens.size() || tokens[pos].type == CssTokenType::EndOfFile) break;
        
        // @media 쿼리 처리
        if (tokens[pos].type == CssTokenType::At) {
            ++pos;
            if (pos < tokens.size() && tokens[pos].value == "media") {
                ++pos;
                auto mq = parseMediaQuery(tokens, pos);
                // 미디어 쿼리 안의 규칙을 추가
                for (auto& rule : mq.rules) {
                    rules.push_back(std::move(rule));
                }
                continue;
            }
            // 기타 @규칙 — 건너뛰기
            while (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseBrace &&
                   tokens[pos].type != CssTokenType::Semicolon) {
                if (tokens[pos].type == CssTokenType::OpenBrace) {
                    // 중첩 블록 건너뛰기
                    int depth = 1;
                    ++pos;
                    while (pos < tokens.size() && depth > 0) {
                        if (tokens[pos].type == CssTokenType::OpenBrace) ++depth;
                        if (tokens[pos].type == CssTokenType::CloseBrace) --depth;
                        ++pos;
                    }
                    break;
                }
                ++pos;
            }
            if (pos < tokens.size()) ++pos; // ';' 또는 '}' 건너뛰기
            continue;
        }
        
        // 일반 규칙 파싱
        auto rule = parseRule(tokens, pos);
        if (!rule.selector.raw.empty()) {
            rules.push_back(std::move(rule));
        }
    }
    
    return rules;
}

std::vector<CssToken> CssParser::tokenize(const std::string& css) {
    input_ = css;
    pos_ = 0;
    tokens_.clear();
    
    runTokenizer(css);
    
    return tokens_;
}

CssSelector CssParser::parseSelector(const std::string& selector_str) {
    auto tokens = tokenize(selector_str);
    size_t pos = 0;
    auto selector = parseSelectorTokens(tokens, pos);
    selector.specificity = calculateSpecificity(selector);
    return selector;
}

Specificity CssParser::calculateSpecificity(const CssSelector& selector) {
    Specificity spec;
    
    for (const auto& part : selector.parts) {
        switch (part.type) {
            case SelectorType::Id:
                spec.id_count++;
                break;
            case SelectorType::Class:
            case SelectorType::Attribute:
            case SelectorType::PseudoClass:
                spec.class_count++;
                break;
            case SelectorType::Type:
                spec.type_count++;
                break;
            case SelectorType::PseudoElement:
                spec.type_count++;
                break;
            case SelectorType::Universal:
            case SelectorType::Combinator:
                // 특이성에 영향 없음
                break;
        }
    }
    
    return spec;
}

std::vector<MediaQuery> CssParser::parseMediaQueries(const std::string& css) {
    auto tokens = tokenize(css);
    std::vector<MediaQuery> queries;
    size_t pos = 0;
    
    while (pos < tokens.size()) {
        // 공백 건너뛰기
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
        
        if (pos >= tokens.size()) break;
        
        if (tokens[pos].type == CssTokenType::At) {
            ++pos;
            if (pos < tokens.size() && tokens[pos].value == "media") {
                ++pos;
                queries.push_back(parseMediaQuery(tokens, pos));
                continue;
            }
        }
        ++pos;
    }
    
    return queries;
}

PropertyMap CssParser::parseInlineStyle(const std::string& style_str) {
    PropertyMap props;
    
    // 세미콜론으로 분리
    std::istringstream iss(style_str);
    std::string declaration;
    
    while (std::getline(iss, declaration, ';')) {
        // 콜론으로 속성명과 값 분리
        auto colon_pos = declaration.find(':');
        if (colon_pos == std::string::npos) continue;
        
        std::string prop_name = declaration.substr(0, colon_pos);
        std::string prop_value = declaration.substr(colon_pos + 1);
        
        // 공백 트리밍
        auto trim = [](std::string& s) {
            size_t start = s.find_first_not_of(" \t\n\r");
            size_t end = s.find_last_not_of(" \t\n\r");
            if (start == std::string::npos) { s.clear(); return; }
            s = s.substr(start, end - start + 1);
        };
        
        trim(prop_name);
        trim(prop_value);
        
        if (prop_name.empty()) continue;
        
        // 소문자로 변환
        std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        // !important 확인
        bool important = false;
        auto imp_pos = prop_value.find("!important");
        if (imp_pos != std::string::npos) {
            important = true;
            prop_value = prop_value.substr(0, imp_pos);
            trim(prop_value);
        }
        
        props[prop_name] = CssValue(prop_value, important);
    }
    
    return props;
}

// ============================================================
// 토크나이저 구현
// ============================================================

void CssParser::runTokenizer(const std::string& /*css*/) {
    while (!atEnd()) {
        char c = peek();
        
        // 공백
        if (std::isspace(static_cast<unsigned char>(c))) {
            skipWhitespace();
            tokens_.push_back({CssTokenType::Whitespace, " "});
            continue;
        }
        
        // 주석 건너뛰기
        if (c == '/' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '*') {
            pos_ += 2;
            while (pos_ + 1 < input_.size()) {
                if (input_[pos_] == '*' && input_[pos_ + 1] == '/') {
                    pos_ += 2;
                    break;
                }
                ++pos_;
            }
            continue;
        }
        
        // 단일 문자 토큰
        switch (c) {
            case '{':
                tokens_.push_back({CssTokenType::OpenBrace, "{"});
                consume(); continue;
            case '}':
                tokens_.push_back({CssTokenType::CloseBrace, "}"});
                consume(); continue;
            case '[':
                tokens_.push_back({CssTokenType::OpenBracket, "["});
                consume(); continue;
            case ']':
                tokens_.push_back({CssTokenType::CloseBracket, "]"});
                consume(); continue;
            case '(':
                tokens_.push_back({CssTokenType::OpenParen, "("});
                consume(); continue;
            case ')':
                tokens_.push_back({CssTokenType::CloseParen, ")"});
                consume(); continue;
            case ',':
                tokens_.push_back({CssTokenType::Comma, ","});
                consume(); continue;
            case ';':
                tokens_.push_back({CssTokenType::Semicolon, ";"});
                consume(); continue;
            case '*':
                tokens_.push_back({CssTokenType::Star, "*"});
                consume(); continue;
            case '>':
                tokens_.push_back({CssTokenType::Greater, ">"});
                consume(); continue;
            case '+':
                tokens_.push_back({CssTokenType::Plus, "+"});
                consume(); continue;
            case '~':
                tokens_.push_back({CssTokenType::Tilde, "~"});
                consume(); continue;
            case '@':
                tokens_.push_back({CssTokenType::At, "@"});
                consume(); continue;
            default:
                break;
        }
        
        // 해시 (#)
        if (c == '#') {
            consume();
            std::string value = readIdent();
            tokens_.push_back({CssTokenType::Hash, value});
            continue;
        }
        
        // 점 (.)
        if (c == '.') {
            tokens_.push_back({CssTokenType::Dot, "."});
            consume();
            continue;
        }
        
        // 콜론 (:) 또는 이중 콜론 (::)
        if (c == ':') {
            consume();
            if (!atEnd() && peek() == ':') {
                consume();
                tokens_.push_back({CssTokenType::DoubleColon, "::"});
            } else {
                tokens_.push_back({CssTokenType::Colon, ":"});
            }
            continue;
        }
        
        // 문자열 리터럴
        if (c == '"' || c == '\'') {
            consume();
            std::string value = readString(c);
            tokens_.push_back({CssTokenType::String, value});
            continue;
        }
        
        // 숫자
        if (std::isdigit(static_cast<unsigned char>(c)) || 
            (c == '-' && pos_ + 1 < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])))) {
            std::string value = readNumber();
            tokens_.push_back({CssTokenType::Number, value});
            continue;
        }
        
        // 식별자 (속성명, 태그명, 키워드)
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            std::string value = readIdent();
            tokens_.push_back({CssTokenType::Ident, value});
            continue;
        }
        
        // 기타 구분자
        tokens_.push_back({CssTokenType::Delim, std::string(1, c)});
        consume();
    }
    
    tokens_.push_back({CssTokenType::EndOfFile, ""});
}

char CssParser::consume() {
    if (pos_ < input_.size()) {
        return input_[pos_++];
    }
    return '\0';
}

char CssParser::peek() const {
    if (pos_ < input_.size()) {
        return input_[pos_];
    }
    return '\0';
}

bool CssParser::atEnd() const {
    return pos_ >= input_.size();
}

void CssParser::skipWhitespace() {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(peek()))) {
        consume();
    }
}

std::string CssParser::readIdent() {
    std::string result;
    while (!atEnd()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            result += consume();
        } else {
            break;
        }
    }
    return result;
}

std::string CssParser::readString(char quote) {
    std::string result;
    while (!atEnd()) {
        char c = consume();
        if (c == quote) break;
        if (c == '\\' && !atEnd()) {
            result += consume(); // 이스케이프된 문자
        } else {
            result += c;
        }
    }
    return result;
}

std::string CssParser::readNumber() {
    std::string result;
    if (peek() == '-') result += consume();
    
    while (!atEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')) {
        result += consume();
    }
    
    // 단위 (px, em, %, rem, vh, vw 등)
    if (!atEnd() && (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '%')) {
        while (!atEnd() && (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '%')) {
            result += consume();
        }
    }
    
    return result;
}

// ============================================================
// 파서 내부 구현
// ============================================================

CssRule CssParser::parseRule(const std::vector<CssToken>& tokens, size_t& pos) {
    CssRule rule;
    
    // 셀렉터 파싱 — '{' 전까지
    rule.selector = parseSelectorTokens(tokens, pos);
    rule.selector.specificity = calculateSpecificity(rule.selector);
    
    // 선언 블록 파싱
    rule.properties = parseDeclarationBlock(tokens, pos);
    
    return rule;
}

PropertyMap CssParser::parseDeclarationBlock(const std::vector<CssToken>& tokens, size_t& pos) {
    PropertyMap props;
    
    // '{' 확인 및 건너뛰기
    while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    if (pos >= tokens.size() || tokens[pos].type != CssTokenType::OpenBrace) {
        errors_.push_back("선언 블록에서 '{' 기대됨");
        return props;
    }
    ++pos; // '{' 건너뛰기
    
    // '}' 전까지 선언 파싱
    while (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseBrace) {
        // 공백 건너뛰기
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
        
        if (pos >= tokens.size() || tokens[pos].type == CssTokenType::CloseBrace) break;
        
        // 속성 이름 읽기
        std::string prop_name;
        while (pos < tokens.size() && tokens[pos].type != CssTokenType::Colon &&
               tokens[pos].type != CssTokenType::CloseBrace) {
            if (tokens[pos].type != CssTokenType::Whitespace) {
                prop_name += tokens[pos].value;
            }
            ++pos;
        }
        
        if (pos >= tokens.size() || tokens[pos].type != CssTokenType::Colon) {
            // 콜론이 없으면 선언 건너뛰기
            while (pos < tokens.size() && tokens[pos].type != CssTokenType::Semicolon &&
                   tokens[pos].type != CssTokenType::CloseBrace) {
                ++pos;
            }
            if (pos < tokens.size() && tokens[pos].type == CssTokenType::Semicolon) ++pos;
            continue;
        }
        ++pos; // ':' 건너뛰기
        
        // 속성 값 읽기 — ';' 또는 '}' 전까지
        std::string prop_value;
        bool important = false;
        
        while (pos < tokens.size() && tokens[pos].type != CssTokenType::Semicolon &&
               tokens[pos].type != CssTokenType::CloseBrace) {
            prop_value += tokens[pos].value;
            ++pos;
        }
        
        // 공백 트리밍
        auto trim = [](std::string& s) {
            size_t start = s.find_first_not_of(" \t\n\r");
            size_t end = s.find_last_not_of(" \t\n\r");
            if (start == std::string::npos) { s.clear(); return; }
            s = s.substr(start, end - start + 1);
        };
        
        trim(prop_name);
        trim(prop_value);
        
        // !important 체크
        auto imp_pos = prop_value.find("!important");
        if (imp_pos != std::string::npos) {
            important = true;
            prop_value = prop_value.substr(0, imp_pos);
            trim(prop_value);
        }
        
        // 소문자로 정규화
        std::transform(prop_name.begin(), prop_name.end(), prop_name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        if (!prop_name.empty() && !prop_value.empty()) {
            props[prop_name] = CssValue(prop_value, important);
        }
        
        // ';' 건너뛰기
        if (pos < tokens.size() && tokens[pos].type == CssTokenType::Semicolon) ++pos;
    }
    
    // '}' 건너뛰기
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::CloseBrace) ++pos;
    
    return props;
}

CssSelector CssParser::parseSelectorTokens(const std::vector<CssToken>& tokens, size_t& pos) {
    CssSelector selector;
    std::string raw;
    
    // '{' 전까지 셀렉터 토큰 수집
    while (pos < tokens.size() && tokens[pos].type != CssTokenType::OpenBrace &&
           tokens[pos].type != CssTokenType::EndOfFile) {
        const auto& token = tokens[pos];
        
        switch (token.type) {
            case CssTokenType::Ident: {
                // 타입 셀렉터
                SelectorPart part;
                part.type = SelectorType::Type;
                part.value = token.value;
                selector.parts.push_back(part);
                raw += token.value;
                break;
            }
            
            case CssTokenType::Hash: {
                // ID 셀렉터
                SelectorPart part;
                part.type = SelectorType::Id;
                part.value = token.value;
                selector.parts.push_back(part);
                raw += "#" + token.value;
                break;
            }
            
            case CssTokenType::Dot: {
                // 클래스 셀렉터
                ++pos;
                if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident) {
                    SelectorPart part;
                    part.type = SelectorType::Class;
                    part.value = tokens[pos].value;
                    selector.parts.push_back(part);
                    raw += "." + tokens[pos].value;
                }
                break;
            }
            
            case CssTokenType::Star: {
                // 전체 셀렉터
                SelectorPart part;
                part.type = SelectorType::Universal;
                part.value = "*";
                selector.parts.push_back(part);
                raw += "*";
                break;
            }
            
            case CssTokenType::OpenBracket: {
                // 속성 셀렉터
                auto attr_part = parseAttributeSelector(tokens, pos);
                selector.parts.push_back(attr_part);
                raw += "[" + attr_part.attr_name;
                if (attr_part.attr_op != AttributeOp::Exists) {
                    raw += "=" + attr_part.attr_value;
                }
                raw += "]";
                break;
            }
            
            case CssTokenType::Colon: {
                // 의사 클래스
                ++pos;
                if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident) {
                    SelectorPart part;
                    part.type = SelectorType::PseudoClass;
                    part.value = tokens[pos].value;
                    selector.parts.push_back(part);
                    raw += ":" + tokens[pos].value;
                    
                    // 함수형 의사 클래스 (예: :nth-child(2n+1))
                    if (pos + 1 < tokens.size() && tokens[pos + 1].type == CssTokenType::OpenParen) {
                        ++pos; // '(' 건너뛰기
                        raw += "(";
                        ++pos;
                        int depth = 1;
                        while (pos < tokens.size() && depth > 0) {
                            if (tokens[pos].type == CssTokenType::OpenParen) ++depth;
                            if (tokens[pos].type == CssTokenType::CloseParen) --depth;
                            if (depth > 0) {
                                part.value += tokens[pos].value;
                                raw += tokens[pos].value;
                            }
                            ++pos;
                        }
                        raw += ")";
                        --pos; // 다음 ++pos를 위해 보정
                    }
                }
                break;
            }
            
            case CssTokenType::DoubleColon: {
                // 의사 요소
                ++pos;
                if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident) {
                    SelectorPart part;
                    part.type = SelectorType::PseudoElement;
                    part.value = tokens[pos].value;
                    selector.parts.push_back(part);
                    raw += "::" + tokens[pos].value;
                }
                break;
            }
            
            case CssTokenType::Greater:
            case CssTokenType::Plus:
            case CssTokenType::Tilde: {
                // 조합자
                SelectorPart part;
                part.type = SelectorType::Combinator;
                part.combinator = token.value[0];
                part.value = token.value;
                selector.parts.push_back(part);
                raw += " " + token.value + " ";
                break;
            }
            
            case CssTokenType::Whitespace: {
                // 하위 조합자 (공백)
                // 다음 토큰이 조합자가 아닌 셀렉터이면 하위 조합자 추가
                if (!selector.parts.empty() && pos + 1 < tokens.size()) {
                    auto next_type = tokens[pos + 1].type;
                    if (next_type != CssTokenType::OpenBrace &&
                        next_type != CssTokenType::Greater &&
                        next_type != CssTokenType::Plus &&
                        next_type != CssTokenType::Tilde &&
                        next_type != CssTokenType::Whitespace &&
                        next_type != CssTokenType::Comma &&
                        next_type != CssTokenType::EndOfFile) {
                        SelectorPart part;
                        part.type = SelectorType::Combinator;
                        part.combinator = ' ';
                        part.value = " ";
                        selector.parts.push_back(part);
                        raw += " ";
                    }
                }
                break;
            }
            
            case CssTokenType::Comma: {
                // 셀렉터 그룹 — 현재는 첫 번째만 사용
                raw += ", ";
                // 나머지 건너뛰기 (간단한 구현)
                ++pos;
                while (pos < tokens.size() && tokens[pos].type != CssTokenType::OpenBrace) {
                    raw += tokens[pos].value;
                    ++pos;
                }
                --pos;
                break;
            }
            
            default:
                raw += token.value;
                break;
        }
        
        ++pos;
    }
    
    selector.raw = raw;
    return selector;
}

SelectorPart CssParser::parseAttributeSelector(const std::vector<CssToken>& tokens, size_t& pos) {
    SelectorPart part;
    part.type = SelectorType::Attribute;
    
    ++pos; // '[' 건너뛰기
    
    // 공백 건너뛰기
    while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    
    // 속성 이름
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident) {
        part.attr_name = tokens[pos].value;
        ++pos;
    }
    
    // 공백 건너뛰기
    while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    
    // 연산자 확인
    if (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseBracket) {
        std::string op;
        
        // 접두 연산자 확인 (*=, ^=, $=, ~=, |=)
        if (tokens[pos].type == CssTokenType::Delim || tokens[pos].type == CssTokenType::Star ||
            tokens[pos].type == CssTokenType::Tilde) {
            op = tokens[pos].value;
            ++pos;
        }
        
        // '=' 확인
        if (pos < tokens.size() && tokens[pos].value == "=") {
            op += "=";
            ++pos;
        }
        
        // 연산자 결정
        if (op == "=") part.attr_op = AttributeOp::Equals;
        else if (op == "*=") part.attr_op = AttributeOp::Contains;
        else if (op == "^=") part.attr_op = AttributeOp::StartsWith;
        else if (op == "$=") part.attr_op = AttributeOp::EndsWith;
        else if (op == "|=") part.attr_op = AttributeOp::DashMatch;
        else if (op == "~=") part.attr_op = AttributeOp::WordMatch;
        
        // 공백 건너뛰기
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
        
        // 속성 값
        if (pos < tokens.size()) {
            if (tokens[pos].type == CssTokenType::String || tokens[pos].type == CssTokenType::Ident) {
                part.attr_value = tokens[pos].value;
                ++pos;
            }
        }
    }
    
    // ']' 찾기
    while (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseBracket) ++pos;
    // ']'는 상위에서 ++pos로 건너뛰어짐
    
    return part;
}

// ============================================================
// 미디어 쿼리 파싱
// ============================================================

MediaQuery CssParser::parseMediaQuery(const std::vector<CssToken>& tokens, size_t& pos) {
    MediaQuery mq;
    mq.media_type = "all"; // 기본값
    
    // 공백 건너뛰기
    while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    
    // not 키워드 확인
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident && tokens[pos].value == "not") {
        mq.negate = true;
        ++pos;
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    }
    
    // 미디어 타입 읽기
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident) {
        std::string potential_type = tokens[pos].value;
        std::transform(potential_type.begin(), potential_type.end(), potential_type.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        if (potential_type == "all" || potential_type == "screen" || potential_type == "print" ||
            potential_type == "speech") {
            mq.media_type = potential_type;
            ++pos;
        }
    }
    
    // 'and' + 조건들 읽기
    while (pos < tokens.size() && tokens[pos].type != CssTokenType::OpenBrace) {
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
        
        if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident && tokens[pos].value == "and") {
            ++pos;
            while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
        }
        
        if (pos < tokens.size() && tokens[pos].type == CssTokenType::OpenParen) {
            mq.conditions.push_back(parseMediaCondition(tokens, pos));
        } else {
            break;
        }
    }
    
    // '{' 이후 규칙 블록 파싱
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::OpenBrace) {
        ++pos; // '{' 건너뛰기
        
        while (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseBrace) {
            while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
            
            if (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseBrace) {
                auto rule = parseRule(tokens, pos);
                if (!rule.selector.raw.empty()) {
                    mq.rules.push_back(std::move(rule));
                }
            }
        }
        
        if (pos < tokens.size()) ++pos; // '}' 건너뛰기
    }
    
    return mq;
}

MediaCondition CssParser::parseMediaCondition(const std::vector<CssToken>& tokens, size_t& pos) {
    MediaCondition cond;
    
    ++pos; // '(' 건너뛰기
    while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    
    // 기능 이름
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::Ident) {
        std::string feature = tokens[pos].value;
        std::transform(feature.begin(), feature.end(), feature.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        // min-/max- 접두사 처리
        if (feature.substr(0, 4) == "min-") {
            cond.is_min = true;
            feature = feature.substr(4);
        } else if (feature.substr(0, 4) == "max-") {
            cond.is_max = true;
            feature = feature.substr(4);
        }
        
        cond.feature = feature;
        ++pos;
    }
    
    // 공백 건너뛰기
    while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
    
    // ':' 다음 값
    if (pos < tokens.size() && tokens[pos].type == CssTokenType::Colon) {
        ++pos;
        while (pos < tokens.size() && tokens[pos].type == CssTokenType::Whitespace) ++pos;
        
        // 값 읽기
        std::string value;
        while (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseParen) {
            if (tokens[pos].type != CssTokenType::Whitespace || !value.empty()) {
                value += tokens[pos].value;
            }
            ++pos;
        }
        
        // 후행 공백 제거
        size_t end = value.find_last_not_of(" \t");
        if (end != std::string::npos) {
            value = value.substr(0, end + 1);
        }
        
        cond.value = value;
    }
    
    // ')' 건너뛰기
    while (pos < tokens.size() && tokens[pos].type != CssTokenType::CloseParen) ++pos;
    if (pos < tokens.size()) ++pos;
    
    return cond;
}

} // namespace ordinal::rendering
