# AOG_full_CAN_Teensy_40

This version is for full CANBUS based system running on Teensy 4.0 or Teensy 4.1 microcontroller. Program flow is event based for everything that is read from the bus, and all of the redundant code is removed, i.e. this version works only with everything on the bus!

Setup is as follows:

1. Valve control via CAN using ISOBUS flow commands. This works with every type of CAN valve using the standard flow commands, e.g. Danfoss PVEA-CI, or you can directly drive a spool valve in the tractor hydraulics, for example.

2. WAS via J1939, this is configured for Danfoss DST X510 sensor, but the code is easy to adapt to any sensor utilizing the J1939 protocol. Note, that the steering angle calculation needs to happen inside the WAS function, as the WAS is not queried, but there's a new WAS message every 100 ms or so in the bus, so the WAS reading is event driven.

3. Steering disengage is via a hydraulic pressure sensor. This is configured for Sensortechnik Wiedeman STW M01 J1939 sensor, but the code can be adapted to other pressure sensors, or to utilize a cut-off from a SASAIID or a similar sensor. Sensor cut-off pressure is set as normal via AOG.

4. Steering control is via Blinkmarine J1939 keypad, with button led colors going to keypad direction and steering on/off commands coming back from the keypad.

You can use this with perfectly standard V5.x AOG install, 
