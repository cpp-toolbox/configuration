#include "configuration.hpp"
#include <fstream>

Configuration::Configuration(const std::filesystem::path &config_path,
                             const SectionKeyPairToConfigLogic &section_key_to_config_logic, bool apply)
    : config_path(fs_utils::expand_tilde(config_path)), section_key_to_config_logic(section_key_to_config_logic) {
    parse_config_file();

    if (apply) {
        apply_config_logic();
    }
}

void Configuration::reload_config_from_file() {
    section_to_key_to_value.clear();
    parse_config_file();
    apply_config_logic();
}

void Configuration::register_config_handler(const std::string &section, const std::string &key,
                                            std::function<void(const std::string)> logic) {
    SectionKeyPair pair{section, key};
    section_key_to_config_logic[pair] = std::move(logic);
}

void Configuration::parse_config_file() {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        console_logger.error("Unable to open config file: {}", config_path.string());
        return;
    }

    std::string line, current_section;
    while (std::getline(file, line)) {

        // Find the first comment character: either '#' or ';'
        size_t hash_pos = line.find('#');
        size_t semicolon_pos = line.find(';');

        // Determine which appears first (if any)
        size_t comment_pos = std::min(hash_pos, semicolon_pos);

        // std::string::npos is a very large number, so this works safely
        if (comment_pos != std::string::npos) {
            // NOTE:  Grabs everything up until the comment position, when the comment
            // is at the start of the line this returns the empty string which is what
            // we want
            line = line.substr(0, comment_pos);
        }

        line = trim(line);
        if (line.empty())
            continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = trim(line.substr(1, line.length() - 2));
        } else {
            size_t delimiter_pos = line.find('=');
            if (delimiter_pos == std::string::npos) {
                console_logger.warn("Invalid line in config file: {}", line);
                continue;
            }

            std::string key = trim(line.substr(0, delimiter_pos));
            std::string raw_value = line.substr(delimiter_pos + 1);

            std::string value;
            bool string_is_all_spaces =
                std::all_of(raw_value.begin(), raw_value.end(), [](char c) { return c == ' '; });
            if (string_is_all_spaces) {
                // then simplify to a single space
                value = " ";
            } else {
                // otherwise trim off excess.
                value = trim(raw_value);
            }

            section_to_key_to_value[current_section][key] = value;
        }
    }
}

void Configuration::apply_config_logic() {
    for (const auto &[section, key_values] : section_to_key_to_value) {
        for (const auto &[key, value] : key_values) {
            auto logic_it = section_key_to_config_logic.find({section, key});
            if (logic_it != section_key_to_config_logic.end()) {
                console_logger.debug("running config logic on {}, {} with value {}", section, key, value);
                logic_it->second(value);
            } else {
                console_logger.warn("there was no function associated with the "
                                    "section, key pair: {}, {}",
                                    section, key);
            }
        }
    }
}

// New methods for live configuration modification

bool Configuration::set_value(const std::string &section, const std::string &key, const std::string &value,
                              const bool apply) {
    // Update the internal state
    section_to_key_to_value[section][key] = value;

    if (apply) {
        apply_config_logic_for_key(section, key);
    }

    console_logger.debug("Set config value [{}].{} = {}", section, key, value);
    return true;
}

std::optional<std::string> Configuration::get_value(const std::string &section, const std::string &key) const {
    auto section_it = section_to_key_to_value.find(section);
    if (section_it == section_to_key_to_value.end()) {
        return std::nullopt;
    }

    auto key_it = section_it->second.find(key);
    if (key_it == section_it->second.end()) {
        return std::nullopt;
    }

    return key_it->second;
}

bool Configuration::is_on(const std::string &section, const std::string &key) {
    auto value_opt = get_value(section, key);
    return value_opt.has_value() && *value_opt == "on";
}

bool Configuration::remove_value(const std::string &section, const std::string &key) {
    auto section_it = section_to_key_to_value.find(section);
    if (section_it == section_to_key_to_value.end()) {
        return false;
    }

    auto key_it = section_it->second.find(key);
    if (key_it == section_it->second.end()) {
        return false;
    }

    section_it->second.erase(key_it);

    // Remove section if it's empty
    if (section_it->second.empty()) {
        section_to_key_to_value.erase(section_it);
    }

    console_logger.debug("Removed config value [{}].{}", section, key);
    return true;
}

bool Configuration::has_section(const std::string &section) const {
    return section_to_key_to_value.find(section) != section_to_key_to_value.end();
}

bool Configuration::has_value(const std::string &section, const std::string &key) {
    auto section_it = section_to_key_to_value.find(section);
    if (section_it == section_to_key_to_value.end()) {
        return false;
    }
    return section_it->second.find(key) != section_it->second.end();
}

std::vector<std::string> Configuration::get_sections() const {
    std::vector<std::string> sections;
    sections.reserve(section_to_key_to_value.size());
    for (const auto &[section, _] : section_to_key_to_value) {
        sections.push_back(section);
    }
    return sections;
}

std::vector<std::string> Configuration::get_keys(const std::string &section) {
    std::vector<std::string> keys;
    auto section_it = section_to_key_to_value.find(section);
    if (section_it != section_to_key_to_value.end()) {
        keys.reserve(section_it->second.size());
        for (const auto &[key, _] : section_it->second) {
            keys.push_back(key);
        }
    }
    return keys;
}

bool Configuration::save_to_file() { return save_to_file(config_path); }

bool Configuration::save_to_file(const std::filesystem::path &path) {

    fs_utils::create_file(path);

    std::ofstream file(path);
    if (!file.is_open()) {
        console_logger.error("Unable to open config file for writing: {}", path.string());
        return false;
    }

    for (const auto &[section, key_values] : section_to_key_to_value) {
        console_logger.debug("Writing section: [{}]", section);

        // Write section header
        file << "[" << section << "]\n";

        for (const auto &[key, value] : key_values) {
            console_logger.debug("  {} = {}", key, value);

            // Write key-value pair
            file << key << " = " << value << "\n";
        }

        // Add blank line between sections for readability
        file << "\n";
    }

    if (!file.good()) {
        console_logger.error("Error occurred while writing to config file: {}", path.string());
        return false;
    }

    console_logger.info("Successfully saved configuration to: {}", path.string());
    return true;
}

bool Configuration::backup_config(const std::filesystem::path &backup_path) {
    try {
        std::filesystem::copy_file(config_path, backup_path, std::filesystem::copy_options::overwrite_existing);
        console_logger.info("Configuration backed up to: {}", backup_path.string());
        return true;
    } catch (const std::filesystem::filesystem_error &e) {
        console_logger.error("Failed to backup configuration: {}", e.what());
        return false;
    }
}

void Configuration::apply_config_logic_for_key(const std::string &section, const std::string &key) {
    auto value_opt = get_value(section, key);
    if (!value_opt) {
        return;
    }

    auto logic_it = section_key_to_config_logic.find({section, key});
    if (logic_it != section_key_to_config_logic.end()) {
        try {
            logic_it->second(*value_opt);
            console_logger.debug("Applied config logic for [{}].{}", section, key);
        } catch (const std::exception &e) {
            console_logger.error("Failed to apply config logic for [{}].{}: {}", section, key, e.what());
        }
    }
}

// TODO: move to text utils
// Trims leading and trailing whitespace from a string
std::string Configuration::trim(const std::string &str) {
    size_t start = str.find_first_not_of(" \t");
    size_t end = str.find_last_not_of(" \t");
    return (start == std::string::npos || end == std::string::npos) ? "" : str.substr(start, end - start + 1);
}
