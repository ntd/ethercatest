This is basically a rewrite of the `simple_test` program included in the
[SOEM](https://github.com/OpenEtherCATSociety/SOEM) project.

Major differences from `simple_test`:
* this is a self contained project, meant to showcase how to include
  the SOEM dependency with meson;
* the threaded code has been removed: no idea what was there for;
* the new SOEM APIs are used intead of the legacy ones;
* GLib has been added as dependency, just for my own convenience.
