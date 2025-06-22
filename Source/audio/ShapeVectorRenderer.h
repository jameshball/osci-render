#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "../visualiser/VisualiserParameters.h"

class ShapeVectorRenderer {
public:
    ShapeVectorRenderer(double sampleRate = 44100.0, double frequency = 60.0);
    
    // Set new shapes to render
    void setShapes(std::vector<std::unique_ptr<osci::Shape>> newShapes);
    
    // Get the next point in the shape vector sequence
    osci::Point nextVector();
    
    // Update sample rate if it changes
    void setSampleRate(double newSampleRate);
    
    // Set the frequency at which shapes should be drawn
    void setFrequency(double newFrequency);
    
private:
    double currentSampleRate = 44100.0;
    double frequency = 60.0;
    
    std::vector<std::unique_ptr<osci::Shape>> shapes;
    double shapesLength = 0.0;
    int currentShape = 0;
    double shapeDrawn = 0.0;
    double frameDrawn = 0.0;
    
    void incrementShapeDrawing();
};
