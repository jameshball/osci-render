std::string texturedFragmentShader = R"(

uniform sampler2D uTexture0;
varying vec2 vTexCoord;
uniform float uCropEnabled;
uniform float uPreserveAlpha;

void main() {
    gl_FragColor = texture2D(uTexture0, vTexCoord);
    if (uPreserveAlpha < 0.5) {
        gl_FragColor.a = 1.0;
    }
}

)";
