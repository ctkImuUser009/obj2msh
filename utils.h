/*
 * utils.h
 *
 * Small utility helpers for obj2msh.
 *
 * Source: written for obj2msh
 */

#pragma once
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

/** Return true if s ends with suffix (case-sensitive). */
inline bool ends_with(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/** Ensure path ends with ".msh". */
inline std::string ensure_msh_ext(const std::string& path)
{
    if (ends_with(path, ".msh")) return path;
    return path + ".msh";
}
