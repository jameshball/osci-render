#pragma once

#include <numbers>

class MathUtil {
public:
    
    // from https://stackoverflow.com/questions/11980292/how-to-wrap-around-a-range
    static inline double wrapAngle(double angle) {
        double twoPi = 2.0 * std::numbers::pi;
        return angle - twoPi * floor(angle / twoPi);
    }
};
