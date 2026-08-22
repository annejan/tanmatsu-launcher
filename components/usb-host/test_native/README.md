# Report descriptor tests

`hid_layout.c` is plain C with no ESP-IDF dependencies beyond logging, so it can be built and
run on a host against report descriptors captured from real devices.

```
make test
```

The descriptors in `test_descriptors.h` come from actual hardware. The mice, the Stadia
controller and the DualShock 4 clone were captured for
[konsool-HID](https://github.com/annejan/konsool-HID/pull/2), the DualShock 3 and the Competition
Pro were captured on a Tanmatsu and on a Linux host while working on this component.
