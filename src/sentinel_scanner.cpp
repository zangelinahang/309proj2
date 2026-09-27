#include "core/sentinel_scanner.h"

#include <utility>

SentinelScanner::SentinelScanner(std::string sentinel)
    : sentinel_(std::move(sentinel)) {}

SentinelScanner::Out SentinelScanner::feed(std::string_view chunk) {
    if (found_) {
        return {"", true};
    }

    // An empty sentinel is considered matched immediately. This also avoids
    // subtracting one from sentinel_.size() below.
    if (sentinel_.empty()) {
        pending_.clear();
        found_ = true;
        return {"", true};
    }

    std::string combined;
    combined.reserve(pending_.size() + chunk.size());
    combined += pending_;
    combined.append(chunk.data(), chunk.size());

    const std::size_t match = combined.find(sentinel_);
    if (match != std::string::npos) {
        std::string safe = combined.substr(0, match);
        pending_.clear();
        found_ = true;
        return {std::move(safe), true};
    }

    const std::size_t max_pending = sentinel_.size() - 1;
    if (combined.size() <= max_pending) {
        pending_ = std::move(combined);
        return {"", false};
    }

    const std::size_t safe_count = combined.size() - max_pending;
    std::string safe = combined.substr(0, safe_count);
    pending_ = combined.substr(safe_count);
    return {std::move(safe), false};
}

SentinelScanner::Out SentinelScanner::flush() {
    if (found_) {
        return {"", true};
    }

    std::string safe = std::move(pending_);
    pending_.clear();
    return {std::move(safe), false};
}
