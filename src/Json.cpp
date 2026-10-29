#include <iiJson/Json.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace ii::json {
namespace {
constexpr std::size_t maximumDepth=256;
template<class T> bool readReal(const char* begin,const char* end,T& value) {
    if constexpr (requires { std::from_chars(begin,end,value,std::chars_format::general); }) {
        const auto result=std::from_chars(begin,end,value,std::chars_format::general);
        return result.ec==std::errc{} && result.ptr==end && std::isfinite(value);
    } else {
        // Some target standard libraries omit floating from_chars. Use only a
        // private classic-locale stream, never process-global locale/strtod.
        std::istringstream input(std::string(begin,end));
        input.imbue(std::locale::classic());
        input>>std::noskipws>>value;
        if (!input.eof() || !std::isfinite(value)) return false;
        // num_get may set failbit for representable subnormal values too.
        if (input.fail() && std::abs(value)>=std::numeric_limits<T>::min()) return false;
        if (value==0) {
            for (const char* at=begin;at!=end && *at!='e' && *at!='E';++at)
                if (*at>='1' && *at<='9') return false; // underflow to zero
        }
        return true;
    }
}
void appendScalar(std::string& out,char32_t scalar) {
    if (scalar<0x80) out.push_back(char(scalar));
    else if (scalar<0x800) { out.push_back(char(0xc0|(scalar>>6))); out.push_back(char(0x80|(scalar&63))); }
    else if (scalar<0x10000) { out.push_back(char(0xe0|(scalar>>12))); out.push_back(char(0x80|((scalar>>6)&63))); out.push_back(char(0x80|(scalar&63))); }
    else { out.push_back(char(0xf0|(scalar>>18))); out.push_back(char(0x80|((scalar>>12)&63))); out.push_back(char(0x80|((scalar>>6)&63))); out.push_back(char(0x80|(scalar&63))); }
}
// Use a classic-locale stream so number output remains independent of the
// process locale and floating charconv availability on older platform SDKs.
std::string formatReal(double value) {
    auto render=[&](int precision,std::ios_base::fmtflags mode) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.setf(mode,std::ios_base::floatfield);
        out<<std::setprecision(precision)<<value;
        return out.str();
    };
    auto roundtrips=[&](const std::string& text) {
        double decoded{};
        return readReal(text.data(),text.data()+text.size(),decoded) && decoded==value;
    };
    int low=1,high=std::numeric_limits<double>::max_digits10;
    while (low<high) {
        const int middle=(low+high)/2;
        if (roundtrips(render(middle,{}))) high=middle; else low=middle+1;
    }
    auto result=render(low,{});
    auto scientific=render(low-1,std::ios_base::scientific);
    if (scientific.size()<result.size() && roundtrips(scientific)) result=std::move(scientific);
    const int exponent=value==0 ? 0 : int(std::floor(std::log10(std::abs(value))));
    auto fixed=render(std::max(0,low-1-exponent),std::ios_base::fixed);
    if (fixed.size()<result.size() && roundtrips(fixed)) result=std::move(fixed);
    return result;
}
class Parser final {
    std::string_view input;
    Limits limits;
    DuplicateKeyPolicy duplicateKeys;
    std::size_t offset=0,items=0;
    [[noreturn]] void fail(ErrorCode code,const char* message,std::size_t at=SIZE_MAX) const {
        if (at==SIZE_MAX) at=offset;
        Error error{code,at,1,1,message};
        for (std::size_t i=0;i<at && i<input.size();++i) {
            if (input[i]=='\n') { ++error.line; error.column=1; } else ++error.column;
        }
        throw error;
    }
    void whitespace() { while (offset<input.size() && (input[offset]==' ' || input[offset]=='\t' || input[offset]=='\n' || input[offset]=='\r')) ++offset; }
    bool take(char token) {
        if (offset<input.size() && input[offset]==token) { ++offset; return true; }
        return false;
    }
    void item() { if (++items>limits.items) fail(ErrorCode::limit_exceeded,"JSON item limit exceeded"); }
    std::uint16_t hex() {
        std::uint16_t result=0;
        for (int count=0;count<4;++count) {
            if (offset>=input.size()) fail(ErrorCode::syntax,"Truncated Unicode escape");
            const char digit=input[offset++];
            unsigned value=16;
            if (digit>='0' && digit<='9') value=unsigned(digit-'0');
            else if (digit>='a' && digit<='f') value=unsigned(digit-'a'+10);
            else if (digit>='A' && digit<='F') value=unsigned(digit-'A'+10);
            if (value>15) fail(ErrorCode::syntax,"Invalid Unicode escape",offset-1);
            result=std::uint16_t((result<<4)|value);
        }
        return result;
    }
    std::string text() {
        if (!take('"')) fail(ErrorCode::syntax,"Expected JSON string");
        std::string result;
        while (offset<input.size()) {
            const auto byte=static_cast<unsigned char>(input[offset++]);
            if (byte=='"') return result;
            if (byte<0x20) fail(ErrorCode::syntax,"Unescaped control byte",offset-1);
            if (byte!='\\') result.push_back(char(byte));
            else {
                if (offset>=input.size()) fail(ErrorCode::syntax,"Truncated escape");
                switch (input[offset++]) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        char32_t scalar=hex();
                        if (scalar>=0xd800 && scalar<=0xdbff) {
                            if (!take('\\') || !take('u')) fail(ErrorCode::syntax,"Unpaired high surrogate");
                            const char32_t low=hex();
                            if (low<0xdc00 || low>0xdfff) fail(ErrorCode::syntax,"Unpaired high surrogate");
                            scalar=0x10000+((scalar-0xd800)<<10)+(low-0xdc00);
                        } else if (scalar>=0xdc00 && scalar<=0xdfff) {
                            fail(ErrorCode::syntax,"Unpaired low surrogate");
                        }
                        appendScalar(result,scalar);
                        break;
                    }
                    default: fail(ErrorCode::syntax,"Unknown JSON escape",offset-1);
                }
            }
            if (result.size()>limits.string_bytes) fail(ErrorCode::limit_exceeded,"JSON string byte limit exceeded");
        }
        fail(ErrorCode::syntax,"Unterminated JSON string");
    }
    Value number() {
        const auto start=offset;
        const bool negative=take('-');
        if (!take('0')) {
            if (offset==input.size() || input[offset]<'1' || input[offset]>'9') fail(ErrorCode::syntax,"Expected number");
            while (offset<input.size() && input[offset]>='0' && input[offset]<='9') ++offset;
        }
        bool real=false;
        auto digits=[&] {
            const auto begin=offset;
            while (offset<input.size() && input[offset]>='0' && input[offset]<='9') ++offset;
            if (begin==offset) fail(ErrorCode::syntax,"Missing number digits");
        };
        if (take('.')) { real=true; digits(); }
        if (take('e') || take('E')) { real=true; if (!take('+')) (void)take('-'); digits(); }
        const auto begin=input.data()+start,end=input.data()+offset;
        if (real) {
            double result{};
            if (!readReal(begin,end,result))
                fail(ErrorCode::number_out_of_range,"JSON real is outside finite binary64 range",start);
            return Value(result);
        }
        if (negative) {
            std::int64_t result{};
            const auto parsed=std::from_chars(begin,end,result);
            if (parsed.ec!=std::errc{} || parsed.ptr!=end) fail(ErrorCode::number_out_of_range,"JSON signed integer overflow",start);
            return Value(result);
        }
        std::uint64_t result{};
        const auto parsed=std::from_chars(begin,end,result);
        if (parsed.ec!=std::errc{} || parsed.ptr!=end) fail(ErrorCode::number_out_of_range,"JSON unsigned integer overflow",start);
        return result<=std::uint64_t(INT64_MAX) ? Value(std::int64_t(result)) : Value(result);
    }
    Value value(std::size_t depth) {
        whitespace(); item();
        if (depth>limits.depth || depth>maximumDepth) fail(ErrorCode::limit_exceeded,"JSON depth limit exceeded");
        if (offset==input.size()) fail(ErrorCode::syntax,"Missing JSON value");
        if (take('[')) {
            Value::Array result;
            whitespace(); if (take(']')) return Value(std::move(result));
            do { result.push_back(value(depth+1)); whitespace(); if (take(']')) return Value(std::move(result)); }
            while (take(','));
            fail(ErrorCode::syntax,"Expected comma or closing bracket");
        }
        if (take('{')) {
            Value::Object result;
            whitespace(); if (take('}')) return Value(std::move(result));
            do {
                whitespace(); item(); const auto at=offset; auto key=text();
                const auto previous=result.find(key);
                if (previous!=result.end() && duplicateKeys==DuplicateKeyPolicy::reject)
                    fail(ErrorCode::duplicate_key,"Duplicate decoded JSON key",at);
                whitespace(); if (!take(':')) fail(ErrorCode::syntax,"Expected object colon");
                auto entry=value(depth+1);
                if (previous!=result.end()) previous->second=std::move(entry);
                else result.emplace(std::move(key),std::move(entry));
                whitespace(); if (take('}')) return Value(std::move(result));
            } while (take(','));
            fail(ErrorCode::syntax,"Expected comma or closing brace");
        }
        if (input[offset]=='"') return Value(text());
        for (auto literal:{std::string_view("null"),std::string_view("true"),std::string_view("false")}) {
            if (input.substr(offset,literal.size())==literal) {
                offset+=literal.size(); return literal=="null" ? Value() : Value(literal=="true");
            }
        }
        return number();
    }
public:
    Parser(std::string_view source,Limits policy,DuplicateKeyPolicy duplicates)
        :input(source),limits(policy),duplicateKeys(duplicates){}
    Value parse() {
        if (input.size()>limits.input_bytes) fail(ErrorCode::limit_exceeded,"JSON input byte limit exceeded",0);
        if (limits.depth>maximumDepth) fail(ErrorCode::limit_exceeded,"JSON depth configuration exceeds hard ceiling",0);
        if (const auto invalid=detail::invalid_utf8(input))
            fail(ErrorCode::invalid_utf8,"JSON requires valid UTF-8",*invalid);
        auto result=value(1); whitespace();
        if (offset!=input.size()) fail(ErrorCode::syntax,"Trailing JSON data");
        return result;
    }
};
class Writer final {
    Limits limits;
    std::string output;
    std::size_t items=0;
    [[noreturn]] void fail(ErrorCode code,const char* message) const { throw Error{code,output.size(),1,output.size()+1,message}; }
    void append(std::string_view bytes) {
        if (bytes.size()>limits.output_bytes-output.size()) fail(ErrorCode::limit_exceeded,"JSON output byte limit exceeded");
        output.append(bytes);
    }
    void item() { if (++items>limits.items) fail(ErrorCode::limit_exceeded,"JSON item limit exceeded"); }
    void text(std::string_view value) {
        if (value.size()>limits.string_bytes) fail(ErrorCode::limit_exceeded,"JSON string byte limit exceeded");
        append("\"");
        constexpr char hex[]="0123456789abcdef";
        for (unsigned char byte:value) {
            switch (byte) {
                case '"': append("\\\""); break;
                case '\\': append("\\\\"); break;
                case '\b': append("\\b"); break;
                case '\f': append("\\f"); break;
                case '\n': append("\\n"); break;
                case '\r': append("\\r"); break;
                case '\t': append("\\t"); break;
                default:
                    if (byte<32) { char escape[]{'\\','u','0','0',hex[byte>>4],hex[byte&15]}; append({escape,6}); }
                    else { const char single=char(byte); append({&single,1}); }
            }
        }
        append("\"");
    }
    template<class Number> void number(Number value) {
        if constexpr (std::floating_point<Number>) {
            if(!std::isfinite(value))fail(ErrorCode::number_out_of_range,"Number must be finite");
            const auto text=formatReal(value);append(text);
            if(text.find_first_of(".eE")==text.npos)append(".0");
        } else {
        char buffer[64];
        const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
        if (result.ec!=std::errc{}) fail(ErrorCode::number_out_of_range,"Number cannot be formatted");
        const std::string_view text(buffer,std::size_t(result.ptr-buffer));
        append(text);
        }
    }
    void value(const Value& tree,std::size_t depth) {
        item();
        if (depth>limits.depth || depth>maximumDepth) fail(ErrorCode::limit_exceeded,"JSON depth limit exceeded");
        std::visit([&](const auto& data) {
            using T=std::decay_t<decltype(data)>;
            if constexpr (std::same_as<T,std::monostate>) append("null");
            else if constexpr (std::same_as<T,bool>) append(data ? "true" : "false");
            else if constexpr (std::is_arithmetic_v<T>) number(data);
            else if constexpr (std::same_as<T,std::string>) text(data);
            else if constexpr (std::same_as<T,Value::Array>) {
                append("["); bool first=true;
                for (const auto& child:data) { if (!first) append(","); first=false; value(child,depth+1); }
                append("]");
            } else if constexpr (std::same_as<T,Value::Object>) {
                append("{"); bool first=true;
                for (const auto& [key,child]:data) { item(); if (!first) append(","); first=false; text(key); append(":"); value(child,depth+1); }
                append("}");
            } else fail(ErrorCode::unsupported_value,"Binary bytes need an explicit domain encoding for JSON");
        },tree.storage());
    }
public:
    explicit Writer(Limits policy):limits(policy){}
    std::string write(const Value& tree) {
        if (limits.depth>maximumDepth) fail(ErrorCode::limit_exceeded,"JSON depth configuration exceeds hard ceiling");
        value(tree,1); return std::move(output);
    }
};
}
Result<Value> parse(std::string_view input,Limits limits) {
    return parse_with_duplicate_keys(input,DuplicateKeyPolicy::reject,limits);
}
Result<Value> parse_with_duplicate_keys(std::string_view input,DuplicateKeyPolicy policy,Limits limits) {
    try {
        if (policy!=DuplicateKeyPolicy::reject && policy!=DuplicateKeyPolicy::last_wins)
            throw Error{ErrorCode::unsupported_value,0,1,1,"Invalid duplicate key policy"};
        return Parser(input,limits,policy).parse();
    } catch (const Error& error) { return std::unexpected(error); }
}
Result<std::string> stringify(const Value& value,Limits limits) {
    try { return Writer(limits).write(value); } catch (const Error& error) { return std::unexpected(error); }
}
} // namespace ii::json
