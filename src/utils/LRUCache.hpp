#pragma once

#include <unordered_map>
#include <list>
#include <utility>
#include <cstddef>

// Simple count-bounded LRU cache
// Not thread-safe; intended for UI thread use only.
template <typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(std::size_t capacity = 5000) : capacity_(capacity) {}

    void setCapacity(std::size_t cap) { capacity_ = cap; trim(); }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return items_.size(); }

    bool get(const K& key, V& out) {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        items_.splice(items_.begin(), items_, it->second);
        out = it->second->second;
        return true;
    }

    void put(const K& key, const V& val) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = val;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        items_.emplace_front(key, val);
        map_[items_.front().first] = items_.begin();
        trim();
    }

    void clear() {
        items_.clear();
        map_.clear();
    }

private:
    void trim() {
        while (capacity_ > 0 && items_.size() > capacity_) {
            auto last = items_.end();
            --last;
            map_.erase(last->first);
            items_.pop_back();
        }
    }

    std::size_t capacity_;
    std::list<std::pair<K,V>> items_;
    std::unordered_map<K, typename std::list<std::pair<K,V>>::iterator> map_;
};