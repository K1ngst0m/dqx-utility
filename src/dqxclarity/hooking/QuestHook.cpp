#include "QuestHook.hpp"

#include "../signatures/Signatures.hpp"
#include "../pattern/PatternFinder.hpp"
#include "../util/Profile.hpp"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace dqxclarity
{

namespace
{

constexpr std::size_t kMaxLogFileSize = 10u * 1024u * 1024u;
constexpr std::size_t kLogFileBackups = 3;

void WriteQuestLog(const QuestHook::QuestData& data)
{
    static std::once_flag once;
    static std::mutex log_mutex;

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

    std::ofstream stream("logs/quest.log", std::ios::app);
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

    stream << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << " | Quest capture" << '\n';

    auto writeField = [&stream](std::string_view label, const std::string& value)
    {
        stream << "  " << label << ": ";
        if (value.empty())
        {
            stream << "(empty)" << '\n';
            return;
        }

        if (value.find('\n') == std::string::npos)
        {
            stream << value << '\n';
            return;
        }

        stream << '\n';
        std::istringstream lines(value);
        std::string line;
        while (std::getline(lines, line))
        {
            stream << "    " << line << '\n';
        }
    };

    writeField("Subquest", data.subquest_name);
    writeField("Quest", data.quest_name);
    writeField("Description", data.description);
    writeField("Rewards", data.rewards);
    writeField("Repeat Rewards", data.repeat_rewards);

    stream << "----------------------------------------" << '\n';

    stream.flush();

    try
    {
        const std::filesystem::path path{ "logs/quest.log" };
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > kMaxLogFileSize)
        {
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
    }
    catch (...)
    {
    }
}

} // namespace

QuestHook::QuestHook(std::shared_ptr<IProcessMemory> memory)
    : m_memory(std::move(memory))
{
}

QuestHook::~QuestHook()
{
    if (m_is_installed)
    {
        RemoveHook();
    }
}

bool QuestHook::InstallHook(bool enable_patch)
{
    if (m_is_installed && enable_patch)
    {
        return true;
    }

    if (!FindQuestTriggerAddress())
    {
        if (m_logger.error)
            m_logger.error("Failed to find quest trigger address");
        return false;
    }

    if (!AllocateDetourMemory())
    {
        if (m_logger.error)
            m_logger.error("Failed to allocate quest detour memory");
        return false;
    }

    const size_t stolen_bytes = ComputeStolenLength();
    m_original_bytes.resize(stolen_bytes);
    if (!m_memory->ReadMemory(m_hook_address, m_original_bytes.data(), stolen_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to read quest hook original bytes");
        return false;
    }

    if (!WriteDetourCode())
    {
        if (m_logger.error)
            m_logger.error("Failed to write quest detour code");
        return false;
    }

    if (!enable_patch)
    {
        return true;
    }

    return EnablePatch();
}

bool QuestHook::EnablePatch()
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
            m_logger.error("Failed to write quest hook patch bytes");
        return false;
    }

    if (m_verbose && m_logger.info)
    {
        std::ostringstream oss;
        oss << "Quest hook patched at 0x" << std::hex << m_hook_address << std::dec;
        m_logger.info(oss.str());
    }

    m_is_installed = true;
    return true;
}

bool QuestHook::RemoveHook()
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

bool QuestHook::ReapplyPatch()
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
            m_logger.error("Failed to reapply quest hook patch");
        return false;
    }

    return true;
}

bool QuestHook::IsPatched() const
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

bool QuestHook::PollQuestData()
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

    uint32_t quest_ptr_raw = 0;
    if (!m_memory->ReadMemory(m_backup_address, &quest_ptr_raw, sizeof(quest_ptr_raw)))
    {
        uint8_t zero = 0;
        m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));
        return false;
    }

    uint8_t zero = 0;
    m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));

    if (quest_ptr_raw == 0)
    {
        return false;
    }

    const uintptr_t quest_ptr = static_cast<uintptr_t>(quest_ptr_raw);

    QuestData data;
    auto read_field = [&](uint32_t offset, std::string& out)
    {
        std::string value;
        if (m_memory->ReadString(quest_ptr + offset, value, kMaxStringLength))
        {
            out = std::move(value);
        }
        else
        {
            out.clear();
        }
    };

    read_field(kSubquestNameOffset, data.subquest_name);
    read_field(kQuestNameOffset, data.quest_name);
    read_field(kDescriptionOffset, data.description);
    read_field(kRewardsOffset, data.rewards);
    read_field(kRepeatRewardsOffset, data.repeat_rewards);

    m_last_data = std::move(data);
    WriteQuestLog(m_last_data);
    return true;
}

bool QuestHook::FindQuestTriggerAddress()
{
    PROFILE_SCOPE_FUNCTION();
    PatternFinder finder(m_memory);
    const auto& pattern = Signatures::GetQuestText();

    // Tier 1: Module-restricted scan
    {
        PROFILE_SCOPE_CUSTOM("QuestHook.FindInModule");
        if (auto addr = finder.FindInModule(pattern, "DQXGame.exe"))
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Quest trigger found via FindInModule (Tier 1)");
            return true;
        }
    }

    // Tier 2: Executable region scan
    {
        PROFILE_SCOPE_CUSTOM("QuestHook.FindInProcessExec");
        if (auto addr = finder.FindInProcessExec(pattern))
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Quest trigger found via FindInProcessExec (Tier 2)");
            return true;
        }
    }

    // Tier 3: Naive fallback scan (SLOW)
    {
        PROFILE_SCOPE_CUSTOM("QuestHook.FindWithFallback");
        if (m_logger.warn)
            m_logger.warn("Quest trigger not found in Tier 1/2, falling back to naive scan (Tier 3)");

        if (auto addr = finder.FindWithFallback(pattern, "DQXGame.exe", 64u * 1024u * 1024u))
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Quest trigger found via FindWithFallback (Tier 3 - naive scan)");
            return true;
        }
    }

    return false;
}

bool QuestHook::AllocateDetourMemory()
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

bool QuestHook::WriteDetourCode()
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

bool QuestHook::PatchOriginalFunction() { return EnablePatch(); }

void QuestHook::RestoreOriginalFunction()
{
    if (m_hook_address != 0 && !m_original_bytes.empty())
    {
        m_memory->WriteMemory(m_hook_address, m_original_bytes.data(), m_original_bytes.size());
    }
}

std::vector<uint8_t> QuestHook::CreateDetourBytecode()
{
    std::vector<uint8_t> code;
    EmitRegisterBackup(code);
    EmitNewDataFlag(code);
    EmitRegisterRestore(code);
    EmitStolenInstructions(code);
    EmitReturnJump(code);
    return code;
}

void QuestHook::EmitRegisterBackup(std::vector<uint8_t>& code)
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

void QuestHook::EmitRegisterRestore(std::vector<uint8_t>& code)
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

void QuestHook::EmitNewDataFlag(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.setByteAtMem(static_cast<uint32_t>(m_backup_address + kFlagOffset), 0x01);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void QuestHook::EmitStolenInstructions(std::vector<uint8_t>& code)
{
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
}

void QuestHook::EmitReturnJump(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    const uintptr_t return_addr = m_hook_address + m_original_bytes.size();
    const uintptr_t jmp_from = m_detour_address + code.size();
    builder.jmpRel32(jmp_from, return_addr);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

size_t QuestHook::ComputeStolenLength()
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
