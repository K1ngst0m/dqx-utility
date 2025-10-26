#include "ProcessMemory.hpp"
#include <algorithm>
#include <cstring>

namespace dqxclarity
{
namespace
{
template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

libmem::Prot convert_protection_flags(MemoryProtectionFlags flags)
{
    auto int_flags = static_cast<int>(flags);
    uint32_t result_flags = 0;

    if (int_flags & static_cast<int>(MemoryProtectionFlags::Read))
        result_flags |= to_underlying(libmem::Prot::R);
    if (int_flags & static_cast<int>(MemoryProtectionFlags::Write))
        result_flags |= to_underlying(libmem::Prot::W);
    if (int_flags & static_cast<int>(MemoryProtectionFlags::Execute))
        result_flags |= to_underlying(libmem::Prot::X);

    return static_cast<libmem::Prot>(result_flags);
}
} // namespace

ProcessMemory::ProcessMemory()
    : m_process{}
    , m_process_id(0)
{
}

ProcessMemory::~ProcessMemory() { DetachProcess(); }

bool ProcessMemory::AttachProcess(pid_t pid)
{
    if (m_process)
        DetachProcess();

    auto process = libmem::GetProcess(static_cast<libmem::Pid>(pid));
    if (!process)
        return false;

    m_process = *process;
    m_process_id = pid;
    return true;
}

bool ProcessMemory::ReadMemory(uintptr_t address, void* buffer, size_t size)
{
    if (!m_process || address == 0 || buffer == nullptr || size == 0)
        return false;

    size_t bytes_read = libmem::ReadMemory(&m_process.value(), static_cast<libmem::Address>(address),
                                           reinterpret_cast<uint8_t*>(buffer), size);
    return bytes_read == size;
}

bool ProcessMemory::WriteMemory(uintptr_t address, const void* buffer, size_t size)
{
    if (!m_process || address == 0 || buffer == nullptr || size == 0)
        return false;

    // libmem::WriteMemory lacks const-correct signature, requiring const_cast.
    // This is safe as the underlying OS APIs do not modify the source buffer.
    size_t bytes_written = libmem::WriteMemory(&m_process.value(), static_cast<libmem::Address>(address),
                                               const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buffer)), size);
    return bytes_written == size;
}

void ProcessMemory::DetachProcess()
{
    if (m_process)
    {
        m_process.reset();
        m_process_id = 0;
    }
}

bool ProcessMemory::IsProcessAttached() const { return m_process.has_value(); }

pid_t ProcessMemory::GetAttachedPid() const { return m_process.has_value() ? m_process_id : static_cast<pid_t>(-1); }

uintptr_t ProcessMemory::AllocateMemory(size_t size, bool executable)
{
    if (!m_process)
        return 0;

    libmem::Prot protection = executable ? libmem::Prot::XRW : libmem::Prot::RW;
    auto allocated = libmem::AllocMemory(&m_process.value(), size, protection);

    return allocated.value_or(0);
}

bool ProcessMemory::FreeMemory(uintptr_t address, size_t size)
{
    if (!m_process)
        return false;

    return libmem::FreeMemory(&m_process.value(), static_cast<libmem::Address>(address), size);
}

bool ProcessMemory::SetMemoryProtection(uintptr_t address, size_t size, MemoryProtectionFlags protection)
{
    if (!m_process)
        return false;

    libmem::Prot lm_protection = convert_protection_flags(protection);

    auto result = libmem::ProtMemory(&m_process.value(), static_cast<libmem::Address>(address), size, lm_protection);
    return result.has_value();
}

bool ProcessMemory::ReadString(uintptr_t address, std::string& output, size_t max_length)
{
    if (!m_process)
        return false;

    std::vector<char> buffer(max_length + 1, '\0');
    if (!ReadMemory(address, buffer.data(), max_length))
        return false;

    auto null_pos = std::find(buffer.begin(), buffer.end(), '\0');
    output.assign(buffer.begin(), null_pos);
    return true;
}

bool ProcessMemory::WriteString(uintptr_t address, const std::string& text)
{
    if (!m_process)
        return false;

    return WriteMemory(address, text.c_str(), text.length() + 1);
}

uintptr_t ProcessMemory::GetModuleBaseAddress(const std::string& module_name)
{
    if (!m_process)
        return 0;

    if (module_name.empty())
    {
        auto modules = libmem::EnumModules(&m_process.value());
        if (!modules || modules->empty())
            return 0;
        return static_cast<uintptr_t>((*modules)[0].base);
    }
    else
    {
        auto module = libmem::FindModule(&m_process.value(), module_name.c_str());
        if (!module)
            return 0;
        return static_cast<uintptr_t>(module->base);
    }
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
    if (!m_process || base == 0)
        return 0;

    std::vector<libmem::Address> lm_offsets(offsets.begin(), offsets.end());
    auto result = libmem::DeepPointer(&m_process.value(), static_cast<libmem::Address>(base), lm_offsets);

    return result.value_or(0);
}

void ProcessMemory::FlushInstructionCache(uintptr_t address, size_t size)
{
    if (!m_process)
        return;

    // libmem handles instruction cache flushing internally during WriteMemory
    // No explicit flushing needed on any platform
    (void)address;
    (void)size;
}

} // namespace dqxclarity
