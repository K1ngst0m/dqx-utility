#pragma once

#include "IProcessMemory.hpp"
#include "../pattern/MemoryRegion.hpp"
#include "../api/dqxclarity.hpp"
#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <chrono>

namespace dqxclarity
{

/**
 * @brief Alternative dialog text extractor using direct memory reading (no code injection)
 *
 * This class implements a fallback mechanism for extracting dialog text from the game
 * without using code hooks. It searches for a specific byte pattern in non-executable
 * memory regions and reads dialog text via pointer dereferencing.
 *
 * Based on the decompiled Python implementation's DialogReader._read_memory approach.
 */
class DialogMemoryReader
{
public:
    explicit DialogMemoryReader(IProcessMemory* memory);
    ~DialogMemoryReader() = default;

    /**
     * @brief Initialize the memory reader by finding the dialog pattern
     * @return true if initialization successful, false otherwise
     */
    bool Initialize();

    /**
     * @brief Poll for new dialog data (call periodically in polling loop)
     * @return true if new dialog text was captured, false otherwise
     */
    bool PollDialogData();

    /**
     * @brief Get the last captured dialog text
     * @return Dialog text string (empty if none)
     */
    std::string GetLastDialogText() const { return last_dialog_text_; }

    /**
     * @brief Get the last captured NPC name
     * @return NPC name string (may be "No_NPC" if unavailable)
     */
    std::string GetLastNpcName() const { return last_npc_name_; }

    /**
     * @brief Enable/disable verbose logging
     * @param enabled Verbose mode enabled
     */
    void SetVerbose(bool enabled) { verbose_ = enabled; }

    /**
     * @brief Set logger for diagnostic output
     * @param log Logger instance
     */
    void SetLogger(const Logger& log) { logger_ = log; }

    /**
     * @brief Check if the reader is initialized and ready
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return initialized_; }

private:
    static constexpr size_t kMaxStringLength = 4096;

    // Dialog pattern to search for (from Python decompiled code)
    // Pattern: \xFF\xFF\xFF\x7F\xFF\xFF\xFF\x7F\x00\x00\x00\x00\x00\x00\x00\x00\xFD.\xA8\x99
    static constexpr uint8_t kDialogPattern[] = {
        0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0xFF, 0xA8, 0x99 // 0xFF is wildcard for second-to-last byte
    };
    static constexpr size_t kPatternSize = sizeof(kDialogPattern);
    static constexpr size_t kPointerOffset = 36; // Offset from pattern to pointer (32 + 4)

    IProcessMemory* memory_;
    bool verbose_ = false;
    Logger logger_{};
    bool initialized_ = false;

    // Cached state for optimized scanning
    uintptr_t last_pattern_addr_ = 0;
    uintptr_t last_region_base_ = 0;
    size_t last_region_size_ = 0;

    // Captured dialog data
    std::string last_dialog_text_;
    std::string last_npc_name_;
    std::chrono::steady_clock::time_point last_dialog_time_;
    static constexpr std::chrono::milliseconds kStateTimeout{ 5000 }; // Clear state after 5s of inactivity

    /**
     * @brief Find the dialog pattern in memory (fast path: cached region, slow path: full scan)
     * @return Pattern address if found, 0 otherwise
     */
    uintptr_t FindDialogPattern();

    /**
     * @brief Scan a specific memory region for the dialog pattern
     * @param base_address Base address of the region
     * @param size Size of the region
     * @return Pattern address if found, 0 otherwise
     */
    uintptr_t ScanRegionForPattern(uintptr_t base_address, size_t size);

    /**
     * @brief Perform a full memory scan for the dialog pattern in non-executable regions
     * @return Pattern address if found, 0 otherwise
     */
    uintptr_t ScanAllNonExecutableMemory();

    /**
     * @brief Read a null-terminated string from the specified address
     * @param address Address to read from
     * @param output Output string
     * @param max_length Maximum string length to read
     * @return true if successful, false otherwise
     */
    bool ExtractNullTerminatedString(uintptr_t address, std::string& output, size_t max_length = kMaxStringLength);

    /**
     * @brief Get all non-executable memory regions for scanning
     * @return Vector of memory regions
     */
    std::vector<MemoryRegion> GetNonExecutableRegions();

    /**
     * @brief Check if a memory region matches the dialog pattern at the given offset
     * @param buffer Buffer containing memory region data
     * @param buffer_size Size of the buffer
     * @return Offset of pattern match, or SIZE_MAX if not found
     */
    size_t FindPatternInBuffer(const uint8_t* buffer, size_t buffer_size);
};

} // namespace dqxclarity
