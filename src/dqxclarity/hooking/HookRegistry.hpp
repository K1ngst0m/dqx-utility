#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dqxclarity
{

struct Logger;

namespace persistence
{

/**
 * @brief Type of hook installed in the target process
 */
enum class HookType : uint8_t
{
    Dialog = 0,
    Quest = 1,
    Player = 2,
    Network = 3,
    Corner = 4,
    Integrity = 5
};

/**
 * @brief Record of a single hook installation
 *
 * Contains all information needed to restore or remove a hook after a crash.
 * This struct is serialized to binary format for atomic file operations.
 */
struct HookRecord
{
    HookType type; // Type of hook
    uint32_t process_id; // Target process PID
    uintptr_t hook_address; // Where JMP was written
    uintptr_t detour_address; // Allocated memory for detour
    size_t detour_size; // Size of detour allocation
    uintptr_t backup_address; // Allocated memory for backup
    size_t backup_size; // Size of backup allocation
    std::vector<uint8_t> original_bytes; // Original bytes to restore
    std::chrono::system_clock::time_point installed_time; // When hook was installed
    uint32_t hook_checksum; // CRC32 of hook_address region
    uint32_t detour_checksum; // CRC32 of detour code
};

/**
 * @brief Persistent registry for tracking active hooks
 *
 * This class manages a binary file that records all active hooks. The registry
 * provides crash safety: if the application crashes before cleaning up hooks,
 * the next launch can detect orphaned hooks and clean them up before proceeding.
 *
 * File Location: Same directory as executable (hook_registry.bin)
 * File Format: Binary with atomic write-rename pattern
 * Thread Safety: All operations are internally synchronized
 *
 * Usage Pattern:
 * 1. On hook installation: RegisterHook(record)
 * 2. On successful cleanup: UnregisterHook(type)
 * 3. On startup: LoadOrphanedHooks() to detect leftover hooks
 * 4. Clean orphans before normal operation
 */
class HookRegistry
{
public:
    static bool CheckAndCleanup();

    static void SetLogger(const dqxclarity::Logger& logger);

    /**
     * @brief Register a newly installed hook
     *
     * Atomically writes the hook record to the registry file. If a hook of the
     * same type already exists, it will be replaced.
     *
     * @param record Hook record containing all installation details
     * @return true on success, false on I/O error
     */
    static bool RegisterHook(const HookRecord& record);

    /**
     * @brief Unregister a hook after successful cleanup
     *
     * Removes the hook record from the registry. If this is the last hook,
     * the registry file is deleted entirely.
     *
     * @param type Type of hook to unregister
     * @return true on success, false on I/O error
     */
    static bool UnregisterHook(HookType type);

    /**
     * @brief Load all registered hooks (orphans from previous crash)
     *
     * Reads the registry file and returns all hooks that were not properly
     * cleaned up. This should be called on application startup.
     *
     * @return Vector of orphaned hook records (empty if none or on error)
     */
    static std::vector<HookRecord> LoadOrphanedHooks();

    /**
     * @brief Clean up orphaned hooks from previous crash
     *
     * Attempts to restore original bytes and free allocated memory for each
     * orphaned hook. Uses libmem APIs to attach to the process and perform
     * memory operations.
     *
     * @param orphans Vector of orphaned hooks to clean up
     * @return Number of successfully cleaned hooks
     */
    static size_t CleanupOrphanedHooks(const std::vector<HookRecord>& orphans);

    /**
     * @brief Clear the entire registry (for testing or manual cleanup)
     *
     * Deletes the registry file. Use with caution.
     *
     * @return true on success, false if file doesn't exist or deletion failed
     */
    static bool ClearRegistry();

    /**
     * @brief Get the registry file path
     *
     * Returns the absolute path to hook_registry.bin in the executable directory.
     *
     * @return Filesystem path to registry file
     */
    static std::filesystem::path GetRegistryPath();

    /**
     * @brief Convert hook type to string for logging
     *
     * @param type Hook type enum value
     * @return Human-readable string (e.g., "DialogHook")
     */
    static const char* HookTypeToString(HookType type);

    /**
     * @brief Calculate CRC32 checksum of a memory region
     *
     * Used for verifying hook integrity after crashes. Helps detect if game
     * was updated/patched between crash and recovery.
     *
     * @param data Pointer to data
     * @param size Size in bytes
     * @return CRC32 checksum
     */
    static uint32_t ComputeCRC32(const uint8_t* data, size_t size);

private:
    // File format constants
    static constexpr uint64_t MAGIC = 0x484F4F4B44515831ULL;
    static constexpr uint16_t VERSION = 1;

    static bool WriteRegistry(const std::vector<HookRecord>& records);
    static std::optional<std::vector<HookRecord>> ReadRegistry();

    static dqxclarity::Logger s_logger_;
};

} // namespace persistence
} // namespace dqxclarity
