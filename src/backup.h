#pragma once
// Registry / service backup: capture current tweakable values into a JSON file
// so the user can restore them later. Uses jsonxx for serialization.

#include <string>

namespace bk {

// Captures every setting the optimizer touches and writes it to `path`.
// Returns a short summary message.
std::string snapshotToFile(const std::string& path);

// Reads a backup JSON (`path`) and re-applies all captured values.
// Returns the number of restored items (-1 on error).
int restoreFromFile(const std::string& path);

} // namespace bk