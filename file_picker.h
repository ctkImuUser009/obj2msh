/*
 * file_picker.h
 *
 * Interactive terminal file browser for selecting a .obj file.
 * On Linux: uses a built-in ncurses-style arrow-key navigator.
 * On Windows: falls back to a plain readline prompt.
 *
 * Source: written for obj2msh (no external dependency required)
 */

#pragma once
#include <string>

/**
 * Open an interactive file picker in the terminal.
 * Returns the absolute path to the selected .obj file,
 * or an empty string if the user cancelled.
 */
std::string pick_obj_file();
