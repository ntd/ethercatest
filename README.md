This is basically a rewrite of the `simple_test` program included in
the SOEM project, meant to highlight the differences between
[SOEM](https://github.com/OpenEtherCATSociety/SOEM) and
[IgH-EtherCAT](https://etherlab.org/en/ethercat/).

Major differences from `simple_test`:
* this is a self contained project;
* the threaded code has been removed;
* the SOEM version uses the new APIs intead of the legacy ones;
* GLib has been added as dependency, just for convenience;
* by default it updates (every 5000 us, customizable in the arguments)
  a binary counter on the first 8 digital outputs; if you explicitely
  set the period to `0`, it measures the roundtrip performances.

Two implementations are provided: `ethercatest-soem`, based on SOEM,
and `ethercatest-igh`, based on the IgH-EtherCAT software stack.

# Performances

The two projects are really different.

SOEM is basically a userspace library. It is really easy use: you
just need to link your application to it (statically, by default)
and you are ready to go. Thr program tries to guess the Ethernet
device to use but you can override its logic by passing it as
first argument. To be able to access the raw Ethernet device, the
SOEM program must be run as root.

The IgH EtherCAT stack instead is much more complex. A couple of
kernel modules must be configured and loaded before: `ec_master`
and an EtherCAT device. In these tests, the generic EtherCAT device
(`ec_generic`) was used. Furthermore, the application must be linked
to a userspace library. The user running the program must be able
to access the EtherCAT device created by the IgH modules, typically
`/dev/EtherCAT0`. This is usually owned by root but, by leveraging
the `udev` infrastructure, you can change ownership and mode to be
able to access it from a normal user.

The master hardware is a consumer PC with an i7-7700T CPU at 2.90 GHz.
The NIC interface is an e1000e Intel-based one operating at 100 MBit/s.
A quite recent linux kernel (5.11.2) was used.

The PC is connected with a 2 m category 5 UTP cable to a Beckhoff node
with three slaves: one EK1100, one EL1809 and one EL2808.

## SOEM results

These are the results of `ethercatest-soem 0` on an idle system:

    Roundtrip time (usec): min 104  max 599

Output on the same system while building a project in another console:

    Roundtrip time (usec): min 76  max 7372

    # Using `sudo nice -n20 ethercatest-soem 0`
    Roundtrip time (usec): min 77  max 39758

    # Using `sudo nice -n-20 ethercatest-soem 0`
    Roundtrip time (usec): min 76  max 3031

The performances are not stable, meaning that subsequent calls can
give quite different results, e.g.:

    Roundtrip time (usec): min 101  max 661
    Roundtrip time (usec): min 97  max 680
    Roundtrip time (usec): min 93  max 698
    Roundtrip time (usec): min 78  max 729
    Roundtrip time (usec): min 190  max 731
    Roundtrip time (usec): min 169  max 590

## igh-ethercat results

These are the results of `ethercatest-igh 0` on an idle system:

    Roundtrip time (usec): min 190  max 350

Output on the same system while building a project in another console:

    Roundtrip time (usec): min 127  max 398
    Roundtrip time (usec): min 122  max 405
    Roundtrip time (usec): min 148  max 353
    Roundtrip time (usec): min 121  max 382

In this case the performances are **much** more consistent and stable.
By using a dedicated driver instead of `ec_generic` the results would
likely be lower. The problem is the dedicated drivers must be kept in
sync with the kernel, and this could become a maintenance nightmare.
