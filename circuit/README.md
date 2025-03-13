Alternatives
============

- devil-box-N = Arduino power controlled by N-MOSFET on ground side.

- devil-box-P = Arduino power controlled by P-MOSFET on Vcc side.


Revisions
=========

Revision A

  - initial revision, home-made motion sensor

Revision B

  - PCB developed
  - SW-520D sensor

Revision C

  - never implemented, future changes



Changes:
========

N-A -> N-C

- add R6 10k - Arduino can detect unexpected box motion
- better component placement in schematics
- motion sensor changed to SW-520D
- rezistors R1-R4 changed to 10k


P-B -> P->C

- add R6 10k - Arduino can detect unexpected box motion
- better component placement in schematics
- change A7->A0 (A7 is analog in put only)
- change T4: IRF9Z34N
- improve power supply for Arduino by schottky D1
  (internal linear power regulater has voltage drop 1.2V [datasheet max 2V])
- add C3 to limit interference voltage spikes

