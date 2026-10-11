#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

// Resolve once per cube entry (or path edit), never once per output/frame.
class cube_obj_source_t
{
    std::string configured, selected;
    bool initialized = false;
    std::mt19937 random{std::random_device{}()};
  public:
    const std::string& resolve(std::string path, bool new_entry = false)
    {
        if (initialized && configured == path && !new_entry) return selected;
        const auto previous = selected;
        configured = path;
        initialized = true;
        if (path.rfind("~/", 0) == 0)
            if (auto home = std::getenv("HOME")) path = std::string(home) + path.substr(1);
        std::error_code error;
        if (!std::filesystem::is_directory(path, error))
        {
            selected = path;
            return selected;
        }
        std::vector<std::string> candidates;
        std::filesystem::directory_iterator it(path, error), end;
        while (!error && it != end)
        {
            auto extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return std::tolower(c); });
            std::error_code entry_error;
            if ((extension == ".obj" || extension == ".glb") && it->is_regular_file(entry_error))
                candidates.push_back(it->path().string());
            it.increment(error);
        }
        // Avoid immediately repeating a model when there is a choice.
        if (candidates.size() > 1)
            candidates.erase(std::remove(candidates.begin(), candidates.end(), previous), candidates.end());
        selected.clear();
        if (!candidates.empty())
            selected = candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(random)];
        return selected;
    }
};
