std::string outputFragmentShader = R"(

uniform sampler2D uTexture0; //line
uniform sampler2D uTexture1; //tight glow
uniform sampler2D uTexture2; //big glow
uniform sampler2D uTexture3; //screen
uniform sampler2D uTexture4; //reflection
uniform sampler2D uTexture5; //screen glow
uniform float uExposure;
uniform float uOverexposure;
uniform float uLineSaturation;
uniform float uScreenSaturation;
uniform float uNoise;
uniform float uRandom;
uniform float uGlow;
uniform float uAmbient; 
uniform vec3 uBeamColor;
uniform float uFishEye;
uniform float uRealScreen;
uniform float uHueShift;
uniform vec2 uOffset;
uniform vec2 uScale;
// uColour removed; line texture already contains RGB
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

vec3 hueShift(vec3 color, float shift) {
    vec3 p = vec3(0.55735) * dot(vec3(0.55735), color);
    vec3 u = color - p;
    vec3 v = cross(vec3(0.55735), u);

    color = u * cos(shift * 6.2832) + v * sin(shift * 6.2832) + p;
    
    return color;
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
    vec4 scatter = texture2D(uTexture2, linePos);
    
    if (uRealScreen > 0.5) {
        vec4 reflection = texture2D(uTexture4, vTexCoord);
        vec4 screenGlow = texture2D(uTexture5, vTexCoord);
        scatter += max4(screenGlow * reflection * max(1.0 - 0.5 * uAmbient, 0.0), vec4(0.0));
    }
    
    // making the range of the glow slider more useful
    float glow = 1.75 * pow(uGlow, 1.5);
    float scatterScalar = 0.3 * (2.0 + 1.0 * screen.g + 0.5 * screen.r);
    vec3 bloom = glow * ((0.25 * screen.r + 0.75 * screen.g) * tightGlow.rgb + scatter.rgb * scatterScalar);
    float screenFactor = clamp(screen.r * 4.0, 0.1, 1.0);
    vec3 light = screenFactor * line.rgb + bloom;
    // vec3 light = line.rgb + bloom;
    // tone map
    vec3 tlight = 1.0 - exp(-uExposure * light);
    // Overexposure that goes to white regardless of colour, like the old single-colour shader
    // Use a scalar brightness to drive the white clip and keep hue in a normalized base colour.
    float s = max(max(tlight.r, tlight.g), tlight.b); // perceived brightness proxy
    vec3 baseCol = s > 1e-6 ? (tlight / s) : vec3(0.0);
    // Mimic old curve: 0.3 + (brightness^6) * uOverexposure, then clamp to [0,1]
    float whiteMix = clamp(0.3 + pow(s, 3.0) * uOverexposure, 0.0, 1.0);
    vec3 colorOut = mix(baseCol, vec3(1.0), whiteMix) * s;
    gl_FragColor.rgb = desaturate(colorOut, 1.0 - uLineSaturation);
    // Ambient light:
    // - Realistic displays: tint by the screen texture (existing behavior)
    // - Non-real overlays: tint the background by the current beam colour even where there's no beam energy
    float ambient = uExposure * max(uAmbient, 0.0);
    if (ambient > 0.0) {
        if (uRealScreen > 0.5) {
            // this isn't how light works, but it looks cool
            vec3 screenCol = ambient * hueShift(screen.rgb, uHueShift);
            gl_FragColor.rgb += desaturate(screenCol, 1.0 - uScreenSaturation);
        } else {
                // Non-real overlays:
                // - screen.g carries the "screen" texture (smudge/noise)
                // - screen.a carries a graticule mask (0 on grid lines, 1 elsewhere)
                // Using alpha avoids needing to infer the grid from RGB, which can fail
                // depending on the underlying image.
                float gridMask = clamp(1.0 - screen.a, 0.0, 1.0);
                float screenMask = clamp(screen.g, 0.0, 1.0);
                float base = 0.15;
                float ambientMask = base + (1.0 - base) * screenMask;

                // Keep graticule dark: reduce ambient where grid lines exist.
                float gridDarken = 0.15; // 0 = no effect, 1 = maximum darkening on grid
                float maskedAmbient = ambientMask * (1.0 - gridDarken * gridMask);
                vec3 bgCol = hueShift(uBeamColor, uHueShift);
                bgCol = desaturate(bgCol, 1.0 - uScreenSaturation);
                gl_FragColor.rgb += ambient * maskedAmbient * bgCol;
        }
    }
    gl_FragColor.rgb += uNoise * noise(gl_FragCoord.xy * 0.01, uRandom * 100.0);
    gl_FragColor.a = 1.0;
}

)";
