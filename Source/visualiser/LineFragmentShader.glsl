std::string lineFragmentShader = R"(

#define EPS 1E-6
#define TAU 6.283185307179586
#define TAUR 2.5066282746310002
#define SQRT2 1.4142135623730951

uniform float uSize;
uniform float uIntensity;
uniform vec2 uOffset;
uniform vec2 uScale;
uniform float uFishEye;
uniform sampler2D uScreen; // still sampled for focus/gain texturing, but we'll reduce its influence on colour
uniform vec3 uLineColor;
uniform float uUseVertexColor; // 1.0 to use per-vertex RGB, 0.0 to use hue-only
varying float vSize;
varying vec4 uvl;
varying vec2 vTexCoord;
varying vec3 vColor;

// A standard gaussian function, used for weighting samples
float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (TAUR * sigma);
}
               
// This approximates the error function, needed for the gaussian integral
float erf(float x) {
    float s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// Rotate hue around an axis in RGB space (same method as output shader)
vec3 hueShift(vec3 color, float shift) {
    vec3 p = vec3(0.55735) * dot(vec3(0.55735), color);
    vec3 u = color - p;
    vec3 v = cross(vec3(0.55735), u);
    return u * cos(shift * 6.2832) + v * sin(shift * 6.2832) + p;
}

void main() {
    // fish eye distortion
    vec2 uv = vTexCoord - vec2(0.5);
    float uva = atan(uv.x, uv.y);
    float uvd = sqrt(dot(uv, uv));
    uvd = uvd * (1.0 - uFishEye * uvd * uvd);
    vec2 texCoord = vec2(0.5) + vec2(sin(uva), cos(uva)) * uvd;
    
    texCoord = (texCoord - uOffset - 0.5) * uScale + 0.5;

    float len = uvl.z;
    vec2 xy = uvl.xy;
    float brightness;
    // Determine base color: either per-vertex RGB, or a color from the settings
    vec3 baseColor = uUseVertexColor > 0.5 ? vColor : uLineColor;
    baseColor = clamp(baseColor, 0.0, 1.0);
    
    float sigma = vSize/5.0;
    if (len < EPS) {
        // If the beam segment is too short, just calculate intensity at the position.
        brightness = gaussian(length(xy), sigma);
    } else {
        // Otherwise, use analytical integral for accumulated intensity.
        brightness = erf(xy.x/SQRT2/sigma) - erf((xy.x-len)/SQRT2/sigma);
        brightness *= exp(-xy.y*xy.y/(2.0*sigma*sigma))/2.0/len;
    }

    brightness *= uvl.w;
    
    vec3 screenSample = texture2D(uScreen, texCoord).rgb;
    float screenLuma = clamp(screenSample.g, 0.1, 1.0);
    vec3 rgb = brightness * baseColor * screenLuma;
    gl_FragColor = vec4(rgb, 1.0);
}

)";
