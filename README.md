# coMulator: Coordinate Cross-architecture Compilation and Emulation to Accelerate Dynamic Binary Translation

1.  Prepare the environment

    Using Ubuntu 24.04 is strongly recommended; a container is fine.

    ```bash
    apt-get install -y \
      python3 python3-pip python3-venv \
      build-essential clang llvm-18 llvm-18-dev
    python3 -m venv venv
    . venv/bin/activate
    pip install -r _run/_pip_freeze
    ```

2.  Build the project

    ```bash
    _run/build-qemu       # coMulator-modified QEMU
    _run/build            # coMulator toolset
    _run/guest-bw build   # libc offloading component
    ```

## Usage

`_run/build` installs the whole toolset into `_running/app/` (the IR-transforming executables, `lto-plugin.so`, and `guest_rt.a` are symlinks to `build/app/` or build artifacts, while `gclang`/`gclang++`/`glld`/`hqlibs` are scripts symlinked from the source `app/` directory). The toolset revolves around a single JSON config `gyhcall.config`, and the tools are chained together with LLVM IR (`.bc`/`.ll`) and the symbol table `symtbl.json`.

### Layout of `_running/app/`

```
_running/app/
├── gclang        guest C compiler (clang-18 wrapper, links with glld)
├── gclang++      guest C++ compiler (clang++-18 wrapper, links with glld)
├── glld          guest linker (ld.lld-18 wrapper, auto-loads lto-plugin.so and links guest_rt.a)
├── lto-plugin.so link-time LTO transform plugin (auto-loaded by glld, reads GYHCALL_CONFIG_DIR)
├── guest_rt.a    guest runtime archive (auto-linked by glld)
├── hqlibs        compiles host IR into host shared libraries *.qso
├── config        prints the default gyhcall.config
├── outline       IR transform: outlines non-offloadable code
├── analyze       IR transform: analyzes IR to produce the symtbl.json symbol table
├── take-out      IR transform: extracts host-side functions into IR
├── chop-off      IR transform: removes host functions, keeping guest IR
├── pick-up       IR transform: derives a BwList (allow/block list) from IR
├── inspect       inspection: prints a named global symbol from IR
├── funcnum       inspection: counts functions in IR
└── globhash      inspection: compares global hashes of two IRs
```

> Convention: `outline`/`analyze`/`take-out`/`chop-off`/`pick-up` share the same set of options — `--i-c <config>` (optional; when omitted it reads `$GYHCALL_CONFIG_DIR/gyhcall.config`), `--i-ir`, `--i-st`, `--o-ir`, `--o-st`, `--o-bw`. The `.bc`/`.ll` extension may be omitted and is filled in automatically based on `LLorBC` in the config (`true`→`.ll`, `false`→`.bc`).

### Tool usage (most important first)

#### `gclang` / `gclang++`: compile guest code

They wrap `clang-18`/`clang++-18` with `-fPIC -flto=full` and delegate linking to `glld`. Use them (not the system clang) to compile guest code that gyhcall should process. Once `GYHCALL_CONFIG_DIR` is set, the `lto-plugin` reads that directory's `gyhcall.config` at link time and performs the transform.

```
GYHCALL_CONFIG_DIR=work/qlib \
  _running/app/gclang++ -fPIC -flto=full -O2 -o work/exe src/*.cpp
```

#### `glld` / `lto-plugin.so` / `guest_rt.a`: linking and runtime

`glld` wraps `ld.lld-18`: it automatically passes `--load-pass-plugin=lto-plugin.so` and appends `guest_rt.a` at the end. It is usually not invoked directly — `gclang` drives it via `--ld-path`. The `lto-plugin` runs at the end of LTO, extracting offloadable functions into IR written to the qlib's `guest/<uid>/allin1`; setting `GYHCALL_DEBUG_PLUGIN` makes it stop on load so a debugger can attach.

#### `outline`: outline non-offloadable code

Takes `allin1` as input and produces `outlined`. Whether it is used depends on `UsePFO` in the config — when enabled, later stages read `outlined`; otherwise they read `allin1` directly.

```
_running/app/outline \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/allin1 \
  --o-ir work/qlib/guest/<uid>/outlined
```

#### `analyze`: build the symbol table

Analyzes the IR and produces the symbol table `symtbl.json` for `take-out`/`chop-off` to consume.

```
_running/app/analyze \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/outlined \
  --o-st work/qlib/guest/<uid>/symtbl.json
```

#### `take-out`: extract host functions

Uses the symbol table to pull host-side functions into a separate IR (`host`).

```
_running/app/take-out \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/outlined \
  --i-st work/qlib/guest/<uid>/symtbl.json \
  --o-ir work/qlib/guest/<uid>/host
```

#### `chop-off`: remove host, keep guest

Removes host-side functions, leaving the guest IR (`guest`).

```
_running/app/chop-off \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/outlined \
  --i-st work/qlib/guest/<uid>/symtbl.json \
  --o-ir work/qlib/guest/<uid>/guest
```

#### `pick-up`: derive the allow/block list

Derives a `BwList` (JSON) from an IR; writing it back into the config helps select which functions to process.

```
_running/app/pick-up \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/allin1 \
  --o-bw work/bwlist.json
```

#### `hqlibs`: build host shared libraries

Compiles each qlib's `guest/<uid>/host.{bc,ll}` into a host shared library `host/<uid>.qso` for the runtime to load dynamically.

```
_running/app/hqlibs [--debug] [--with-ir] [--guest_bw <dir>] [--clang] work/qlib
# defaults to -O2; --debug uses -g -Og; --with-ir also emits .ll;
# --guest_bw <dir> additionally builds guest_bw's .qso; --clang selects the clang path
# when the positional arg is omitted, it reads $GYHCALL_CONFIG_DIR instead
```

#### `config`: print the default config

Prints the default `gyhcall.config` (JSON), handy for drafting your own config.

```
_running/app/config > work/qlib/gyhcall.config
```

#### `inspect`: inspect a global symbol

Prints the definition of a named global symbol from the IR, or `NOT FOUND` if absent. It reads no config and only needs `--i-ir`.

```
_running/app/inspect --i-ir work/qlib/guest/<uid>/outlined main foo
```

#### `funcnum`: count functions

Counts functions in the IR: total, original, `__gyh_callback_*`, `__gyh_call_*`, and miscellaneous. It reads no config.

```
_running/app/funcnum --i-ir work/qlib/guest/<uid>/guest
```

#### `globhash`: compare global hashes

Compares the global type/function hashes of two IRs and reports collisions and mismatches (printing `NORMAL`/`UNHASHABLE`/`ADDITIONAL`/`COLLISION`/`MISMATCH` counts). It reads no config.

```
_running/app/globhash --i-ir base.ir cmp.ir
```

### End-to-end example

The steps below chain most of the tools above, from guest source to execution. Prepare a working directory `work/`, where `work/qlib/` holds the config and transform outputs, and `work/exe` is the final guest executable.

```bash
# 0. Draft the config: generate defaults with config, then set UsePFO to true to enable outline
_running/app/config > work/qlib/gyhcall.config
# (optional) with an existing IR, you can use pick-up to derive an allow/block list into the config
#   _running/app/pick-up --i-c work/qlib/gyhcall.config --i-ir ref.ir --o-bw work/bwlist.json

# 1. Compile the guest program: at link time lto-plugin reads gyhcall.config
#    and extracts offloadable functions into work/qlib/guest/<uid>/allin1
GYHCALL_CONFIG_DIR=work/qlib \
  _running/app/gclang++ -fPIC -flto=full -O2 -o work/exe src/*.cpp

# 2. Guest IR transform pipeline (uid is the subdirectory name under work/qlib/guest/)
U=work/qlib/guest/<uid>
_running/app/outline  --i-c work/qlib/gyhcall.config --i-ir $U/allin1   --o-ir $U/outlined
_running/app/analyze  --i-c work/qlib/gyhcall.config --i-ir $U/outlined --o-st $U/symtbl.json
_running/app/take-out --i-c work/qlib/gyhcall.config --i-ir $U/outlined --i-st $U/symtbl.json --o-ir $U/host
_running/app/chop-off --i-c work/qlib/gyhcall.config --i-ir $U/outlined --i-st $U/symtbl.json --o-ir $U/guest

# 3. (optional) inspect the transformed guest IR
_running/app/funcnum --i-ir $U/guest

# 4. Build host shared libraries: reads work/qlib/guest/<uid>/host.{bc,ll}, produces work/qlib/host/<uid>.qso
_running/app/hqlibs work/qlib

# 5. Run: guest programs produced by gclang must be run with the coMulator-modified QEMU
#    (built by _run/build-qemu, in _running/qemu/, e.g. qemu-aarch64 / qemu-riscv64 / qemu-x86_64).
#    Set GYHCALL_CONFIG_DIR to the config dir holding the .qso;
#    set GYHCALL_BREAKDOWN_FILE to collect emulation time / syscall / guest-host call stats
mkdir -p work/run
GYHCALL_CONFIG_DIR=work/qlib \
GYHCALL_BREAKDOWN_FILE=work/run/breakdown.txt \
  _running/qemu/qemu-<arch> work/exe <args>
```

After it finishes, `work/run/breakdown.txt` (if produced) has one `name num` line per metric, mapping to `emu_nsecs`/`syscalls`/`calls`/`callbacks`/`callback_returns` and so on; compare the guest program's output against the native version to verify correctness.
