Collection of (somewhat compatible) test programs for analyzing the
performances of the following opensource EtherCAT main stacks:

- [SOEM](https://github.com/OpenEtherCATSociety/SOEM)
- [gatorcat](https://github.com/jeffective/gatorcat)
- [IgH-EtherCAT](https://etherlab.org/en/ethercat/)

`SOEM` and `gatorcat` are quite similar, in the sense they are basically
userspace libraries: you just need to link your application to one of
them and you are ready to go. Access to the raw Ethernet device is
required in both cases, so your program needs special privileges.

The IgH EtherCAT stack instead is much more complex. It has a generic
kernel module (`ec_master`) for managing the finite state machine that
handles the stack and a driver specific module (i.e. `ec_generic`) for
interacting with the real hardware. Your application needs to be linked
with `libethercat`, interacting with the kernel side via `ioctl` calls.

## Results

I have the following EtherCAT node:

```
0  0:0  PREOP  +  EK1100 EtherCAT-Koppler (2A E-Bus)
1  0:1  PREOP  +  EL2808 8Ch. Dig. Output 24V, 0.5A
2  0:2  PREOP  +  EL3164 4Ch. Ana. Input 0-10V
```

The main device is a consumer PC with an i7-7700T CPU at 2.90 GHz.
The NIC interface is an e1000e Intel-based one operating at 100 MBit/s.
A quite recent linux kernel (6.16.8) was used.

```csv
Stack, Busy, Niceness, Period, Min time, Max time, Total time, Errors
"soem", 0, -20, 1000, 3, 125, 106462, 0
"soem", 0, -18, 1000, 2, 94, 106334, 0
"soem", 0, -16, 1000, 3, 119, 105164, 0
"soem", 0, -14, 1000, 2, 104, 104360, 0
"soem", 0, -12, 1000, 2, 120, 106841, 0
"soem", 0, -10, 1000, 3, 125, 107744, 0
"soem", 0, -8, 1000, 2, 118, 101238, 0
"soem", 0, -6, 1000, 3, 120, 105840, 0
"soem", 0, -4, 1000, 2, 118, 103136, 0
"soem", 0, -2, 1000, 2, 119, 99702, 0
"soem", 0, 0, 1000, 2, 117, 102975, 0
"soem", 1, -20, 1000, 2, 108, 14793, 0
"soem", 1, -18, 1000, 2, 1350, 16933, 0
"soem", 1, -16, 1000, 3, 144, 16058, 0
"soem", 1, -14, 1000, 3, 108, 16207, 0
"soem", 1, -12, 1000, 3, 5994, 318732, 0
"soem", 1, -10, 1000, 3, 107, 15903, 0
"soem", 1, -8, 1000, 3, 3995, 261609, 0
"soem", 1, -6, 1000, 3, 2996, 22955, 0
"soem", 1, -4, 1000, 2, 5998, 206955, 0
"soem", 1, -2, 1000, 3, 50996, 2695178, 0
"soem", 1, 0, 1000, 3, 3009, 61040, 0
"gatorcat", 0, -20, 1000, 105, 600, 3461939, 0
"gatorcat", 0, -18, 1000, 101, 618, 3565641, 0
"gatorcat", 0, -16, 1000, 107, 568, 3562478, 0
"gatorcat", 0, -14, 1000, 144, 592, 3655227, 0
"gatorcat", 0, -12, 1000, 145, 625, 3651380, 0
"gatorcat", 0, -10, 1000, 108, 638, 3541147, 0
"gatorcat", 0, -8, 1000, 175, 636, 3573714, 0
"gatorcat", 0, -6, 1000, 111, 587, 3686921, 0
"gatorcat", 0, -4, 1000, 102, 608, 3661436, 0
"gatorcat", 0, -2, 1000, 133, 570, 3615357, 0
"gatorcat", 0, 0, 1000, 103, 606, 3589639, 0
"gatorcat", 1, -20, 1000, 169, 30008, 8638349, 0
"gatorcat", 1, -18, 1000, 113, 10998, 10723346, 0
"gatorcat", 1, -16, 1000, 243, 21627, 15664874, 0
"gatorcat", 1, -14, 1000, 302, 29031, 16457545, 0
"gatorcat", 1, -12, 1000, 244, 38033, 18636477, 0
"gatorcat", 1, -10, 1000, 260, 11999, 19101837, 0
"gatorcat", 1, -8, 1000, 257, 4002, 19610305, 0
"gatorcat", 1, -6, 1000, 245, 43032, 19886120, 0
"gatorcat", 1, -4, 1000, 236, 53014, 19592308, 0
"gatorcat", 1, -2, 1000, 257, 30010, 19277388, 0
"gatorcat", 1, 0, 1000, 204, 89003, 20199920, 0
"igh", 0, -20, 1000, 2, 79, 100021, 0
"igh", 0, -18, 1000, 3, 113, 103475, 0
"igh", 0, -16, 1000, 2, 101, 96272, 0
"igh", 0, -14, 1000, 3, 72, 96853, 0
"igh", 0, -12, 1000, 2, 71, 79492, 0
"igh", 0, -10, 1000, 2, 100, 93188, 0
"igh", 0, -8, 1000, 2, 78, 96822, 0
"igh", 0, -6, 1000, 2, 92, 102991, 0
"igh", 0, -4, 1000, 3, 121, 101988, 0
"igh", 0, -2, 1000, 2, 110, 97959, 0
"igh", 0, 0, 1000, 2, 106, 97810, 0
"igh", 1, -20, 1000, 3, 73, 16728, 0
"igh", 1, -18, 1000, 3, 152, 16065, 0
"igh", 1, -16, 1000, 2, 53, 15803, 0
"igh", 1, -14, 1000, 3, 56, 16777, 0
"igh", 1, -12, 1000, 2, 45, 15210, 0
"igh", 1, -10, 1000, 3, 5229, 34388, 0
"igh", 1, -8, 1000, 3, 5283, 35131, 0
"igh", 1, -6, 1000, 3, 58, 21966, 0
"igh", 1, -4, 1000, 3, 125, 15025, 0
"igh", 1, -2, 1000, 3, 142, 16055, 0
"igh", 1, 0, 1000, 3, 59, 16181, 0
```
