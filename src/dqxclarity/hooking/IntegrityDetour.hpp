#pragma once

#include "../memory/IProcessMemory.hpp"
#include "../pattern/MemoryRegion.hpp"
#include "../api/dqxclarity.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <mutex>

namespace dqxclarity
{

class IntegrityDetour
{
public:
    explicit IntegrityDetour(std::shared_ptr<IProcessMemory> memory);
    ~IntegrityDetour();

    // Install the integrity trampoline and patch the integrity function.
    // Returns true on success.
    bool Install();

    // Remove the patch and restore original bytes.
    void Remove();

    // Address of the state flag byte inside the target process (0/1)
    uintptr_t GetStateAddress() const { return m_state_addr; }

    // Whether installed
    bool IsInstalled() const { return m_installed; }

    void SetLogger(const dqxclarity::Logger& log) { m_log = log; }

    void SetDiagnosticsEnabled(bool d) { m_diag = d; }

    // Set pre-parsed memory regions to avoid repeated ParseMaps calls
    void SetCachedRegions(const std::vector<MemoryRegion>& regions) { m_cached_regions = regions; }

    // Provide a list of hook sites to temporarily restore during integrity.
    // Each call adds one site with its original bytes.
    void AddRestoreTarget(uintptr_t address, const std::vector<uint8_t>& original_bytes)
    {
        std::lock_guard<std::mutex> lock(m_restore_mutex);
        for (auto& site : m_restore_sites)
        {
            if (site.address == address)
            {
                site.bytes = original_bytes;
                return;
            }
        }
        m_restore_sites.push_back({ address, original_bytes });
    }

    void UpdateRestoreTarget(uintptr_t address, const std::vector<uint8_t>& original_bytes)
    {
        AddRestoreTarget(address, original_bytes);
    }

    void MoveRestoreTarget(uintptr_t old_address, uintptr_t new_address, const std::vector<uint8_t>& original_bytes)
    {
        std::lock_guard<std::mutex> lock(m_restore_mutex);
        for (auto& site : m_restore_sites)
        {
            if (site.address == old_address)
            {
                site.address = new_address;
                site.bytes = original_bytes;
                return;
            }
        }
        m_restore_sites.push_back({ new_address, original_bytes });
    }

private:
    struct RestoreSite
    {
        uintptr_t address; // original hook site address
        std::vector<uint8_t> bytes; // original bytes to restore
    };

    std::shared_ptr<IProcessMemory> m_memory;
    bool m_installed = false;
    bool m_diag = false; // detailed diagnostics switch
    dqxclarity::Logger m_log{};

    uintptr_t m_integrity_addr = 0; // address of integrity function where we patch
    uintptr_t m_trampoline_addr = 0; // allocated exec memory with our code
    uintptr_t m_state_addr = 0; // allocated byte for state flag

    std::vector<uint8_t> m_original_bytes; // stolen original bytes (instruction-safe)
    std::vector<RestoreSite> m_restore_sites; // hook sites to temporarily restore
    mutable std::mutex m_restore_mutex;

    // Cached memory regions
    std::vector<MemoryRegion> m_cached_regions;

    bool FindIntegrityAddress(uintptr_t& out_addr);
    bool BuildAndWriteTrampoline();
    bool PatchIntegrityFunction();
    void LogBytes(const char* label, uintptr_t addr, size_t count);

    // Compute an instruction-safe stolen length at m_integrity_addr.
    // Reads a small window (up to 32 bytes) and decodes x86 instructions
    // until at least 5 bytes are covered without splitting any instruction.
    // Falls back to 8 bytes if decoding fails.
    size_t ComputeInstructionSafeStolenLen();
    static size_t DecodeInstrLen(const uint8_t* p, size_t max);
};

} // namespace dqxclarity
