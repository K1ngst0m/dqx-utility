#include "Signatures.hpp"

namespace dqxclarity {

std::unordered_map<std::string, Pattern> Signatures::s_signatures;
bool Signatures::s_initialized = false;

void Signatures::InitializeSignatures() {
    if (s_initialized) {
        return;
    }

    s_signatures["dialog_trigger"] = Pattern::FromString(
        "FF ?? ?? C7 45 ?? 00 00 00 00 C7 45 ?? FD FF FF FF E8"
    );

    s_signatures["integrity_check"] = Pattern::FromString(
        "89 54 24 FC 8D 64 24 FC 8D 64 24 FC 89 0C 24 8D 64 24 FC 89 04 24 E9 ?? ?? ?? ?? 6A 03"
    );

    s_signatures["network_text"] = Pattern::FromString(
        "51 51 8B C4 89 10 8B CF"
    );

    s_signatures["quest_text"] = Pattern::FromString(
        "8D 8E 78 04 00 00 E8 ?? ?? ?? ?? 5F"
    );

    s_signatures["corner_text"] = Pattern::FromString(
        "8B D0 8D 5A 01 66 90 8A 0A 42 84 C9 75 F9 2B D3 0F"
    );

    // UTF-8 for "動画配信の際はサーバー" as used by Python's notice_string
    s_signatures["notice_string"] = Pattern::FromString(
        "E5 8B 95 E7 94 BB E9 85 8D E4 BF A1 E3 81 AE E9 9A 9B E3 81 AF E3 82 B5 E3 83 BC E3 83 90 E3 83 BC"
    );

    // Simplified walkthrough pattern (post-login indicator)
    s_signatures["walkthrough"] = Pattern::FromString(
        "04 02 ?? ?? 10 00 00 00 80 ?? ?? ?? 00 00 00 00 ??"
    );

    s_initialized = true;
}

const Pattern& Signatures::GetDialogTrigger() {
    InitializeSignatures();
    return s_signatures["dialog_trigger"];
}

const Pattern& Signatures::GetIntegrityCheck() {
    InitializeSignatures();
    return s_signatures["integrity_check"];
}

const Pattern& Signatures::GetNetworkText() {
    InitializeSignatures();
    return s_signatures["network_text"];
}

const Pattern& Signatures::GetQuestText() {
    InitializeSignatures();
    return s_signatures["quest_text"];
}

const Pattern& Signatures::GetCornerText() {
    InitializeSignatures();
    return s_signatures["corner_text"];
}

const Pattern& Signatures::GetNoticeString() {
    InitializeSignatures();
    return s_signatures["notice_string"];
}

const Pattern& Signatures::GetWalkthroughPattern() {
    InitializeSignatures();
    return s_signatures["walkthrough"];
}

const Pattern* Signatures::GetSignature(const std::string& name) {
    InitializeSignatures();
    auto it = s_signatures.find(name);
    if (it != s_signatures.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace dqxclarity
