/*
 * file_picker.cpp
 *
 * Interactive terminal file browser.
 *
 * Linux/macOS  – built-in ncurses arrow-key navigator that lets the user
 *                browse directories and select a .obj file.
 * Windows      – plain readline prompt (ncurses not available on MinGW).
 *
 * Source: written for obj2msh
 */

#include "file_picker.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <clocale>
#include <clocale>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers shared by both paths
// ─────────────────────────────────────────────────────────────────────────────
static std::string normalize_path(const std::string& p)
{
    try {
        return fs::canonical(p).string();
    } catch (...) {
        return p;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// POSIX (Linux / macOS) – ncurses navigator
// ─────────────────────────────────────────────────────────────────────────────
#if !defined(_WIN32)

#include <ncurses.h>

struct DirEntry {
    std::string name;   // display name (dirs end with '/')
    std::string full;   // full path
    bool        is_dir;
    bool        is_obj;
};

static std::vector<DirEntry> list_dir(const fs::path& dir)
{
    std::vector<DirEntry> entries;

    // Add ".." unless at filesystem root
    if (dir.has_parent_path() && dir != dir.root_path()) {
        DirEntry up;
        up.name   = "../";
        up.full   = (dir / "..").lexically_normal().string();
        up.is_dir = true;
        up.is_obj = false;
        entries.push_back(up);
    }

    for (const auto& e : fs::directory_iterator(dir,
             fs::directory_options::skip_permission_denied))
    {
        DirEntry de;
        de.is_dir = e.is_directory();
        de.is_obj = (!de.is_dir &&
                     e.path().extension() == ".obj");
        // Show only directories and .obj files
        if (!de.is_dir && !de.is_obj)
            continue;
        de.full = e.path().string();
        de.name = e.path().filename().string() + (de.is_dir ? "/" : "");
        entries.push_back(de);
    }

    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.name == "../") return true;
        if (b.name == "../") return false;
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });

    return entries;
}

std::string pick_obj_file()
{
    // Start in current working directory
    fs::path current_dir = fs::current_path();

    // Set locale from environment so ncurses knows whether the terminal
    // supports UTF-8. Without this, box-drawing characters render as garbage
    // on terminals with LANG=C or no UTF-8 locale configured.
    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    // Colour support
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN,   COLOR_BLACK);  // directories
        init_pair(2, COLOR_GREEN,  COLOR_BLACK);  // .obj files
        init_pair(3, COLOR_BLACK,  COLOR_WHITE);  // selected item
        init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // header / footer
    }

    std::string result;
    int         selected = 0;
    int         scroll   = 0;

    auto redraw = [&](const std::vector<DirEntry>& entries) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        (void)max_x;
        int list_rows = max_y - 5;  // 3 header + 2 footer

        clear();

        // Header
        if (has_colors()) attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(0, 0, " obj2msh - Select .obj File ");
        if (has_colors()) attroff(COLOR_PAIR(4) | A_BOLD);

        mvprintw(1, 0, " Dir: %s", current_dir.string().c_str());
        // Use plain ASCII dashes -- Unicode box-drawing chars (U+2500) render
        // as garbage when the terminal locale is not UTF-8.
        move(2, 0);
        hline('-', max_x > 0 ? max_x : 60);

        // List
        for (int i = 0; i < list_rows && (scroll + i) < (int)entries.size(); ++i) {
            int idx = scroll + i;
            const DirEntry& de = entries[idx];

            if (idx == selected) {
                if (has_colors()) attron(COLOR_PAIR(3) | A_BOLD);
                else              attron(A_REVERSE);
            } else if (de.is_dir) {
                if (has_colors()) attron(COLOR_PAIR(1));
            } else {
                if (has_colors()) attron(COLOR_PAIR(2));
            }

            mvprintw(3 + i, 0, " %-60s", de.name.c_str());

            if (idx == selected) {
                if (has_colors()) attroff(COLOR_PAIR(3) | A_BOLD);
                else              attroff(A_REVERSE);
            } else if (de.is_dir) {
                if (has_colors()) attroff(COLOR_PAIR(1));
            } else {
                if (has_colors()) attroff(COLOR_PAIR(2));
            }
        }

        // Footer
        if (has_colors()) attron(COLOR_PAIR(4));
        mvprintw(max_y - 2, 0,
                 " Up/Dn=navigate  ENTER=select/open  q=quit  "
                 "Dirs shown in cyan, .obj in green ");
        if (has_colors()) attroff(COLOR_PAIR(4));

        refresh();
    };

    std::vector<DirEntry> entries = list_dir(current_dir);

    while (true) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        (void)max_x;
        int list_rows = max_y - 5;

        // Clamp scroll
        if (selected < scroll)
            scroll = selected;
        if (selected >= scroll + list_rows)
            scroll = selected - list_rows + 1;

        redraw(entries);

        int ch = getch();
        switch (ch) {
        case KEY_UP:
            if (selected > 0) --selected;
            break;
        case KEY_DOWN:
            if (selected < (int)entries.size() - 1) ++selected;
            break;
        case KEY_PPAGE:
            selected = std::max(0, selected - list_rows);
            break;
        case KEY_NPAGE:
            selected = std::min((int)entries.size() - 1, selected + list_rows);
            break;
        case '\n':
        case KEY_ENTER: {
            if (entries.empty()) break;
            const DirEntry& de = entries[selected];
            if (de.is_dir) {
                try {
                    current_dir = fs::canonical(de.full);
                    entries     = list_dir(current_dir);
                    selected    = 0;
                    scroll      = 0;
                } catch (const std::exception& ex) {
                    // Permission denied, etc. – stay put
                    (void)ex;
                }
            } else {
                result = normalize_path(de.full);
                endwin();
                return result;
            }
            break;
        }
        case 'q':
        case 'Q':
        case 27: /* ESC */
            endwin();
            return "";
        default:
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows – plain readline prompt (ncurses not available under MinGW)
// ─────────────────────────────────────────────────────────────────────────────
#else  // _WIN32

std::string pick_obj_file()
{
    std::cout << "============================================================\n";
    std::cout << "  obj2msh File Picker\n";
    std::cout << "============================================================\n";
    std::cout << "Enter the full path to your .obj file:\n> ";

    std::string path;
    if (!std::getline(std::cin, path)) {
        return "";
    }

    // Trim surrounding whitespace / quotes
    while (!path.empty() && (path.front() == ' ' || path.front() == '"'))
        path.erase(path.begin());
    while (!path.empty() && (path.back() == ' ' || path.back() == '"'))
        path.pop_back();

    return path;
}

#endif  // _WIN32
