std::string texturedFragmentShader = R"(

uniform sampler2D uTexture0;
varying vec2 vTexCoord;
uniform float uCropEnabled;

void main() {
    gl_FragColor = texture2D(uTexture0, vTexCoord);
    gl_FragColor.a = 1.0;
}

)";
