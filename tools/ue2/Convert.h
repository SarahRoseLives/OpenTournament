#pragma once

#include <string>

#include "map/MapFormat.h"

// Convert a UT2004 map (.ut2) into an OpenTournament map (.otmap). External
// packages (StaticMeshes/*.usx, Textures/*.utx) are resolved from ut2004Root.
// Returns true on success.
bool ue2ToOtMap(const std::string& ut2Path, const std::string& ut2004Root,
                ot::map::Map& out);

// The ue2tool command-line entry point (thin CLI around the parsing helpers).
int ue2tool_main(int argc, char** argv);
