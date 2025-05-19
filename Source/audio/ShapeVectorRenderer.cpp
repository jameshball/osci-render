#include "ShapeVectorRenderer.h"

ShapeVectorRenderer::ShapeVectorRenderer(double sampleRate, double frequency)
    : currentSampleRate(sampleRate), frequency(frequency) {
}

void ShapeVectorRenderer::setShapes(std::vector<std::unique_ptr<osci::Shape>> newShapes) {
    shapes.clear();
    
    // Move the shapes from the input vector to our internal vector
    for (auto& shape : newShapes) {
        shapes.push_back(std::move(shape));
    }
    
    // Calculate the total length of all shapes
    shapesLength = osci::Shape::totalLength(shapes);
    
    // Reset the drawing state
    currentShape = 0;
    shapeDrawn = 0.0;
    frameDrawn = 0.0;
}

osci::Point ShapeVectorRenderer::nextVector() {
    osci::Point point = { 0.0, 0.0, 1.0f };
    
    if (shapes.size() <= 0) {
        return point;
    }
    
    if (currentShape < shapes.size()) {
        auto& shape = shapes[currentShape];
        double length = shape->length();
        double drawingProgress = length == 0.0 ? 1.0 : shapeDrawn / length;
        point = shape->nextVector(drawingProgress);
        point.z = 1.0f;
    }
    
    incrementShapeDrawing();
    
    if (frameDrawn >= shapesLength) {
        frameDrawn -= shapesLength;
        currentShape = 0;
    }
    
    return point;
}

void ShapeVectorRenderer::setSampleRate(double newSampleRate) {
    currentSampleRate = newSampleRate;
}

void ShapeVectorRenderer::setFrequency(double newFrequency) {
    frequency = newFrequency;
}

void ShapeVectorRenderer::incrementShapeDrawing() {
    if (shapes.size() <= 0) return;
    
    double length = currentShape < shapes.size() ? shapes[currentShape]->len : 0.0;
    double lengthIncrement = shapesLength / (currentSampleRate / frequency);
    
    frameDrawn += lengthIncrement;
    shapeDrawn += lengthIncrement;

    // Need to skip all shapes that the lengthIncrement draws over.
    // This is especially an issue when there are lots of small lines being
    // drawn.
    while (shapeDrawn > length) {
        shapeDrawn -= length;
        currentShape++;
        if (currentShape >= shapes.size()) {
            currentShape = 0;
        }
        length = shapes[currentShape]->len;
    }
}
