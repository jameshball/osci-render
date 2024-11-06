uniform sampler2D uTexture0;
varying vec2 vTexCoord;

void main() {
    gl_FragColor = texture2D(uTexture0, vTexCoord);
    gl_FragColor.a = 1.0;
}
