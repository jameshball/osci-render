#!/usr/bin/env python3
"""Original recurring forms for Return Path; no arranged project is generated."""
import argparse
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw
from create_motion_dah_assets import animated_path, layer

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

def obj(name, paths):
    vertices, lines = [], []
    for path in paths:
        first = len(vertices) + 1
        vertices.extend(path)
        lines.append('l ' + ' '.join(str(i) for i in range(first, len(vertices) + 1)))
    (args.output / name).write_text('\n'.join([f'v {x} {y} {z}' for x,y,z in vertices] + lines) + '\n')

# A folded, asymmetric packet: its nose remains identifiable as it turns.
a, b, c, d = (0,.65,0),(-.35,-.35,.2),(.35,-.35,.2),(0,-.15,-.3)
obj('Carrier.obj', [[a,b,c,a,d,b],[c,d]])
# Open brackets become a destination, a cage, and finally a shared frame.
obj('Receiver.obj', [[(-.55,.7,0),(-.8,.7,0),(-.8,-.7,0),(-.55,-.7,0)],[(.55,.7,0),(.8,.7,0),(.8,-.7,0),(.55,-.7,0)]])
obj('Link.obj', [[(-1,0,0),(1,0,0)]])
# Perspective architecture uses the same receiver profile, connected in depth.
paths=[]
for z in (0,.65,1.3,1.95):
    paths.append([(-.8,.7,z),(-.8,-.7,z),(.8,-.7,z),(.8,.7,z)])
for x,y in ((-.8,.7),(-.8,-.7),(.8,-.7),(.8,.7)):
    paths.append([(x,y,0),(x,y,1.95)])
obj('Relay corridor.obj',paths)
# Structured interference grows from a clean communication line; no randomness.
(args.output/'Interference.lua').write_text('local u=phase/math.pi-1; local t=play_time; local w=math.sin(math.pi*u)^2; return {u,0.35*w*math.sin(7*phase+2*math.pi*t/3.2),0.18*w*math.sin(3*phase-2*math.pi*t/3.2),-1,-1,-1}\n')
# The same line is calm after the break: a slow, coherent travelling response.
(args.output/'Reply.lua').write_text('local u=phase/math.pi-1; return {u,0.07*math.sin(math.pi*u)^2*math.sin(2*phase-2*math.pi*play_time/3.2),0,-1,-1,-1}\n')
# Geometric relay animation: a packet travels left-to-right, rather than looping
# a decorative rotation. Scale/position in the composition supplies context.
path=animated_path(lambda t:[(-100+200*t-6,0),(-100+200*t,6),(-100+200*t+6,0),(-100+200*t,-6)],True,96,8)
data={'v':'5.7.4','fr':30,'ip':0,'op':96,'w':256,'h':256,'nm':'Relay packet','ddd':0,'assets':[], 'layers':[layer(1,'Packet',path,(1,.65,.2),96)]}
(args.output/'Relay packet.json').write_text(json.dumps(data))
for name,content in [('Return path.txt','RETURN PATH'),('Anyone there.txt','ANYONE THERE?'),('Still here.txt','STILL HERE.')]:
    (args.output/name).write_text(content)
# A receiver display scans for the packet, then locks onto its outline.
frames=[]
for index in range(32):
    image=Image.new('P',(128,96),0)
    image.putpalette([0,0,0,60,205,240]+[0,0,0]*254)
    draw=ImageDraw.Draw(image)
    draw.line([(20,12),(8,12),(8,84),(20,84)],fill=1,width=2)
    draw.line([(108,12),(120,12),(120,84),(108,84)],fill=1,width=2)
    x=20+round(index/31*88)
    draw.line([(x,24),(x,72)],fill=1,width=1)
    if index>=20:
        draw.line([(64,28),(49,63),(64,54),(79,63),(64,28)],fill=1,width=2)
    frames.append(image)
frames[0].save(args.output/'Receiver scan.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2,transparency=0)
print('Created 11 narrative source assets in',args.output)
