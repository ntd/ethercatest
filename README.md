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
handles the cycle and a driver specific module (e.g. `ec_generic`) for
interacting with the real hardware. Your application needs to be linked
with `libethercat`, that in turn iteracts with the kernel via `ioctl`
calls.

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
"soem", 0, -20, 1000, 3, 118, 205289, 0
"soem", 0, -18, 1000, 2, 135, 208884, 0
"soem", 0, -16, 1000, 3, 119, 210644, 0
"soem", 0, -14, 1000, 2, 119, 215112, 0
"soem", 0, -12, 1000, 3, 141, 217121, 0
"soem", 0, -10, 1000, 2, 117, 204910, 0
"soem", 0, -8, 1000, 3, 119, 213524, 0
"soem", 0, -6, 1000, 3, 118, 216855, 0
"soem", 0, -4, 1000, 2, 126, 206631, 0
"soem", 0, -2, 1000, 2, 138, 200136, 0
"soem", 0, 0, 1000, 2, 107, 213472, 0
"soem", 1, -20, 1000, 3, 3445, 49532, 0
"soem", 1, -18, 1000, 3, 172, 33551, 0
"soem", 1, -16, 1000, 2, 24996, 265619, 0
"soem", 1, -14, 1000, 2, 3003, 40085, 0
"soem", 1, -12, 1000, 2, 2996, 43634, 0
"soem", 1, -10, 1000, 2, 781, 33664, 0
"soem", 1, -8, 1000, 2, 3021, 48059, 0
"soem", 1, -6, 1000, 2, 3763, 110230, 0
"soem", 1, -4, 1000, 2, 1932, 36032, 0
"soem", 1, -2, 1000, 3, 3492, 48898, 0
"soem", 1, 0, 1000, 2, 2997, 42681, 0
"gatorcat", 0, -20, 1000, 14, 214, 587111, 0
"gatorcat", 0, -18, 1000, 13, 215, 589917, 0
"gatorcat", 0, -16, 1000, 13, 221, 598309, 0
"gatorcat", 0, -14, 1000, 13, 360, 579914, 0
"gatorcat", 0, -12, 1000, 13, 214, 557999, 0
"gatorcat", 0, -10, 1000, 12, 251, 586506, 0
"gatorcat", 0, -8, 1000, 12, 216, 612569, 0
"gatorcat", 0, -6, 1000, 13, 216, 602220, 0
"gatorcat", 0, -4, 1000, 13, 237, 603508, 0
"gatorcat", 0, -2, 1000, 12, 269, 605958, 0
"gatorcat", 0, 0, 1000, 13, 218, 618220, 0
"gatorcat", 1, -20, 1000, 18, 9033, 185484, 0
"gatorcat", 1, -18, 1000, 18, 3129, 188190, 0
"gatorcat", 1, -16, 1000, 18, 237, 170187, 0
"gatorcat", 1, -14, 1000, 17, 3941, 547435, 0
"gatorcat", 1, -12, 1000, 18, 3163, 216089, 0
"gatorcat", 1, -10, 1000, 17, 3548, 324447, 0
"gatorcat", 1, -8, 1000, 18, 599, 175536, 0
"gatorcat", 1, -6, 1000, 18, 3058, 815535, 0
"gatorcat", 1, -4, 1000, 18, 3912, 380193, 0
"gatorcat", 1, -2, 1000, 18, 2976, 182076, 0
"gatorcat", 1, 0, 1000, 18, 2973, 183001, 0
"igh", 0, -20, 1000, 2, 117, 205898, 0
"igh", 0, -18, 1000, 2, 86, 199824, 0
"igh", 0, -16, 1000, 2, 106, 201654, 0
"igh", 0, -14, 1000, 2, 325, 195011, 0
"igh", 0, -12, 1000, 2, 101, 179992, 0
"igh", 0, -10, 1000, 2, 125, 188657, 0
"igh", 0, -8, 1000, 2, 118, 193218, 0
"igh", 0, -6, 1000, 2, 145, 159540, 0
"igh", 0, -4, 1000, 3, 93, 206130, 0
"igh", 0, -2, 1000, 2, 85, 192166, 0
"igh", 0, 0, 1000, 3, 115, 196593, 0
"igh", 1, -20, 1000, 2, 43, 30562, 0
"igh", 1, -18, 1000, 2, 56, 31911, 0
"igh", 1, -16, 1000, 2, 19431, 51093, 0
"igh", 1, -14, 1000, 3, 52, 32365, 0
"igh", 1, -12, 1000, 3, 153, 31933, 0
"igh", 1, -10, 1000, 2, 117, 30811, 0
"igh", 1, -8, 1000, 2, 32583, 81864, 0
"igh", 1, -6, 1000, 2, 7846, 43668, 0
"igh", 1, -4, 1000, 2, 53, 31180, 0
"igh", 1, -2, 1000, 3, 172, 31102, 0
"igh", 1, 0, 1000, 2, 220, 30964, 0
```
