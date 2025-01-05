std::string outputFragmentShader = R"(

uniform sampler2D uTexture0; //line
uniform sampler2D uTexture1; //tight glow
uniform sampler2D uTexture2; //big glow
uniform sampler2D uTexture3; //screen
uniform sampler2D uTexture4; //reflection
uniform sampler2D uTexture5; //screen glow
uniform float uExposure;
uniform float uSaturation;
uniform float uNoise;
uniform float uRandom;
uniform float uGlow;
uniform float uAmbient;
uniform float uFishEye;
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

float noise(vec2 texCoord, float time) {
    // Combine texture coordinate and time to create a unique seed
    float seed = dot(texCoord, vec2(12.9898, 78.233)) + time;
    
    // Use fract and sin to generate a pseudo-random value
    return fract(sin(seed) * 43758.5453) - 0.5;
}

vec4 max4(vec4 a, vec4 b) {
	return vec4(max(a.r, b.r), max(a.g, b.g), max(a.b, b.b), max(a.a, b.a));
}

void main() {
    vec2 linePos = (vTexCoordCanvas - 0.5) / uScale + 0.5 + uOffset;
    
    // fish eye distortion
    vec2 uv = linePos - vec2(0.5);
    float uva = atan(uv.x, uv.y);
    float uvd = sqrt(dot(uv, uv));
    uvd = uvd * (1.0 + uFishEye * uvd * uvd);
    linePos = vec2(0.5) + vec2(sin(uva), cos(uva)) * uvd;
    
    vec4 line = texture2D(uTexture0, linePos);
    // r components have grid; g components do not.
    vec4 screen = texture2D(uTexture3, vTexCoord);
    vec4 tightGlow = texture2D(uTexture1, linePos);
    vec4 scatter = texture2D(uTexture2, linePos) + (1.0 - uRealScreen) * max(uAmbient - 0.35, 0.0);
    
    if (uRealScreen > 0.5) {
        vec4 reflection = texture2D(uTexture4, vTexCoord);
        vec4 screenGlow = texture2D(uTexture5, vTexCoord);
        scatter += max4(screenGlow * reflection * max(1.0 - uAmbient, 0.0), vec4(0.0));
    }
    
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
    float noiseR = noise(gl_FragCoord.xy * 0.01, uRandom * 100.0);
    float noiseG = noise(gl_FragCoord.xy * 0.005, uRandom * 50.0);
    float noiseB = noise(gl_FragCoord.xy * 0.07, uRandom * 80.0);
    gl_FragColor.rgb += uNoise * vec3(noiseR, noiseG, noiseB);
    gl_FragColor.a = 1.0;
}

)";
