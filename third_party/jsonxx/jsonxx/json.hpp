#ifndef JSONXX_HPP
#define JSONXX_HPP

// jsonxx - a small, dependency-free JSON library for C++17.
// Parse and serialize JSON with a simple DOM API.

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <stdexcept>
#include <cmath>
#include <sstream>

namespace jsonxx {

class Value;                  // forward
namespace detail { void write(const Value&, std::string&, bool, int); }  // forward

using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null), num_(0), boolean_(false) {}
    Value(std::nullptr_t) : Value() {}

    Value(bool b)    : type_(Type::Bool),   num_(0), boolean_(b) {}
    Value(int v)     : type_(Type::Number), num_(v), boolean_(false) {}
    Value(double v)  : type_(Type::Number), num_(v), boolean_(false) {}
    Value(const char* s) : type_(Type::String), num_(0), boolean_(false), str_(s) {}
    Value(const std::string& s)  : type_(Type::String), num_(0), boolean_(false), str_(s) {}
    Value(Object o)  : type_(Type::Object), num_(0), boolean_(false), obj_(std::move(o)) {}
    Value(Array a)   : type_(Type::Array),  num_(0), boolean_(false), arr_(std::move(a)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }

    bool asBool() const {
        if (type_ == Type::Bool) return boolean_;
        if (type_ == Type::Number) return num_ != 0;
        throw std::runtime_error("jsonxx: not a bool");
    }
    double asNumber() const {
        if (type_ == Type::Number) return num_;
        throw std::runtime_error("jsonxx: not a number");
    }
    int asInt() const { return static_cast<int>(std::llround(asNumber())); }
    const std::string& asString() const {
        if (type_ == Type::String) return str_;
        throw std::runtime_error("jsonxx: not a string");
    }
    const Array& asArray() const {
        if (type_ == Type::Array) return arr_;
        throw std::runtime_error("jsonxx: not an array");
    }
    const Object& asObject() const {
        if (type_ == Type::Object) return obj_;
        throw std::runtime_error("jsonxx: not an object");
    }

    bool has(const std::string& key) const {
        return type_ == Type::Object && obj_.count(key) > 0;
    }
    const Value& at(const std::string& key) const {
        auto it = obj_.find(key);
        if (it == obj_.end()) throw std::runtime_error("jsonxx: missing key '" + key + "'");
        return it->second;
    }
    const Value& at(size_t index) const {
        return arr_.at(index);
    }
    // convenience: object["key"]
    const Value& operator[](const std::string& key) const { return at(key); }

private:
    friend Value parse(const std::string&);
    friend std::string stringify(const Value&, bool);
    friend void detail::write(const Value&, std::string&, bool, int);

    Type type_;
    double num_;
    bool   boolean_;
    std::string str_;
    Array  arr_;
    Object obj_;
};

namespace detail {
struct Parser {
    const std::string& s;
    size_t i = 0;
    Parser(const std::string& str) : s(str) {}

    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    bool eof() const { return i >= s.size(); }

    [[noreturn]] void fail(const std::string& msg) const {
        throw std::runtime_error("jsonxx parse error at " + std::to_string(i) + ": " + msg);
    }

    Value parseValue() {
        ws();
        if (eof()) fail("unexpected end");
        char c = s[i];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Value(parseString());
        if (c == 't') { expect("true");  return Value(true); }
        if (c == 'f') { expect("false"); return Value(false); }
        if (c == 'n') { expect("null");  return Value(nullptr); }
        return parseNumber();
    }

    void expect(const std::string& tok) {
        if (s.compare(i, tok.size(), tok) != 0) fail("bad token");
        i += tok.size();
    }

    std::string parseString() {
        if (s[i] != '"') fail("expected string");
        ++i;
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        out += parseUnicode();
                        break;
                    }
                    default: fail("unknown escape");
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
    }

    std::string parseUnicode() {
        if (i + 4 > s.size()) fail("bad \\u escape");
        unsigned cp = 0;
        for (int k = 0; k < 4; ++k) {
            char h = s[i++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= (h - '0');
            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
            else fail("bad hex");
        }
        // Encode code point as UTF-8.
        std::string out;
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        return out;
    }

    Value parseNumber() {
        size_t start = i;
        bool neg = false;
        if (!eof() && s[i] == '-') { neg = true; ++i; }
        bool isDouble = false;
        while (!eof()) {
            char c = s[i];
            if (c >= '0' && c <= '9') { ++i; }
            else if (c == '.' || c == 'e' || c == 'E') { isDouble = true; ++i; }
            else if (c == '+' || c == '-') { ++i; }
            else break;
        }
        std::string tok = s.substr(start, i - start);
        if (tok.empty()) fail("bad number");
        try {
            double d = std::stod(tok);
            return isDouble ? Value(d) : Value(d);
        } catch (...) { fail("bad number"); }
    }

    Value parseObject() {
        ++i; // '{'
        Object obj;
        ws();
        if (!eof() && s[i] == '}') { ++i; return Value(std::move(obj)); }
        while (true) {
            ws();
            std::string key = parseString();
            ws();
            if (eof() || s[i] != ':') fail("expected ':'");
            ++i;
            Value v = parseValue();
            obj.emplace(std::move(key), std::move(v));
            ws();
            if (eof()) fail("unterminated object");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; break; }
            fail("expected ',' or '}'");
        }
        return Value(std::move(obj));
    }

    Value parseArray() {
        ++i; // '['
        Array arr;
        ws();
        if (!eof() && s[i] == ']') { ++i; return Value(std::move(arr)); }
        while (true) {
            ws();
            arr.push_back(parseValue());
            ws();
            if (eof()) fail("unterminated array");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; break; }
            fail("expected ',' or ']'");
        }
        return Value(std::move(arr));
    }
};

inline std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

inline void write(const Value& v, std::string& out, bool pretty, int depth) {
    auto indent = [&](int d) {
        if (pretty) for (int k = 0; k < d; ++k) out += "  ";
    };
    switch (v.type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += v.boolean_ ? "true" : "false"; break;
        case Type::Number: {
            if (std::floor(v.num_) == v.num_ && std::abs(v.num_) < 1e15) {
                out += std::to_string(static_cast<long long>(v.num_));
            } else {
                std::ostringstream oss;
                oss.precision(17);
                oss << v.num_;
                out += oss.str();
            }
            break;
        }
        case Type::String: out += '"'; out += escape(v.str_); out += '"'; break;
        case Type::Array: {
            if (v.arr_.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t k = 0; k < v.arr_.size(); ++k) {
                if (k) out += ',';
                if (pretty) { out += '\n'; indent(depth + 1); }
                write(v.arr_[k], out, pretty, depth + 1);
            }
            if (pretty) { out += '\n'; indent(depth); }
            out += ']';
            break;
        }
        case Type::Object: {
            if (v.obj_.empty()) { out += "{}"; break; }
            out += '{';
            size_t k = 0;
            for (const auto& kv : v.obj_) {
                if (k++) out += ',';
                if (pretty) { out += '\n'; indent(depth + 1); }
                out += '"'; out += escape(kv.first); out += '"';
                out += pretty ? ": " : ":";
                write(kv.second, out, pretty, depth + 1);
            }
            if (pretty) { out += '\n'; indent(depth); }
            out += '}';
            break;
        }
    }
}
} // namespace detail

inline Value parse(const std::string& text) {
    detail::Parser p(text);
    Value v = p.parseValue();
    p.ws();
    if (!p.eof()) throw std::runtime_error("jsonxx: trailing data after value");
    return v;
}

inline std::string stringify(const Value& v, bool pretty = false) {
    std::string out;
    out.reserve(256);
    detail::write(v, out, pretty, 0);
    return out;
}

} // namespace jsonxx

#endif // JSONXX_HPP