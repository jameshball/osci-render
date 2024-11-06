#define EPS 1E-6

uniform float uInvert;
uniform float uSize;
uniform float uNEdges;
uniform float uFadeAmount;
uniform float uIntensity;
uniform float uGain;
attribute vec3 aStart, aEnd;
attribute float aIdx;
varying vec4 uvl;
varying vec2 vTexCoord;
varying float vLen;
varying float vSize;

void main () {
    float tang;
    vec2 current;
    // All points in quad contain the same data:
    // segment start point and segment end point.
    // We determine point position using its index.
    float idx = mod(aIdx,4.0);
    
    vec2 aStartPos = aStart.xy;
    vec2 aEndPos = aEnd.xy;
    float aStartBrightness = aStart.z;
    float aEndBrightness = aEnd.z;
    
    // `dir` vector is storing the normalized difference
    // between end and start
    vec2 dir = (aEndPos-aStartPos)*uGain;
    uvl.z = length(dir);
    
    if (uvl.z > EPS) {
        dir = dir / uvl.z;
    } else {
        // If the segment is too short, just draw a square
        dir = vec2(1.0, 0.0);
    }
    
    vSize = uSize;
    float intensity = 0.015 * uIntensity / uSize;
    vec2 norm = vec2(-dir.y, dir.x);
    
    if (idx >= 2.0) {
        current = aEndPos*uGain;
        tang = 1.0;
        uvl.x = -vSize;
        uvl.w = aEndBrightness;
    } else {
        current = aStartPos*uGain;
        tang = -1.0;
        uvl.x = uvl.z + vSize;
        uvl.w = aStartBrightness;
    }
    // `side` corresponds to shift to the "right" or "left"
    float side = (mod(idx, 2.0)-0.5)*2.0;
    uvl.y = side * vSize;
    
    uvl.w *= intensity * mix(1.0-uFadeAmount, 1.0, floor(aIdx / 4.0 + 0.5)/uNEdges);
                             
    vec4 pos = vec4((current+(tang*dir+norm*side)*vSize)*uInvert,0.0,1.0);
    gl_Position = pos;
    vTexCoord = 0.5*pos.xy+0.5;
    //float seed = floor(aIdx/4.0);
    //seed = mod(sin(seed*seed), 7.0);
    //if (mod(seed/2.0, 1.0)<0.5) gl_Position = vec4(10.0);
}
