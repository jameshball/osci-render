#!/usr/bin/env python3
"""Check production traversal arithmetic against an independent linear oracle.

Extracts the current method into a minimal fixture; the project fuzzer separately
exercises real parser/producer/voice integration. Compile and execute with --run."""
import argparse
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'Source/audio/synth/ShapeVoice.cpp').read_text()
start = source.index('void ShapeVoice::incrementShapeDrawing()')
brace = source.index('{', start)
level = 1
end = brace + 1
while level:
    level += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]
prefix = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
struct Shape { float len; double cumulativeEndLength; };
struct ShapeVoice {
    std::vector<std::unique_ptr<Shape>> frame;
    double frameLength=0, shapeDrawn=0, frameDrawn=0, lengthIncrement=0;
    int currentShape=0;
    void incrementShapeDrawing();
};
'''
tests = r'''
int checks=0;
double maxError=0;
ShapeVoice make(const std::vector<float>& lengths, int index=0, double local=0, double increment=0) {
    ShapeVoice v;
    float oldTotal=0;
    double total=0;
    for (float length: lengths) {
        oldTotal += length;
        total += length;
        v.frame.push_back(std::make_unique<Shape>(Shape{length,total}));
    }
    v.frameLength=oldTotal;
    v.currentShape=index; v.shapeDrawn=local; v.lengthIncrement=increment;
    return v;
}
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void check(ShapeVoice& v, int expectedIndex, double expectedLocal, double expectedDrawn) {
    const double error=std::abs(v.shapeDrawn-expectedLocal);
    maxError=std::max(maxError,error);
    const double tolerance=1e-11*std::max(1.0,std::abs(expectedLocal));
    if (v.currentShape!=expectedIndex || error>tolerance || v.frameDrawn!=expectedDrawn) {
        std::printf("FAIL #%d index %d expected %d local %.17g expected %.17g frame %.17g expected %.17g\n",
                    checks,v.currentShape,expectedIndex,v.shapeDrawn,expectedLocal,v.frameDrawn,expectedDrawn);
        throw std::runtime_error("drawing state mismatch");
    }
    ++checks;
}
void oracle(ShapeVoice& v) {
    if (v.frame.empty() || v.frameLength<=0) return;
    v.frameDrawn+=v.lengthIncrement;
    v.shapeDrawn+=v.lengthIncrement;
    double length=v.currentShape < int(v.frame.size()) ? v.frame[v.currentShape]->len : 0;
    int steps=0;
    while (v.shapeDrawn>length) {
        require(++steps<1000000,"oracle bound exceeded");
        v.shapeDrawn-=length;
        v.currentShape=(v.currentShape+1)%v.frame.size();
        length=v.frame[v.currentShape]->len;
    }
}
void explicitCase(const std::vector<float>& lengths,int start,double local,double increment,int target,double result) {
    auto v=make(lengths,start,local,increment);
    v.incrementShapeDrawing();
    check(v,target,result,increment);
}
int main() {
    explicitCase({1,2,3},0,0,.5,0,.5);
    explicitCase({1,2,3},0,0,1,0,1);
    explicitCase({1,2,3},0,0,1.5,1,.5);
    explicitCase({1,2,3},0,0,3,1,2);
    explicitCase({1,2,3},0,0,6,2,3);
    explicitCase({1,2,3},0,0,6.25,0,.25);
    explicitCase({1,2,3},0,0,12,2,3);
    explicitCase({1,2,3},1,1,2,2,1);
    explicitCase({0,2,0,3,0},0,0,2,1,2);
    explicitCase({0,2,0,3,0},0,0,2.5,3,.5);
    explicitCase({0,2,0,3,0},0,0,5,3,3);
    explicitCase({0,2,0,3,0},0,0,5.25,1,.25);
    // Huge wraps use known exact expected results, not the slow linear oracle.
    explicitCase({1,2,3},0,0,6000000000000.25,0,.25);
    explicitCase({1,2,3},0,0,6000000000000.,2,3);
    for (auto lengths : {std::vector<float>{},std::vector<float>{0,0,0}}) {
        auto v=make(lengths,0,0,1);
        v.incrementShapeDrawing(); check(v,0,0,0);
    }
    std::mt19937 rng(19477);
    for (int c=0;c<20000;++c) {
        std::vector<float> lengths(1+rng()%64);
        for (auto& length:lengths) length=float(rng()%1024)/256;
        lengths[rng()%lengths.size()]+=1;
        int index=rng()%lengths.size();
        double local=double(rng()%4096)/1024;
        double increment=double(rng()%32768)/1024;
        auto expected=make(lengths,index,local,increment);
        auto actual=make(lengths,index,local,increment);
        // frameDrawn deliberately unrelated: frame transitions retain local residual.
        actual.frameDrawn=expected.frameDrawn=double(rng()%8192)/256;
        oracle(expected); actual.incrementShapeDrawing();
        check(actual,expected.currentShape,expected.shapeDrawn,expected.frameDrawn);
    }
    // Non-dyadic tiny lengths and float/double accumulated-total differences.
    for (int c=0;c<500;++c) {
        std::vector<float> lengths(32);
        for (auto& length:lengths) length=float(1+rng()%1024)*1e-8f;
        int index=rng()%lengths.size();
        double increment=1e-6*(1+rng()%100);
        auto expected=make(lengths,index,0,increment);
        auto actual=make(lengths,index,0,increment);
        oracle(expected); actual.incrementShapeDrawing();
        check(actual,expected.currentShape,expected.shapeDrawn,expected.frameDrawn);
    }
    double sequenceMaxError=0, sequenceMaxNormalisedError=0;
    int equivalentEndpointMismatches=0;
    for (int c=0;c<40;++c) {
        std::vector<float> lengths(16+rng()%113);
        for (auto& length:lengths) length=float(1+rng()%1000)*0.000137f;
        auto expected=make(lengths);
        auto actual=make(lengths);
        const double total=actual.frame.back()->cumulativeEndLength;
        for (int step=0;step<5000;++step) {
            // 220..20000 Hz with varying frequency; at most one frame/callback sample.
            const double frequency=220.0+double(rng()%1978100)/100.0;
            const double increment=actual.frameLength*frequency/48000.0;
            expected.lengthIncrement=actual.lengthIncrement=increment;
            oracle(expected); actual.incrementShapeDrawing();
            const double actualPosition=actual.shapeDrawn+(actual.currentShape?actual.frame[actual.currentShape-1]->cumulativeEndLength:0);
            const double expectedPosition=expected.shapeDrawn+(expected.currentShape?expected.frame[expected.currentShape-1]->cumulativeEndLength:0);
            double error=std::abs(actualPosition-expectedPosition);
            error=std::min(error,std::abs(total-error));
            sequenceMaxError=std::max(sequenceMaxError,error);
            require(error <= 1e-9*std::max(1.0,total),"sequential geometric progress error");
            if (actual.currentShape!=expected.currentShape) {
                ++equivalentEndpointMismatches;
                require(error <= 1e-11*std::max(1.0,total),"non-equivalent sequential shape index");
            }
            if (actual.currentShape==expected.currentShape) {
                const double length=actual.frame[actual.currentShape]->len;
                sequenceMaxNormalisedError=std::max(sequenceMaxNormalisedError,std::abs(actual.shapeDrawn-expected.shapeDrawn)/length);
            }
            require(actual.frameDrawn==expected.frameDrawn,"sequential frame clock mismatch");
            ++checks;
            // Match actual caller, including one subtraction and retained local residual.
            if (actual.frameDrawn>=actual.frameLength) {
                actual.frameDrawn-=actual.frameLength;
                expected.frameDrawn-=expected.frameLength;
                actual.currentShape=expected.currentShape=0;
            }
            // Additional fresh-frame-style reset with intentionally retained residual.
            if (step%127==0) actual.currentShape=expected.currentShape=0;
        }
    }
    std::printf("Sequential: 200000 advances, max circular progress error %.17g, max normalised local error %.17g, equivalent endpoint index mismatches %d\n",sequenceMaxError,sequenceMaxNormalisedError,equivalentEndpointMismatches);
    std::printf("PASS %d checks; maximum local-progress error %.17g; tolerance 1e-11*max(1,abs(expected))\n",checks,maxError);
}
'''
folder = root / 'build/performance-review/shape-check'
folder.mkdir(parents=True, exist_ok=True)
fixture = folder / 'fixture.cpp'
fixture.write_text(prefix + body + '\n' + tests)
args = argparse.ArgumentParser()
args.add_argument('--run', action='store_true')
if args.parse_args().run:
    executable = folder / 'fixture'
    subprocess.run(['clang++','-std=c++17','-O2',str(fixture),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True,timeout=30)
else:
    print(f'Prepared {fixture}; no compilation or execution.')
