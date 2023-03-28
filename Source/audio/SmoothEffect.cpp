#include "SmoothEffect.h"

SmoothEffect::SmoothEffect() {
	this->window = std::vector<Vector2>(MAX_WINDOW_SIZE);
}

SmoothEffect::~SmoothEffect() {}

Vector2 SmoothEffect::apply(int index, Vector2 input, double value, double frequency, double sampleRate) {
    int newWindowSize = (int)(256 * value);
	windowSize = std::max(1, std::min(MAX_WINDOW_SIZE, newWindowSize));
	
    window[head++] = input;
    if (head >= MAX_WINDOW_SIZE) {
        head = 0;
    }
    double totalX = 0;
    double totalY = 0;
    int newHead = head - 1;
    for (int i = 0; i < windowSize; i++) {
        if (newHead < 0) {
            newHead = MAX_WINDOW_SIZE - 1;
        }
        
        totalX += window[newHead].x;
        totalY += window[newHead].y;

        newHead--;
    }

    return Vector2(totalX / windowSize, totalY / windowSize);
}
