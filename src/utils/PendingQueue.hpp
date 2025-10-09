#pragma once

#include <vector>
#include <mutex>
#include <iterator>
#include <utility>
#include <cstddef>

template <typename T>
class PendingQueue {
public:
    void push(T&& item) {
        std::lock_guard<std::mutex> lock(m_);
        q_.push_back(std::move(item));
    }

    void drain(std::vector<T>& out) {
        std::lock_guard<std::mutex> lock(m_);
        out.insert(out.end(), std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.end()));
        q_.clear();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_);
        return q_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(m_);
        return q_.size();
    }

private:
    mutable std::mutex m_;
    std::vector<T> q_;
};
