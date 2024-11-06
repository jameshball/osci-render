uniform sampler2D uTexture0; //line
uniform sampler2D uTexture1; //tight glow
uniform sampler2D uTexture2; //big glow
uniform sampler2D uTexture3; //screen
uniform float uExposure;
uniform float uSaturation;
uniform vec3 uColour;
varying vec2 vTexCoord;
varying vec2 vTexCoordCanvas;

vec3 desaturate(vec3 color, float factor) {
    vec3 lum = vec3(0.299, 0.587, 0.114);
    vec3 gray = vec3(dot(lum, color));
    return vec3(mix(color, gray, factor));
}

/* Gradient noise from Jorge Jimenez's presentation: */
/* http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare */
float gradientNoise(in vec2 uv) {
    return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec4 line = texture2D(uTexture0, vTexCoordCanvas);
    // r components have grid; g components do not.
    vec4 screen = texture2D(uTexture3, vTexCoord);
    vec4 tightGlow = texture2D(uTexture1, vTexCoord);
    vec4 scatter = texture2D(uTexture2, vTexCoord)+0.35;
    float light = line.r + 1.5*screen.g*screen.g*tightGlow.r;
    light += 0.4*scatter.g * (2.0+1.0*screen.g + 0.5*screen.r);
    float tlight = 1.0-pow(2.0, -uExposure*light);
    float tlight2 = tlight*tlight*tlight;
    gl_FragColor.rgb = mix(uColour, vec3(1.0), 0.3+tlight2*tlight2*0.5)*tlight;
    gl_FragColor.rgb = desaturate(gl_FragColor.rgb, 1.0 - uSaturation);
    gl_FragColor.rgb += (1.0 / 255.0) * gradientNoise(gl_FragCoord.xy) - (0.5 / 255.0);
    gl_FragColor.a = 1.0;
}
