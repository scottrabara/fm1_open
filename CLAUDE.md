# CLAUDE.md — fm1_open (Sideband fork)

Scott's fork of M-VAVE's Official Open firmware for the FM-1 (8-track
groovebox), with extensions for the Sideband iOS editor
(github.com/scottrabara/Sideband). Upstream: github.com/m-vave/fm1_open.

- **Only `app/` and `tools/` change.** `sdk/` is upstream's and its files are
  hash-checked by `sdk/Build.ps1` against `sdk/manifest.json`; the engine
  (`sdk/lib/libfm1_engine.a`) is a closed binary. `web/engine` is the
  simulator's stand-in for that engine: change it only to mirror upstream.
- **Sideband's extensions live in `app/sideband.c`** (protocol in its header
  comment: commands 22 'S' marker, 40 track map, 41 set DX7 parameters),
  hooked from `app/transfer.c` in a few lines. Everything is additive: a
  client that never sends them sees V14 exactly.
- **Flash is nearly full.** The app image must stay under 453,921 bytes
  (`sdk/Package.cs`: `FlashLimit − UserSize − BtSize − MinimumVm − Area −
  AppData − 447`); upstream V14 leaves a few hundred bytes. A first version
  with change notices and F0 43 10 handling didn't fit. CI prints the size
  ("FM1: N bytes app image"); mark new code `cold, minsize`.
- **Use only engine calls the panel uses** (`groove_fm_edit`,
  `groove_fm_name_edit`, `groove_operator_toggle`, `groove_fm_copy`,
  `groove_fm_import`): they exist in the device's engine as well as the
  simulator's.
- **Test on the host:** `tools/host-test/run.sh` builds the APP with the
  simulator platform natively and runs `*_test.c`; `CC=gcc SANITIZE=1` with
  ASan/UBSan. Every protocol change gets a case in
  `tools/host-test/sideband_test.c`. CI (`.github/workflows/sideband.yml`)
  runs them on Linux, then builds the browser preview and the firmware with
  its workbench on Windows (JieLi toolchain 2.5.3).
- **Replies fit 96 bytes** (the reply cache) and payloads 64 bytes; requests
  ≤ 192 bytes, data ≤ 132 bytes unpacked.
- **Licence:** GPLv3 with the SDK linking exception (LICENSE.md). Builds we
  hand out carry the matching source URL (`FM1_SOURCE_URL`, set by CI to
  the commit's archive).
- Commit messages end with the `Co-Authored-By` trailer used in history.
