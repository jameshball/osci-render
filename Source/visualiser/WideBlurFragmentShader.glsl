std::string wideBlurFragmentShader = R"(

uniform sampler2D uTexture0;
uniform vec2 uOffset;
varying vec2 vTexCoord;

void main() {
    vec4 sum = vec4(0.0);

    sum += texture2D(uTexture0, vTexCoord + uOffset * -32.0) * 7.936396739629738e-9;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -31.0) * 2.1238899047869243e-8;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -30.0) * 5.5089512435892845e-8;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -29.0) * 1.3849501900610678e-7;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -28.0) * 3.37464176550827e-7;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -27.0) * 7.969838069860066e-7;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -26.0) * 0.0000018243141024985921;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -25.0) * 0.000004047417737721991;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -24.0) * 0.000008703315823121481;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -23.0) * 0.000018139267838241068;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -22.0) * 0.00003664232826522744;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -21.0) * 0.0000717421959978491;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -20.0) * 0.00013614276559291948;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -19.0) * 0.00025040486855393973;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -18.0) * 0.00044639494279724747;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -17.0) * 0.00077130129517095;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -16.0) * 0.0012916865959769006;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -15.0) * 0.0020966142949301195;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -14.0) * 0.003298437274607801;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -13.0) * 0.005029516233086111;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -12.0) * 0.007433143141769405;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -11.0) * 0.010647485948997013;
    sum += texture2D(uTexture0, vTexCoord + uOffset * -10.0) * 0.014782570282805454;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -9.0) * 0.019892122581030056;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -8.0) * 0.02594421881668063;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -7.0) * 0.03279656561871859;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -6.0) * 0.040183192177668754;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -5.0) * 0.04771872140419803;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -4.0) * 0.05492391166591576;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -3.0) * 0.06127205113483162;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -2.0) * 0.06625088366795348;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  -1.0) * 0.06943032995966593;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   0.0) * 0.07052369856294818;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   1.0) * 0.06943032995966593;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   2.0) * 0.06625088366795348;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   3.0) * 0.06127205113483162;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   4.0) * 0.05492391166591576;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   5.0) * 0.04771872140419803;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   6.0) * 0.040183192177668754;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   7.0) * 0.03279656561871859;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   8.0) * 0.02594421881668063;
    sum += texture2D(uTexture0, vTexCoord + uOffset *   9.0) * 0.019892122581030056;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  10.0) * 0.014782570282805454;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  11.0) * 0.010647485948997013;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  12.0) * 0.007433143141769405;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  13.0) * 0.005029516233086111;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  14.0) * 0.003298437274607801;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  15.0) * 0.0020966142949301195;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  16.0) * 0.0012916865959769006;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  17.0) * 0.00077130129517095;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  18.0) * 0.00044639494279724747;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  19.0) * 0.00025040486855393973;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  20.0) * 0.00013614276559291948;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  21.0) * 0.0000717421959978491;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  22.0) * 0.00003664232826522744;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  23.0) * 0.000018139267838241068;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  24.0) * 0.000008703315823121481;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  25.0) * 0.000004047417737721991;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  26.0) * 0.0000018243141024985921;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  27.0) * 7.969838069860066e-7;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  28.0) * 3.37464176550827e-7;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  29.0) * 1.3849501900610678e-7;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  30.0) * 5.5089512435892845e-8;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  31.0) * 2.1238899047869243e-8;
    sum += texture2D(uTexture0, vTexCoord + uOffset *  32.0) * 7.936396739629738e-9;

    gl_FragColor = sum;
}

)";
