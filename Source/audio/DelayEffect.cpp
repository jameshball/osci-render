#include "DelayEffect.h"

DelayEffect::DelayEffect() {}

DelayEffect::~DelayEffect() {}

Vector2 DelayEffect::apply(int index, Vector2 vector, const std::vector<double>& values, double sampleRate) {
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

    Vector2 echo = delayBuffer[position];
    vector = Vector2(
        vector.x + echo.x * decay,
        vector.y + echo.y * decay
    );

    delayBuffer[head] = vector;

    head++;
    position++;
    samplesSinceLastDelay++;

    return vector;
}
