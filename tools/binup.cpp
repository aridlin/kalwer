#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace {

constexpr const char* kDefaultUrl = "https://bin.aridlin.pl/";

struct Input {
    std::vector<std::string> texts;
    std::vector<std::filesystem::path> files;
    std::string base_url = kDefaultUrl;
};

void usage(std::ostream& output) {
    output <<
        "usage: binup [OPTIONS] [FILE ...]\n"
        "\n"
        "Replace the current bin.aridlin.pl paste with text, files, or both.\n"
        "\n"
        "  -t, --text TEXT       add a text pane (repeatable)\n"
        "  -T, --text-file FILE  add a file's contents as a text pane\n"
        "      --stdin           add standard input as a text pane\n"
        "      --url URL         use another compatible endpoint\n"
        "  -h, --help            show this help\n"
        "\n"
        "With no --text option, piped standard input is uploaded as text.\n"
        "Examples:\n"
        "  binup screenshot.png\n"
        "  binup --text 'build 42' kalwer.exe\n"
        "  journalctl -b | binup\n";
}

bool read_file(const std::filesystem::path& path, std::string& output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    output.assign(std::istreambuf_iterator<char>(input), {});
    return input.good() || input.eof();
}

std::string read_stdin() {
    return std::string(std::istreambuf_iterator<char>(std::cin), {});
}

bool parse_arguments(int argc, char** argv, Input& input) {
    bool force_stdin = false;
    bool positional = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (!positional && argument == "--") {
            positional = true;
        } else if (!positional && (argument == "-h" || argument == "--help")) {
            usage(std::cout);
            std::exit(0);
        } else if (!positional && (argument == "-t" || argument == "--text")) {
            if (++index >= argc) {
                std::cerr << "binup: " << argument << " needs text\n";
                return false;
            }
            input.texts.emplace_back(argv[index]);
        } else if (!positional && (argument == "-T" || argument == "--text-file")) {
            if (++index >= argc) {
                std::cerr << "binup: " << argument << " needs a file\n";
                return false;
            }
            std::string text;
            if (!read_file(argv[index], text)) {
                std::cerr << "binup: cannot read " << argv[index] << '\n';
                return false;
            }
            input.texts.push_back(std::move(text));
        } else if (!positional && argument == "--stdin") {
            force_stdin = true;
        } else if (!positional && argument == "--url") {
            if (++index >= argc) {
                std::cerr << "binup: --url needs an endpoint\n";
                return false;
            }
            input.base_url = argv[index];
        } else if (!positional && !argument.empty() && argument.front() == '-') {
            std::cerr << "binup: unknown option " << argument << '\n';
            return false;
        } else {
            input.files.emplace_back(argument);
        }
    }
    if (force_stdin || (input.texts.empty() && input.files.empty() && !isatty(fileno(stdin)))) {
        input.texts.push_back(read_stdin());
    }
    if (input.texts.empty() && input.files.empty()) {
        usage(std::cerr);
        return false;
    }
    if (input.base_url.empty() || input.base_url.back() != '/') input.base_url.push_back('/');
    for (const auto& file : input.files) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error)) {
            std::cerr << "binup: not a readable regular file: " << file.string() << '\n';
            return false;
        }
    }
    return true;
}

size_t receive(char* data, size_t size, size_t count, void* user_data) {
    const size_t bytes = size * count;
    static_cast<std::string*>(user_data)->append(data, bytes);
    return bytes;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

int main(int argc, char** argv) {
    Input input;
    if (!parse_arguments(argc, argv, input)) return 2;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << "binup: could not initialize libcurl\n";
        return 1;
    }
    CURL* request = curl_easy_init();
    if (!request) {
        curl_global_cleanup();
        std::cerr << "binup: could not create request\n";
        return 1;
    }
    curl_mime* mime = curl_mime_init(request);
    for (const std::string& text : input.texts) {
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, "texts[]");
        curl_mime_type(part, "text/plain; charset=utf-8");
        curl_mime_data(part, text.data(), text.size());
    }
    for (const auto& file : input.files) {
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, "files[]");
        curl_mime_filename(part, file.filename().string().c_str());
        curl_mime_filedata(part, file.string().c_str());
    }

    std::string response;
    curl_easy_setopt(request, CURLOPT_URL, input.base_url.c_str());
    curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(request, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(request, CURLOPT_USERAGENT, "binup/1.0");
    curl_easy_setopt(request, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(request, CURLOPT_WRITEDATA, &response);
    const CURLcode uploaded = curl_easy_perform(request);
    if (uploaded != CURLE_OK) {
        std::cerr << "binup: upload failed: " << curl_easy_strerror(uploaded) << '\n';
        curl_mime_free(mime);
        curl_easy_cleanup(request);
        curl_global_cleanup();
        return 1;
    }
    std::cout << trim(response) << '\n';

    // Read the new bundle once so the CLI can print the server's authoritative,
    // indexed file URLs instead of guessing multipart ordering.
    response.clear();
    curl_easy_setopt(request, CURLOPT_MIMEPOST, nullptr);
    curl_easy_setopt(request, CURLOPT_HTTPGET, 1L);
    const CURLcode listed = curl_easy_perform(request);
    if (listed == CURLE_OK) {
        const std::regex link_expression(R"regex(href="(/file/[^"]+)")regex");
        std::set<std::string> emitted;
        for (std::sregex_iterator match(response.begin(), response.end(), link_expression), end;
             match != end; ++match) {
            const std::string link = input.base_url.substr(0, input.base_url.size() - 1) +
                                     (*match)[1].str();
            if (emitted.insert(link).second) std::cout << link << '\n';
        }
    }
    curl_mime_free(mime);
    curl_easy_cleanup(request);
    curl_global_cleanup();
    return 0;
}
