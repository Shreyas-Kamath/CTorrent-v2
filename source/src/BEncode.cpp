#include "Bencode.hpp"
#include <cctype>

BEncodeParser::BEncodeParser(const std::string& input) : _data(input), pos(0) {}

BEncodeValue BEncodeParser::parse() {
    auto v = parse_value();
    return v;
}

BEncodeValue BEncodeParser::parse_value() {
    if (pos >= _data.size()) throw std::runtime_error("Unexpected end of input");
    char c = _data[pos];
    if (c == 'i') {
        return BEncodeValue{ parse_int() };
    } else if (c == 'l') {
        ++pos;
        return BEncodeValue{ parse_list() };
    } else if (c == 'd') {
        ++pos;
        return BEncodeValue{ parse_dict() };
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
        return BEncodeValue{ parse_string() };
    } else {
        throw std::runtime_error(std::string("Invalid BEncode token: ") + c);
    }
}

// parse integer like: i-123e or i0e
int64_t BEncodeParser::parse_int() {
    if (_data[pos] != 'i') throw std::runtime_error("Expected 'i' at start of integer");
    ++pos; // skip 'i'
    bool neg = false;
    if (pos < _data.size() && _data[pos] == '-') {
        neg = true;
        ++pos;
        if (pos >= _data.size() || !std::isdigit(static_cast<unsigned char>(_data[pos])))
            throw std::runtime_error("Invalid negative integer");
    }
    // parse digits
    int64_t value = 0;
    if (pos >= _data.size()) throw std::runtime_error("Unexpected end in integer");
    if (_data[pos] == '0') {
        // zero must be alone
        ++pos;
        if (pos < _data.size() && std::isdigit(static_cast<unsigned char>(_data[pos])))
            throw std::runtime_error("Leading zeros not allowed");
    } else {
        while (pos < _data.size() && std::isdigit(static_cast<unsigned char>(_data[pos]))) {
            value = value * 10 + (_data[pos] - '0');
            ++pos;
        }
    }
    if (_data[pos] != 'e') throw std::runtime_error("Missing 'e' for integer");
    ++pos; // skip 'e'
    return neg ? -value : value;
}

// parse string: <len>:<data>
std::string_view BEncodeParser::parse_string() {
    // parse length
    size_t start = pos;
    size_t colon = _data.find(':', pos);
    if (colon == std::string::npos) throw std::runtime_error("Missing ':' in string");
    // parse number without allocating
    size_t len = 0;
    for (size_t i = pos; i < colon; ++i) {
        char ch = _data[i];
        if (!std::isdigit(static_cast<unsigned char>(ch))) throw std::runtime_error("Invalid string length");
        len = len * 10 + (ch - '0');
    }
    pos = colon + 1;
    if (pos + len > _data.size()) throw std::runtime_error("String length exceeds input");
    // create string_view pointing into original data
    std::string_view sv(_data.data() + pos, len);
    pos += len;
    return sv;
}

BEncodeValue::List BEncodeParser::parse_list() {
    BEncodeValue::List list;
    while (pos < _data.size() && _data[pos] != 'e') {
        list.push_back(parse_value());
    }
    if (pos >= _data.size() || _data[pos] != 'e') throw std::runtime_error("Missing 'e' at end of list");
    ++pos; // skip 'e'
    return list;
}

BEncodeValue::Dict BEncodeParser::parse_dict() {
    BEncodeValue::Dict dict;
    while (pos < _data.size() && _data[pos] != 'e') {
        std::string_view key_sv = parse_string();
        size_t val_start = pos;
        BEncodeValue value = parse_value();
        size_t val_end = pos;
        std::string key(key_sv); // map key must be owned string
        if (key == "info") {
            _info_start = val_start;
            _info_end = val_end;
        }
        dict.emplace(std::move(key), std::move(value));
    }
    if (pos >= _data.size() || _data[pos] != 'e') throw std::runtime_error("Missing 'e' at end of dict");
    ++pos;
    return dict;
}
