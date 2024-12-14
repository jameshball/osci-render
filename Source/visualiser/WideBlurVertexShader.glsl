std::string wideBlurVertexShader = R"(

attribute vec2 aPos;
varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = (0.5*aPos+0.5);
}

)";
