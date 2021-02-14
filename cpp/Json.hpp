#pragma once

/// @brief JSON stream parser

#include <queue>

#include "Types.hpp"
#include "String.hpp"
#include "Result.hpp"
#include "Vector.hpp"
#include "UniquePtr.hpp"
#include "UnorderedMap.hpp"

namespace hsd {
    namespace json_detail {
        template <typename CharT>
        inline auto read_character(FILE* stream)
        {
            if constexpr(is_same<CharT, char>::value)
            {
                return fgetc(stream);
            }
            else if(is_same<CharT, wchar>::value)
            {
                return fgetwc(stream);
            }
        }
    } // namespace json_detail
    

    enum class JsonToken {
        Null, True, False,
        Number, String,
        BArray, EArray, BObject, EObject,
        Comma, Colon,

        Eof = -1,
        Error = -2,
        Empty = -3 // For internal purposes
    };

    class JsonError : public runtime_error {
        hsd::string str;
    public:
        usize position;

        JsonError(const char* msg, usize pos)
            : runtime_error(msg), position(pos) {}

        const char* operator()() {
            if (str.size() != 0)
                return str.c_str();

            return (str = _err + to_string(position)).c_str();
        }
    };

    template <typename CharT>
    class JsonStream {
        using vstr = basic_string_view<CharT>;
        using str = basic_string<CharT>;

        std::queue<JsonToken> tokens;
        std::queue<str> qtok_string;
        std::queue<i32> qtok_number;
        JsonToken current_token = JsonToken::Empty;
        const char* token_kw = nullptr;
        usize token_position = 0;
        str token_str;

        usize pos = 0;

    public:
        std::queue<JsonToken>& get_tokens() {return tokens; }
        str pop_string() {
            str s = move(qtok_string.front());
            qtok_string.pop();
            return s;
        }
        i32 pop_number() {
            i32 n = qtok_number.front();
            qtok_number.pop();
            return n;
        }

        // If an error occurs, the rest of buffer can be passed in
        Result<void, JsonError> lex(vstr frag) {
            static const char* const s_keywords[] = {"null", "true", "false"};
            for (CharT ch : frag) {
                ++pos;
                _reparse:
                if (current_token == JsonToken::Empty) {
                    if (basic_cstring<CharT>::iswhitespace(ch))
                        continue;
                    switch (ch) {
                        #define CASE_CH(ch, tok) \
                            case static_cast<CharT>(ch): \
                                tokens.push(tok); \
                                break;

                        CASE_CH('[', JsonToken::BArray)
                        CASE_CH(']', JsonToken::EArray)
                        CASE_CH('{', JsonToken::BObject)
                        CASE_CH('}', JsonToken::EObject)
                        CASE_CH(',', JsonToken::Comma)
                        CASE_CH(':', JsonToken::Colon)
                        #undef CASE_CH

                        case static_cast<CharT>('n'):
                            current_token = JsonToken::Null;
                            token_kw = s_keywords[0];
                            ++token_position;
                            break;
                        case static_cast<CharT>('t'):
                            current_token = JsonToken::True;
                            token_kw = s_keywords[1];
                            ++token_position;
                            break;
                        case static_cast<CharT>('f'):
                            current_token = JsonToken::False;
                            token_kw = s_keywords[2];
                            ++token_position;
                            break;
                        case static_cast<CharT>('"'):
                            current_token = JsonToken::String;
                            break;
                        default:
                            // Only ASCII numbers supported
                            if (ch == static_cast<CharT>('-') or ch == static_cast<CharT>('+') or
                                (ch >= static_cast<CharT>('0') and ch <= static_cast<CharT>('9')))
                            {
                                current_token = JsonToken::Number;
                                if (ch != static_cast<CharT>('+') and ch != static_cast<CharT>('-'))
                                    token_str.push_back('+'); // Explicit sign for parse_i
                                token_str.push_back(ch);
                                ++token_position;
                            } else {
                                tokens.push(JsonToken::Error);
                                return JsonError("Syntax error: unexpected character", pos);
                            }
                    }
                } else {
                    if (token_kw) {
                        if (ch != static_cast<CharT>(token_kw[token_position])) {
                            // Error and recover
                            tokens.push(JsonToken::Error);
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_kw = nullptr;
                            return JsonError("Syntax error: unexpected character", pos);
                        }
                        ++token_position;
                        if (token_kw[token_position] == 0) {
                            tokens.push(current_token);
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_kw = nullptr;
                        }
                    } else if (current_token == JsonToken::String) {
                        // handle escape sequences
                        if (ch == static_cast<CharT>('"')) {
                            tokens.push(current_token);
                            qtok_string.push(move(token_str));
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_str.clear();
                        } else {
                            ++token_position;
                            token_str.push_back(ch);
                        }
                    } else if (current_token == JsonToken::Number) {
                        if (ch >= static_cast<CharT>('0') and ch <= static_cast<CharT>('9')) {
                            token_position++;
                            token_str.push_back(ch);
                        } else {
                            tokens.push(current_token);
                            qtok_number.push(basic_cstring<CharT>::parse_i(token_str.c_str()));
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_str.clear();

                            goto _reparse;
                        }
                    }
                }
            }
            return {};
        }

        // If an error occurs, the rest of buffer can be passed in
        Result<void, JsonError> lex_file(string_view filename) {
            static const char* const s_keywords[] = {"null", "true", "false"};
            auto* stream = fopen(filename.data(), "r");
            auto read_ch = json_detail::read_character<CharT>;

            for (auto _value = read_ch(stream); _value != EOF; _value = read_ch(stream)) {
                CharT ch = static_cast<CharT>(_value);
                ++pos;
                _reparse:
                if (current_token == JsonToken::Empty) {
                    if (basic_cstring<CharT>::iswhitespace(ch))
                        continue;
                    switch (ch) {
                        #define CASE_CH(ch, tok) \
                            case static_cast<CharT>(ch): \
                                tokens.push(tok); \
                                break;

                        CASE_CH('[', JsonToken::BArray)
                        CASE_CH(']', JsonToken::EArray)
                        CASE_CH('{', JsonToken::BObject)
                        CASE_CH('}', JsonToken::EObject)
                        CASE_CH(',', JsonToken::Comma)
                        CASE_CH(':', JsonToken::Colon)
                        #undef CASE_CH

                        case static_cast<CharT>('n'):
                            current_token = JsonToken::Null;
                            token_kw = s_keywords[0];
                            ++token_position;
                            break;
                        case static_cast<CharT>('t'):
                            current_token = JsonToken::True;
                            token_kw = s_keywords[1];
                            ++token_position;
                            break;
                        case static_cast<CharT>('f'):
                            current_token = JsonToken::False;
                            token_kw = s_keywords[2];
                            ++token_position;
                            break;
                        case static_cast<CharT>('"'):
                            current_token = JsonToken::String;
                            break;
                        default:
                            // Only ASCII numbers supported
                            if (ch == static_cast<CharT>('-') or ch == static_cast<CharT>('+') or
                                (ch >= static_cast<CharT>('0') and ch <= static_cast<CharT>('9')))
                            {
                                current_token = JsonToken::Number;
                                if (ch != static_cast<CharT>('+') and ch != static_cast<CharT>('-'))
                                    token_str.push_back('+'); // Explicit sign for parse_i
                                token_str.push_back(ch);
                                ++token_position;
                            } else {
                                tokens.push(JsonToken::Error);
                                return JsonError("Syntax error: unexpected character", pos);
                            }
                    }
                } else {
                    if (token_kw) {
                        if (ch != static_cast<CharT>(token_kw[token_position])) {
                            // Error and recover
                            tokens.push(JsonToken::Error);
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_kw = nullptr;
                            return JsonError("Syntax error: unexpected character", pos);
                        }
                        ++token_position;
                        if (token_kw[token_position] == 0) {
                            tokens.push(current_token);
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_kw = nullptr;
                        }
                    } else if (current_token == JsonToken::String) {
                        // handle escape sequences
                        if (ch == static_cast<CharT>('"')) {
                            tokens.push(current_token);
                            qtok_string.push(move(token_str));
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_str.clear();
                        } else {
                            ++token_position;
                            token_str.push_back(ch);
                        }
                    } else if (current_token == JsonToken::Number) {
                        if (ch >= static_cast<CharT>('0') and ch <= static_cast<CharT>('9')) {
                            token_position++;
                            token_str.push_back(ch);
                        } else {
                            tokens.push(current_token);
                            qtok_number.push(basic_cstring<CharT>::parse_i(token_str.c_str()));
                            current_token = JsonToken::Empty;
                            token_position = 0;
                            token_str.clear();

                            goto _reparse;
                        }
                    }
                }
            }
            if(ferror(stream))
            {
                fclose(stream);
                return JsonError("Error while reading a file", pos);
            }
            else if(feof(stream))
            {
                fclose(stream);
                return {};
            }

            fclose(stream);
            return JsonError("Undefined Behaviour", pos);
        }

        // End the stream of tokens
        Result<void, JsonError> push_eot() {
            ++pos;
            if (current_token == JsonToken::Number) {
                tokens.push(current_token);
                qtok_number.push(basic_cstring<CharT>::parse_i(token_str.c_str()));
                current_token = JsonToken::Empty;
                token_position = 0;
                token_str.clear();
            }
            if (current_token != JsonToken::Empty) {
                tokens.push(JsonToken::Error);
                return JsonError("Syntax error: unexpected end of transmission", pos);
            }
            tokens.push(JsonToken::Eof);
            return {};
        }
    };

    enum class JsonValueType {
        Null, True, False, Number, String, Object, Array,
    };

    // forward declaration
    template <typename CharT>
    class JsonString;

    class JsonValue {
    public:
        virtual ~JsonValue() = default;
        virtual JsonValueType type() const noexcept = 0;
        virtual bool is_complete() const noexcept {return true; };

        template <typename Class>
        Class& as() {return static_cast<Class&>(*this); }

        template <typename CharT>
        JsonString<CharT>& as_str() {return static_cast<JsonString<CharT>&>(*this); }

        template <typename Class>
        Result<Class&, runtime_error> try_as(JsonValueType t) {
            return type() == t ? static_cast<Class&>(*this) : runtime_error("Cast to wrong type");
        }
    };

    // Streaming pending value, todo
    class JsonPendingValue : public JsonValue {
        bool is_complete() const noexcept override {return false; }
    };

    class JsonPrimitive : public JsonValue {
        JsonValueType t; // only Null, True & False allowed

        JsonPrimitive(JsonValueType t) : t(t) {}
    public:
        JsonValueType type() const noexcept override {
            return t;
        }

        static JsonPrimitive mk_null()  {return {JsonValueType::Null }; }
        static JsonPrimitive mk_true()  {return {JsonValueType::True }; }
        static JsonPrimitive mk_false() {return {JsonValueType::False}; }
    };

    class JsonNumber : public JsonValue {
        i32 _value;

    public:
        explicit JsonNumber(i32 v) : _value(v) {}

        JsonValueType type() const noexcept override {
            return JsonValueType::Number;
        }

        i32 value() const { return _value; }
    };

    template <typename CharT>
    class JsonString : public JsonValue {
        basic_string<CharT> _value;

    public:
        explicit JsonString(basic_string<CharT>&& v) : _value(move(v)) {}

        JsonValueType type() const noexcept override {
            return JsonValueType::String;
        }

        basic_string_view<CharT> value() const { return static_cast<basic_string_view<CharT>>(_value); }
    };

    class JsonArray : public JsonValue {
        vector<unique_ptr<JsonValue>> _values;

    public:
        JsonArray(vector<unique_ptr<JsonValue>>&& v) : _values(move(v)) {}

        JsonValueType type() const noexcept override {
            return JsonValueType::Array;
        }

        auto& values() {
            return _values;
        }
    };

    template <typename CharT> // XXX?
    class JsonObject : public JsonValue {
        unordered_map<basic_string<CharT>, unique_ptr<JsonValue>> _values;

    public:
        JsonObject(unordered_map<basic_string<CharT>, unique_ptr<JsonValue>>&& v)
            : _values(move(v)) {}

        JsonValueType type() const noexcept override {
            return JsonValueType::Object;
        }

        auto& values() {
            return _values;
        }
    };

    template <typename CharT>
    class JsonTokenIterator {
        JsonStream<CharT>& _stream;
    public:
        JsonTokenIterator(JsonStream<CharT>& stream)
            : _stream(stream) {}
        
        JsonToken next() {
            JsonToken tk = _stream.get_tokens().front();
            _stream.get_tokens().pop();
            return tk;
        }

        JsonToken peek() {
            return _stream.get_tokens().front();
        }

        void skip() {
            _stream.get_tokens().pop();
        }

        bool empty() const {
            return _stream.get_tokens().empty();
        }

        auto next_number() {
            return _stream.pop_number();
        }

        auto next_string() {
            return _stream.pop_string();
        }
    };

    template <typename CharT>
    class JsonParser {
        JsonTokenIterator<CharT> stream;

    public:
        JsonParser(JsonStream<CharT>& s) : stream(s) {}

        Result<unique_ptr<JsonValue>, JsonError> parse_next() {
            if (stream.empty())
                //return JsonPendingValue::make();
                return JsonError("Work in progress: pending values not supported", 0);

            JsonToken tok = stream.next();
            switch (tok) {
                case JsonToken::Eof:
                    return JsonError("Unexpected EOF", 0);
                case JsonToken::Null:
                    return make_unique<JsonPrimitive>(JsonPrimitive::mk_null());
                case JsonToken::True:
                    return make_unique<JsonPrimitive>(JsonPrimitive::mk_true());
                case JsonToken::False:
                    return make_unique<JsonPrimitive>(JsonPrimitive::mk_false());
                case JsonToken::Number:
                    return make_unique<JsonNumber>(stream.next_number());
                case JsonToken::String:
                    return make_unique<JsonString<CharT>>(stream.next_string());
                case JsonToken::BArray: {
                    vector<unique_ptr<JsonValue>> array;
                    if (stream.empty())
                        _partial_arr:
                        return JsonError("Work in progress: partial arrays", 0);
                    if (stream.peek() != JsonToken::EArray) {
                        while (true) {
                            auto r = parse_next();
                            if (!r)
                                return r;
                            array.push_back(r.expect());
                            if (stream.empty())
                                goto _partial_arr;
                            if (stream.peek() == JsonToken::EArray) {
                                stream.skip();
                                break;
                            }
                            if (stream.next() != JsonToken::Comma)
                                return JsonError("Syntax error: expected a comma while parsing array", 0);
                        }
                    }
                    return make_unique<JsonArray>(move(array));
                }
                case JsonToken::BObject: {
                    unordered_map<basic_string<CharT>, unique_ptr<JsonValue>> map;
                    if (stream.empty())
                        _partial_obj:
                        return JsonError("Work in progress: partial objects", 0);
                    if (stream.peek() != JsonToken::EObject) {
                        while (true) {
                            if (stream.next() != JsonToken::String)
                                return JsonError("Syntax error: expected string name", 0);
                            auto name = stream.next_string();
                            if (stream.empty())
                                goto _partial_obj;
                            if (stream.next() != JsonToken::Colon)
                                return JsonError("Syntax error: expected a colon", 0);
                            auto r = parse_next();
                            if (!r)
                                return r;
                            map.emplace(move(name), r.expect());
                            if (stream.peek() == JsonToken::EObject) {
                                stream.skip();
                                break;
                            }
                            if (stream.next() != JsonToken::Comma)
                                return JsonError("Syntax error: expected a comma while parsing object", 0);
                            if (stream.empty())
                                goto _partial_obj;
                        }
                    }
                    return make_unique<JsonObject<CharT>>(move(map));
                }
                default:
                    return JsonError("Syntax error: unexpected token", static_cast<usize>(tok));
            }
        }
    };
}