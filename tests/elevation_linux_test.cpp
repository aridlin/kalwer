#include "../elevation_linux.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
int main() {
    const std::string input = "printf '%s\\n' \"a'b\" | cat > '/tmp/a b'; echo $HOME";
    const auto elevated = kalwer::sudo_command(input);
    gchar** args = nullptr; gint count = 0;
    assert(g_shell_parse_argv(elevated.c_str(), &count, &args, nullptr));
    assert(count == 6 && std::string(args[0]) == "sudo" && std::string(args[3]) == "/bin/sh");
    assert(std::string(args[5]) == input); g_strfreev(args);
    const auto path = std::filesystem::temp_directory_path() / ("kalwer-elevation-" + std::to_string(getpid()) + ".desktop");
    std::ofstream(path) << "[Desktop Entry]\nType=Application\nName=Test $(false)\nIcon=test icon\nExec=/usr/bin/printf \"%c\" %i %% %U\nPath=/tmp\n";
    const auto command = kalwer::desktop_command(path.string());
    assert(command == "cd '/tmp' && '/usr/bin/printf' 'Test $(false)' --icon 'test icon' '%' ");
    assert(kalwer::desktop_command("kalwer-nonexistent.desktop").empty());
    std::filesystem::remove(path);
    std::cout << "Elevation quoting, desktop fields, working directory and missing-app checks passed.\n";
}
