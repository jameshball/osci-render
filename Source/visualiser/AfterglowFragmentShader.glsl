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
    float luminance = max(max(line.r, line.g), line.b);
    float adjustedAfterglow = afterglowAmount * 1.5; // scale to make it closely match old shader
    float x = min(luminance / adjustedAfterglow, 10.0);
    float minFade = 0.1 * (1.0 - clamp(adjustedAfterglow / 10.0, 0.0, 1.0));
    float fade = fadeAmount * ((1.0 - minFade) * hypTan(x) + minFade);
    fade = clamp(fade, 0.0, fadeAmount);
    
    gl_FragColor = vec4(0.0, 0.0, 0.0, fade);
}

)";
