#include <iiJson/Json.h>
#include <cstdint>
#include <iostream>

int main() {
    using namespace ii::json;
    // Opt in only for a format whose established contract uses the last key.
    const auto document=parse_with_duplicate_keys(R"({"revision":1,"revision":2})",
                                                  DuplicateKeyPolicy::last_wins);
    if (!document) return 1;
    const auto field=document->find("revision");
    if (!field) return 1;
    const auto revision=field->get().get<std::int64_t>();
    if (!revision || revision->get()!=2) return 1;
    std::cout<<"legacy revision="<<revision->get()<<'\n';
}
