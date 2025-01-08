std::string glowFragmentShader = R"(

uniform sampler2D uTexture0;
uniform vec2 uOffset;
uniform vec2 uScale;
varying vec2 vTexCoord;

void main() {
    vec2 texCoord = (vTexCoord - 0.5) / uScale + uOffset + 0.5;
    
    vec2 reflectionLinePos1 = (texCoord - 0.5) / 0.875 + 0.5;
    vec2 reflectionLinePos2 = (texCoord - 0.5) / 1.125 + 0.5;
    vec2 reflectionLinePos3 = (texCoord - 0.5) / 1.375 + 0.5;
    vec2 reflectionLinePos4 = (texCoord - 0.5) / 1.625 + 0.5;
    vec2 reflectionLinePos5 = (texCoord - 0.5) / 1.875 + 0.5;
    
    vec2 distVec = (texCoord - 0.5);
    float dist = dot(distVec, distVec);
    float decay = 1.0 - dist;
    
    vec4 sum = vec4(0.0);
    sum += texture2D(uTexture0, reflectionLinePos1);
    sum += texture2D(uTexture0, reflectionLinePos2);
    sum += texture2D(uTexture0, reflectionLinePos3);
    sum += texture2D(uTexture0, reflectionLinePos4);
    sum += texture2D(uTexture0, reflectionLinePos5);
    sum *= 3.0 * pow(decay, 4.0);
    
    gl_FragColor = sum;
    gl_FragColor.a = 1.0;
}

)";
