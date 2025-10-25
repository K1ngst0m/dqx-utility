#include "ProcessMemory.hpp"
#include <algorithm>
#include <cstring>

namespace dqxclarity
{

ProcessMemory::ProcessMemory()
    : m_process{}
    , m_process_id(0)
    , m_is_attached(false)
{
}

ProcessMemory::~ProcessMemory()
{
    DetachProcess();
}

bool ProcessMemory::AttachProcess(pid_t pid)
{
    if (m_is_attached)
        DetachProcess();

    // Get process information by PID
    lm_process_t process;
    if (LM_GetProcessEx(static_cast<lm_pid_t>(pid), &process) != LM_TRUE)
        return false;

    m_process = process;
    m_process_id = pid;
    m_is_attached = true;
    return true;
}

bool ProcessMemory::ReadMemory(uintptr_t address, void* buffer, size_t size)
{
    if (!m_is_attached || address == 0 || buffer == nullptr || size == 0)
        return false;

    lm_size_t bytes_read = LM_ReadMemoryEx(&m_process, static_cast<lm_address_t>(address),
                                           reinterpret_cast<lm_byte_t*>(buffer), size);
    return bytes_read == size;
}

bool ProcessMemory::WriteMemory(uintptr_t address, const void* buffer, size_t size)
{
    if (!m_is_attached || address == 0 || buffer == nullptr || size == 0)
        return false;

    lm_size_t bytes_written = LM_WriteMemoryEx(&m_process, static_cast<lm_address_t>(address),
                                               reinterpret_cast<lm_bytearray_t>(buffer), size);
    return bytes_written == size;
}

void ProcessMemory::DetachProcess()
{
    if (m_is_attached)
    {
        // No need to close - lm_process_t is just a data structure
        m_process = {};
        m_process_id = 0;
        m_is_attached = false;
    }
}

bool ProcessMemory::IsProcessAttached() const
{
    return m_is_attached;
}

pid_t ProcessMemory::GetAttachedPid() const
{
    return m_is_attached ? m_process_id : static_cast<pid_t>(-1);
}

uintptr_t ProcessMemory::AllocateMemory(size_t size, bool executable)
{
    if (!m_is_attached)
        return 0;

    lm_prot_t protection = executable ? LM_PROT_XRW : LM_PROT_RW;
    lm_address_t allocated = LM_AllocMemoryEx(&m_process, size, protection);

    return static_cast<uintptr_t>(allocated);
}

bool ProcessMemory::FreeMemory(uintptr_t address, size_t size)
{
    if (!m_is_attached)
        return false;

    return LM_FreeMemoryEx(&m_process, static_cast<lm_address_t>(address), size) == LM_TRUE;
}

bool ProcessMemory::SetMemoryProtection(uintptr_t address, size_t size, MemoryProtectionFlags protection)
{
    if (!m_is_attached)
        return false;

    lm_prot_t lm_protection = ConvertProtectionFlags(protection);
    lm_prot_t old_protection = LM_PROT_NONE;

    return LM_ProtMemoryEx(&m_process, static_cast<lm_address_t>(address), size, lm_protection,
                           &old_protection) == LM_TRUE;
}

bool ProcessMemory::ReadString(uintptr_t address, std::string& output, size_t max_length)
{
    if (!m_is_attached)
        return false;

    std::vector<char> buffer(max_length + 1, '\0');
    if (!ReadMemory(address, buffer.data(), max_length))
        return false;

    // Find null terminator or use entire buffer
    auto null_pos = std::find(buffer.begin(), buffer.end(), '\0');
    output.assign(buffer.begin(), null_pos);
    return true;
}

bool ProcessMemory::WriteString(uintptr_t address, const std::string& text)
{
    if (!m_is_attached)
        return false;

    // Write string including null terminator
    return WriteMemory(address, text.c_str(), text.length() + 1);
}

uintptr_t ProcessMemory::GetModuleBaseAddress(const std::string& module_name)
{
    if (!m_is_attached)
        return 0;

    lm_module_t module;

    // Empty module name means get the main module
    if (module_name.empty())
    {
        // Get first module (main executable) using callback enumeration
        if (LM_EnumModulesEx(
                &m_process,
                [](lm_module_t* pmodule, lm_void_t* arg) -> lm_bool_t
                {
                    *reinterpret_cast<lm_module_t*>(arg) = *pmodule;
                    return LM_FALSE; // Stop enumeration after first module
                },
                &module) != LM_TRUE)
        {
            return 0;
        }
    }
    else
    {
        // Find module by name
        if (LM_FindModuleEx(&m_process, module_name.c_str(), &module) != LM_TRUE)
            return 0;
    }

    return static_cast<uintptr_t>(module.base);
}

int ProcessMemory::ReadInt32(uintptr_t address)
{
    int32_t value = 0;
    if (!ReadMemory(address, &value, sizeof(value)))
        return 0;
    return value;
}

uint64_t ProcessMemory::ReadInt64(uintptr_t address)
{
    uint64_t value = 0;
    if (!ReadMemory(address, &value, sizeof(value)))
        return 0;
    return value;
}

uintptr_t ProcessMemory::GetPointerAddress(uintptr_t base, const std::vector<uintptr_t>& offsets)
{
    if (!m_is_attached || base == 0)
        return 0;

    uintptr_t current_address = base;

    // Traverse the pointer chain
    for (size_t i = 0; i < offsets.size(); ++i)
    {
        // Read pointer at current address
        uintptr_t pointer_value = 0;
        if (!ReadMemory(current_address, &pointer_value, sizeof(pointer_value)))
            return 0;

        // Add offset to get next address
        current_address = pointer_value + offsets[i];

        // Validate the address is not null (except for final offset)
        if (pointer_value == 0 && i < offsets.size() - 1)
            return 0;
    }

    return current_address;
}

void ProcessMemory::FlushInstructionCache(uintptr_t address, size_t size)
{
    if (!m_is_attached)
        return;

    // libmem should handle instruction cache flushing internally for hooks
    // If explicit flushing is needed, we can use platform-specific code or libmem's hook API
    // For now, this is a no-op as libmem manages this automatically
    (void)address;
    (void)size;
}

lm_prot_t ProcessMemory::ConvertProtectionFlags(MemoryProtectionFlags flags)
{
    lm_prot_t result = LM_PROT_NONE;

    auto int_flags = static_cast<int>(flags);

    if (int_flags & static_cast<int>(MemoryProtectionFlags::Read))
        result |= LM_PROT_R;
    if (int_flags & static_cast<int>(MemoryProtectionFlags::Write))
        result |= LM_PROT_W;
    if (int_flags & static_cast<int>(MemoryProtectionFlags::Execute))
        result |= LM_PROT_X;

    return result;
}

} // namespace dqxclarity
