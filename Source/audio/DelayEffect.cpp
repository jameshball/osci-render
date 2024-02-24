#include "DelayEffect.h"

DelayEffect::DelayEffect() {}

DelayEffect::~DelayEffect() {}

Point DelayEffect::apply(int index, Point vector, const std::vector<double>& values, double sampleRate) {
    double decay = values[0];
    double decayLength = values[1];
	int delayBufferLength = (int)(sampleRate * decayLength);
    if (head >= delayBuffer.size()){
        head = 0;
    }
    if (position >= delayBuffer.size()){
        position = 0;
    }
    if (samplesSinceLastDelay >= delayBufferLength) {
        samplesSinceLastDelay = 0;
        position = head - delayBufferLength;
        if (position < 0) {
            position += delayBuffer.size();
        }
    }

    Point echo = delayBuffer[position];
    vector = Point(
        vector.x + echo.x * decay,
        vector.y + echo.y * decay,
        vector.z + echo.z * decay
    );

    delayBuffer[head] = vector;

    head++;
    position++;
    samplesSinceLastDelay++;

    return vector;
}
