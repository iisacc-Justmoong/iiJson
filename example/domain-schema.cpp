#include <iiJson/Schema.h>
#include <iostream>

struct Project final {
    std::string title;
    std::uint64_t frameRate = 24;
};

template<> struct ii::json::Codec<Project> final {
    static const Schema& schema() {
        static const auto definition = Schema::object({
            Field::required("schemaVersion", Schema::literal(Value(1))),
            Field::required("title", Schema::string({1, 80})),
            Field::required("frameRate", Schema::unsigned_integer(1, 240))
        });
        return definition;
    }
    static Result<Value> encode(const Project& project) {
        return Value(Value::Object{
            {"schemaVersion", Value(1)}, {"title", Value(project.title)},
            {"frameRate", Value(project.frameRate)}
        });
    }
    static Result<Project> decode(const Value& value) {
        // ii::json::decode<Project> validates the schema before this conversion.
        const auto& rate = value.find("frameRate")->get();
        const auto unsignedRate = rate.get<std::uint64_t>();
        return Project{value.find("title")->get().get<std::string>()->get(),
                       unsignedRate ? unsignedRate->get() : std::uint64_t(rate.get<std::int64_t>()->get())};
    }
};

int main() {
    const auto project = ii::json::decode<Project>(
        R"({"schemaVersion":1,"title":"First film","frameRate":24})");
    if (!project) {
        std::cerr << project.error().path << ": " << project.error().message << '\n';
        return 1;
    }
    const auto output = ii::json::encode(*project);
    if (!output) {
        std::cerr << output.error().path << ": " << output.error().message << '\n';
        return 1;
    }
    std::cout << *output << '\n';
}
