This is basically a rewrite of the `simple_test` program included in the
[SOEM](https://github.com/OpenEtherCATSociety/SOEM) project.

Major differences from `simple_test`:
* this is a self contained project, meant to highlight the differences
  between SOEM and igh-ethercat;
* the threaded code has been removed: no idea what was there for;
* the SOEM version uses the new APIs intead of the legacy ones;
* GLib has been added as dependency, just for my own convenience;
* if the period is explicitely set to `0` in the arguments, it
  measures the roundtrip performances.

Two similar programs are provided: `ethercatest-soem`, based on SOEM,
and `ethercatest-igh`, based on the
[IgH EtherCAT](https://etherlab.org/en/ethercat/) software stack.

# SOEM performances

The SOEM program must be run as root to be able to access the ethernet
device. These are the results of `ethercatest-soem 0` on my idle system:

    Roundtrip time (usec): min 104  max 599

Output on the same system while building a project in another console:

    Roundtrip time (usec): min 76  max 7372

    # Using `sudo nice -n20 ./ethercatest`
    Roundtrip time (usec): min 77  max 39758

    # Using `sudo nice -n-20 ./ethercatest`
    Roundtrip time (usec): min 76  max 3031

The performances are not stable, meaning that subsequent calls can
give quite different results, e.g.:

    Roundtrip time (usec): min 101  max 661
    Roundtrip time (usec): min 97  max 680
    Roundtrip time (usec): min 93  max 698
    Roundtrip time (usec): min 78  max 729
    Roundtrip time (usec): min 190  max 731
    Roundtrip time (usec): min 169  max 590

# igh-ethercat performances

The IgH program requires a couple of kernel modules: `ec_master` and an
EtherCAT device. In my tests I used the generic EtherCAT device
(`ec_generic`). The user running the program must be able to access the
EtherCAT device created by the IgH code (typically `/dev/EtherCAT0`).
These are the results of `ethercatest-igh 0` on my idle system:

    Roundtrip time (usec): min 190  max 350

Output on the same system while building a project in another console:

    Roundtrip time (usec): min 127  max 398
    Roundtrip time (usec): min 122  max 405
    Roundtrip time (usec): min 148  max 353
    Roundtrip time (usec): min 121  max 382


In this case the performances are **much** more consistent and stable.
I suspect by using a dedicated driver instead of `ec_generic` would
gain some speed.
