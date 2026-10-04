#pragma once

namespace motion {
// The last value computed for a time. The beam renderer draws a whole plan at
// one timeline time, so prepared stages evaluate their curves once per plan
// instead of once per sample. Each prepared snapshot is rendered by one
// thread at a time, so the cache is not shared.
template <typename Value>
class TimeCache {
public:
    template <typename Compute>
    const Value& at(double time, Compute&& compute) const {
        if (!valid || time != cachedTime) {
            value = compute();
            cachedTime = time;
            valid = true;
        }
        return value;
    }

private:
    mutable Value value {};
    mutable double cachedTime = 0;
    mutable bool valid = false;
};
}
