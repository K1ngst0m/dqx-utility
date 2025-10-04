#include "ProcessMemory.hpp"
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>

namespace dqxclarity {

ProcessMemory::ProcessMemory()
    : m_process_handle(INVALID_HANDLE_VALUE)
    , m_process_id(0)
    , m_is_attached(false) {}

ProcessMemory::~ProcessMemory() {
    DetachProcess();
}

bool ProcessMemory::AttachProcess(pid_t pid) {
    if (m_is_attached) DetachProcess();
    HANDLE process_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, static_cast<DWORD>(pid));
    if (!process_handle || process_handle == INVALID_HANDLE_VALUE) return false;
    DWORD_PTR handle_value = reinterpret_cast<DWORD_PTR>(process_handle);
    handle_value &= 0xFFFFFFFF;
    m_process_handle = reinterpret_cast<HANDLE>(handle_value);
    m_process_id = static_cast<DWORD>(pid);
    m_is_attached = true;
    return true;
}

bool ProcessMemory::ReadMemory(uintptr_t address, void* buffer, size_t size) {
    if (!m_is_attached) return false;
    if (address == 0 || address > 0x7FFFFFFF) return false;
    SIZE_T bytes_read = 0;
    BOOL ok = ReadProcessMemory(m_process_handle, reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read);
    return ok && bytes_read == size;
}

bool ProcessMemory::WriteMemory(uintptr_t address, const void* buffer, size_t size) {
    if (!m_is_attached) return false;
    SIZE_T bytes_written = 0;
    BOOL ok = WriteProcessMemory(m_process_handle, reinterpret_cast<LPVOID>(address), buffer, size, &bytes_written);
    return ok && bytes_written == size;
}

void ProcessMemory::DetachProcess() {
    if (m_process_handle && m_process_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_process_handle);
        m_process_handle = INVALID_HANDLE_VALUE;
    }
    m_process_id = 0;
    m_is_attached = false;
}

bool ProcessMemory::IsProcessAttached() const { return m_is_attached; }

pid_t ProcessMemory::GetAttachedPid() const { return static_cast<pid_t>(m_process_id); }

uintptr_t ProcessMemory::AllocateMemory(size_t size, bool executable) {
    if (!m_is_attached) return 0;
    DWORD protection = executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    LPVOID allocated = VirtualAllocEx(m_process_handle, NULL, size, MEM_COMMIT | MEM_RESERVE, protection);
    return reinterpret_cast<uintptr_t>(allocated);
}

bool ProcessMemory::FreeMemory(uintptr_t address, size_t size) {
    if (!m_is_attached) return false;
    return VirtualFreeEx(m_process_handle, reinterpret_cast<LPVOID>(address), size, MEM_RELEASE) != 0;
}

bool ProcessMemory::SetMemoryProtection(uintptr_t address, size_t size, MemoryProtectionFlags protection) {
    if (!m_is_attached) return false;
    DWORD win_protection = ConvertProtectionFlags(protection);
    DWORD old_protection = 0;
    return VirtualProtectEx(m_process_handle, reinterpret_cast<LPVOID>(address), size, win_protection, &old_protection) != 0;
}

bool ProcessMemory::ReadString(uintptr_t address, std::string& output, size_t max_length) {
    if (!m_is_attached) return false;
    output.clear();
    for (size_t i = 0; i < max_length; ++i) {
        char ch = 0;
        if (!ReadMemory(address + i, &ch, 1)) return false;
        if (ch == '\0') break;
        output.push_back(ch);
    }
    return true;
}

bool ProcessMemory::WriteString(uintptr_t address, const std::string& text) {
    if (!m_is_attached) return false;
    std::string s = text + '\0';
    return WriteMemory(address, s.c_str(), s.size());
}

uintptr_t ProcessMemory::GetModuleBaseAddress(const std::string& module_name) {
    if (!m_is_attached) return 0;
    HMODULE modules[1024];
    DWORD bytes_needed = 0;
    if (!EnumProcessModules(m_process_handle, modules, sizeof(modules), &bytes_needed)) return 0;
    int module_count = bytes_needed / sizeof(HMODULE);
    for (int i = 0; i < module_count; ++i) {
        char module_path[MAX_PATH] = {0};
        if (GetModuleFileNameExA(m_process_handle, modules[i], module_path, MAX_PATH)) {
            std::string path_str(module_path);
            size_t last = path_str.find_last_of("\\/");
            std::string filename = (last != std::string::npos) ? path_str.substr(last + 1) : path_str;
            if (module_name.empty() && i == 0) return reinterpret_cast<uintptr_t>(modules[i]);
            std::string a = filename; std::string b = module_name;
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            if (a == b) return reinterpret_cast<uintptr_t>(modules[i]);
        }
    }
    return 0;
}

int ProcessMemory::ReadInt32(uintptr_t address) {
    int v = 0; ReadMemory(address, &v, sizeof(v)); return v;
}

uint64_t ProcessMemory::ReadInt64(uintptr_t address) {
    uint64_t v = 0; ReadMemory(address, &v, sizeof(v)); return v;
}

uintptr_t ProcessMemory::GetPointerAddress(uintptr_t base, const std::vector<uintptr_t>& offsets) {
    if (!m_is_attached || offsets.empty()) return 0;
    uintptr_t current = ReadInt32(base);
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (i != offsets.size() - 1) current = ReadInt32(current + offsets[i]);
        else current = current + offsets[i];
    }
    return current;
}

void ProcessMemory::FlushInstructionCache(uintptr_t address, size_t size) {
    if (!m_is_attached) return;
    ::FlushInstructionCache(m_process_handle, reinterpret_cast<LPCVOID>(address), size);
}

DWORD ProcessMemory::ConvertProtectionFlags(MemoryProtectionFlags flags) {
    switch (flags) {
        case MemoryProtectionFlags::Read: return PAGE_READONLY;
        case MemoryProtectionFlags::Write: return PAGE_READWRITE;
        case MemoryProtectionFlags::Execute: return PAGE_EXECUTE;
        case MemoryProtectionFlags::ReadWrite: return PAGE_READWRITE;
        case MemoryProtectionFlags::ReadExecute: return PAGE_EXECUTE_READ;
        case MemoryProtectionFlags::ReadWriteExecute: return PAGE_EXECUTE_READWRITE;
        default: return PAGE_NOACCESS;
    }
}

} // namespace dqxclarity
