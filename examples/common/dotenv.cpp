#include "dotenv.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace OpenAnimaExample {

namespace {

constexpr int LevelsToSearch = 4;

std::string trim(const std::string& text) {
    const std::string whitespace = " \t";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

std::string stripQuotes(const std::string& value) {
    const bool quoted = value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
                        value.back() == value.front();
    return quoted ? value.substr(1, value.size() - 2) : value;
}

void setEnvironmentVariable(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

}  // namespace

std::vector<EnvironmentEntry> readDotEnv(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    std::vector<EnvironmentEntry> entries;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        std::string name = trim(trimmed.substr(0, separator));
        if (name.empty()) {
            continue;
        }
        entries.emplace_back(std::move(name), stripQuotes(trim(trimmed.substr(separator + 1))));
    }
    return entries;
}

std::string findDotEnv() {
    std::filesystem::path directory = std::filesystem::current_path();
    for (int level = 0; level < LevelsToSearch; ++level) {
        const std::filesystem::path candidate = directory / ".env";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
        if (!directory.has_parent_path() || directory.parent_path() == directory) {
            break;
        }
        directory = directory.parent_path();
    }
    return {};
}

void applyDotEnv(const std::string& path) {
    if (path.empty()) {
        return;
    }
    for (const EnvironmentEntry& entry : readDotEnv(path)) {
        if (std::getenv(entry.first.c_str()) == nullptr) {
            setEnvironmentVariable(entry.first, entry.second);
        }
    }
}

}  // namespace OpenAnimaExample
