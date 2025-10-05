#pragma once

#include "../pattern/Pattern.hpp"
#include <string>
#include <unordered_map>

namespace dqxclarity {

class Signatures {
public:
    static const Pattern& GetDialogTrigger();
    static const Pattern& GetIntegrityCheck();
    static const Pattern& GetNetworkText();
    static const Pattern& GetQuestText();
    static const Pattern& GetCornerText();
    static const Pattern& GetNoticeString();

    static const Pattern* GetSignature(const std::string& name);

private:
    static void InitializeSignatures();
    static std::unordered_map<std::string, Pattern> s_signatures;
    static bool s_initialized;
};

} // namespace dqxclarity
