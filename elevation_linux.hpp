#pragma once
#include <gio/gdesktopappinfo.h>
#include <string>
namespace kalwer {
inline std::string sudo_command(const std::string& command) {
    gchar* quoted = g_shell_quote(command.c_str());
    std::string result = "sudo --preserve-env=DISPLAY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR,XAUTHORITY -- /bin/sh -c " + std::string(quoted);
    g_free(quoted);
    return result;
}

inline std::string desktop_command(const std::string& identifier) {
    GDesktopAppInfo* app = g_desktop_app_info_new(identifier.c_str());
    if (!app && g_path_is_absolute(identifier.c_str()))
        app = g_desktop_app_info_new_from_filename(identifier.c_str());
    if (!app) return {};
    const char* exec = g_app_info_get_commandline(G_APP_INFO(app));
    gchar** args = nullptr; gint count = 0;
    std::string command;
    if (exec && g_shell_parse_argv(exec, &count, &args, nullptr)) {
        for (int i = 0; i < count; ++i) {
            std::string arg = args[i];
            if (arg == "%f" || arg == "%F" || arg == "%u" || arg == "%U") continue;
            if (arg == "%i") {
                gchar* icon = g_desktop_app_info_get_string(app, "Icon");
                if (icon && *icon) {
                    gchar* quoted = g_shell_quote(icon);
                    command += "--icon " + std::string(quoted) + " "; g_free(quoted);
                }
                g_free(icon); continue;
            }
            std::string expanded;
            for (size_t j = 0; j < arg.size(); ++j) {
                if (arg[j] != '%' || j + 1 == arg.size()) { expanded += arg[j]; continue; }
                const char field = arg[++j];
                if (field == '%') expanded += '%';
                else if (field == 'c') expanded += g_app_info_get_name(G_APP_INFO(app));
                else if (field == 'k') expanded += g_desktop_app_info_get_filename(app);
            }
            gchar* quoted = g_shell_quote(expanded.c_str());
            command += std::string(quoted) + " "; g_free(quoted);
        }
        g_strfreev(args);
    }
    gchar* directory = g_desktop_app_info_get_string(app, "Path");
    if (directory && *directory && !command.empty()) {
        gchar* quoted = g_shell_quote(directory);
        command = "cd " + std::string(quoted) + " && " + command; g_free(quoted);
    }
    g_free(directory);
    g_object_unref(app);
    return command;
}
}
