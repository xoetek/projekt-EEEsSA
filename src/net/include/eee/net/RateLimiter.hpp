#pragma once

#include <algorithm>

namespace eee::net {

// Simple deterministic token bucket. `nowSeconds` is supplied by the caller so
// tests and servers can use their own monotonic clock.
class TokenBucket {
public:
    TokenBucket(double refillPerSecond = 1.0, double burst = 1.0)
        : refillPerSecond_(refillPerSecond > 0.0 ? refillPerSecond : 1.0),
          burst_(burst > 0.0 ? burst : 1.0),
          tokens_(burst_) {}

    bool consume(double nowSeconds, double amount = 1.0) {
        if (initialized_) {
            const double elapsed = std::max(0.0, nowSeconds - lastSeconds_);
            tokens_ = std::min(burst_, tokens_ + elapsed * refillPerSecond_);
        }
        initialized_ = true;
        lastSeconds_ = nowSeconds;
        if (tokens_ < amount) {
            return false;
        }
        tokens_ -= amount;
        return true;
    }

    [[nodiscard]] double tokens() const { return tokens_; }
    [[nodiscard]] double burst() const { return burst_; }

private:
    double refillPerSecond_;
    double burst_;
    double tokens_;
    double lastSeconds_ = 0.0;
    bool initialized_ = false;
};

} // namespace eee::net
