# ChainOSCnano storage limit test data

The generator creates whole-settings JSON files containing 10, 20, 30, 40,
and 41 synthetic devices. Encoder, Angle, Key, Joystick, and ToF entries are
distributed evenly and use identities reserved for this test.

Recommended order:

1. Back up the current settings.
2. Import the 10-device file and reboot to verify restoration.
3. Repeat with 20, 30, and 40 devices.
4. Import the 41-device file and confirm that it is rejected by the 40-device
   input limit without changing the saved settings.

Importing a larger file does not remove settings that are absent from that
file. For a clean capacity measurement, erase settings before each boundary
test or delete settings created by the preceding test.
