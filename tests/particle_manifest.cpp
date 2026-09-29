#include "northstar_ps4/particle_manifest.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace northstar::ps4::mods;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    std::string body, error;
    CHECK(ExtractParticleManifestBody(
        "particles_manifest\n{\n  file \"particles/base.rpak\"\n}\n", body, error));
    CHECK(body.find("particles/base.rpak") != std::string::npos);

    CHECK(ExtractParticleManifestBody(
        "#base \"ignored.txt\"\n\"particles_manifest\" {\n"
        "// a brace in a comment }\nfile \"particles/{quoted}.rpak\"\n}\n", body, error));
    CHECK(body.find("{quoted}") != std::string::npos);
    CHECK(!ExtractParticleManifestBody("wrong { file x }", body, error));
    CHECK(!ExtractParticleManifestBody("particles_manifest { file x", body, error));
    CHECK(!ExtractParticleManifestBody("particles_manifest {} trailing", body, error));

    std::vector<std::pair<std::string, std::string>> mods = {
        {"Low", "file \"particles/low.rpak\"\n"},
        {"High", "file \"particles/high.rpak\"\n"},
    };
    const std::string built = BuildParticleManifest("file \"particles/base.rpak\"\n", mods);
    CHECK(built.find("particles/base.rpak") < built.find("// [Low]"));
    CHECK(built.find("// [Low]") < built.find("// [High]"));
    CHECK(built.rfind("}\n") == built.size() - 2);

    std::puts("particle_manifest tests passed");
    return 0;
}
