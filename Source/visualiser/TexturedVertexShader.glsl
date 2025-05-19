std::string texturedVertexShader = R"(

attribute vec2 aPos;
varying vec2 vTexCoord;
uniform float uResizeForCanvas;
uniform float uCropEnabled;
uniform vec4 uCropRect; // x, y, width, height

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    
    // Calculate normalized screen coordinates from -1,1 to 0,1 range
    vec2 texCoord = 0.5 * aPos + 0.5;
    
    // Apply crop if enabled
    if (uCropEnabled > 0.5) {
        // Map the current full-screen quad texture coordinates to the cropped region
        // For example, if we want to see just the middle 50% of the texture:
        // texCoord (0,0) -> (0.25,0.25) and texCoord (1,1) -> (0.75,0.75)
        texCoord = uCropRect.xy + texCoord * uCropRect.zw;
    }
    
    // Apply canvas resize factor if needed
    vTexCoord = texCoord * uResizeForCanvas;
}

)";
