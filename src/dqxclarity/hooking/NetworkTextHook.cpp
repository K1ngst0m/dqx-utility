#include "NetworkTextHook.hpp"

#include "../pattern/PatternFinder.hpp"
#include "../signatures/Signatures.hpp"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string_view>

namespace dqxclarity
{

namespace
{

constexpr std::size_t kMaxLogFileSize = 10u * 1024u * 1024u;
constexpr std::size_t kLogFileBackups = 3u;

bool IsValidUtf8(std::string_view text)
{
    std::size_t index = 0;
    const std::size_t size = text.size();

    while (index < size)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);

        if (lead < 0x80u)
        {
            ++index;
            continue;
        }

        if ((lead >> 5) == 0x6)
        {
            if (index + 1 >= size)
                return false;
            const unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
            if ((c1 & 0xC0u) != 0x80u)
                return false;
            const uint32_t codepoint = ((lead & 0x1Fu) << 6) | (c1 & 0x3Fu);
            if (codepoint < 0x80u)
                return false; // overlong
            index += 2;
            continue;
        }

        if ((lead >> 4) == 0xEu)
        {
            if (index + 2 >= size)
                return false;
            const unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[index + 2]);
            if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u)
                return false;
            const uint32_t codepoint = ((lead & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            if (codepoint < 0x800u)
                return false; // overlong
            if (codepoint >= 0xD800u && codepoint <= 0xDFFFu)
                return false; // surrogate range
            index += 3;
            continue;
        }

        if ((lead >> 3) == 0x1Eu)
        {
            if (index + 3 >= size)
                return false;
            const unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[index + 2]);
            const unsigned char c3 = static_cast<unsigned char>(text[index + 3]);
            if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u)
                return false;
            const uint32_t codepoint =
                ((lead & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
            if (codepoint < 0x10000u || codepoint > 0x10FFFFu)
                return false;
            index += 4;
            continue;
        }

        return false;
    }

    return true;
}

std::string EscapeJson(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 16);
    for (char ch : input)
    {
        switch (ch)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch));
                out += oss.str();
            }
            else
            {
                out += ch;
            }
            break;
        }
    }
    return out;
}

std::string ToHex(uintptr_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

std::string BytesToHex(std::string_view data)
{
    if (data.empty())
    {
        return {};
    }

    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    bool first = true;
    for (unsigned char ch : data)
    {
        if (!first)
        {
            oss << ' ';
        }
        first = false;
        oss << std::setw(2) << static_cast<int>(ch);
    }
    return oss.str();
}

std::string FormatTimestamp(const std::tm& tm)
{
    char buffer[32] = { 0 };
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm) == 0)
    {
        return {};
    }
    return buffer;
}

void RotateLogsIfNeeded()
{
    try
    {
        const std::filesystem::path path{ "logs/network.log" };
        if (!std::filesystem::exists(path))
        {
            return;
        }

        const auto size = std::filesystem::file_size(path);
        if (size <= kMaxLogFileSize)
        {
            return;
        }

        for (std::size_t i = kLogFileBackups; i > 0; --i)
        {
            const auto from = i == 1 ? path : std::filesystem::path(path.string() + "." + std::to_string(i - 1));
            const auto to = std::filesystem::path(path.string() + "." + std::to_string(i));
            if (std::filesystem::exists(from))
            {
                std::error_code ec;
                std::filesystem::rename(from, to, ec);
            }
        }

        std::ofstream truncate_stream(path, std::ios::trunc);
    }
    catch (...)
    {
    }
}

bool ShouldAppendNetworkLog()
{
    static bool append_mode = true;
    static std::once_flag check_once;
    std::call_once(check_once,
                   []
                   {
                       std::ifstream marker(".dqx_append_logs");
                       if (marker.is_open())
                       {
                           std::string value;
                           if (std::getline(marker, value) && value == "false")
                           {
                               append_mode = false;
                           }
                       }
                   });
    return append_mode;
}

void WriteNetworkLog(const NetworkTextHook::Capture& data)
{
    static std::once_flag once;
    static std::mutex log_mutex;
    static bool first_write = true;

    std::call_once(once,
                   []
                   {
                       try
                       {
                           std::filesystem::create_directories("logs");
                       }
                       catch (...)
                       {
                       }
                   });

    std::lock_guard<std::mutex> lock(log_mutex);

    auto mode = std::ios::app;
    if (first_write)
    {
        mode = ShouldAppendNetworkLog() ? std::ios::app : std::ios::trunc;
        first_write = false;
    }

    std::ofstream stream("logs/network.log", mode);
    if (!stream.is_open())
    {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif

    std::ostringstream entry;
    entry << '{' << "\"timestamp\":\"" << EscapeJson(FormatTimestamp(local_tm)) << "\",";
    entry << "\"text_ptr\":\"" << EscapeJson(ToHex(data.text_ptr)) << "\",";
    entry << "\"category_ptr\":\"" << EscapeJson(ToHex(data.category_ptr)) << "\",";
    entry << "\"category\":\"" << EscapeJson(data.category) << "\",";
    entry << "\"text\":\"" << EscapeJson(data.text) << "\",";
    entry << "\"category_raw_hex\":\"" << EscapeJson(data.category_raw_hex) << "\",";
    entry << "\"text_raw_hex\":\"" << EscapeJson(data.text_raw_hex) << "\",";
    entry << "\"category_strategy\":\"" << EscapeJson(data.category_strategy) << "\",";
    entry << "\"text_strategy\":\"" << EscapeJson(data.text_strategy) << "\"";
    entry << '}';

    stream << entry.str() << '\n';
    stream.flush();

    RotateLogsIfNeeded();
}

} // namespace

NetworkTextHook::NetworkTextHook(std::shared_ptr<IProcessMemory> memory)
    : m_memory(std::move(memory))
{
}

NetworkTextHook::~NetworkTextHook()
{
    if (m_is_installed)
    {
        RemoveHook();
    }
}

bool NetworkTextHook::InstallHook(bool enable_patch)
{
    if (m_is_installed && enable_patch)
    {
        return true;
    }

    if (!FindNetworkTriggerAddress())
    {
        if (m_logger.error)
            m_logger.error("Failed to find network text trigger address");
        return false;
    }

    if (!AllocateDetourMemory())
    {
        if (m_logger.error)
            m_logger.error("Failed to allocate network text detour memory");
        return false;
    }

    const size_t stolen_bytes = ComputeStolenLength();
    m_original_bytes.resize(stolen_bytes);
    if (!m_memory->ReadMemory(m_hook_address, m_original_bytes.data(), stolen_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to read network text hook original bytes");
        return false;
    }

    if (!WriteDetourCode())
    {
        if (m_logger.error)
            m_logger.error("Failed to write network text detour code");
        return false;
    }

    if (!enable_patch)
    {
        return true;
    }

    return EnablePatch();
}

bool NetworkTextHook::EnablePatch()
{
    if (m_original_bytes.empty())
    {
        return false;
    }

    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9);
    const uint32_t jump_offset = Rel32From(m_hook_address, m_detour_address);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<const uint8_t*>(&jump_offset),
                       reinterpret_cast<const uint8_t*>(&jump_offset) + sizeof(uint32_t));

    while (patch_bytes.size() < m_original_bytes.size())
    {
        patch_bytes.push_back(0x90);
    }

    if (!MemoryPatch::WriteWithProtect(*m_memory, m_hook_address, patch_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to write network text hook patch bytes");
        return false;
    }

    m_is_installed = true;
    return true;
}

bool NetworkTextHook::RemoveHook()
{
    if (!m_is_installed)
    {
        return true;
    }

    RestoreOriginalFunction();

    if (m_detour_address != 0)
    {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
    }

    if (m_backup_address != 0)
    {
        m_memory->FreeMemory(m_backup_address, 256);
        m_backup_address = 0;
    }

    m_is_installed = false;
    return true;
}

bool NetworkTextHook::ReapplyPatch()
{
    if (m_original_bytes.empty())
    {
        return false;
    }

    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9);
    const uint32_t jump_offset = Rel32From(m_hook_address, m_detour_address);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<const uint8_t*>(&jump_offset),
                       reinterpret_cast<const uint8_t*>(&jump_offset) + sizeof(uint32_t));

    while (patch_bytes.size() < m_original_bytes.size())
    {
        patch_bytes.push_back(0x90);
    }

    if (!MemoryPatch::WriteWithProtect(*m_memory, m_hook_address, patch_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to reapply network text hook patch");
        return false;
    }

    return true;
}

bool NetworkTextHook::IsPatched() const
{
    if (m_hook_address == 0 || m_detour_address == 0 || m_original_bytes.empty())
    {
        return false;
    }

    std::vector<uint8_t> cur(m_original_bytes.size());
    if (!m_memory->ReadMemory(m_hook_address, cur.data(), cur.size()))
    {
        return false;
    }
    if (cur.size() < 5 || cur[0] != 0xE9)
    {
        return false;
    }

    uint32_t rel = 0;
    std::memcpy(&rel, &cur[1], sizeof(uint32_t));
    return rel == Rel32From(m_hook_address, m_detour_address);
}

bool NetworkTextHook::PollNetworkText()
{
    if (!m_is_installed || m_backup_address == 0)
    {
        return false;
    }

    uint8_t flag = 0;
    if (!m_memory->ReadMemory(m_backup_address + kFlagOffset, &flag, sizeof(flag)))
    {
        return false;
    }

    if (flag == 0)
    {
        return false;
    }

    uint8_t zero = 0;
    m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));

    uint32_t text_ptr_raw = 0;
    uint32_t category_ptr_raw = 0;
    if (!m_memory->ReadMemory(m_backup_address + kTextRegisterOffset, &text_ptr_raw, sizeof(text_ptr_raw)))
    {
        return false;
    }
    if (!m_memory->ReadMemory(m_backup_address + kCategoryRegisterOffset, &category_ptr_raw, sizeof(category_ptr_raw)))
    {
        return false;
    }

    const uintptr_t text_ptr = static_cast<uintptr_t>(text_ptr_raw);
    const uintptr_t category_ptr = static_cast<uintptr_t>(category_ptr_raw);

    m_last_capture.text_ptr = text_ptr;
    m_last_capture.category_ptr = category_ptr;

    std::string category;
    std::string text;
    if (category_ptr != 0)
    {
        if (!m_memory->ReadString(category_ptr, category, kMaxCategoryLength))
        {
            category.clear();
        }
    }
    const std::string category_raw = category;
    const bool category_has_raw = !category_raw.empty();
    const bool category_valid = category_has_raw ? IsValidUtf8(category_raw) : true;
    if (!category_valid)
    {
        category.clear();
    }
    if (text_ptr != 0)
    {
        if (!m_memory->ReadString(text_ptr, text, kMaxTextLength))
        {
            text.clear();
        }
    }
    const std::string text_raw = text;
    const bool text_has_raw = !text_raw.empty();
    const bool text_valid = text_has_raw ? IsValidUtf8(text_raw) : true;
    if (!text_valid)
    {
        text.clear();
    }

    m_last_capture.category = std::move(category);
    m_last_capture.text = std::move(text);
    m_last_capture.category_raw_hex = BytesToHex(category_raw);
    m_last_capture.text_raw_hex = BytesToHex(text_raw);
    m_last_capture.category_strategy = category_has_raw ? (category_valid ? "utf8" : "discard") : "empty";
    m_last_capture.text_strategy = text_has_raw ? (text_valid ? "utf8" : "discard") : "empty";

    WriteNetworkLog(m_last_capture);
    return category_has_raw || text_has_raw;
}

bool NetworkTextHook::FindNetworkTriggerAddress()
{
    PatternFinder finder(m_memory);
    const auto& pattern = Signatures::GetNetworkText();

    if (auto addr = finder.FindInModule(pattern, "DQXGame.exe"))
    {
        m_hook_address = *addr;
        return true;
    }

    if (auto addr = finder.FindInProcessExec(pattern))
    {
        m_hook_address = *addr;
        return true;
    }

    if (auto addr = finder.FindWithFallback(pattern, "DQXGame.exe", 64u * 1024u * 1024u))
    {
        m_hook_address = *addr;
        return true;
    }

    return false;
}

bool NetworkTextHook::AllocateDetourMemory()
{
    m_detour_address = m_memory->AllocateMemory(4096, true);
    if (m_detour_address == 0)
    {
        return false;
    }

    m_backup_address = m_memory->AllocateMemory(256, false);
    if (m_backup_address == 0)
    {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
        return false;
    }

    uint8_t zero = 0;
    m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));

    return true;
}

bool NetworkTextHook::WriteDetourCode()
{
    auto bytecode = CreateDetourBytecode();
    if (bytecode.empty())
    {
        return false;
    }

    if (!m_memory->WriteMemory(m_detour_address, bytecode.data(), bytecode.size()))
    {
        return false;
    }

    m_memory->FlushInstructionCache(m_detour_address, bytecode.size());
    return true;
}

bool NetworkTextHook::PatchOriginalFunction() { return EnablePatch(); }

void NetworkTextHook::RestoreOriginalFunction()
{
    if (m_hook_address != 0 && !m_original_bytes.empty())
    {
        m_memory->WriteMemory(m_hook_address, m_original_bytes.data(), m_original_bytes.size());
    }
}

std::vector<uint8_t> NetworkTextHook::CreateDetourBytecode()
{
    std::vector<uint8_t> code;
    EmitRegisterBackup(code);
    EmitNewDataFlag(code);
    EmitRegisterRestore(code);
    EmitStolenInstructions(code);
    EmitReturnJump(code);
    return code;
}

void NetworkTextHook::EmitRegisterBackup(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.movToMem(X86CodeBuilder::Register::EAX, static_cast<uint32_t>(m_backup_address));
    builder.movToMem(X86CodeBuilder::Register::EBX, static_cast<uint32_t>(m_backup_address + 4));
    builder.movToMem(X86CodeBuilder::Register::ECX, static_cast<uint32_t>(m_backup_address + 8));
    builder.movToMem(X86CodeBuilder::Register::EDX, static_cast<uint32_t>(m_backup_address + 12));
    builder.movToMem(X86CodeBuilder::Register::ESI, static_cast<uint32_t>(m_backup_address + 16));
    builder.movToMem(X86CodeBuilder::Register::EDI, static_cast<uint32_t>(m_backup_address + 20));
    builder.movToMem(X86CodeBuilder::Register::EBP, static_cast<uint32_t>(m_backup_address + 24));
    builder.movToMem(X86CodeBuilder::Register::ESP, static_cast<uint32_t>(m_backup_address + 28));
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void NetworkTextHook::EmitRegisterRestore(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.movFromMem(X86CodeBuilder::Register::EAX, static_cast<uint32_t>(m_backup_address));
    builder.movFromMem(X86CodeBuilder::Register::EBX, static_cast<uint32_t>(m_backup_address + 4));
    builder.movFromMem(X86CodeBuilder::Register::ECX, static_cast<uint32_t>(m_backup_address + 8));
    builder.movFromMem(X86CodeBuilder::Register::EDX, static_cast<uint32_t>(m_backup_address + 12));
    builder.movFromMem(X86CodeBuilder::Register::ESI, static_cast<uint32_t>(m_backup_address + 16));
    builder.movFromMem(X86CodeBuilder::Register::EDI, static_cast<uint32_t>(m_backup_address + 20));
    builder.movFromMem(X86CodeBuilder::Register::EBP, static_cast<uint32_t>(m_backup_address + 24));
    builder.movFromMem(X86CodeBuilder::Register::ESP, static_cast<uint32_t>(m_backup_address + 28));
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void NetworkTextHook::EmitNewDataFlag(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.setByteAtMem(static_cast<uint32_t>(m_backup_address + kFlagOffset), 0x01);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void NetworkTextHook::EmitStolenInstructions(std::vector<uint8_t>& code)
{
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
}

void NetworkTextHook::EmitReturnJump(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    const uintptr_t return_addr = m_hook_address + m_original_bytes.size();
    const uintptr_t jmp_from = m_detour_address + code.size();
    builder.jmpRel32(jmp_from, return_addr);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

size_t NetworkTextHook::ComputeStolenLength()
{
    if (!m_instr_safe)
    {
        return kDefaultStolenBytes;
    }

    std::vector<uint8_t> head(kDefaultStolenBytes);
    if (!m_memory->ReadMemory(m_hook_address, head.data(), head.size()))
    {
        return kDefaultStolenBytes;
    }
    return kDefaultStolenBytes;
}

} // namespace dqxclarity
