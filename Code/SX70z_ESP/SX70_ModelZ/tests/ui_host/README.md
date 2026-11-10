# OLED host checks

Run `python tests/ui_host/run.py` after setting up a host GCC, Clang, or MSVC
environment. On Windows with Visual Studio, run `vcvars64.bat` first. Optionally
pass an output directory, for example `python tests/ui_host/run.py tests/ui_host/output`;
this writes PBM framebuffers and a PNG contact sheet and requires Pillow.

The runner compiles the actual firmware drawing and display-manager code with
small platform stubs. It excludes LCD initialization and replaces the ESP LCD
transport with a stub that checks the native frame dimensions. It checks the
rotation mapping, clipping, buffer guards, scroll wrap, shutter labels, wireless
combinations, settings, countdowns, and missing-display handling. It does not
model I2C, panel mirroring, task concurrency, or a physical display. The shutter
label stub mirrors the table in `shutter_cal.c`; keep it in sync when changing it.
