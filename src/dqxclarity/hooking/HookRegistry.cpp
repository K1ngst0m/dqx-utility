#include "HookRegistry.hpp"
#include "../api/dqxclarity.hpp"
#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"

#include <libmem/libmem.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace dqxclarity
{
namespace persistence
{

namespace
{
constexpr std::array<uint32_t, 256> GenerateCRC32Table()
{
    std::array<uint32_t, 256> table{};
    constexpr uint32_t polynomial = 0xEDB88320;

    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j)
        {
            crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = GenerateCRC32Table();

} // anonymous namespace

dqxclarity::Logger HookRegistry::s_logger_ = {};

void HookRegistry::SetLogger(const dqxclarity::Logger& logger) { s_logger_ = logger; }

bool HookRegistry::CheckAndCleanup()
{
    auto orphans = LoadOrphanedHooks();
    if (orphans.empty())
        return true;

    if (s_logger_.warn)
        s_logger_.warn("Found " + std::to_string(orphans.size()) + " orphaned hooks from previous session");

    size_t cleaned = CleanupOrphanedHooks(orphans);

    if (cleaned > 0 && s_logger_.info)
        s_logger_.info("Successfully cleaned up " + std::to_string(cleaned) + " of " + std::to_string(orphans.size()) +
                       " orphaned hooks");

    return cleaned > 0;
}

std::filesystem::path HookRegistry::GetRegistryPath()
{
    std::filesystem::path exe_path;

#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    exe_path = buffer;
#else
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1)
    {
        buffer[len] = '\0';
        exe_path = buffer;
    }
#endif

    return exe_path.parent_path() / "hook_registry.bin";
}

bool HookRegistry::IsProcessAlive(uint32_t pid)
{
    auto process = libmem::GetProcess(static_cast<libmem::Pid>(pid));
    if (!process)
        return false;

    return libmem::IsProcessAlive(&*process);
}

const char* HookRegistry::HookTypeToString(HookType type)
{
    switch (type)
    {
    case HookType::Dialog:
        return "DialogHook";
    case HookType::Quest:
        return "QuestHook";
    case HookType::Player:
        return "PlayerHook";
    case HookType::Network:
        return "NetworkHook";
    case HookType::Corner:
        return "CornerHook";
    case HookType::Integrity:
        return "IntegrityHook";
    default:
        return "UnknownHook";
    }
}

uint32_t HookRegistry::ComputeCRC32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < size; ++i)
    {
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ data[i]) & 0xFF];
    }

    return ~crc;
}

std::optional<std::vector<HookRecord>> HookRegistry::ReadRegistry()
{
    auto path = GetRegistryPath();
    if (!std::filesystem::exists(path))
    {
        return std::vector<HookRecord>{};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (s_logger_.error)
            s_logger_.error("Failed to open registry file for reading: " + path.string());
        return std::nullopt;
    }

    uint64_t magic;
    uint16_t version;
    uint16_t record_count;
    uint32_t reserved;

    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
    file.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));

    if (!file || magic != MAGIC)
    {
        if (s_logger_.error)
            s_logger_.error("Invalid magic number in registry file");
        return std::nullopt;
    }

    if (version != VERSION)
    {
        if (s_logger_.warn)
            s_logger_.warn("Registry version mismatch (got " + std::to_string(version) + ", expected " +
                           std::to_string(VERSION) + ")");
        return std::nullopt;
    }

    std::vector<HookRecord> records;
    records.reserve(record_count);

    for (uint16_t i = 0; i < record_count; ++i)
    {
        HookRecord record;

        uint8_t type_raw;
        file.read(reinterpret_cast<char*>(&type_raw), sizeof(type_raw));
        record.type = static_cast<HookType>(type_raw);

        file.read(reinterpret_cast<char*>(&record.process_id), sizeof(record.process_id));
        file.read(reinterpret_cast<char*>(&record.hook_address), sizeof(record.hook_address));
        file.read(reinterpret_cast<char*>(&record.detour_address), sizeof(record.detour_address));
        file.read(reinterpret_cast<char*>(&record.detour_size), sizeof(record.detour_size));
        file.read(reinterpret_cast<char*>(&record.backup_address), sizeof(record.backup_address));
        file.read(reinterpret_cast<char*>(&record.backup_size), sizeof(record.backup_size));

        int64_t timestamp_ms;
        file.read(reinterpret_cast<char*>(&timestamp_ms), sizeof(timestamp_ms));
        record.installed_time = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp_ms));

        uint16_t original_bytes_length;
        file.read(reinterpret_cast<char*>(&original_bytes_length), sizeof(original_bytes_length));

        record.original_bytes.resize(original_bytes_length);
        file.read(reinterpret_cast<char*>(record.original_bytes.data()), original_bytes_length);

        file.read(reinterpret_cast<char*>(&record.hook_checksum), sizeof(record.hook_checksum));
        file.read(reinterpret_cast<char*>(&record.detour_checksum), sizeof(record.detour_checksum));

        if (!file)
        {
            if (s_logger_.error)
                s_logger_.error("Failed to read record " + std::to_string(i));
            return std::nullopt;
        }

        records.push_back(std::move(record));
    }

    return records;
}

bool HookRegistry::WriteRegistry(const std::vector<HookRecord>& records)
{
    auto path = GetRegistryPath();
    auto temp_path = path.parent_path() / (path.filename().string() + ".tmp");

    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            if (s_logger_.error)
                s_logger_.error("Failed to open temp file for writing: " + temp_path.string());
            return false;
        }

        uint64_t magic = MAGIC;
        uint16_t version = VERSION;
        uint16_t record_count = static_cast<uint16_t>(records.size());
        uint32_t reserved = 0;

        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&record_count), sizeof(record_count));
        file.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));

        for (const auto& record : records)
        {
            uint8_t type_raw = static_cast<uint8_t>(record.type);
            file.write(reinterpret_cast<const char*>(&type_raw), sizeof(type_raw));
            file.write(reinterpret_cast<const char*>(&record.process_id), sizeof(record.process_id));
            file.write(reinterpret_cast<const char*>(&record.hook_address), sizeof(record.hook_address));
            file.write(reinterpret_cast<const char*>(&record.detour_address), sizeof(record.detour_address));
            file.write(reinterpret_cast<const char*>(&record.detour_size), sizeof(record.detour_size));
            file.write(reinterpret_cast<const char*>(&record.backup_address), sizeof(record.backup_address));
            file.write(reinterpret_cast<const char*>(&record.backup_size), sizeof(record.backup_size));

            int64_t timestamp_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(record.installed_time.time_since_epoch()).count();
            file.write(reinterpret_cast<const char*>(&timestamp_ms), sizeof(timestamp_ms));

            uint16_t original_bytes_length = static_cast<uint16_t>(record.original_bytes.size());
            file.write(reinterpret_cast<const char*>(&original_bytes_length), sizeof(original_bytes_length));
            file.write(reinterpret_cast<const char*>(record.original_bytes.data()), original_bytes_length);

            file.write(reinterpret_cast<const char*>(&record.hook_checksum), sizeof(record.hook_checksum));
            file.write(reinterpret_cast<const char*>(&record.detour_checksum), sizeof(record.detour_checksum));
        }

        file.flush();
        if (!file)
        {
            if (s_logger_.error)
                s_logger_.error("Failed to write registry data");
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, path, ec);
    if (ec)
    {
        if (s_logger_.error)
            s_logger_.error("Failed to rename temp file: " + ec.message());
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    return true;
}

bool HookRegistry::RegisterHook(const HookRecord& record)
{
    auto existing = ReadRegistry();
    if (!existing)
    {
        if (s_logger_.error)
            s_logger_.error("Failed to read existing registry");
        return false;
    }

    existing->erase(std::remove_if(existing->begin(), existing->end(),
                                   [&record](const HookRecord& r)
                                   {
                                       return r.type == record.type;
                                   }),
                    existing->end());

    existing->push_back(record);

    bool success = WriteRegistry(*existing);
    if (success && s_logger_.info)
    {
        s_logger_.info("Registered " + std::string(HookTypeToString(record.type)) + " at 0x" +
                       std::to_string(record.hook_address) + " for PID " + std::to_string(record.process_id));
    }

    return success;
}

bool HookRegistry::UnregisterHook(HookType type)
{
    auto existing = ReadRegistry();
    if (!existing)
    {
        if (s_logger_.error)
            s_logger_.error("Failed to read existing registry");
        return false;
    }

    size_t original_size = existing->size();
    existing->erase(std::remove_if(existing->begin(), existing->end(),
                                   [type](const HookRecord& r)
                                   {
                                       return r.type == type;
                                   }),
                    existing->end());

    if (existing->size() == original_size)
    {
        return true;
    }

    if (existing->empty())
    {
        return ClearRegistry();
    }

    bool success = WriteRegistry(*existing);
    if (success && s_logger_.info)
    {
        s_logger_.info("Unregistered " + std::string(HookTypeToString(type)));
    }

    return success;
}

std::vector<HookRecord> HookRegistry::LoadOrphanedHooks()
{
    auto records = ReadRegistry();
    if (!records)
    {
        if (s_logger_.warn)
            s_logger_.warn("Failed to load registry (corrupt or I/O error)");
        return {};
    }

    if (records->empty())
    {
        if (s_logger_.debug)
            s_logger_.debug("No orphaned hooks found");
    }
    else
    {
        if (s_logger_.warn)
            s_logger_.warn("Found " + std::to_string(records->size()) + " orphaned hooks from previous session");
    }

    return *records;
}

size_t HookRegistry::CleanupOrphanedHooks(const std::vector<HookRecord>& orphans)
{
    size_t cleaned_count = 0;

    for (const auto& record : orphans)
    {
        if (s_logger_.info)
        {
            s_logger_.info("Attempting to clean up orphaned " + std::string(HookTypeToString(record.type)) +
                           " (PID: " + std::to_string(record.process_id) + ", addr: 0x" +
                           std::to_string(record.hook_address) + ")");
        }

        if (!IsProcessAlive(record.process_id))
        {
            if (s_logger_.info)
                s_logger_.info("Process " + std::to_string(record.process_id) + " not running, marking as cleaned");
            UnregisterHook(record.type);
            cleaned_count++;
            continue;
        }

        auto memory = MemoryFactory::CreatePlatformMemory();
        if (!memory || !memory->AttachProcess(record.process_id))
        {
            if (s_logger_.error)
                s_logger_.error("Failed to attach to process for PID " + std::to_string(record.process_id));
            continue;
        }

        std::vector<uint8_t> current_bytes(record.original_bytes.size());
        if (!memory->ReadMemory(record.hook_address, current_bytes.data(), current_bytes.size()))
        {
            if (s_logger_.error)
                s_logger_.error("Failed to read current bytes at hook address (expected " +
                                std::to_string(record.original_bytes.size()) + " bytes)");
            continue;
        }

        uint32_t current_checksum = ComputeCRC32(current_bytes.data(), current_bytes.size());
        uint32_t original_checksum = ComputeCRC32(record.original_bytes.data(), record.original_bytes.size());

        if (current_checksum == original_checksum)
        {
            if (s_logger_.warn)
                s_logger_.warn("Hook bytes match original - hook may have already been cleaned");
            UnregisterHook(record.type);
            cleaned_count++;
            continue;
        }

        if (!memory->WriteMemory(record.hook_address, const_cast<uint8_t*>(record.original_bytes.data()),
                                 record.original_bytes.size()))
        {
            if (s_logger_.error)
                s_logger_.error("Failed to restore original bytes");
            continue;
        }

        if (s_logger_.info)
            s_logger_.info("Successfully restored original bytes");

        if (record.detour_address != 0 && record.detour_size > 0)
        {
            if (memory->FreeMemory(record.detour_address, record.detour_size))
            {
                if (s_logger_.info)
                    s_logger_.info("Freed detour memory at 0x" + std::to_string(record.detour_address));
            }
            else
            {
                if (s_logger_.warn)
                    s_logger_.warn("Failed to free detour memory (may have been freed already)");
            }
        }

        if (record.backup_address != 0 && record.backup_size > 0)
        {
            if (memory->FreeMemory(record.backup_address, record.backup_size))
            {
                if (s_logger_.info)
                    s_logger_.info("Freed backup memory at 0x" + std::to_string(record.backup_address));
            }
            else
            {
                if (s_logger_.warn)
                    s_logger_.warn("Failed to free backup memory (may have been freed already)");
            }
        }

        UnregisterHook(record.type);
        cleaned_count++;
    }

    if (cleaned_count > 0 && s_logger_.info)
    {
        s_logger_.info("Cleanup complete: " + std::to_string(cleaned_count) + " of " + std::to_string(orphans.size()) +
                       " hooks cleaned");
    }

    return cleaned_count;
}

bool HookRegistry::ClearRegistry()
{
    auto path = GetRegistryPath();
    if (!std::filesystem::exists(path))
    {
        return true;
    }

    std::error_code ec;
    bool removed = std::filesystem::remove(path, ec);

    if (!removed || ec)
    {
        if (s_logger_.error)
            s_logger_.error("Failed to delete registry file: " + ec.message());
        return false;
    }

    if (s_logger_.info)
        s_logger_.info("Registry cleared");
    return true;
}

} // namespace persistence
} // namespace dqxclarity
