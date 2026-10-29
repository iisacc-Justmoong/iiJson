#include <iiJson/Json.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace ii::json;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){
    try {
        constexpr auto duplicate=R"({"a":1,"\u0061":2,"nested":{"x":true,"x":false}})";
        const auto strict=parse(duplicate,{});
        check(!strict && strict.error().code==ErrorCode::duplicate_key,"default parser accepted duplicate key");
        const auto explicitStrict=parse_with_duplicate_keys(duplicate,DuplicateKeyPolicy::reject);
        check(!explicitStrict && explicitStrict.error().code==ErrorCode::duplicate_key &&
              explicitStrict.error().offset==strict.error().offset,"explicit reject changed strict behavior");
        const auto legacy=parse_with_duplicate_keys(duplicate,DuplicateKeyPolicy::last_wins);
        check(bool(legacy),"explicit last-wins rejected valid legacy JSON");
        const auto a=legacy->find("a");check(bool(a),"escaped key was not decoded");
        const auto value=a->get().get<std::int64_t>();
        check(value && value->get()==2,"last decoded duplicate value did not win");
        const auto nested=legacy->find("nested");check(bool(nested),"nested object missing");
        const auto x=nested->get().find("x");check(bool(x),"nested key missing");
        const auto flag=x->get().get<bool>();check(flag && !flag->get(),"nested last value did not win");
        const auto invalid=parse_with_duplicate_keys(R"({"a":1,"a":})",DuplicateKeyPolicy::last_wins);
        check(!invalid && invalid.error().code==ErrorCode::syntax,"invalid duplicate value was skipped");
        Limits limit;limit.items=4;
        const auto bounded=parse_with_duplicate_keys(R"({"a":1,"a":2})",DuplicateKeyPolicy::last_wins,limit);
        check(!bounded && bounded.error().code==ErrorCode::limit_exceeded,"duplicate keys bypassed item budget");
        limit=Limits{};limit.depth=2;
        const auto deep=parse_with_duplicate_keys(R"({"a":{"x":{"y":1}}})",DuplicateKeyPolicy::last_wins,limit);
        check(!deep && deep.error().code==ErrorCode::limit_exceeded,"last-wins bypassed depth limit");
        const auto bad=parse_with_duplicate_keys("{}",static_cast<DuplicateKeyPolicy>(999));
        check(!bad && bad.error().code==ErrorCode::unsupported_value,"invalid duplicate policy was accepted");
        std::cout<<"DUPLICATE_KEY_POLICY_OK\n";
    } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
