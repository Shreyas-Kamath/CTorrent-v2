#pragma once

#include <map>
#include <string>
#include <vector>
#include <variant>
#include <stdexcept>
#include <cstdint>
#include <string_view>

struct BEncodeValue {
    using List = std::vector<BEncodeValue>;
    using Dict = std::map<std::string, BEncodeValue>;

    // Use string_view for raw strings to avoid copies
    std::variant<int64_t, std::string_view, List, Dict> value;

    bool is_int() const { return std::holds_alternative<int64_t>(value); }
    bool is_string() const { return std::holds_alternative<std::string_view>(value); }
    bool is_list() const { return std::holds_alternative<List>(value); }
    bool is_dict() const { return std::holds_alternative<Dict>(value); }

    int64_t as_int() const { return std::get<int64_t>(value); }
    const std::string_view& as_string() const { return std::get<std::string_view>(value); }
    const List& as_list() const { return std::get<List>(value); }
    const Dict& as_dict() const { return std::get<Dict>(value); }
};

class BEncodeParser {
public:
    // parser takes a const reference to an *owned* string_buffer that must outlive results
    explicit BEncodeParser(const std::string& input);

    BEncodeValue parse();

    // info start/end are offsets into the original input (byte indexes)
    std::pair<size_t, size_t> get_info_start_end() const { return { _info_start, _info_end }; }

private:
    BEncodeValue parse_value();
    int64_t parse_int();
    std::string_view parse_string();
    BEncodeValue::List parse_list();
    BEncodeValue::Dict parse_dict();

    const std::string& _data;
    size_t pos{};

    size_t _info_start{}, _info_end{}; // positions of the "info" dictionary in the original bencoded string
};
