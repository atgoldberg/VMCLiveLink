# vmc_sender.py
# Minimal VMC-style sender for Unreal test
# Requires: pip install python-osc

from pythonosc.udp_client import SimpleUDPClient
from time import sleep
import math

# If Unreal is on this machine, leave as localhost.
# If Unreal is on another machine, put that machine's IPv4 here.
UE_IP   = "127.0.0.1"
UE_PORT = 39539  # must match the port your source listens on

c = SimpleUDPClient(UE_IP, UE_PORT)

def send_bone(name, px, py, pz, qx, qy, qz, qw):
    c.send_message("/VMC/Ext/Bone/Pos", [name, px, py, pz, qx, qy, qz, qw])

def send_root(px, py, pz, qx, qy, qz, qw):
    c.send_message("/VMC/Ext/Root/Pos", [px, py, pz, qx, qy, qz, qw])

def send_curve(name, value):
    c.send_message("/VMC/Ext/Blend/Val", [name, float(value)])

def apply():
    c.send_message("/VMC/Ext/Blend/Apply", [])

# Simple loop: bob the root/bone in Z, modulate a couple curves
t = 0.0
print(f"Sending VMC messages to {UE_IP}:{UE_PORT} ... Ctrl+C to stop")
try:
    while True:
        z = 0.1 * math.sin(t)  # UE units (cm) in our current no-conversion path
        # Root transform (position + rotation quaternion)
        send_root(0.0, 0.0, z, 0.0, 0.0, 0.0, 1.0)
        # One bone named "root" (matches the MVP plugin logic)
        #send_bone("root", 0.0, 0.0, z, 0.0, 0.0, 0.0, 1.0)
        send_bone("Hips", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0)         # -> pelvis
        send_bone("Spine", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678)        # -> spine_01
        send_bone("Chest", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678)       # -> spine_02
        send_bone("Head", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678)        # -> head
        send_bone("LeftUpperArm", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678) 
        send_bone("LeftLowerArm", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678) 
        send_bone("LeftHand", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678) 
        send_bone("RightUpperArm", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678) 
        send_bone("RightLowerArm", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678) 
        send_bone("RightHand", 0.0, 0.0, 0.0, 0.0, 0.0, 0.70710678, 0.70710678) 
        # A couple curves (0..1-ish)
        send_curve("Face.JawOpen", 0.6 + 0.4 * math.sin(t * 1.5))
        send_curve("Face.EyeBlinkLeft", 0.5 + 0.5 * math.sin(t * 2.0))
        # Tell the receiver "this frame is ready"       
        apply()

        t += 0.05
        sleep(1/60)
except KeyboardInterrupt:
    print("\nStopped.")
