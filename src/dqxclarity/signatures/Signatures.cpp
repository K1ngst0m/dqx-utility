#include "Signatures.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <cctype>

namespace dqxclarity
{

static inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

static inline bool ParseTomlLine(const std::string& line, std::string& key, std::string& value)
{
    // Accept lines like: key = "value"
    auto pos = line.find('=');
    if (pos == std::string::npos)
        return false;
    key = Trim(line.substr(0, pos));
    std::string rhs = Trim(line.substr(pos + 1));
    if (key.empty())
        return false;
    if (!rhs.empty() && rhs[0] == '"')
    {
        // Find closing quote (no escape handling needed for our patterns)
        auto endq = rhs.find('"', 1);
        if (endq == std::string::npos)
            return false;
        value = rhs.substr(1, endq - 1);
        return true;
    }
    // Fallback: unquoted
    value = rhs;
    return !value.empty();
}

static bool LoadSignaturesFromToml(const std::string& path, std::unordered_map<std::string, Pattern>& out)
{
    std::ifstream in(path);
    if (!in.is_open())
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::string t = Trim(line);
        if (t.empty())
            continue;
        if (t[0] == '#')
            continue;
        if (t[0] == '[')
            continue; // ignore sections
        std::string key, val;
        if (!ParseTomlLine(t, key, val))
            continue;
        auto pat = Pattern::FromString(val);
        if (pat.IsValid())
            out[key] = std::move(pat);
    }
    return !out.empty();
}

std::unordered_map<std::string, Pattern> Signatures::s_signatures;
bool Signatures::s_initialized = false;

void Signatures::InitializeSignatures()
{
    if (s_initialized)
    {
        return;
    }

    // Preferred: load from assets/signatures.toml (relative to working directory)
    if (!LoadSignaturesFromToml("assets/signatures.toml", s_signatures))
    {
        // Fallback: initialize built-in defaults if file missing
        s_signatures["dialog_trigger"] = Pattern::FromString("FF ?? ?? C7 45 ?? 00 00 00 00 C7 45 ?? FD FF FF FF E8");
        s_signatures["integrity_check"] = Pattern::FromString(
            "89 54 24 FC 8D 64 24 FC 89 4C 24 FC 8D 64 24 FC 8D 64 24 FC 89 04 24 E9 ?? ?? ?? ?? 89");
        s_signatures["network_text"] = Pattern::FromString("51 51 8B C4 89 10 8B CF");
        s_signatures["network_text_trigger"] = Pattern::FromString("8B CA 8D 71 ?? 8A 01 41 84 C0 75 F9 EB 20");
        s_signatures["quest_text"] = Pattern::FromString("8D 8E 78 04 00 00 E8 ?? ?? ?? ?? 5F");
        s_signatures["corner_text"] = Pattern::FromString("8B D0 8D 5A 01 66 90 8A 0A 42 84 C9 75 F9 2B D3 0F");
        s_signatures["corner_text_trigger"] = Pattern::FromString("8B D0 8D 5A 01 66 90 8A 0A 42 84 C9 75 F9 2B D3 0F");
        s_signatures["notice_string"] = Pattern::FromString(
            "E5 8B 95 E7 94 BB E9 85 8D E4 BF A1 E3 81 AE E9 9A 9B E3 81 AF E3 82 B5 E3 83 BC E3 83 90 E3 83 BC");
        s_signatures["walkthrough"] = Pattern::FromString("04 02 ?? ?? 10 00 00 00 80 ?? ?? ?? 00 00 00 00 ??");
    }

    s_initialized = true;
}

const Pattern& Signatures::GetDialogTrigger()
{
    InitializeSignatures();
    return s_signatures["dialog_trigger"];
}

const Pattern& Signatures::GetIntegrityCheck()
{
    InitializeSignatures();
    return s_signatures["integrity_check"];
}

const Pattern& Signatures::GetNetworkText()
{
    InitializeSignatures();
    auto it = s_signatures.find("network_text_trigger");
    if (it != s_signatures.end())
    {
        return it->second;
    }
    return s_signatures["network_text"];
}

const Pattern& Signatures::GetQuestText()
{
    InitializeSignatures();
    return s_signatures["quest_text"];
}

const Pattern& Signatures::GetCornerText()
{
    InitializeSignatures();
    auto it = s_signatures.find("corner_text_trigger");
    if (it != s_signatures.end())
    {
        return it->second;
    }
    return s_signatures["corner_text"];
}

const Pattern& Signatures::GetNoticeString()
{
    InitializeSignatures();
    return s_signatures["notice_string"];
}

const Pattern& Signatures::GetWalkthroughPattern()
{
    InitializeSignatures();
    return s_signatures["walkthrough"];
}

const Pattern* Signatures::GetSignature(const std::string& name)
{
    InitializeSignatures();
    auto it = s_signatures.find(name);
    if (it != s_signatures.end())
    {
        return &it->second;
    }
    return nullptr;
}

} // namespace dqxclarity
