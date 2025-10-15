#pragma once

#include <memory>
#include <string>

class SingleInstanceGuard
{
public:
    static std::unique_ptr<SingleInstanceGuard> Acquire();
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

private:
    SingleInstanceGuard();

#ifdef _WIN32
    SingleInstanceGuard(void* handle, std::wstring name);
    void* mutex_handle_ = nullptr;
    std::wstring mutex_name_;
#endif
};
