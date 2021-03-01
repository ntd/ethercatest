This is basically a rewrite of the `simple_test` program included in the
[SOEM](https://github.com/OpenEtherCATSociety/SOEM) project.

Major differences from `simple_test`:
* this is a self contained project, meant to showcase how to include
  the SOEM dependency with meson;
* the threaded code has been removed: no idea what was there for;
* the new SOEM APIs are used intead of the legacy ones;
* GLib has been added as dependency, just for my own convenience.

# Performances

This are the results on my system:

    Roundtrip time (usec): min 104  max 599

Output on the same system while building a project in another console:

    Roundtrip time (usec): min 76  max 7372

    # Using `sudo nice -n20 ./ethercatest`
    Roundtrip time (usec): min 77  max 39758

    # Using `sudo nice -n-20 ./ethercatest`
    Roundtrip time (usec): min 76  max 3031

The deviation is not stable, meaning that subsequent calls gives quite
different results, e.g.:

    Roundtrip time (usec): min 101  max 661
    Roundtrip time (usec): min 97  max 680
    Roundtrip time (usec): min 93  max 698
    Roundtrip time (usec): min 78  max 729

