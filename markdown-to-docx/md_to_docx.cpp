#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    std::optional<fs::path> input;
    std::optional<fs::path> output;
    bool show_help = false;
};

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (fs::temp_directory_path() / "md-to-docx.XXXXXX").string();
        std::vector<char> writable_pattern(pattern.begin(), pattern.end());
        writable_pattern.push_back('\0');

        char* created = mkdtemp(writable_pattern.data());
        if (created == nullptr) {
            throw std::runtime_error(
                std::string("Could not create a temporary folder: ") +
                std::strerror(errno));
        }
        path_ = created;
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
            source.parent_path().string() + ":" + input.string();
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
