#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <filesystem>

/**
 * @brief Configuration class for training data tool
 * 
 * This class centralizes all configuration parameters that were previously
 * stored as global variables, providing better type safety and encapsulation.
 */
class Config {
public:
    // Default values
    static constexpr size_t DEFAULT_MAX_FILES_PER_DIRECTORY = 10000;
    static constexpr int64_t DEFAULT_MAX_GAMES_TO_CONVERT = 10000000;
    static constexpr size_t DEFAULT_CHUNKS_PER_FILE = 4096;
    static constexpr size_t DEFAULT_DEDUP_UNIQ_BUFFERSIZE = 50000;
    static constexpr float DEFAULT_DEDUP_Q_RATIO = 1.0f;
    static constexpr const char* DEFAULT_OUTPUT_PREFIX = "supervised-";

    Config() = default;
    
    // Getters
    size_t max_files_per_directory() const { return max_files_per_directory_; }
    int64_t max_games_to_convert() const { return max_games_to_convert_; }
    size_t chunks_per_file() const { return chunks_per_file_; }
    size_t dedup_uniq_buffersize() const { return dedup_uniq_buffersize_; }
    float dedup_q_ratio() const { return dedup_q_ratio_; }
    const std::string& output_prefix() const { return output_prefix_; }
    bool verbose() const { return verbose_; }
    bool lichess_mode() const { return lichess_mode_; }
    bool deduplication_mode() const { return deduplication_mode_; }

    // Setters
    void set_max_files_per_directory(size_t value) { max_files_per_directory_ = value; }
    void set_max_games_to_convert(int64_t value) { max_games_to_convert_ = value; }
    void set_chunks_per_file(size_t value) { chunks_per_file_ = value; }
    void set_dedup_uniq_buffersize(size_t value) { dedup_uniq_buffersize_ = value; }
    void set_dedup_q_ratio(float value) { dedup_q_ratio_ = value; }
    void set_output_prefix(const std::string& value) { output_prefix_ = value; }
    void set_verbose(bool value) { verbose_ = value; }
    void set_lichess_mode(bool value) { lichess_mode_ = value; }
    void set_deduplication_mode(bool value) { deduplication_mode_ = value; }

    /**
     * @brief Validates and sanitizes a file path to prevent path traversal attacks
     * @param path The input path to validate
     * @return Sanitized path if valid, empty string if invalid
     */
    static std::string sanitize_path(const std::string& path);

private:
    size_t max_files_per_directory_ = DEFAULT_MAX_FILES_PER_DIRECTORY;
    int64_t max_games_to_convert_ = DEFAULT_MAX_GAMES_TO_CONVERT;
    size_t chunks_per_file_ = DEFAULT_CHUNKS_PER_FILE;
    size_t dedup_uniq_buffersize_ = DEFAULT_DEDUP_UNIQ_BUFFERSIZE;
    float dedup_q_ratio_ = DEFAULT_DEDUP_Q_RATIO;
    std::string output_prefix_ = DEFAULT_OUTPUT_PREFIX;
    
    // Runtime options
    bool verbose_ = false;
    bool lichess_mode_ = false;
    bool deduplication_mode_ = false;
};

#endif // CONFIG_H