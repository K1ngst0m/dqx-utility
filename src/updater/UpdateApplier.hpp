#pragma once

#include <functional>
#include <string>

namespace updater
{

using ApplyCallback = std::function<void(bool success, const std::string& message)>;

class UpdateApplier
{
public:
    UpdateApplier(const std::string& appDir);
    ~UpdateApplier() = default;

    bool applyUpdate(const std::string& packagePath, const std::string& configTemplatePath,
                    ApplyCallback callback, std::string& outError);

private:
    std::string appDir_;

    bool generateBatchScript(const std::string& packagePath, const std::string& configTemplatePath,
                            std::string& outScriptPath, std::string& outError);
    bool launchBatchScript(const std::string& scriptPath, std::string& outError);
};

} // namespace updater
