# sane-backends-opticfilm135i

A working copy of the [SANE](http://www.sane-project.org/) `genesys` backend with the
GL126 ASIC and the **Plustek OpticFilm 135i** (USB `07b3:1436`), kept so the backend can be
built, run and tested on one real scanner. **This is not a submission to SANE and nothing
here is sent to the SANE project.**

The GL126/135i backend is Christian Gillinger's work
([cgillinger/opticfilm135i-linux](https://github.com/cgillinger/opticfilm135i-linux)); he has
submitted it to sane-backends himself as
[merge request !1032](https://gitlab.com/sane-project/backends/-/merge_requests/1032).
`main` here is his revision **v9** unchanged (tip `18bea9b32`, tree `60c3bff8`; the seven
commits include the calibration stop gate, with `bearyjd` as co-author of the gate commit).
Everything else is a local branch on top of it.

## Branches

| branch | what it is | status |
|---|---|---|
| `main` | v9 as exported by the maintainer | his work, unchanged |
| `ot135i-v9-load-fix` | one commit, **logging only**: logs the sensor and register values at the magazine edge and after the settle, `reg 0x32` at load op 0, and the polls that completed before a failing op. No new USB transfers, no behaviour change | reviewed, tested, offered to the maintainer |
| `ot135i-cand-interrupt-drain` | candidate: bounded, read-only drain of endpoint 0x83 after the jog and before LOAD, as the Python driver does; one switch (`kDrainInterruptEvents`) | **unproven**, no evidence it is needed |
| `ot135i-cand-longer-settle` | candidate: a longer stable window before LOAD; named constants | **unproven**, no evidence it is needed |

The candidates are branches off the logging commit, kept so they can be tried if a logged
failure points at them. They are not recommendations.

## Why this exists: the load that fails sometimes

On one unit (slide holder), `scanimage --load-film` with v9 failed once and succeeded once
with the same behaviour. Failing run: load op 24 (the engaging feed's completion poll, mask
`0xfb` want `0xf0`) read `0xfc` for 4.83 s, so loader-sensor bit `0x08` never cleared and the
cassette was not taken. Succeeding run (with the logging build): loaded, ejected normally.
From the first LOAD write on, the backend's load and the Python driver's are identical on the
wire (66 transfers, same replies, same waits). The cause of the failure is **not found**. See
[issue 50](https://github.com/cgillinger/opticfilm135i-linux/issues/50) on his repo for the
logs and the wire comparison.

## Build

Needs `autoconf`, `automake`, `libtool`, `autoconf-archive`, `gettext-devel`, `libusb1-devel`,
`libtiff-devel`, `libjpeg-turbo-devel` (verified in a Fedora 43 container).

```
./autogen.sh
./configure --sysconfdir=/etc
make -C lib && make -C sanei && make -C backend libsane-genesys.la
```

## Run without installing (nothing is copied to the system)

```
A=$PWD; S=$HOME/sane-scratch; mkdir -p $S/conf $S/lib
ln -sf $A/backend/.libs/libsane-genesys.so $S/lib/libsane-genesys.so.1
printf 'genesys\n' > $S/conf/dll.conf
printf 'usb 0x07b3 0x1436\n' > $S/conf/genesys.conf
export LD_LIBRARY_PATH=$S/lib SANE_CONFIG_DIR=$S/conf SANE_DEBUG_GENESYS=4
SANE_DEBUG_DLL=4 scanimage -L 2>&1 | grep -iE 'dlopen|genesys'   # must name $S/lib
```

## Tests

The backend's tests drive the built library with a fake USB device and live in the maintainer's
driver repo (`tests/test_sane_magazine.py`, `test_sane_open_params.py`,
`test_sane_calibration_cache.py`; set `SANE_BACKENDS_DIR` to a build of this tree). The tests
for the logging patch are on the branch
[`sane-load-diagnostics-tests`](https://github.com/bearyjd/opticfilm135i-linux/tree/sane-load-diagnostics-tests)
of a fork of that repo; its `tests/gen_gl126_load_film_goldens.sh` regenerates the
transfer-sequence goldens from v9 plus only the test hooks. The candidates' tests are on
`cand-drain-tests` and `cand-settle-tests` of the same fork.

## Safety

A load, scan or eject moves the motor. This tree has been run on one scanner, with an owner
present, power switch in reach. If the holder grinds, scrapes or stops: switch the scanner off,
send nothing more, power-cycle. Never pull a magazine that does not come out freely. No
guarantees; read the code first.

## License

The SANE sources keep their own licenses (`COPYING`, `LICENSE`): GPL-2.0-or-later with SANE's
linking exception for the backend code; the GL126 files carry the maintainer's notices.
