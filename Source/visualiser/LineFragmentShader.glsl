std::string lineFragmentShader = R"(

#define EPS 1E-6
#define TAU 6.283185307179586
#define TAUR 2.5066282746310002
#define SQRT2 1.4142135623730951

uniform float uSize;
uniform float uIntensity;
uniform vec2 uOffset;
uniform vec2 uScale;
uniform float uScreenType;
uniform float uFishEye;
uniform sampler2D uScreen;
varying float vSize;
varying vec4 uvl;
varying vec2 vTexCoord;

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
    
    gl_FragColor = 2.0 * texture2D(uScreen, texCoord) * brightness;
    gl_FragColor.a = 1.0;
}

)";
