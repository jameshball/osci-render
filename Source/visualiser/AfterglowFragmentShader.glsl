std::string afterglowFragmentShader = R"(

uniform sampler2D uTexture0;
varying vec2 vTexCoord;
uniform float fadeAmount;
uniform float afterglowAmount;

// tanh is not available in GLSL ES 1.0, so we define it here.
float hypTan(float x) {
    return (exp(x) - exp(-x)) / (exp(x) + exp(-x));
}

void main() {
    vec4 line = texture2D(uTexture0, vTexCoord);
    float fade = fadeAmount * hypTan(min(line.r / afterglowAmount, 10.0));
    fade = clamp(fade, 0.0, fadeAmount);
    
    gl_FragColor = vec4(0.0, 0.0, 0.0, fade);
}

)";
