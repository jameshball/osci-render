std::string outputFragmentShader = R"(

uniform sampler2D uTexture0; //line
uniform sampler2D uTexture1; //tight glow
uniform sampler2D uTexture2; //big glow
uniform sampler2D uTexture3; //screen
uniform float uExposure;
uniform float uSaturation;
uniform float uNoise;
uniform float uTime;
uniform float uGlow;
uniform float uAmbient;
uniform float uRealScreen;
uniform vec2 uOffset;
uniform vec2 uScale;
uniform vec3 uColour;
varying vec2 vTexCoord;
varying vec2 vTexCoordCanvas;

vec3 desaturate(vec3 color, float factor) {
    vec3 lum = vec3(0.299, 0.587, 0.114);
    vec3 gray = vec3(dot(lum, color));
    return vec3(mix(color, gray, factor));
}

float noise(in vec2 uv, in float time) {
    return (fract(sin(dot(uv, vec2(12.9898,78.233)*2.0 + time)) * 43758.5453)) - 0.5;
}

void main() {
    vec2 linePos = (vTexCoordCanvas - 0.5) / uScale + 0.5 + uOffset;
    vec4 line = texture2D(uTexture0, linePos);
    // r components have grid; g components do not.
    vec4 screen = texture2D(uTexture3, vTexCoord);
    vec4 tightGlow = texture2D(uTexture1, linePos);
    vec4 scatter = texture2D(uTexture2, linePos) + (1.0 - uRealScreen) * max(uAmbient - 0.45, 0.0);
    float light = line.r + uGlow * 1.5 * screen.g * screen.g * tightGlow.r;
    light += uGlow * 0.3 * scatter.g * (2.0 + 1.0 * screen.g + 0.5 * screen.r);
    float tlight = 1.0-pow(2.0, -uExposure*light);
    float tlight2 = tlight * tlight * tlight;
    gl_FragColor.rgb = mix(uColour, vec3(1.0), 0.3+tlight2*tlight2*0.5) * tlight;
    if (uRealScreen > 0.5) {
        float ambient = pow(2.0, uExposure) * uAmbient;
        gl_FragColor.rgb += ambient * screen.rgb;
    }
    gl_FragColor.rgb = desaturate(gl_FragColor.rgb, 1.0 - uSaturation);
    gl_FragColor.rgb += uNoise * noise(gl_FragCoord.xy, uTime);
    gl_FragColor.a = 1.0;
}

)";
