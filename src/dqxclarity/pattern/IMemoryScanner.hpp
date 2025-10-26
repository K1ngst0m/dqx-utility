#pragma once

#include "Pattern.hpp"
#include <cstdint>
#include <optional>
#include <vector>
#include <memory>

namespace dqxclarity
{

class IMemoryScanner
{
public:
    virtual ~IMemoryScanner() = default;

    virtual std::optional<uintptr_t> ScanProcess(const Pattern& pattern, bool require_executable) = 0;
    virtual std::vector<uintptr_t> ScanProcessAll(const Pattern& pattern, bool require_executable) = 0;
};

class ProcessMemoryScanner : public IMemoryScanner
{
public:
    explicit ProcessMemoryScanner(std::shared_ptr<class IProcessMemory> memory);

    std::optional<uintptr_t> ScanProcess(const Pattern& pattern, bool require_executable) override;
    std::vector<uintptr_t> ScanProcessAll(const Pattern& pattern, bool require_executable) override;

private:
    std::shared_ptr<class IProcessMemory> memory_;
};

} // namespace dqxclarity

