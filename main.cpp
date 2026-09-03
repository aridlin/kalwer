#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <pango/pangocairo.h>
#include <epoxy/gl.h>
#include <vte/vte.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <sys/wait.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kWindowWidth = 650;
constexpr int kWindowHeight = 632;
constexpr double kSearchX = 15.0;
constexpr double kSearchY = 12.0;
constexpr double kSearchWidth = 620.0;
constexpr double kSearchHeight = 66.0;
constexpr double kResultsY = 88.0;
constexpr double kResultX = 25.0;
constexpr double kResultWidth = 600.0;
constexpr double kRowHeight = 58.0;
constexpr double kRowPitch = 66.0;
constexpr int kVisibleResults = 8;
constexpr int kSelectableResults = 5;
constexpr int kQueryLimit = 512;
constexpr int kOutputWidth = 320;
constexpr int kOutputHeight = 378;

struct Result {
    std::string identifier;
    std::string text;
    std::string subtext;
    std::string icon;
    std::string provider;
    std::string action;
    std::string favorite_action;
    bool pinned = false;
    int source_rank = 0;
    std::string fuzzy_field;
    std::vector<int> fuzzy_positions;
};

struct QueryContext {
    guint serial = 0;
};

struct CommandJob {
    std::string session;
    std::string command;
    std::string status_path;
    std::string background_path;
    bool background = false;
};

struct State {
    GtkApplication* app = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* overlay = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* entry = nullptr;
    GtkIconTheme* icon_theme = nullptr;
    std::vector<Result> results;
    std::vector<std::string> favorite_keys;
    std::unordered_map<std::string, GdkPixbuf*> icons;
    int selection = 0;
    int scroll_offset = 0;
    double selection_visual = 0.0;
    double scroll_accumulator = 0.0;
    bool resident = false;
    bool held = false;
    bool opening = false;
    gint64 opened_us = 0;
    gint64 animation_last_us = 0;
    guint animation_source = 0;
    guint query_delay_source = 0;
    guint query_serial = 0;
    GSubprocess* query_process = nullptr;
    bool query_pending = false;
    bool query_failed = false;
    std::vector<std::string> completion_candidates;
    std::string completion_source;
    int completion_index = -1;
    int completion_start = 0;
    int completion_end = 0;
    bool applying_completion = false;
    cairo_surface_t* finished_surface = nullptr;
    int surface_scale = 0;
    GLuint gl_program = 0;
    GLuint gl_texture = 0;
    GLuint gl_vertex_array = 0;
    bool texture_dirty = true;
    GtkWidget* output_window = nullptr;
    GtkWidget* output_canvas = nullptr;
    GtkWidget* output_content = nullptr;
    GtkWidget* output_terminal = nullptr;
    GtkWidget* output_status = nullptr;
    guint output_animation_source = 0;
    guint output_close_source = 0;
    gint64 output_opened_us = 0;
    bool output_interacted = false;
    bool output_finished = false;
    int output_exit_status = 0;
    guint output_status_source = 0;
    std::string output_session;
    std::string output_command;
    bool output_keep_session = false;
    guint command_serial = 0;
    std::vector<CommandJob> jobs;
    gint64 hidden_us = 0;
};

State state;

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double ease_out_cubic_cpu(double value) {
    const double inverse = 1.0 - clamp01(value);
    return 1.0 - inverse * inverse * inverse;
}

std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string favorite_key(const Result& result) {
    return result.provider + "\n" + result.identifier;
}

void load_favorites() {
    gchar* path = g_build_filename(g_get_user_config_dir(), "kalwer", "favorites-v1", nullptr);
    gchar* contents = nullptr;
    gsize length = 0;
    if (g_file_get_contents(path, &contents, &length, nullptr)) {
        gchar** lines = g_strsplit(contents, "\n", -1);
        for (gchar** line = lines; *line; ++line) {
            if (!**line) continue;
            gsize decoded_length = 0;
            guchar* decoded = g_base64_decode(*line, &decoded_length);
            if (decoded && decoded_length > 0) {
                std::string key(reinterpret_cast<char*>(decoded), decoded_length);
                if (std::find(state.favorite_keys.begin(), state.favorite_keys.end(), key) ==
                    state.favorite_keys.end()) {
                    state.favorite_keys.push_back(std::move(key));
                }
            }
            g_free(decoded);
        }
        g_strfreev(lines);
    }
    g_free(contents);
    g_free(path);
}

void save_favorites() {
    gchar* directory = g_build_filename(g_get_user_config_dir(), "kalwer", nullptr);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_free(directory);
        return;
    }
    gchar* path = g_build_filename(directory, "favorites-v1", nullptr);
    std::string contents;
    for (const std::string& key : state.favorite_keys) {
        gchar* encoded = g_base64_encode(
            reinterpret_cast<const guchar*>(key.data()), key.size());
        contents.append(encoded).push_back('\n');
        g_free(encoded);
    }
    if (g_file_set_contents(path, contents.data(), contents.size(), nullptr)) {
        g_chmod(path, 0600);
    }
    g_free(path);
    g_free(directory);
}

void apply_favorite_order(std::vector<Result>& results) {
    std::unordered_map<std::string, size_t> favorite_rank;
    favorite_rank.reserve(state.favorite_keys.size());
    for (size_t index = 0; index < state.favorite_keys.size(); ++index) {
        favorite_rank.emplace(state.favorite_keys[index], index);
    }
    for (Result& result : results) {
        result.pinned = favorite_rank.contains(favorite_key(result));
    }
    std::stable_sort(results.begin(), results.end(), [&](const Result& left, const Result& right) {
        const auto left_favorite = favorite_rank.find(favorite_key(left));
        const auto right_favorite = favorite_rank.find(favorite_key(right));
        const bool left_pinned = left_favorite != favorite_rank.end();
        const bool right_pinned = right_favorite != favorite_rank.end();
        if (left_pinned != right_pinned) return left_pinned;
        if (left_pinned) return left_favorite->second < right_favorite->second;
        return left.source_rank < right.source_rank;
    });
}

void rounded_rectangle(cairo_t* cr, double x, double y, double width,
                       double height, double radius) {
    const double degrees = G_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

std::string ellipsize_utf8(const std::string& text, glong maximum) {
    if (text.empty() || g_utf8_strlen(text.c_str(), -1) <= maximum) return text;
    const char* end = g_utf8_offset_to_pointer(text.c_str(), std::max<glong>(1, maximum - 1));
    return std::string(text.c_str(), end) + "…";
}

void invalidate_surfaces() {
    if (state.finished_surface) cairo_surface_destroy(state.finished_surface);
    state.finished_surface = nullptr;
    state.surface_scale = 0;
    state.texture_dirty = true;
}

void invalidate_finished() {
    if (state.finished_surface) cairo_surface_destroy(state.finished_surface);
    state.finished_surface = nullptr;
    state.texture_dirty = true;
}

GdkPixbuf* result_icon(const std::string& name) {
    const std::string key = name.empty() ? "application-x-executable" : name;
    const auto found = state.icons.find(key);
    if (found != state.icons.end()) return found->second;

    const int scale = state.window ? std::max(1, gtk_widget_get_scale_factor(state.window)) : 1;
    const std::vector<std::string> candidates{key, "application-x-executable", "applications-other"};
    GdkPixbuf* icon = nullptr;
    for (const auto& candidate : candidates) {
        if (!gtk_icon_theme_has_icon(state.icon_theme, candidate.c_str())) continue;
        GError* error = nullptr;
        icon = gtk_icon_theme_load_icon_for_scale(
            state.icon_theme, candidate.c_str(), 38, scale,
            static_cast<GtkIconLookupFlags>(GTK_ICON_LOOKUP_FORCE_SIZE |
                                            GTK_ICON_LOOKUP_FORCE_REGULAR),
            &error);
        if (error) g_error_free(error);
        if (icon) break;
    }
    state.icons[key] = icon;
    return icon;
}

void draw_icon(cairo_t* cr, const std::string& name, double x, double y, double size) {
    GdkPixbuf* icon = result_icon(name);
    if (icon) {
        const int width = gdk_pixbuf_get_width(icon);
        const int height = gdk_pixbuf_get_height(icon);
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, size / width, size / height);
        gdk_cairo_set_source_pixbuf(cr, icon, 0, 0);
        cairo_paint_with_alpha(cr, 0.94);
        cairo_restore(cr);
        return;
    }

    rounded_rectangle(cr, x, y, size, size, 8);
    cairo_set_source_rgba(cr, 0.30, 0.68, 0.47, 0.90);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.075, 0.043, 1.0);
    cairo_select_font_face(cr, "JetBrainsMono Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 19);
    cairo_move_to(cr, x + 12, y + 26);
    cairo_show_text(cr, "?");
}

std::size_t byte_offset_for_character(const std::string& text, int character) {
    if (character <= 0) return 0;
    const char* start = text.c_str();
    const char* position = g_utf8_offset_to_pointer(start, character);
    return std::min<std::size_t>(position - start, text.size());
}

void draw_layout(cairo_t* cr, const std::string& text, double x, double y,
                 const char* font, double red, double green, double blue,
                 const std::vector<int>* highlighted = nullptr) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* description = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, description);
    pango_layout_set_text(layout, text.c_str(), -1);

    if (highlighted && !highlighted->empty()) {
        PangoAttrList* attributes = pango_attr_list_new();
        for (const int position : *highlighted) {
            if (position < 0 || position >= g_utf8_strlen(text.c_str(), -1)) continue;
            const std::size_t start = byte_offset_for_character(text, position);
            const std::size_t end = byte_offset_for_character(text, position + 1);
            PangoAttribute* foreground = pango_attr_foreground_new(158 * 257, 232 * 257, 180 * 257);
            foreground->start_index = static_cast<guint>(start);
            foreground->end_index = static_cast<guint>(end);
            pango_attr_list_insert(attributes, foreground);
        }
        pango_layout_set_attributes(layout, attributes);
        pango_attr_list_unref(attributes);
    }

    cairo_set_source_rgba(cr, red, green, blue, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(description);
    g_object_unref(layout);
}

void draw_entry_contents(cairo_t* cr, const std::string& text) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* description =
        pango_font_description_from_string("JetBrainsMono Nerd Font SemiBold 15");
    pango_layout_set_font_description(layout, description);
    pango_layout_set_text(layout, text.c_str(), -1);

    const int cursor = gtk_editable_get_position(GTK_EDITABLE(state.entry));
    const std::size_t cursor_byte = byte_offset_for_character(text, cursor);
    PangoRectangle cursor_position{};
    pango_layout_index_to_pos(layout, static_cast<int>(cursor_byte), &cursor_position);
    const double cursor_layout_x = cursor_position.x / static_cast<double>(PANGO_SCALE);
    constexpr double text_x = 67.0;
    constexpr double text_y = 33.0;
    constexpr double available_width = 548.0;
    const double scroll_x = std::max(0.0, cursor_layout_x - available_width + 4.0);

    cairo_save(cr);
    cairo_rectangle(cr, text_x, 27.0, available_width, 31.0);
    cairo_clip(cr);

    gint selection_start = 0;
    gint selection_end = 0;
    if (gtk_editable_get_selection_bounds(GTK_EDITABLE(state.entry),
                                          &selection_start, &selection_end)) {
        const std::size_t start_byte = byte_offset_for_character(text, selection_start);
        const std::size_t end_byte = byte_offset_for_character(text, selection_end);
        PangoRectangle start_position{};
        PangoRectangle end_position{};
        pango_layout_index_to_pos(layout, static_cast<int>(start_byte), &start_position);
        pango_layout_index_to_pos(layout, static_cast<int>(end_byte), &end_position);
        const double start_x = text_x - scroll_x +
                               start_position.x / static_cast<double>(PANGO_SCALE);
        const double end_x = text_x - scroll_x +
                             end_position.x / static_cast<double>(PANGO_SCALE);
        cairo_rectangle(cr, std::min(start_x, end_x), 30.0,
                        std::max(1.0, std::abs(end_x - start_x)), 25.0);
        cairo_set_source_rgba(cr, 0.16, 0.48, 0.29, 0.92);
        cairo_fill(cr);
    }

    cairo_set_source_rgba(cr, 0.81, 0.89, 0.82, 1.0);
    cairo_move_to(cr, text_x - scroll_x, text_y);
    pango_cairo_show_layout(cr, layout);

    const double caret_x = text_x - scroll_x + cursor_layout_x;
    cairo_set_source_rgba(cr, 0.62, 0.91, 0.70, 0.92);
    cairo_rectangle(cr, caret_x, 31.0, 1.8, 23.0);
    cairo_fill(cr);
    cairo_restore(cr);

    pango_font_description_free(description);
    g_object_unref(layout);
}

void draw_search(cairo_t* cr) {
    rounded_rectangle(cr, kSearchX, kSearchY, kSearchWidth, kSearchHeight, 12);
    cairo_set_source_rgba(cr, 0.0, 0.075, 0.043, 0.975);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 0.31, 0.68, 0.47, 0.86);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0.46, 0.82, 0.57, 0.96);
    cairo_set_line_width(cr, 2.0);
    cairo_arc(cr, 40, 44, 9, 0, 2 * G_PI);
    cairo_stroke(cr);
    cairo_move_to(cr, 46.5, 50.5);
    cairo_line_to(cr, 53.5, 57.5);
    cairo_stroke(cr);

    const std::string query = state.entry ? gtk_entry_get_text(GTK_ENTRY(state.entry)) : "";
    if (query.empty()) {
        draw_layout(cr, "SEARCH THE VAULT", 67, 33,
                    "JetBrainsMono Nerd Font SemiBold 15", 0.46, 0.67, 0.52);
    } else {
        draw_entry_contents(cr, query);
    }

    draw_layout(cr, "KALWER", 67, 17, "JetBrainsMono Nerd Font Bold 7.5",
                0.30, 0.68, 0.47);
    const bool completing = !state.completion_candidates.empty();
    const std::string help = completing
        ? "TAB " + std::to_string(state.completion_index + 1) + "/" +
              std::to_string(state.completion_candidates.size()) + "   SHIFT+TAB BACK"
        : "> PTY   < JOBS   ? GOOGLE   ↑↓ SCROLL   ↵ GO";
    draw_layout(cr, help, completing ? 360 : 350, 60,
                "JetBrainsMono Nerd Font Medium 7.5", 0.46, 0.67, 0.52);
}

void draw_results(cairo_t* cr) {
    // This is part of the finished UI texture, not a separate compositor
    // surface, so the shader cuts the panel through the same dots as its rows.
    rounded_rectangle(cr, kSearchX, kResultsY - 6, kSearchWidth,
                      kWindowHeight - kResultsY - 4, 12);
    cairo_set_source_rgba(cr, 0.0, 0.075, 0.043, 0.88);
    cairo_fill(cr);

    const int visible_end = std::min<int>(
        static_cast<int>(state.results.size()), state.scroll_offset + kVisibleResults);
    for (int index = state.scroll_offset; index < visible_end; ++index) {
        const Result& result = state.results[index];
        const int display_row = index - state.scroll_offset;
        const double y = kResultsY + display_row * kRowPitch;
        const bool selected = index == state.selection;

        rounded_rectangle(cr, kResultX, y, kResultWidth, kRowHeight, 10);
        if (selected) {
            cairo_set_source_rgba(cr, 0.02, 0.20, 0.105, 0.97);
        } else {
            cairo_set_source_rgba(cr, 0.0, 0.105, 0.057, 0.90);
        }
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, 0.31, 0.68, 0.47, selected ? 0.40 : 0.20);
        cairo_stroke(cr);

        draw_icon(cr, result.icon, kResultX + 12, y + 10, 38);

        const std::string title = ellipsize_utf8(result.text, 48);
        const std::string subtext = ellipsize_utf8(
            result.subtext.empty() ? result.identifier : result.subtext, 68);
        const std::vector<int>* title_positions =
            result.fuzzy_field == "text" ? &result.fuzzy_positions : nullptr;
        const std::vector<int>* subtitle_positions =
            result.fuzzy_field == "subtext" ? &result.fuzzy_positions : nullptr;
        draw_layout(cr, title, kResultX + 62, y + 9,
                    "JetBrainsMono Nerd Font Bold 11.5", 0.81, 0.89, 0.82,
                    title_positions);
        draw_layout(cr, subtext, kResultX + 62, y + 32,
                    "JetBrainsMono Nerd Font Medium 8", 0.46, 0.67, 0.52,
                    subtitle_positions);

        const std::string rank = index < 9 ? "0" + std::to_string(index + 1)
                                           : std::to_string(index + 1);
        if (result.pinned) {
            draw_layout(cr, "★", kResultX + kResultWidth - 52, y + 20,
                        "JetBrainsMono Nerd Font Bold 9", 0.62, 0.91, 0.70);
        }
        draw_layout(cr, rank, kResultX + kResultWidth - 31, y + 21,
                    "JetBrainsMono Nerd Font Bold 8", 0.30, 0.68, 0.47);
    }

    if (state.results.empty()) {
        const char* message = state.query_failed ? "ELEPHANT IS UNAVAILABLE"
                              : state.query_pending ? "ASKING ELEPHANT…"
                                                    : "NO RESULTS";
        draw_layout(cr, message, 42, kResultsY + 16,
                    "JetBrainsMono Nerd Font Bold 8.5", 0.30, 0.68, 0.47);
    } else if (state.results.size() > kVisibleResults) {
        const double track_y = kResultsY + 5;
        const double track_height = kVisibleResults * kRowPitch - 14;
        const double fraction = static_cast<double>(kVisibleResults) / state.results.size();
        const double thumb_height = std::max(26.0, track_height * fraction);
        const int maximum_offset = static_cast<int>(state.results.size()) - kVisibleResults;
        const double thumb_y = track_y + (track_height - thumb_height) *
            (maximum_offset > 0 ? static_cast<double>(state.scroll_offset) / maximum_offset : 0.0);
        rounded_rectangle(cr, kSearchX + kSearchWidth - 7, track_y, 2.5, track_height, 1.25);
        cairo_set_source_rgba(cr, 0.30, 0.68, 0.47, 0.18);
        cairo_fill(cr);
        rounded_rectangle(cr, kSearchX + kSearchWidth - 7, thumb_y, 2.5, thumb_height, 1.25);
        cairo_set_source_rgba(cr, 0.46, 0.82, 0.57, 0.80);
        cairo_fill(cr);
    }
}

void ensure_surfaces(GtkWidget* widget) {
    const int scale = std::max(1, gtk_widget_get_scale_factor(widget));
    if (state.surface_scale != scale) {
        invalidate_surfaces();
        state.surface_scale = scale;
    }
    if (!state.finished_surface) {
        state.finished_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, kWindowWidth * scale, kWindowHeight * scale);
        cairo_surface_set_device_scale(state.finished_surface, scale, scale);
        cairo_t* cr = cairo_create(state.finished_surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        draw_search(cr);
        draw_results(cr);
        cairo_destroy(cr);
        cairo_surface_flush(state.finished_surface);
    }
}

GLuint compile_shader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(std::max(1, length));
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        g_warning("Elephant Field shader: %s", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool initialize_gl() {
    if (state.gl_program) return true;
    static const char* vertex_source = R"glsl(
        #version 330 core
        out vec2 uv;
        void main() {
            const vec2 positions[4] = vec2[4](
                vec2(-1.0, -1.0), vec2(1.0, -1.0),
                vec2(-1.0,  1.0), vec2(1.0,  1.0));
            const vec2 coordinates[4] = vec2[4](
                vec2(0.0, 1.0), vec2(1.0, 1.0),
                vec2(0.0, 0.0), vec2(1.0, 0.0));
            gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
            uv = coordinates[gl_VertexID];
        }
    )glsl";
    static const char* fragment_source = R"glsl(
        #version 330 core
        in vec2 uv;
        out vec4 color;
        uniform sampler2D ui_texture;
        uniform vec2 logical_size;
        uniform float opening;
        uniform float selection_y;
        uniform int has_results;

        const int bayer[64] = int[64](
             0,48,12,60, 3,51,15,63,
            32,16,44,28,35,19,47,31,
             8,56, 4,52,11,59, 7,55,
            40,24,36,20,43,27,39,23,
             2,50,14,62, 1,49,13,61,
            34,18,46,30,33,17,45,29,
            10,58, 6,54, 9,57, 5,53,
            42,26,38,22,41,25,37,21);

        float ease_out_cubic(float value) {
            float inverse = 1.0 - clamp(value, 0.0, 1.0);
            return 1.0 - inverse * inverse * inverse;
        }

        float rounded_box(vec2 point, vec2 center, vec2 half_size, float radius) {
            vec2 q = abs(point - center) - half_size + vec2(radius);
            return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
        }

        void main() {
            vec2 point = uv * logical_size;
            vec4 ui = texture(ui_texture, uv);

            if (has_results != 0) {
                vec2 center = vec2(325.0, selection_y + 29.0);
                float distance_to_box = rounded_box(
                    point, center, vec2(301.5, 30.5), 11.5);
                float inside = 1.0 - smoothstep(-0.2, 1.0, distance_to_box);
                float outline = 1.0 - smoothstep(0.75, 1.65, abs(distance_to_box));
                vec4 fill = vec4(0.31, 0.68, 0.47, 0.07 * inside);
                fill.rgb *= fill.a;
                ui = fill + ui * (1.0 - fill.a);
                vec4 border = vec4(0.46, 0.82, 0.57, 0.94 * outline);
                border.rgb *= border.a;
                ui = border + ui * (1.0 - border.a);
            }

            const float pitch = 8.0;
            float row = floor(point.y / pitch);
            float offset = mod(row, 2.0) * pitch * 0.5;
            float column = floor((point.x - offset) / pitch);
            vec2 center = vec2((column + 0.5) * pitch + offset,
                               (row + 0.5) * pitch);
            // Exempt the first five results without resetting the taper: row
            // six picks up the exact dot size and density it had when the
            // halftone curve began below row three.
            const float curve_origin = 286.0;
            const float halftone_start = 418.0;
            float depth = clamp((point.y - curve_origin) /
                                (logical_size.y - curve_origin), 0.0, 1.0);
            float tapered_depth = smoothstep(0.0, 1.0, depth);
            // Windshield-frit proportions: dot diameter performs most of the
            // taper. Smoothstep gives the solid-to-circle handoff a flat
            // tangent instead of an abrupt seam at the first halftone row.
            float radius = mix(5.80, 0.42, pow(tapered_depth, 0.92));
            // Keep the lattice complete around the handoff. Site density only
            // starts thinning farther down and removes a restrained 16% at
            // the bottom, with each site fading smoothly rather than popping.
            float density_depth = smoothstep(0.28, 1.0, depth);
            float density = 1.0 - 0.16 * pow(density_depth, 1.65);
            int bx = int(mod(column, 8.0));
            int by = int(mod(row, 8.0));
            float threshold = (float(bayer[by * 8 + bx]) + 0.5) / 64.0;
            float antialias = max(fwidth(length(point - center)), 0.65);
            // Density changes the radius of a site instead of multiplying its
            // entire grid cell. This preserves circular windows and prevents
            // the Bayer lattice from appearing as translucent square tiles.
            float site_visibility = density >= 0.999
                                        ? 1.0
                                        : 1.0 - smoothstep(
                                              density - 0.035,
                                              density + 0.035,
                                              threshold);
            float site_radius = radius * sqrt(max(site_visibility, 0.0));
            float dot = site_visibility < 0.01
                            ? 0.0
                            : 1.0 - smoothstep(
                                  site_radius - antialias,
                                  site_radius + antialias,
                                  length(point - center));
            float coverage = point.y < halftone_start ? 1.0 : dot;

            float reveal_front = 88.0 + ease_out_cubic(opening) *
                                 (logical_size.y - 86.0);
            float reveal = point.y < 88.0
                               ? 1.0
                               : 1.0 - smoothstep(reveal_front - 2.0,
                                                   reveal_front + 2.0, point.y);
            color = ui * (coverage * reveal);
        }
    )glsl";

    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    state.gl_program = glCreateProgram();
    glAttachShader(state.gl_program, vertex);
    glAttachShader(state.gl_program, fragment);
    glLinkProgram(state.gl_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(state.gl_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint length = 0;
        glGetProgramiv(state.gl_program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(std::max(1, length));
        glGetProgramInfoLog(state.gl_program, length, nullptr, log.data());
        g_warning("Elephant Field shader link: %s", log.data());
        glDeleteProgram(state.gl_program);
        state.gl_program = 0;
        return false;
    }

    glGenVertexArrays(1, &state.gl_vertex_array);
    glGenTextures(1, &state.gl_texture);
    glBindTexture(GL_TEXTURE_2D, state.gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    state.texture_dirty = true;
    return true;
}

gboolean on_render(GtkGLArea* area, GdkGLContext*, gpointer) {
    ensure_surfaces(GTK_WIDGET(area));
    if (gtk_gl_area_get_error(area) || !initialize_gl()) return TRUE;

    const int scale = std::max(1, gtk_widget_get_scale_factor(GTK_WIDGET(area)));
    const int pixel_width = kWindowWidth * scale;
    const int pixel_height = kWindowHeight * scale;
    glViewport(0, 0, gtk_widget_get_allocated_width(GTK_WIDGET(area)) * scale,
               gtk_widget_get_allocated_height(GTK_WIDGET(area)) * scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.gl_texture);
    if (state.texture_dirty) {
        cairo_surface_flush(state.finished_surface);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,
                      cairo_image_surface_get_stride(state.finished_surface) / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixel_width, pixel_height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE,
                     cairo_image_surface_get_data(state.finished_surface));
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        state.texture_dirty = false;
    }

    const double elapsed_ms = (g_get_monotonic_time() - state.opened_us) / 1000.0;
    const float progress = state.opening
                               ? static_cast<float>(clamp01(elapsed_ms / 360.0))
                               : 1.0f;
    glUseProgram(state.gl_program);
    glUniform1i(glGetUniformLocation(state.gl_program, "ui_texture"), 0);
    glUniform2f(glGetUniformLocation(state.gl_program, "logical_size"),
                kWindowWidth, kWindowHeight);
    glUniform1f(glGetUniformLocation(state.gl_program, "opening"), progress);
    glUniform1f(glGetUniformLocation(state.gl_program, "selection_y"),
                static_cast<float>(kResultsY +
                    (state.selection_visual - state.scroll_offset) * kRowPitch));
    glUniform1i(glGetUniformLocation(state.gl_program, "has_results"),
                state.results.empty() ? 0 : 1);
    glBindVertexArray(state.gl_vertex_array);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
    return TRUE;
}

void stop_query() {
    if (!state.query_process) return;
    if (!g_subprocess_get_if_exited(state.query_process)) {
        g_subprocess_force_exit(state.query_process);
    }
    g_clear_object(&state.query_process);
}

std::string json_string(JsonObject* object, const char* member) {
    if (!object || !json_object_has_member(object, member)) return {};
    const char* value = json_object_get_string_member(object, member);
    return value ? value : "";
}

std::vector<Result> parse_results(const char* output) {
    std::vector<Result> parsed;
    if (!output) return parsed;
    gchar** lines = g_strsplit(output, "\n", -1);
    for (gchar** line = lines; *line && parsed.size() < kQueryLimit; ++line) {
        if (!**line) continue;
        JsonParser* parser = json_parser_new();
        GError* error = nullptr;
        if (!json_parser_load_from_data(parser, *line, -1, &error)) {
            if (error) g_error_free(error);
            g_object_unref(parser);
            continue;
        }
        JsonNode* root = json_parser_get_root(parser);
        if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
            g_object_unref(parser);
            continue;
        }
        JsonObject* response = json_node_get_object(root);
        if (!json_object_has_member(response, "item")) {
            g_object_unref(parser);
            continue;
        }
        JsonObject* item = json_object_get_object_member(response, "item");
        Result result;
        result.identifier = json_string(item, "identifier");
        result.text = json_string(item, "text");
        result.subtext = json_string(item, "subtext");
        result.icon = json_string(item, "icon");
        result.provider = json_string(item, "provider");

        if (json_object_has_member(item, "actions")) {
            JsonArray* actions = json_object_get_array_member(item, "actions");
            for (guint index = 0; index < json_array_get_length(actions); ++index) {
                const char* action = json_array_get_string_element(actions, index);
                if (!action) continue;
                if (result.action.empty()) result.action = action;
                if (g_strcmp0(action, "pin") == 0 || g_strcmp0(action, "unpin") == 0) {
                    result.favorite_action = action;
                }
                if (g_strcmp0(action, "start") == 0 || g_strcmp0(action, "open") == 0) {
                    result.action = action;
                }
            }
        }
        if (result.action.empty()) result.action = "start";

        if (json_object_has_member(item, "state")) {
            JsonArray* item_state = json_object_get_array_member(item, "state");
            for (guint index = 0; index < json_array_get_length(item_state); ++index) {
                const char* value = json_array_get_string_element(item_state, index);
                if (g_strcmp0(value, "pinned") == 0) result.pinned = true;
            }
        }

        if (json_object_has_member(item, "fuzzyinfo")) {
            JsonObject* fuzzy = json_object_get_object_member(item, "fuzzyinfo");
            result.fuzzy_field = json_string(fuzzy, "field");
            if (json_object_has_member(fuzzy, "positions")) {
                JsonArray* positions = json_object_get_array_member(fuzzy, "positions");
                for (guint index = 0; index < json_array_get_length(positions); ++index) {
                    result.fuzzy_positions.push_back(
                        static_cast<int>(json_array_get_int_element(positions, index)));
                }
            }
        }
        if (!result.identifier.empty() && !result.text.empty() && !result.provider.empty()) {
            result.source_rank = static_cast<int>(parsed.size());
            parsed.push_back(std::move(result));
        }
        g_object_unref(parser);
    }
    g_strfreev(lines);
    return parsed;
}

void query_finished(GObject* source, GAsyncResult* async_result, gpointer data) {
    auto* context = static_cast<QueryContext*>(data);
    gchar* output = nullptr;
    gchar* error_output = nullptr;
    GError* error = nullptr;
    const gboolean ok = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), async_result, &output, &error_output, &error);

    if (context->serial == state.query_serial) {
        state.query_pending = false;
        state.query_failed = !ok;
        if (ok) {
            state.results = parse_results(output);
            apply_favorite_order(state.results);
            state.selection = 0;
            state.scroll_offset = 0;
            state.selection_visual = 0.0;
            invalidate_finished();
            if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
        }
        if (state.query_process == G_SUBPROCESS(source)) {
            g_clear_object(&state.query_process);
        }
    }
    if (error) g_error_free(error);
    g_free(output);
    g_free(error_output);
    delete context;
}

gboolean run_query(gpointer) {
    state.query_delay_source = 0;
    stop_query();
    ++state.query_serial;
    state.query_pending = true;
    state.query_failed = false;

    std::string query = gtk_entry_get_text(GTK_ENTRY(state.entry));
    std::replace(query.begin(), query.end(), ';', ' ');
    const std::string request = "desktopapplications;" + query + ";" +
                                std::to_string(kQueryLimit);
    GError* error = nullptr;
    state.query_process = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                      G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error, "elephant", "query", "--json", "--async=false",
        request.c_str(), nullptr);
    if (!state.query_process) {
        if (error) g_error_free(error);
        state.query_pending = false;
        state.query_failed = true;
        state.results.clear();
        invalidate_finished();
        gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
        return G_SOURCE_REMOVE;
    }
    auto* context = new QueryContext{state.query_serial};
    g_subprocess_communicate_utf8_async(state.query_process, nullptr, nullptr,
                                        query_finished, context);
    return G_SOURCE_REMOVE;
}

void schedule_query(guint delay_ms) {
    if (state.query_delay_source) g_source_remove(state.query_delay_source);
    state.query_delay_source = g_timeout_add(delay_ms, run_query, nullptr);
}

void hide_kalwer() {
    stop_query();
    if (state.query_delay_source) {
        g_source_remove(state.query_delay_source);
        state.query_delay_source = 0;
    }
    if (state.window && gtk_widget_get_visible(state.window)) {
        state.hidden_us = g_get_monotonic_time();
    }
    if (state.resident && state.window) {
        gtk_widget_hide(state.window);
    } else if (state.app) {
        g_application_quit(G_APPLICATION(state.app));
    }
}

CommandJob* find_job(const std::string& session) {
    const auto found = std::find_if(state.jobs.begin(), state.jobs.end(),
        [&](const CommandJob& job) { return job.session == session; });
    return found == state.jobs.end() ? nullptr : &*found;
}

bool tmux_session_exists(const std::string& session) {
    if (session.empty()) return false;
    gchar* argv[] = {
        const_cast<gchar*>("tmux"),
        const_cast<gchar*>("has-session"),
        const_cast<gchar*>("-t"),
        const_cast<gchar*>(session.c_str()),
        nullptr,
    };
    gint status = 1;
    GError* error = nullptr;
    const gboolean spawned = g_spawn_sync(
        nullptr, argv, nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                 G_SPAWN_STDOUT_TO_DEV_NULL |
                                 G_SPAWN_STDERR_TO_DEV_NULL),
        nullptr, nullptr, nullptr, nullptr, &status, &error);
    if (error) g_error_free(error);
    return spawned && g_spawn_check_wait_status(status, nullptr);
}

void kill_tmux_session(const std::string& session) {
    if (session.empty()) return;
    gchar* argv[] = {
        const_cast<gchar*>("tmux"),
        const_cast<gchar*>("kill-session"),
        const_cast<gchar*>("-t"),
        const_cast<gchar*>(session.c_str()),
        nullptr,
    };
    GError* error = nullptr;
    g_spawn_async(nullptr, argv, nullptr,
                  static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                           G_SPAWN_STDOUT_TO_DEV_NULL |
                                           G_SPAWN_STDERR_TO_DEV_NULL),
                  nullptr, nullptr, nullptr, &error);
    if (error) g_error_free(error);
}

std::string create_command_job(const std::string& command) {
    const std::string runtime_directory =
        std::string(g_get_user_runtime_dir()) + "/kalwer";
    if (g_mkdir_with_parents(runtime_directory.c_str(), 0700) != 0) return {};

    const std::string session = "kalwer-" + std::to_string(++state.command_serial) +
                                "-" + std::to_string(g_random_int());
    const std::string status_path = runtime_directory + "/" + session + ".status";
    const std::string background_path = runtime_directory + "/" + session + ".background";
    const std::string command_path = runtime_directory + "/" + session + ".command";
    g_file_set_contents(command_path.c_str(), command.c_str(), -1, nullptr);

    gchar* quoted_command = g_shell_quote(command.c_str());
    gchar* quoted_status = g_shell_quote(status_path.c_str());
    gchar* quoted_background = g_shell_quote(background_path.c_str());
    const std::string wrapper =
        "kalwer_command=" + std::string(quoted_command) + "; "
        "( eval -- \"$kalwer_command\" ); kalwer_status=$?; "
        "print -r -- $kalwer_status >| " + std::string(quoted_status) + "; "
        "if [[ -e " + std::string(quoted_background) + " ]]; then "
        "if (( kalwer_status == 0 )); then "
        "notify-send -a Kalwer -u low -i emblem-default "
        "'Kalwer command finished' \"$kalwer_command\\nexit 0\"; "
        "else notify-send -a Kalwer -u critical -i dialog-error "
        "'Kalwer command failed' \"$kalwer_command\\nexit $kalwer_status\"; fi; fi; "
        "sleep 2.2; exit $kalwer_status";
    g_free(quoted_command);
    g_free(quoted_status);
    g_free(quoted_background);

    gchar* argv[] = {
        const_cast<gchar*>("tmux"),
        const_cast<gchar*>("new-session"),
        const_cast<gchar*>("-d"),
        const_cast<gchar*>("-s"),
        const_cast<gchar*>(session.c_str()),
        const_cast<gchar*>("-c"),
        const_cast<gchar*>(g_get_home_dir()),
        const_cast<gchar*>("/usr/bin/zsh"),
        const_cast<gchar*>("-lc"),
        const_cast<gchar*>(wrapper.c_str()),
        nullptr,
    };
    gint status = 1;
    gchar* error_output = nullptr;
    GError* error = nullptr;
    const gboolean spawned = g_spawn_sync(
        nullptr, argv, nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                 G_SPAWN_STDOUT_TO_DEV_NULL),
        nullptr, nullptr, nullptr, &error_output, &status, &error);
    const bool okay = spawned && g_spawn_check_wait_status(status, nullptr);
    if (error) g_error_free(error);
    g_free(error_output);
    if (!okay) return {};

    gchar* option_argv[] = {
        const_cast<gchar*>("tmux"),
        const_cast<gchar*>("set-option"),
        const_cast<gchar*>("-t"),
        const_cast<gchar*>(session.c_str()),
        const_cast<gchar*>("status"),
        const_cast<gchar*>("off"),
        nullptr,
    };
    g_spawn_sync(nullptr, option_argv, nullptr,
                 static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                          G_SPAWN_STDOUT_TO_DEV_NULL |
                                          G_SPAWN_STDERR_TO_DEV_NULL),
                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    state.jobs.push_back({session, command, status_path, background_path, false});
    return session;
}

bool read_job_status(const std::string& session, int& exit_status) {
    CommandJob* job = find_job(session);
    if (!job) return false;
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(job->status_path.c_str(), &contents, &length, nullptr)) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(contents, &end, 10);
    const bool parsed = end && end != contents;
    g_free(contents);
    if (parsed) exit_status = static_cast<int>(value);
    return parsed;
}

void close_output_and_kalwer() {
    if (state.output_window) {
        gtk_widget_destroy(state.output_window);
    } else {
        hide_kalwer();
    }
}

void mark_output_interaction() {
    state.output_interacted = true;
    if (state.output_close_source) {
        g_source_remove(state.output_close_source);
        state.output_close_source = 0;
    }
    if (state.output_finished && state.output_status) {
        std::string status = "EXIT " + std::to_string(state.output_exit_status) + " · PIN";
        gtk_label_set_text(GTK_LABEL(state.output_status), status.c_str());
    }
}

gboolean close_output_timeout(gpointer);

void mark_output_finished(int exit_status) {
    if (state.output_finished) return;
    state.output_finished = true;
    state.output_exit_status = exit_status;
    if (state.output_status) {
        const std::string label = "EXIT " + std::to_string(exit_status) +
            (state.output_interacted ? " · PIN" : " · 2s");
        gtk_label_set_text(GTK_LABEL(state.output_status), label.c_str());
    }
    if (!state.output_interacted && !state.output_close_source) {
        state.output_close_source = g_timeout_add(2000, close_output_timeout, nullptr);
    }
}

gboolean poll_output_status(gpointer) {
    if (!state.output_window || state.output_session.empty()) {
        state.output_status_source = 0;
        return G_SOURCE_REMOVE;
    }
    int exit_status = 0;
    if (read_job_status(state.output_session, exit_status)) {
        mark_output_finished(exit_status);
        state.output_status_source = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

gboolean close_output_timeout(gpointer) {
    state.output_close_source = 0;
    if (!state.output_interacted) close_output_and_kalwer();
    return G_SOURCE_REMOVE;
}

gboolean on_output_draw(GtkWidget*, cairo_t* cr, gpointer) {
    const double elapsed = (g_get_monotonic_time() - state.output_opened_us) / 1000.0;
    const double line_progress = ease_out_cubic_cpu(elapsed / 220.0);
    const double detach_progress = ease_out_cubic_cpu((elapsed - 245.0) / 120.0);
    const double unfold_progress = ease_out_cubic_cpu((elapsed - 390.0) / 390.0);
    const double line_start = 18.0 * detach_progress;
    const double line_end = line_start + (kOutputWidth - 26.0) * line_progress;
    constexpr double hinge_y = 18.0;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (unfold_progress > 0.0) {
        const double height = 2.0 + (kOutputHeight - hinge_y - 8.0) * unfold_progress;
        cairo_rectangle(cr, line_start, hinge_y, kOutputWidth - line_start - 8.0,
                        height);
        cairo_set_source_rgba(cr, 0.0, 0.075, 0.043, 0.975);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 1.5);
        cairo_set_source_rgba(cr, 0.46, 0.82, 0.57, 0.96);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgba(cr, 0.46, 0.82, 0.57, 0.96);
    cairo_move_to(cr, line_start, hinge_y);
    cairo_line_to(cr, line_end, hinge_y);
    cairo_stroke(cr);
    return FALSE;
}

gboolean output_animation_tick(GtkWidget*, GdkFrameClock* clock, gpointer) {
    const double elapsed = (gdk_frame_clock_get_frame_time(clock) -
                            state.output_opened_us) / 1000.0;
    if (state.output_canvas) gtk_widget_queue_draw(state.output_canvas);
    if (state.output_content) {
        const double unfold = ease_out_cubic_cpu((elapsed - 390.0) / 390.0);
        const int content_height = std::max(
            1, static_cast<int>((kOutputHeight - 33.0) * unfold));
        gtk_widget_set_size_request(state.output_content, -1, content_height);
        gtk_widget_set_opacity(state.output_content, unfold > 0.0 ? 1.0 : 0.0);
    }
    if (elapsed < 820.0) return G_SOURCE_CONTINUE;

    state.output_animation_source = 0;
    if (state.output_content) gtk_widget_set_opacity(state.output_content, 1.0);
    if (state.output_terminal) gtk_widget_grab_focus(state.output_terminal);
    return G_SOURCE_REMOVE;
}

void copy_output(GtkButton*, gpointer);
void background_current_job(GtkButton*, gpointer);
void continue_in_ghostty(GtkButton*, gpointer);

gboolean on_output_key(GtkWidget*, GdkEventKey* event, gpointer) {
    if (event->keyval == GDK_KEY_Escape) {
        close_output_and_kalwer();
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
        if (event->keyval == GDK_KEY_B || event->keyval == GDK_KEY_b) {
            background_current_job(nullptr, nullptr);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_G || event->keyval == GDK_KEY_g) {
            continue_in_ghostty(nullptr, nullptr);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_C || event->keyval == GDK_KEY_c) {
            copy_output(nullptr, nullptr);
            return TRUE;
        }
    }
    mark_output_interaction();
    return FALSE;
}

gboolean on_output_pointer(GtkWidget*, GdkEvent*, gpointer) {
    mark_output_interaction();
    return FALSE;
}

void copy_output(GtkButton*, gpointer) {
    if (!state.output_terminal) return;
    mark_output_interaction();
    char* output = vte_terminal_get_text_format(
        VTE_TERMINAL(state.output_terminal), VTE_FORMAT_TEXT);
    if (!output) return;
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, output, -1);
    gtk_clipboard_store(clipboard);
    g_free(output);
    if (state.output_status) {
        gtk_label_set_text(GTK_LABEL(state.output_status), "COPIED · PIN");
    }
}

void output_child_exited(VteTerminal*, gint status, gpointer) {
    if (!state.output_window || state.output_finished) return;
    int command_status = 0;
    if (read_job_status(state.output_session, command_status)) {
        mark_output_finished(command_status);
    } else if (!tmux_session_exists(state.output_session)) {
        mark_output_finished(WIFEXITED(status) ? WEXITSTATUS(status) : status);
    }
}

void output_spawned(VteTerminal* terminal, GPid, GError* error, gpointer) {
    if (!error) return;
    const std::string message = std::string("kalwer: ") + error->message + "\r\n";
    vte_terminal_feed(terminal, message.c_str(), -1);
    if (state.output_status) {
        gtk_label_set_text(GTK_LABEL(state.output_status), "FAILED · 2s");
    }
    mark_output_finished(127);
}

void mark_current_job_background() {
    CommandJob* job = find_job(state.output_session);
    if (!job) return;
    g_file_set_contents(job->background_path.c_str(), "1\n", -1, nullptr);
    job->background = true;
}

void background_current_job(GtkButton*, gpointer) {
    if (state.output_session.empty()) return;
    mark_current_job_background();
    state.output_keep_session = true;
    gtk_widget_destroy(state.output_window);
}

void continue_in_ghostty(GtkButton*, gpointer) {
    if (state.output_session.empty()) return;
    mark_current_job_background();
    gchar* argv[] = {
        const_cast<gchar*>("ghostty"),
        const_cast<gchar*>("-e"),
        const_cast<gchar*>("tmux"),
        const_cast<gchar*>("attach-session"),
        const_cast<gchar*>("-t"),
        const_cast<gchar*>(state.output_session.c_str()),
        nullptr,
    };
    GError* error = nullptr;
    const gboolean spawned = g_spawn_async(
        g_get_home_dir(), argv, nullptr, G_SPAWN_SEARCH_PATH,
        nullptr, nullptr, nullptr, &error);
    if (error) g_error_free(error);
    if (!spawned) return;
    state.output_keep_session = true;
    gtk_widget_destroy(state.output_window);
}

void output_destroyed(GtkWidget*, gpointer) {
    if (state.output_animation_source && state.output_canvas) {
        gtk_widget_remove_tick_callback(state.output_canvas, state.output_animation_source);
    }
    if (state.output_close_source) g_source_remove(state.output_close_source);
    if (state.output_status_source) g_source_remove(state.output_status_source);
    const std::string session = state.output_session;
    const bool keep_session = state.output_keep_session;
    state.output_animation_source = 0;
    state.output_close_source = 0;
    state.output_status_source = 0;
    state.output_window = nullptr;
    state.output_canvas = nullptr;
    state.output_content = nullptr;
    state.output_terminal = nullptr;
    state.output_status = nullptr;
    state.output_session.clear();
    state.output_command.clear();
    state.output_keep_session = false;
    if (!keep_session && !session.empty()) kill_tmux_session(session);
    hide_kalwer();
}

void start_command_popup(const std::string& command,
                         const std::string& existing_session = {}) {
    if (command.empty() || state.output_window) return;
    const std::string session = existing_session.empty()
                                    ? create_command_job(command)
                                    : existing_session;
    if (session.empty() || !tmux_session_exists(session)) return;
    state.output_interacted = false;
    state.output_finished = false;
    state.output_exit_status = 0;
    state.output_session = session;
    state.output_command = command;
    state.output_keep_session = false;
    state.output_opened_us = g_get_monotonic_time();

    state.output_window = gtk_application_window_new(state.app);
    gtk_window_set_title(GTK_WINDOW(state.output_window), "Kalwer Command Output");
    gtk_window_set_default_size(GTK_WINDOW(state.output_window), kOutputWidth, kOutputHeight);
    gtk_window_set_resizable(GTK_WINDOW(state.output_window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(state.output_window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(state.output_window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(state.output_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(state.output_window), TRUE);
    gtk_widget_set_app_paintable(state.output_window, TRUE);
    if (GdkVisual* visual = gdk_screen_get_rgba_visual(
            gtk_widget_get_screen(state.output_window))) {
        gtk_widget_set_visual(state.output_window, visual);
    }

    GtkWidget* overlay = gtk_overlay_new();
    state.output_canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(state.output_canvas, kOutputWidth, kOutputHeight);
    gtk_container_add(GTK_CONTAINER(overlay), state.output_canvas);

    state.output_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(state.output_content, "kalwer-output-content");
    gtk_widget_set_halign(state.output_content, GTK_ALIGN_FILL);
    gtk_widget_set_valign(state.output_content, GTK_ALIGN_START);
    gtk_widget_set_margin_start(state.output_content, 19);
    gtk_widget_set_margin_end(state.output_content, 9);
    gtk_widget_set_margin_top(state.output_content, 24);
    gtk_widget_set_margin_bottom(state.output_content, 9);
    gtk_widget_set_opacity(state.output_content, 0.0);
    gtk_widget_set_size_request(state.output_content, -1, 1);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_widget_set_name(header, "kalwer-output-header");
    GtkWidget* title = gtk_label_new(ellipsize_utf8("> " + command, 24).c_str());
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(title), 13);
    gtk_widget_set_tooltip_text(title, command.c_str());
    state.output_status = gtk_label_new("RUNNING");
    GtkWidget* copy = gtk_button_new_with_label("COPY");
    GtkWidget* ghostty = gtk_button_new_with_label("GHOST");
    GtkWidget* background_button = gtk_button_new_with_label("BG");
    GtkWidget* close = gtk_button_new_with_label("×");
    gtk_widget_set_tooltip_text(copy, "Copy output (Ctrl+Shift+C)");
    gtk_widget_set_tooltip_text(ghostty, "Continue in Ghostty (Ctrl+Shift+G)");
    gtk_widget_set_tooltip_text(background_button, "Run in background (Ctrl+Shift+B)");
    gtk_style_context_add_class(gtk_widget_get_style_context(copy), "kalwer-output-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(ghostty), "kalwer-output-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(background_button), "kalwer-output-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "kalwer-output-button");
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header), state.output_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), copy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), ghostty, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), background_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), close, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state.output_content), header, FALSE, FALSE, 0);

    state.output_terminal = vte_terminal_new();
    PangoFontDescription* font =
        pango_font_description_from_string("JetBrainsMono Nerd Font 9");
    vte_terminal_set_font(VTE_TERMINAL(state.output_terminal), font);
    pango_font_description_free(font);
    const GdkRGBA foreground{0.81, 0.89, 0.82, 1.0};
    const GdkRGBA background{0.0, 0.075, 0.043, 1.0};
    vte_terminal_set_colors(VTE_TERMINAL(state.output_terminal),
                            &foreground, &background, nullptr, 0);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(state.output_terminal), 5000);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(state.output_terminal), TRUE);
    gtk_widget_set_hexpand(state.output_terminal, TRUE);
    gtk_widget_set_vexpand(state.output_terminal, TRUE);
    gtk_box_pack_start(GTK_BOX(state.output_content), state.output_terminal, TRUE, TRUE, 0);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), state.output_content);
    gtk_container_add(GTK_CONTAINER(state.output_window), overlay);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#kalwer-output-content { background: #00130b; border-radius: 0; }"
        "#kalwer-output-header { min-height: 29px; padding: 3px 4px; color: #cfe3d2; "
        "font-family: 'JetBrainsMono Nerd Font'; font-size: 8px; font-weight: bold; }"
        ".kalwer-output-button { min-width: 24px; min-height: 20px; padding: 0 5px; "
        "background: #002e18; color: #9ee8b4; border: 1px solid #75d191; "
        "border-radius: 5px; box-shadow: none; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gtk_widget_get_screen(state.output_window), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    g_signal_connect(state.output_canvas, "draw", G_CALLBACK(on_output_draw), nullptr);
    g_signal_connect(state.output_window, "key-press-event", G_CALLBACK(on_output_key), nullptr);
    g_signal_connect(state.output_terminal, "button-press-event", G_CALLBACK(on_output_pointer), nullptr);
    g_signal_connect(state.output_terminal, "scroll-event", G_CALLBACK(on_output_pointer), nullptr);
    g_signal_connect(state.output_terminal, "child-exited", G_CALLBACK(output_child_exited), nullptr);
    g_signal_connect(copy, "clicked", G_CALLBACK(copy_output), nullptr);
    g_signal_connect(ghostty, "clicked", G_CALLBACK(continue_in_ghostty), nullptr);
    g_signal_connect(background_button, "clicked", G_CALLBACK(background_current_job), nullptr);
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        close_output_and_kalwer();
    }), nullptr);
    g_signal_connect(state.output_window, "destroy", G_CALLBACK(output_destroyed), nullptr);

    gtk_widget_show_all(state.output_window);
    gtk_window_present(GTK_WINDOW(state.output_window));
    state.output_animation_source = gtk_widget_add_tick_callback(
        state.output_canvas, output_animation_tick, nullptr, nullptr);
    state.output_status_source = g_timeout_add(100, poll_output_status, nullptr);

    gchar* argv[] = {
        const_cast<gchar*>("tmux"),
        const_cast<gchar*>("attach-session"),
        const_cast<gchar*>("-t"),
        const_cast<gchar*>(state.output_session.c_str()),
        nullptr,
    };
    vte_terminal_spawn_async(
        VTE_TERMINAL(state.output_terminal), VTE_PTY_DEFAULT, g_get_home_dir(), argv,
        nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, -1, nullptr,
        output_spawned, nullptr);
}

void dismiss_popup() {
    if (state.output_window) close_output_and_kalwer();
    else hide_kalwer();
}

std::string clean_field(std::string value) {
    std::replace(value.begin(), value.end(), ';', ' ');
    return value;
}

int most_recent_firefox_workspace() {
    gchar* output = nullptr;
    gchar* error_output = nullptr;
    gint exit_status = 0;
    gchar* argv[] = {
        const_cast<gchar*>("hyprctl"),
        const_cast<gchar*>("-j"),
        const_cast<gchar*>("clients"),
        nullptr,
    };
    GError* error = nullptr;
    const gboolean spawned = g_spawn_sync(
        nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
        &output, &error_output, &exit_status, &error);
    if (!spawned || exit_status != 0 || !output) {
        if (error) g_error_free(error);
        g_free(output);
        g_free(error_output);
        return -1;
    }

    int best_workspace = -1;
    gint64 best_history = G_MAXINT64;
    JsonParser* parser = json_parser_new();
    if (json_parser_load_from_data(parser, output, -1, &error)) {
        JsonNode* root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_ARRAY(root)) {
            JsonArray* clients = json_node_get_array(root);
            for (guint index = 0; index < json_array_get_length(clients); ++index) {
                JsonObject* client = json_array_get_object_element(clients, index);
                const std::string window_class = ascii_lower(json_string(client, "class"));
                const std::string initial_class = ascii_lower(json_string(client, "initialClass"));
                if (window_class.find("firefox") == std::string::npos &&
                    initial_class.find("firefox") == std::string::npos) continue;
                if (!json_object_has_member(client, "workspace")) continue;
                JsonObject* workspace = json_object_get_object_member(client, "workspace");
                if (!workspace || !json_object_has_member(workspace, "id")) continue;
                const int workspace_id =
                    static_cast<int>(json_object_get_int_member(workspace, "id"));
                const gint64 history = json_object_has_member(client, "focusHistoryID")
                                            ? json_object_get_int_member(client, "focusHistoryID")
                                            : G_MAXINT32;
                if (workspace_id > 0 && history < best_history) {
                    best_history = history;
                    best_workspace = workspace_id;
                }
            }
        }
    }
    if (error) g_error_free(error);
    g_object_unref(parser);
    g_free(output);
    g_free(error_output);
    return best_workspace;
}

void open_google_in_firefox(const std::string& raw_query) {
    const std::string query = trim_copy(raw_query);
    const int workspace = most_recent_firefox_workspace();
    if (workspace > 0) {
        const std::string workspace_text = std::to_string(workspace);
        gchar* workspace_argv[] = {
            const_cast<gchar*>("hyprctl"),
            const_cast<gchar*>("dispatch"),
            const_cast<gchar*>("workspace"),
            const_cast<gchar*>(workspace_text.c_str()),
            nullptr,
        };
        GError* error = nullptr;
        g_spawn_sync(nullptr, workspace_argv, nullptr, G_SPAWN_SEARCH_PATH,
                     nullptr, nullptr, nullptr, nullptr, nullptr, &error);
        if (error) g_error_free(error);
    }

    gchar* escaped = g_uri_escape_string(query.c_str(), nullptr, TRUE);
    const std::string url = query.empty()
                                ? "https://www.google.com/"
                                : "https://www.google.com/search?q=" +
                                      std::string(escaped ? escaped : "");
    g_free(escaped);
    gchar* firefox_argv[] = {
        const_cast<gchar*>("firefox"),
        const_cast<gchar*>("--new-tab"),
        const_cast<gchar*>(url.c_str()),
        nullptr,
    };
    GError* error = nullptr;
    g_spawn_async(nullptr, firefox_argv, nullptr, G_SPAWN_SEARCH_PATH,
                  nullptr, nullptr, nullptr, &error);
    if (error) g_error_free(error);
    hide_kalwer();
}

class ExpressionParser {
public:
    explicit ExpressionParser(const std::string& source) : source_(source) {}

    bool parse(double& value) {
        position_ = 0;
        valid_ = true;
        saw_binary_operator_ = false;
        value = expression();
        whitespace();
        return valid_ && position_ == source_.size() && saw_binary_operator_ &&
               std::isfinite(value);
    }

private:
    void whitespace() {
        while (position_ < source_.size() &&
               g_ascii_isspace(static_cast<guchar>(source_[position_]))) {
            ++position_;
        }
    }

    bool take(char character) {
        whitespace();
        if (position_ >= source_.size() || source_[position_] != character) return false;
        ++position_;
        return true;
    }

    double expression() {
        double value = term();
        while (valid_) {
            if (take('+')) {
                saw_binary_operator_ = true;
                value += term();
            } else if (take('-')) {
                saw_binary_operator_ = true;
                value -= term();
            } else {
                break;
            }
        }
        return value;
    }

    double term() {
        double value = unary();
        while (valid_) {
            if (take('*')) {
                saw_binary_operator_ = true;
                value *= unary();
            } else if (take('/')) {
                saw_binary_operator_ = true;
                const double divisor = unary();
                if (std::abs(divisor) < 1e-15) valid_ = false;
                else value /= divisor;
            } else if (take('%')) {
                saw_binary_operator_ = true;
                value = (value / 100.0) * unary();
            } else {
                break;
            }
        }
        return value;
    }

    double unary() {
        if (take('+')) return unary();
        if (take('-')) return -unary();
        return power();
    }

    double power() {
        double value = primary();
        if (take('^')) {
            saw_binary_operator_ = true;
            value = std::pow(value, unary());
        }
        return value;
    }

    double primary() {
        whitespace();
        bool square_root = false;
        if (source_.compare(position_, 4, "sqrt") == 0) {
            position_ += 4;
            square_root = true;
        } else if (source_.compare(position_, std::string("√").size(), "√") == 0) {
            position_ += std::string("√").size();
            square_root = true;
        }
        if (square_root) {
            saw_binary_operator_ = true;
            whitespace();
            double value = 0.0;
            if (take('(')) {
                value = expression();
                if (!take(')')) valid_ = false;
            } else {
                value = unary();
            }
            if (value < 0.0) {
                valid_ = false;
                return 0.0;
            }
            return std::sqrt(value);
        }
        if (take('(')) {
            const double value = expression();
            if (!take(')')) valid_ = false;
            return value;
        }
        if (position_ >= source_.size()) {
            valid_ = false;
            return 0.0;
        }
        char* end = nullptr;
        const char* start = source_.c_str() + position_;
        const double value = std::strtod(start, &end);
        if (!end || end == start) {
            valid_ = false;
            return 0.0;
        }
        position_ += static_cast<std::size_t>(end - start);
        return value;
    }

    const std::string& source_;
    std::size_t position_ = 0;
    bool valid_ = true;
    bool saw_binary_operator_ = false;
};

std::string format_number(double value) {
    if (std::abs(value) < 5e-14) value = 0.0;
    std::ostringstream stream;
    stream << std::setprecision(12) << std::defaultfloat << value;
    return stream.str();
}

bool simple_integer_fraction(const std::string& source, long long& numerator,
                             long long& denominator) {
    const std::size_t slash = source.find('/');
    if (slash == std::string::npos || source.find('/', slash + 1) != std::string::npos) {
        return false;
    }
    const std::string left = trim_copy(source.substr(0, slash));
    const std::string right = trim_copy(source.substr(slash + 1));
    if (left.empty() || right.empty()) return false;
    char* left_end = nullptr;
    char* right_end = nullptr;
    numerator = std::strtoll(left.c_str(), &left_end, 10);
    denominator = std::strtoll(right.c_str(), &right_end, 10);
    return left_end && *left_end == '\0' && right_end && *right_end == '\0' &&
           denominator != 0;
}

bool approximate_fraction(double value, long long& numerator, long long& denominator) {
    constexpr long long maximum_denominator = 100000;
    const bool negative = value < 0.0;
    const double target = std::abs(value);
    double remaining = target;
    long long numerator_previous_two = 0;
    long long numerator_previous = 1;
    long long denominator_previous_two = 1;
    long long denominator_previous = 0;
    long long best_numerator = 0;
    long long best_denominator = 1;

    for (int iteration = 0; iteration < 32; ++iteration) {
        const double integral = std::floor(remaining);
        if (integral > static_cast<double>(std::numeric_limits<long long>::max() / 4)) {
            break;
        }
        const long long coefficient = static_cast<long long>(integral);
        if (numerator_previous != 0 && coefficient >
                (std::numeric_limits<long long>::max() - numerator_previous_two) /
                    std::abs(numerator_previous)) break;
        if (denominator_previous != 0 && coefficient >
                (std::numeric_limits<long long>::max() - denominator_previous_two) /
                    std::abs(denominator_previous)) break;
        const long long next_numerator = coefficient * numerator_previous +
                                         numerator_previous_two;
        const long long next_denominator = coefficient * denominator_previous +
                                           denominator_previous_two;
        if (next_denominator <= 0 || next_denominator > maximum_denominator) break;
        best_numerator = next_numerator;
        best_denominator = next_denominator;
        if (std::abs(static_cast<double>(best_numerator) / best_denominator - target) <
            1e-10) break;
        const double fractional = remaining - integral;
        if (fractional < 1e-14) break;
        remaining = 1.0 / fractional;
        numerator_previous_two = numerator_previous;
        numerator_previous = next_numerator;
        denominator_previous_two = denominator_previous;
        denominator_previous = next_denominator;
    }
    if (best_denominator <= 0) return false;
    numerator = negative ? -best_numerator : best_numerator;
    denominator = best_denominator;
    return true;
}

Result calculator_result(const std::string& value, const std::string& label) {
    Result result;
    result.identifier = value;
    result.text = value;
    result.subtext = label;
    result.icon = "accessories-calculator";
    result.provider = "kalwer-calculator";
    result.action = "copy";
    return result;
}

std::vector<Result> calculator_results(const std::string& input) {
    double value = 0.0;
    ExpressionParser parser(input);
    if (!parser.parse(value)) return {};

    std::vector<Result> results;
    if (std::abs(value - std::round(value)) > 1e-11) {
        long long numerator = 0;
        long long denominator = 0;
        long long source_numerator = 0;
        long long source_denominator = 0;
        if (simple_integer_fraction(input, source_numerator, source_denominator)) {
            const long long divisor = std::gcd(source_numerator, source_denominator);
            numerator = source_numerator / divisor;
            denominator = source_denominator / divisor;
            if (denominator < 0) {
                denominator = -denominator;
                numerator = -numerator;
            }
        } else {
            approximate_fraction(value, numerator, denominator);
        }
        if (denominator > 0) {
            const std::string fraction = std::to_string(numerator) + "/" +
                                         std::to_string(denominator);
            results.push_back(calculator_result(fraction, "REDUCED FRACTION"));
            if (std::llabs(numerator) > denominator) {
                const long long whole = numerator / denominator;
                const long long remainder = std::llabs(numerator % denominator);
                if (remainder != 0) {
                    const std::string mixed = std::to_string(whole) + " " +
                                              std::to_string(remainder) + "/" +
                                              std::to_string(denominator);
                    results.push_back(calculator_result(mixed, "MIXED NUMBER"));
                }
            }
        }
        results.insert(results.begin(), calculator_result(format_number(value), "DECIMAL"));
        results.push_back(calculator_result(format_number(value * 100.0) + "%", "PERCENT"));
    } else {
        const std::string result = format_number(value);
        results.push_back(calculator_result(result, "CALCULATED RESULT · ENTER TO COPY"));
    }
    return results;
}

std::vector<Result> background_job_results(const std::string& filter) {
    std::vector<Result> results;
    const std::string lowered_filter = ascii_lower(trim_copy(filter));
    for (const CommandJob& job : state.jobs) {
        if (!job.background || !tmux_session_exists(job.session)) continue;
        int exit_status = 0;
        if (read_job_status(job.session, exit_status)) continue;
        if (!lowered_filter.empty() &&
            ascii_lower(job.command).find(lowered_filter) == std::string::npos) continue;
        Result result;
        result.identifier = job.session;
        result.text = "BACKGROUND · RUNNING";
        result.subtext = job.command;
        result.icon = "utilities-terminal";
        result.provider = "kalwer-background";
        result.action = "attach";
        results.push_back(std::move(result));
    }
    return results;
}

void show_local_results(std::vector<Result> results) {
    if (state.query_delay_source) {
        g_source_remove(state.query_delay_source);
        state.query_delay_source = 0;
    }
    stop_query();
    ++state.query_serial;
    state.results = std::move(results);
    state.selection = 0;
    state.scroll_offset = 0;
    state.selection_visual = 0.0;
    state.query_pending = false;
    state.query_failed = false;
    invalidate_finished();
    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
}

void activate_selection() {
    if (state.selection < 0 || state.selection >= static_cast<int>(state.results.size())) return;
    const Result result = state.results[state.selection];
    if (result.provider == "kalwer-command") {
        start_command_popup(trim_copy(result.identifier));
        return;
    }
    if (result.provider == "kalwer-google") {
        open_google_in_firefox(result.identifier);
        return;
    }
    if (result.provider == "kalwer-background") {
        CommandJob* job = find_job(result.identifier);
        if (job) start_command_popup(job->command, job->session);
        return;
    }
    if (result.provider == "kalwer-calculator") {
        GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, result.identifier.c_str(), -1);
        gtk_clipboard_store(clipboard);
        hide_kalwer();
        return;
    }
    const std::string query = clean_field(gtk_entry_get_text(GTK_ENTRY(state.entry)));
    const std::string request = clean_field(result.provider) + ";" +
                                clean_field(result.identifier) + ";" +
                                clean_field(result.action) + ";" + query + ";";
    GError* error = nullptr;
    gchar* argv[] = {
        const_cast<gchar*>("elephant"),
        const_cast<gchar*>("activate"),
        const_cast<gchar*>(request.c_str()),
        nullptr,
    };
    g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH,
                  nullptr, nullptr, nullptr, &error);
    if (error) g_error_free(error);
    dismiss_popup();
}

void toggle_favorite() {
    if (state.selection < 0 || state.selection >= static_cast<int>(state.results.size())) return;
    const Result chosen = state.results[state.selection];
    if (chosen.provider.rfind("kalwer-", 0) == 0) return;
    const std::string key = favorite_key(chosen);
    const auto existing = std::find(
        state.favorite_keys.begin(), state.favorite_keys.end(), key);
    if (existing == state.favorite_keys.end()) {
        state.favorite_keys.insert(state.favorite_keys.begin(), key);
    } else {
        state.favorite_keys.erase(existing);
    }
    save_favorites();
    apply_favorite_order(state.results);
    const auto selected = std::find_if(
        state.results.begin(), state.results.end(), [&](const Result& result) {
            return favorite_key(result) == key;
        });
    state.selection = selected == state.results.end()
                          ? 0
                          : static_cast<int>(selected - state.results.begin());
    state.scroll_offset = std::clamp(
        state.selection - kSelectableResults + 1, 0,
        std::max(0, static_cast<int>(state.results.size()) - kSelectableResults));
    if (state.selection < kSelectableResults) state.scroll_offset = 0;
    state.selection_visual = state.selection;
    invalidate_finished();
    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
}

void move_selection(int delta) {
    if (state.results.empty()) return;
    const int count = static_cast<int>(state.results.size());
    state.selection = std::clamp(state.selection + delta, 0, count - 1);
    if (state.selection < state.scroll_offset) {
        state.scroll_offset = state.selection;
    } else if (state.selection >= state.scroll_offset + kSelectableResults) {
        state.scroll_offset = state.selection - kSelectableResults + 1;
    }
    if (state.selection_visual < state.scroll_offset) {
        state.selection_visual = state.scroll_offset;
    } else if (state.selection_visual > state.scroll_offset + kSelectableResults - 1) {
        state.selection_visual = state.scroll_offset + kSelectableResults - 1;
    }
    invalidate_finished();
    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
}

gboolean animation_tick(GtkWidget*, GdkFrameClock* clock, gpointer) {
    const gint64 now = gdk_frame_clock_get_frame_time(clock);
    const double elapsed_ms = state.animation_last_us
                                  ? (now - state.animation_last_us) / 1000.0
                                  : 16.0;
    state.animation_last_us = now;
    bool continue_animation = false;

    if (state.opening) {
        if ((now - state.opened_us) / 1000.0 >= 360.0) {
            state.opening = false;
        } else {
            continue_animation = true;
        }
    }

    const double target = static_cast<double>(state.selection);
    const double difference = target - state.selection_visual;
    if (std::abs(difference) > 0.002) {
        state.selection_visual += difference * (1.0 - std::exp(-elapsed_ms / 52.0));
        continue_animation = true;
    } else {
        state.selection_visual = target;
    }

    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
    if (!continue_animation) {
        state.animation_source = 0;
        state.animation_last_us = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void ensure_animation() {
    if (state.animation_source || !state.canvas) return;
    state.animation_last_us = g_get_monotonic_time();
    state.animation_source = gtk_widget_add_tick_callback(
        state.canvas, animation_tick, nullptr, nullptr);
}

void clear_completion() {
    state.completion_candidates.clear();
    state.completion_source.clear();
    state.completion_index = -1;
    state.completion_start = 0;
    state.completion_end = 0;
}

std::vector<std::string> zsh_completions(const std::string& prefix,
                                         bool command_position) {
    if (prefix.empty()) return {};
    static constexpr const char* script =
        "p=$1; mode=$2; "
        "if [[ $mode == command ]]; then "
        "typeset -a matches; "
        "matches=( ${(ok)commands[(I)${p}*]} ${(ok)builtins[(I)${p}*]} ); "
        "print -rl -- ${(u)matches}; "
        "else "
        "pattern=${p}*; matches=( ${~pattern}(N) ); "
        "for match in $matches; do "
        "[[ -d $match ]] && match+=/; print -r -- ${(q)match}; "
        "done; fi";
    gchar* output = nullptr;
    gchar* error_output = nullptr;
    gint status = 0;
    gchar* argv[] = {
        const_cast<gchar*>("/usr/bin/zsh"),
        const_cast<gchar*>("-fc"),
        const_cast<gchar*>(script),
        const_cast<gchar*>("--"),
        const_cast<gchar*>(prefix.c_str()),
        const_cast<gchar*>(command_position ? "command" : "path"),
        nullptr,
    };
    GError* error = nullptr;
    const gboolean spawned = g_spawn_sync(
        g_get_home_dir(), argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
        &output, &error_output, &status, &error);
    std::vector<std::string> candidates;
    if (spawned && status == 0 && output) {
        gchar** lines = g_strsplit(output, "\n", 0);
        for (int index = 0; lines[index] && candidates.size() < 256; ++index) {
            std::string candidate = lines[index];
            if (candidate.empty()) continue;
            if (command_position || candidate.back() != '/') candidate += ' ';
            if (std::find(candidates.begin(), candidates.end(), candidate) ==
                candidates.end()) {
                candidates.push_back(std::move(candidate));
            }
        }
        g_strfreev(lines);
    }
    if (error) g_error_free(error);
    g_free(output);
    g_free(error_output);
    return candidates;
}

bool complete_command(bool backwards) {
    if (!state.entry) return false;
    const std::string input = gtk_entry_get_text(GTK_ENTRY(state.entry));
    if (input.empty() || input.front() != '>') return false;

    if (state.completion_candidates.empty()) {
        const int cursor_character = gtk_editable_get_position(GTK_EDITABLE(state.entry));
        const std::size_t cursor_byte = byte_offset_for_character(input, cursor_character);
        std::size_t start_byte = cursor_byte;
        while (start_byte > 1 &&
               !g_ascii_isspace(static_cast<guchar>(input[start_byte - 1]))) {
            --start_byte;
        }
        std::size_t end_byte = cursor_byte;
        while (end_byte < input.size() &&
               !g_ascii_isspace(static_cast<guchar>(input[end_byte]))) {
            ++end_byte;
        }
        const std::string before = input.substr(1, start_byte > 1 ? start_byte - 1 : 0);
        const bool command_position = trim_copy(before).empty();
        const std::string prefix = input.substr(start_byte, cursor_byte - start_byte);
        state.completion_candidates = zsh_completions(prefix, command_position);
        if (state.completion_candidates.empty()) return false;
        state.completion_source = input;
        state.completion_start = static_cast<int>(
            g_utf8_strlen(input.c_str(), static_cast<gssize>(start_byte)));
        state.completion_end = static_cast<int>(
            g_utf8_strlen(input.c_str(), static_cast<gssize>(end_byte)));
        state.completion_index = backwards
                                     ? static_cast<int>(state.completion_candidates.size()) - 1
                                     : 0;
    } else {
        const int count = static_cast<int>(state.completion_candidates.size());
        state.completion_index = (state.completion_index + (backwards ? -1 : 1) + count) % count;
    }

    const std::string replacement = state.completion_candidates[state.completion_index];
    state.applying_completion = true;
    gtk_editable_delete_text(GTK_EDITABLE(state.entry),
                             state.completion_start, state.completion_end);
    gint position = state.completion_start;
    gtk_editable_insert_text(GTK_EDITABLE(state.entry), replacement.c_str(),
                             static_cast<gint>(replacement.size()), &position);
    state.completion_end = position;
    gtk_editable_set_position(GTK_EDITABLE(state.entry), position);
    state.applying_completion = false;
    invalidate_finished();
    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
    return true;
}

gboolean on_entry_key(GtkWidget*, GdkEventKey* event, gpointer) {
    switch (event->keyval) {
        case GDK_KEY_Escape:
            dismiss_popup();
            return TRUE;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            move_selection(-1);
            ensure_animation();
            return TRUE;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            move_selection(1);
            ensure_animation();
            return TRUE;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            move_selection(-kSelectableResults);
            ensure_animation();
            return TRUE;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            move_selection(kSelectableResults);
            ensure_animation();
            return TRUE;
        case GDK_KEY_Tab:
        case GDK_KEY_ISO_Left_Tab:
            complete_command((event->state & GDK_SHIFT_MASK) != 0 ||
                             event->keyval == GDK_KEY_ISO_Left_Tab);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (event->state & GDK_SHIFT_MASK) toggle_favorite();
            else activate_selection();
            return TRUE;
        case GDK_KEY_p:
        case GDK_KEY_P:
            if (event->state & GDK_CONTROL_MASK) {
                move_selection(-1);
                ensure_animation();
                return TRUE;
            }
            break;
        case GDK_KEY_n:
        case GDK_KEY_N:
            if (event->state & GDK_CONTROL_MASK) {
                move_selection(1);
                ensure_animation();
                return TRUE;
            }
            break;
        default:
            break;
    }
    return FALSE;
}

void on_entry_changed(GtkEditable*, gpointer) {
    if (!state.applying_completion) clear_completion();
    const std::string input = gtk_entry_get_text(GTK_ENTRY(state.entry));
    if (!input.empty() && input.front() == '<') {
        show_local_results(background_job_results(input.substr(1)));
        return;
    }
    if (!input.empty() && (input.front() == '>' || input.front() == '?')) {
        const bool command_mode = input.front() == '>';
        const std::string payload = trim_copy(input.substr(1));
        Result action;
        action.identifier = payload;
        action.text = command_mode ? "RUN IN INTERACTIVE SHELL"
                                   : "SEARCH GOOGLE IN FIREFOX";
        action.subtext = payload.empty()
                             ? (command_mode ? "Type a command after >"
                                             : "Type a search after ?")
                             : payload;
        action.icon = command_mode ? "utilities-terminal" : "firefox";
        action.provider = command_mode ? "kalwer-command" : "kalwer-google";
        action.action = "start";
        show_local_results({std::move(action)});
        return;
    }
    std::vector<Result> calculation = calculator_results(input);
    if (!calculation.empty()) {
        show_local_results(std::move(calculation));
        return;
    }
    invalidate_finished();
    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
    schedule_query(24);
}

void on_entry_cursor_changed(GObject*, GParamSpec*, gpointer) {
    invalidate_finished();
    if (state.canvas) gtk_gl_area_queue_render(GTK_GL_AREA(state.canvas));
}

int pointer_result(double x, double y) {
    if (x < kResultX || x > kResultX + kResultWidth || y < kResultsY) return -1;
    const int row = static_cast<int>((y - kResultsY) / kRowPitch);
    if (row < 0 || row >= kSelectableResults) return -1;
    const double local_y = y - (kResultsY + row * kRowPitch);
    const int result = state.scroll_offset + row;
    if (result >= static_cast<int>(state.results.size())) return -1;
    return local_y <= kRowHeight ? result : -1;
}

gboolean on_motion(GtkWidget*, GdkEventMotion* event, gpointer) {
    const int row = pointer_result(event->x, event->y);
    if (row >= 0 && row != state.selection) {
        state.selection = row;
        invalidate_finished();
        ensure_animation();
    }
    return TRUE;
}

gboolean on_button(GtkWidget*, GdkEventButton* event, gpointer) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    const int row = pointer_result(event->x, event->y);
    if (row < 0) return FALSE;
    state.selection = row;
    activate_selection();
    return TRUE;
}

gboolean focus_kalwer(gpointer) {
    if (!state.window || !gtk_widget_get_visible(state.window) || state.output_window) {
        return G_SOURCE_REMOVE;
    }
    const char* selector = "title:^Kalwer$";
    gchar* argv[] = {
        const_cast<gchar*>("hyprctl"),
        const_cast<gchar*>("dispatch"),
        const_cast<gchar*>("focuswindow"),
        const_cast<gchar*>(selector),
        nullptr,
    };
    GError* error = nullptr;
    g_spawn_async(nullptr, argv, nullptr,
                  static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                           G_SPAWN_STDOUT_TO_DEV_NULL |
                                           G_SPAWN_STDERR_TO_DEV_NULL),
                  nullptr, nullptr, nullptr, &error);
    if (error) g_error_free(error);
    gtk_window_present(GTK_WINDOW(state.window));
    gtk_widget_grab_focus(state.entry);
    return G_SOURCE_REMOVE;
}

gboolean on_scroll(GtkWidget*, GdkEventScroll* event, gpointer) {
    int steps = 0;
    if (event->direction == GDK_SCROLL_UP) steps = -1;
    else if (event->direction == GDK_SCROLL_DOWN) steps = 1;
    else if (event->direction == GDK_SCROLL_SMOOTH) {
        double delta_x = 0.0;
        double delta_y = 0.0;
        if (gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event),
                                        &delta_x, &delta_y)) {
            state.scroll_accumulator += delta_y;
            if (std::abs(state.scroll_accumulator) >= 0.55) {
                steps = state.scroll_accumulator > 0 ? 1 : -1;
                state.scroll_accumulator = 0.0;
            }
        }
    }
    if (!steps) return FALSE;
    move_selection(steps);
    ensure_animation();
    return TRUE;
}

void show_popup() {
    clear_completion();
    const gint64 now = g_get_monotonic_time();
    const std::string previous_query = gtk_entry_get_text(GTK_ENTRY(state.entry));
    const bool restore_recent = state.hidden_us > 0 && !previous_query.empty() &&
                                now - state.hidden_us <= 3000000;
    if (!restore_recent) {
        gtk_entry_set_text(GTK_ENTRY(state.entry), "");
        state.results.clear();
        state.query_pending = true;
        state.query_failed = false;
        state.selection = 0;
        state.scroll_offset = 0;
        state.selection_visual = 0.0;
    }
    state.scroll_accumulator = 0.0;
    state.opening = true;
    state.opened_us = now;
    invalidate_surfaces();
    gtk_widget_show_all(state.window);
    gtk_window_present(GTK_WINDOW(state.window));
    gtk_widget_grab_focus(state.entry);
    g_timeout_add(35, focus_kalwer, nullptr);
    g_timeout_add(180, focus_kalwer, nullptr);
    ensure_animation();
    if (!restore_recent || state.query_pending) schedule_query(8);
}

void activate(GtkApplication* app, gpointer) {
    if (state.window) {
        if (gtk_widget_get_visible(state.window)) dismiss_popup();
        else show_popup();
        return;
    }

    state.app = app;
    state.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state.window), "Kalwer");
    gtk_window_set_default_size(GTK_WINDOW(state.window), kWindowWidth, kWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(state.window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(state.window), FALSE);
    gtk_window_set_position(GTK_WINDOW(state.window), GTK_WIN_POS_CENTER);
    gtk_window_set_keep_above(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(state.window), TRUE);
    gtk_widget_set_app_paintable(state.window, TRUE);

    GdkScreen* screen = gtk_widget_get_screen(state.window);
    if (GdkVisual* visual = gdk_screen_get_rgba_visual(screen)) {
        gtk_widget_set_visual(state.window, visual);
    }

    state.overlay = gtk_overlay_new();
    state.canvas = gtk_gl_area_new();
    gtk_gl_area_set_has_alpha(GTK_GL_AREA(state.canvas), TRUE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(state.canvas), FALSE);
    gtk_gl_area_set_required_version(GTK_GL_AREA(state.canvas), 3, 3);
    gtk_widget_set_size_request(state.canvas, kWindowWidth, kWindowHeight);
    gtk_widget_add_events(state.canvas, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                        GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_container_add(GTK_CONTAINER(state.overlay), state.canvas);

    state.entry = gtk_entry_new();
    gtk_widget_set_name(state.entry, "kalwer-input-proxy");
    gtk_widget_set_size_request(state.entry, static_cast<int>(kSearchWidth),
                                static_cast<int>(kSearchHeight));
    gtk_widget_set_halign(state.entry, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state.entry, GTK_ALIGN_START);
    gtk_widget_set_margin_top(state.entry, static_cast<int>(kSearchY));
    gtk_widget_set_opacity(state.entry, 0.0);
    gtk_overlay_add_overlay(GTK_OVERLAY(state.overlay), state.entry);
    gtk_container_add(GTK_CONTAINER(state.window), state.overlay);
    state.icon_theme = gtk_icon_theme_get_default();

    GtkCssProvider* input_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(input_css,
        "#kalwer-input-proxy { padding-left: 50px; padding-right: 18px; "
        "font-family: 'JetBrainsMono Nerd Font'; font-size: 15px; "
        "background: transparent; border: 0; box-shadow: none; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        screen, GTK_STYLE_PROVIDER(input_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(input_css);

    g_signal_connect(state.canvas, "render", G_CALLBACK(on_render), nullptr);
    g_signal_connect(state.canvas, "motion-notify-event", G_CALLBACK(on_motion), nullptr);
    g_signal_connect(state.canvas, "button-press-event", G_CALLBACK(on_button), nullptr);
    g_signal_connect(state.canvas, "scroll-event", G_CALLBACK(on_scroll), nullptr);
    g_signal_connect(state.entry, "key-press-event", G_CALLBACK(on_entry_key), nullptr);
    g_signal_connect(state.entry, "changed", G_CALLBACK(on_entry_changed), nullptr);
    g_signal_connect(state.entry, "notify::cursor-position",
                     G_CALLBACK(on_entry_cursor_changed), nullptr);
    g_signal_connect(state.entry, "notify::selection-bound",
                     G_CALLBACK(on_entry_cursor_changed), nullptr);
    g_signal_connect(state.window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        state.window = nullptr;
        state.canvas = nullptr;
        state.entry = nullptr;
    }), nullptr);

    if (state.resident) {
        if (!state.held) {
            g_application_hold(G_APPLICATION(app));
            state.held = true;
        }
        gtk_widget_realize(state.window);
        gtk_widget_realize(state.canvas);
        gtk_widget_realize(state.entry);
    } else {
        show_popup();
    }
}

void cleanup() {
    stop_query();
    if (state.query_delay_source) g_source_remove(state.query_delay_source);
    invalidate_surfaces();
    for (auto& [_, icon] : state.icons) {
        if (icon) g_object_unref(icon);
    }
    state.icons.clear();
}

}  // namespace

int main(int argc, char** argv) {
    load_favorites();
    std::vector<char*> filtered_arguments;
    filtered_arguments.reserve(argc + 1);
    filtered_arguments.push_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        if (g_strcmp0(argv[index], "--daemon") == 0) state.resident = true;
        else filtered_arguments.push_back(argv[index]);
    }
    filtered_arguments.push_back(nullptr);

    g_set_prgname("elephant-field");
    GtkApplication* app = gtk_application_new(
        "pl.aridlin.ElephantField", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    const int status = g_application_run(
        G_APPLICATION(app), static_cast<int>(filtered_arguments.size() - 1),
        filtered_arguments.data());
    cleanup();
    g_object_unref(app);
    return status;
}
