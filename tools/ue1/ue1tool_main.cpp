#include "Convert.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "map/MapFormat.h"

static void usage() {
    std::fprintf(stderr, "usage: ue1tool extract <file.unr> --out <path>\n");
}

int main(int argc, char** argv) {
    return ue1tool_main(argc, argv);
}

int ue1tool_main(int argc, char** argv) {
    if (argc < 4) {
        usage();
        return 1;
    }
    const std::string cmd = argv[1];
    const std::string path = argv[2];
    if (cmd != "extract") {
        usage();
        return 1;
    }
    std::string outPath;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outPath = argv[++i];
    }
    if (outPath.empty()) {
        usage();
        return 1;
    }
    ot::map::Map map;
    if (!ue1ToOtMap(path, "C:\\UnrealTournament", map)) {
        std::fprintf(stderr, "[ue1] conversion failed\n");
        return 1;
    }
    if (ot::map::saveMap(map, outPath)) {
        std::printf("wrote map %s\n", outPath.c_str());
    } else {
        std::fprintf(stderr, "failed to write map %s\n", outPath.c_str());
    }
    return 0;
}
