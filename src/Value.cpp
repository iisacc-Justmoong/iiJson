#include <iiJson/Value.h>

namespace ii::json::detail {
std::optional<std::size_t> invalid_utf8(std::string_view text) noexcept {
    for (std::size_t index=0; index<text.size();) {
        const auto start=index;
        const auto first=static_cast<unsigned char>(text[index]);
        if (first<0x80) { ++index; continue; }
        std::size_t count=0;
        char32_t scalar=0, minimum=0;
        if (first>=0xc2 && first<=0xdf) { count=2; scalar=first&0x1f; minimum=0x80; }
        else if (first>=0xe0 && first<=0xef) { count=3; scalar=first&0x0f; minimum=0x800; }
        else if (first>=0xf0 && first<=0xf4) { count=4; scalar=first&0x07; minimum=0x10000; }
        else return start;
        if (count>text.size()-index) return start;
        for (std::size_t offset=1; offset<count; ++offset) {
            const auto byte=static_cast<unsigned char>(text[index+offset]);
            if ((byte&0xc0)!=0x80) return start;
            scalar=(scalar<<6)|(byte&0x3f);
        }
        if (scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff)) return start;
        index+=count;
    }
    return {};
}
} // namespace ii::json::detail
