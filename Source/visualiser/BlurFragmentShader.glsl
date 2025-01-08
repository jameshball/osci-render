std::string blurFragmentShader = R"(

uniform sampler2D uTexture0;
uniform vec2 uOffset;
varying vec2 vTexCoord;

void main() {
    vec4 sum = vec4(0.0);
    sum += texture2D(uTexture0, vTexCoord - uOffset*8.0) * 0.000078;
    sum += texture2D(uTexture0, vTexCoord - uOffset*7.0) * 0.000489;
    sum += texture2D(uTexture0, vTexCoord - uOffset*6.0) * 0.002403;
    sum += texture2D(uTexture0, vTexCoord - uOffset*5.0) * 0.009245;
    sum += texture2D(uTexture0, vTexCoord - uOffset*4.0) * 0.027835;
    sum += texture2D(uTexture0, vTexCoord - uOffset*3.0) * 0.065592;
    sum += texture2D(uTexture0, vTexCoord - uOffset*2.0) * 0.12098;
    sum += texture2D(uTexture0, vTexCoord - uOffset*1.0) * 0.17467;
    sum += texture2D(uTexture0, vTexCoord + uOffset*0.0) * 0.19742;
    sum += texture2D(uTexture0, vTexCoord + uOffset*1.0) * 0.17467;
    sum += texture2D(uTexture0, vTexCoord + uOffset*2.0) * 0.12098;
    sum += texture2D(uTexture0, vTexCoord + uOffset*3.0) * 0.065592;
    sum += texture2D(uTexture0, vTexCoord + uOffset*4.0) * 0.027835;
    sum += texture2D(uTexture0, vTexCoord + uOffset*5.0) * 0.009245;
    sum += texture2D(uTexture0, vTexCoord + uOffset*6.0) * 0.002403;
    sum += texture2D(uTexture0, vTexCoord + uOffset*7.0) * 0.000489;
    sum += texture2D(uTexture0, vTexCoord + uOffset*8.0) * 0.000078;
    gl_FragColor = sum;
}

)";
