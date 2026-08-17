#pragma once

#include <string>

#include "map/MapFormat.h"

// Convert an Unreal Tournament (UT99, UE1) map (.unr) into an OpenTournament
// map (.otmap). Texture packages (*.utx) are resolved from ut99Root.
// Returns true on success.
bool ue1ToOtMap(const std::string& unrPath, const std::string& ut99Root,
                ot::map::Map& out);

// The ue1tool command-line entry point.
int ue1tool_main(int argc, char** argv);
