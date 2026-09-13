#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

struct Options {
    std::optional<fs::path> input;
    std::optional<fs::path> output;
    bool show_help = false;
};

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();

        for (unsigned int attempt = 0; attempt < 100; ++attempt) {
            const fs::path candidate =
                fs::temp_directory_path() /
                ("md-to-docx." + std::to_string(timestamp) + "." +
                 std::to_string(attempt));

            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }

            if (error && error != std::errc::file_exists) {
                throw fs::filesystem_error(
                    "Could not create a temporary folder", candidate, error);
            }
        }

        throw std::runtime_error(
            "Could not find an unused temporary folder name");
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
};

void print_usage(const char* program) {
    std::cout
        << "Usage: " << program << " -i INPUT_FOLDER -o OUTPUT_FOLDER\n\n"
        << "Convert every .md file below INPUT_FOLDER to a .docx file.\n"
        << "Subfolders and file names are copied to OUTPUT_FOLDER.\n\n"
        << "Mermaid code blocks are rendered and embedded as diagrams.\n\n"
        << "Options:\n"
        << "  -i PATH  Markdown input folder\n"
        << "  -o PATH  Word document output folder\n"
        << "  -h       Show this help\n";
}

Options parse_options(int argc, char* argv[]) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "-h" || argument == "--help") {
            options.show_help = true;
            continue;
        }

        if (argument != "-i" && argument != "-o") {
            throw std::runtime_error("Unknown option: " + argument);
        }

        if (index + 1 >= argc) {
            throw std::runtime_error("Missing path after " + argument);
        }

        const fs::path value = argv[++index];
        if (value.empty()) {
            throw std::runtime_error("Empty path after " + argument);
        }

        if (argument == "-i") {
            options.input = value;
        } else {
            options.output = value;
        }
    }

    return options;
}

fs::path absolute_normalized(const fs::path& path) {
    return fs::absolute(path).lexically_normal();
}

bool is_same_or_below(const fs::path& candidate, const fs::path& parent) {
    auto parent_part = parent.begin();
    auto candidate_part = candidate.begin();

    while (parent_part != parent.end() && candidate_part != candidate.end() &&
           *parent_part == *candidate_part) {
        ++parent_part;
        ++candidate_part;
    }

    return parent_part == parent.end();
}

bool has_markdown_extension(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return extension == ".md";
}

std::vector<fs::path> path_entries(const char* value) {
    std::vector<fs::path> entries;
    if (value == nullptr) {
        return entries;
    }

    const std::string path_list = value;
    std::size_t start = 0;
    while (start <= path_list.size()) {
        const std::size_t end = path_list.find(kPathListSeparator, start);
        std::string entry = path_list.substr(start, end - start);

        if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"') {
            entry = entry.substr(1, entry.size() - 2);
        }
        entries.emplace_back(entry.empty() ? "." : entry);

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return entries;
}

bool is_executable_file(const fs::path& path) {
#ifdef _WIN32
    std::error_code error;
    return fs::is_regular_file(path, error) && !error;
#else
    std::error_code error;
    return fs::is_regular_file(path, error) && !error &&
           access(path.c_str(), X_OK) == 0;
#endif
}

bool command_is_available(const std::string& command) {
    const fs::path command_path = command;
    if (command_path.has_parent_path()) {
        return is_executable_file(command_path);
    }

#ifdef _WIN32
    std::vector<std::string> extensions;
    if (command_path.has_extension()) {
        extensions.emplace_back();
    } else {
        const char* path_extensions = std::getenv("PATHEXT");
        const std::string extension_list =
            path_extensions == nullptr || *path_extensions == '\0'
                ? ".COM;.EXE;.BAT;.CMD"
                : path_extensions;
        std::size_t start = 0;
        while (start <= extension_list.size()) {
            const std::size_t end = extension_list.find(';', start);
            extensions.push_back(extension_list.substr(start, end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }
#endif

    for (const fs::path& directory : path_entries(std::getenv("PATH"))) {
#ifdef _WIN32
        for (const std::string& extension : extensions) {
            if (is_executable_file(directory / (command + extension))) {
                return true;
            }
        }
#else
        if (is_executable_file(directory / command)) {
            return true;
        }
#endif
    }

    return false;
}

void check_runtime_dependencies() {
    if (!command_is_available("pandoc")) {
        throw std::runtime_error(
            "Pandoc is not installed or is not available through PATH");
    }
    if (!command_is_available("mermaid-filter")) {
        throw std::runtime_error(
            "mermaid-filter is not installed or is not available through PATH");
    }
}

std::vector<fs::path> find_markdown_files(const fs::path& input,
                                          const fs::path& output) {
    std::vector<fs::path> files;
    const bool skip_output = input != output && is_same_or_below(output, input);

    for (fs::recursive_directory_iterator iterator(input), end;
         iterator != end; ++iterator) {
        const fs::directory_entry& entry = *iterator;
        const fs::path entry_path = absolute_normalized(entry.path());

        if (entry.is_directory() && skip_output &&
            is_same_or_below(entry_path, output)) {
            iterator.disable_recursion_pending();
            continue;
        }

        if (entry.is_regular_file() && has_markdown_extension(entry.path())) {
            files.push_back(entry_path);
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

int run_process(std::vector<std::string> arguments,
                const fs::path& working_directory) {
#ifdef _WIN32
    std::vector<const char*> raw_arguments;
    raw_arguments.reserve(arguments.size() + 1);

    for (const std::string& argument : arguments) {
        raw_arguments.push_back(argument.c_str());
    }
    raw_arguments.push_back(nullptr);

    const fs::path original_directory = fs::current_path();
    fs::current_path(working_directory);
    const intptr_t result =
        _spawnvp(_P_WAIT, raw_arguments[0], raw_arguments.data());
    const int process_error = errno;

    std::error_code restore_error;
    fs::current_path(original_directory, restore_error);
    if (restore_error) {
        throw fs::filesystem_error("Could not restore the working directory",
                                   original_directory, restore_error);
    }

    if (result == -1) {
        throw std::runtime_error(std::string("Could not start Pandoc: ") +
                                 std::strerror(process_error));
    }

    return static_cast<int>(result);
#else
    std::vector<char*> raw_arguments;
    raw_arguments.reserve(arguments.size() + 1);

    for (std::string& argument : arguments) {
        raw_arguments.push_back(argument.data());
    }
    raw_arguments.push_back(nullptr);

    const pid_t child = fork();
    if (child == -1) {
        throw std::runtime_error(std::string("Could not start Pandoc: ") +
                                 std::strerror(errno));
    }

    if (child == 0) {
        if (chdir(working_directory.c_str()) == -1) {
            const int directory_error = errno;
            std::cerr << "Could not enter the temporary folder: "
                      << std::strerror(directory_error) << '\n';
            _exit(125);
        }

        execvp(raw_arguments[0], raw_arguments.data());
        const int exec_error = errno;
        std::cerr << "Could not run Pandoc: " << std::strerror(exec_error)
                  << '\n';
        _exit(exec_error == ENOENT ? 127 : 126);
    }

    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::runtime_error(std::string("Could not wait for Pandoc: ") +
                                     std::strerror(errno));
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        std::cerr << "Pandoc stopped after signal " << WTERMSIG(status) << '\n';
    }
    return 1;
#endif
}

void print_mermaid_error_log(const fs::path& working_directory) {
    std::ifstream error_log(working_directory / "mermaid-filter.err");
    if (!error_log) {
        return;
    }

    std::string line;
    bool printed_heading = false;
    while (std::getline(error_log, line)) {
        if (!printed_heading) {
            std::cerr << "Mermaid renderer output:\n";
            printed_heading = true;
        }
        std::cerr << line << '\n';
    }
}

int convert_files(const fs::path& input, const fs::path& output,
                  const std::vector<fs::path>& sources) {
    const TemporaryDirectory temporary_directory;
    std::set<fs::path> planned_targets;
    std::vector<std::pair<fs::path, fs::path>> conversions;

    for (const fs::path& source : sources) {
        fs::path relative_path = fs::relative(source, input);
        relative_path.replace_extension(".docx");
        const fs::path target = output / relative_path;

        if (!planned_targets.insert(target).second) {
            throw std::runtime_error("More than one Markdown file maps to " +
                                     target.string());
        }

        conversions.emplace_back(source, target);
    }

    int failures = 0;
    for (const auto& [source, target] : conversions) {
        fs::create_directories(target.parent_path());

        std::cout << fs::relative(source, input).generic_string() << " -> "
                  << fs::relative(target, output).generic_string() << '\n';

        const std::string resource_path =
            source.parent_path().string() + kPathListSeparator + input.string();
        const int result = run_process({
            "pandoc",
            source.string(),
            "--from=gfm",
            "--standalone",
            "--filter=mermaid-filter",
            "--resource-path=" + resource_path,
            "--output=" + target.string(),
        }, temporary_directory.path());

        if (result == 127) {
            throw std::runtime_error(
                "Pandoc is not installed or is not available through PATH");
        }

        if (result != 0) {
            print_mermaid_error_log(temporary_directory.path());
            std::cerr << "Conversion failed for " << source << " with exit code "
                      << result << '\n';
            ++failures;
        }
    }

    return failures;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.show_help) {
            print_usage(argv[0]);
            return 0;
        }

        if (!options.input.has_value()) {
            throw std::runtime_error("-i INPUT_FOLDER is required");
        }
        if (!options.output.has_value()) {
            throw std::runtime_error("-o OUTPUT_FOLDER is required");
        }

        const fs::path input = absolute_normalized(*options.input);
        if (!fs::exists(input)) {
            throw std::runtime_error("Input folder does not exist: " +
                                     input.string());
        }
        if (!fs::is_directory(input)) {
            throw std::runtime_error("Input path is not a folder: " +
                                     input.string());
        }

        const fs::path output = absolute_normalized(*options.output);

        if (fs::exists(output) && !fs::is_directory(output)) {
            throw std::runtime_error("Output path is not a folder: " +
                                     output.string());
        }

        const std::vector<fs::path> sources =
            find_markdown_files(input, output);
        if (sources.empty()) {
            std::cout << "No .md files found below " << input << '\n';
            return 0;
        }

        check_runtime_dependencies();

        fs::create_directories(output);
        std::cout << "Converting " << sources.size() << " Markdown file"
                  << (sources.size() == 1 ? "" : "s") << " to " << output
                  << "\n";

        const int failures = convert_files(input, output, sources);
        if (failures != 0) {
            std::cerr << failures << " conversion"
                      << (failures == 1 ? "" : "s") << " failed\n";
            return 1;
        }

        std::cout << "Converted " << sources.size() << " file"
                  << (sources.size() == 1 ? "" : "s") << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        std::cerr << "Run with -h for usage.\n";
        return 1;
    }
}
