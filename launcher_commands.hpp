#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace kalwer {
// Shared command catalog and process-independent popup content for both hosts.
struct PopupDocument { std::string title, body; };
struct Command { std::string_view name, description, replacement; };
inline constexpr std::array commands = {
    Command{"/help", "Commands and keyboard shortcuts", ""},
    Command{"/files", "Search indexed files and folders", ":"},
    Command{"/apps", "Search applications", ""},
    Command{"/web", "Search the web", "? "},
    Command{"/terminal", "Run a shell command", "> "},
    Command{"/jobs", "Show background commands", "<"},
    Command{"/reindex", "Refresh the file index now", ""},
    Command{"/updates", "Update status and running version", ""},
    Command{"/about", "About Kalwer", ""},
    Command{"/settings", "Open settings", ""},
    Command{"/exit", "Quit Kalwer", ""},
};
inline std::vector<const Command*> matching_commands(std::string prefix) {
    for (auto& c : prefix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    std::vector<const Command*> matches;
    if (!prefix.empty() && prefix.front() == '/')
        for (const auto& command : commands) if (command.name.starts_with(prefix)) matches.push_back(&command);
    return matches;
}
inline PopupDocument help() {
    PopupDocument result{"KALWER HELP", "SEARCH MODES\nText: apps    :text: files\n> command: shell    ? text: web\n< : background jobs\n\nCOMMANDS\n"};
    for (const auto& command : commands) result.body += std::string(command.name) + "  " + std::string(command.description) + "\n";
    result.body += "\nKEYBOARD\nTab: accept suggestion\nUp/Down: choose result\nEnter: open/run    Esc: close\nShift+Enter: favorite app\n\nFiles: 1–2 characters match name prefixes;\n3+ match anywhere in paths.\nPopup: select/copy text; scroll to read.\nCtrl+Shift+C: copy all popup text.\n";
    return result;
}
inline PopupDocument about() { return {"ABOUT KALWER", "Kalwer\nResident application and file launcher.\n\nLinux and Windows share commands and\na bundled SQLite file index.\n\nType /help for commands and shortcuts.\n"}; }
}
