#pragma once

#include <string>
#include <utility>
#include <vector>

namespace OpenAnimaExample {

/// A single KEY=VALUE pair read from a .env file, in file order.
using EnvironmentEntry = std::pair<std::string, std::string>;

/// Reads KEY=VALUE pairs from a file. Blank lines and lines starting with '#'
/// are skipped, and one layer of matching quotes around a value is removed.
/// @param path File to read.
/// @return The entries found, empty when the file cannot be opened.
std::vector<EnvironmentEntry> readDotEnv(const std::string& path);

/// Looks for a .env file in the working directory and its parents.
/// The CMake Tools buttons run the binary from the build tree, so a project that
/// keeps .env at its root is still found from there.
/// @return The first existing candidate, or an empty string when there is none.
std::string findDotEnv();

/// Copies the entries into the process environment.
/// Variables that are already set are left alone, so a real environment always
/// wins over the file.
/// @param path File to read, ignored when empty.
void applyDotEnv(const std::string& path);

}  // namespace OpenAnimaExample
