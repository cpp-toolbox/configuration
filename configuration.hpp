#ifndef CONFIGURATION_HPP
#define CONFIGURATION_HPP

#include "sbpt_generated_includes.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Allows you to store values as well as run logic based on section-key pairs
 *
 * @details The configuration is stored live in this class/object, and provides methods for loading and saving
 * configuration from or to file
 *
 */
class Configuration {
  public:
    using SectionKeyPair = std::pair<std::string, std::string>;
    using ConfigLogic = std::function<void(const std::string)>;
    /**
     * @brief Hash function for ConfigKey (pair of strings).
     */
    struct PairHash {
        PairHash() = default;
        size_t operator()(const SectionKeyPair &pair) const {
            return std::hash<std::string>()(pair.first) ^ (std::hash<std::string>()(pair.second) << 1);
        }
    };
    using SectionKeyPairToConfigLogic = std::unordered_map<SectionKeyPair, ConfigLogic, PairHash>;
    using ConfigData = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

    Configuration(const std::filesystem::path &config_path, const SectionKeyPairToConfigLogic &config_logic = {},
                  bool apply = true);

    // discards any in-memory changes
    void reload_config_from_file();

    void register_config_handler(const std::string &section, const std::string &key,
                                 std::function<void(const std::string)> logic);

    // Live configuration modification methods

    bool set_value(const std::string &section, const std::string &key, const std::string &value,
                   const bool apply = false);
    std::optional<std::string> get_value(const std::string &section, const std::string &key) const;

    template <typename T> std::optional<T> get_numeric_value(const std::string &section, const std::string &key) const {
        static_assert(std::is_arithmetic_v<T>);
        auto value_opt = get_value(section, key);
        if (!value_opt) {
            return std::nullopt;
        }
        T value;
        auto result = std::from_chars(value_opt->data(), value_opt->data() + value_opt->size(), value);
        if (result.ec != std::errc{} || result.ptr != value_opt->data() + value_opt->size()) {
            return std::nullopt;
        }
        return value;
    }

    /**
     * @brief Checks whether a configuration value is explicitly set to "on".
     *
     * This function queries the value associated with the given section and key.
     * It returns `true` only if the value exists and is exactly the string "on".
     * Any other case (missing section, missing key, or a value not equal to "on")
     * results in `false`.
     *
     * @param section The name of the configuration section.
     * @param key The key within the specified section.
     * @return true if the key exists and its value is "on"; false otherwise.
     */
    bool is_on(const std::string &section, const std::string &key);
    bool remove_value(const std::string &section, const std::string &key);

    // Query methods

    bool has_section(const std::string &section) const;
    bool has_value(const std::string &section, const std::string &key);
    std::vector<std::string> get_sections() const;
    std::vector<std::string> get_keys(const std::string &section);

    // File operations

    bool save_to_file();
    bool save_to_file(const std::filesystem::path &path);
    bool backup_config(const std::filesystem::path &backup_path);

    void apply_config_logic_for_key(const std::string &section, const std::string &key);
    void apply_config_logic();

  private:
    std::filesystem::path config_path;
    SectionKeyPairToConfigLogic section_key_to_config_logic;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> section_to_key_to_value;
    Logger console_logger{"configuration"};

    void parse_config_file();
    static std::string trim(const std::string &str);
};

#endif // CONFIGURATION_HPP
