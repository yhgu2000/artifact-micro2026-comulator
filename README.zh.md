# coMulator: Coordinate Cross-architecture Compilation and Emulation to Accelerate Dynamic Binary Translation

1.  准备环境

    强烈建议使用 Ubuntu 24.04 系统环境，可以使用容器。

    ```bash
    apt-get install -y \
      python3 python3-pip python3-venv \
      build-essential clang llvm-18 llvm-18-dev
    python3 -m venv venv
    . venv/bin/activate
    pip install -r _run/_pip_freeze
    ```

2.  构建项目

    ```bash
    _run/build-qemu       # coMulator-modified QEMU
    _run/build            # coMulator toolset
    _run/guest-bw build   # libc offloading component
    ```

## 如何使用

`_run/build` 会把工具集统一安装到 `_running/app/` 目录（其中变换类可执行文件、`lto-plugin.so`、`guest_rt.a` 是指向 `build/app/` 或构建产物的符号链接，`gclang`/`gclang++`/`glld`/`hqlibs` 是指向源码 `app/` 的脚本）。整套工具围绕一份 JSON 配置 `gyhcall.config` 工作，工具之间用 LLVM IR（`.bc`/`.ll`）和符号表 `symtbl.json` 串联。

### `_running/app/` 目录结构

```
_running/app/
├── gclang        客方 C 编译器（clang-18 封装，用 glld 链接）
├── gclang++      客方 C++ 编译器（clang++-18 封装，用 glld 链接）
├── glld          客方链接器（ld.lld-18 封装，自动加载 lto-plugin.so、链接 guest_rt.a）
├── lto-plugin.so 链接期 LTO 变换插件（由 glld 自动加载，读 GYHCALL_CONFIG_DIR）
├── guest_rt.a    客方运行时归档（由 glld 自动链接）
├── hqlibs        把 host IR 编译为主机共享库 *.qso
├── config        打印默认 gyhcall.config
├── outline       IR 变换：外联不可卸载代码
├── analyze       IR 变换：分析 IR 生成符号表 symtbl.json
├── take-out      IR 变换：取出主方函数 IR
├── chop-off      IR 变换：切除主方函数，保留客方 IR
├── pick-up       IR 变换：从 IR 反推黑白名单 BwList
├── inspect       检查：打印 IR 中指定全局符号
├── funcnum       检查：统计 IR 函数数量
└── globhash      检查：比对两份 IR 的全局哈希
```

> 约定：`outline`/`analyze`/`take-out`/`chop-off`/`pick-up` 共享同一组参数 —— `--i-c <config>`（可省，省略时读环境变量 `GYHCALL_CONFIG_DIR/gyhcall.config`）、`--i-ir`、`--i-st`、`--o-ir`、`--o-st`、`--o-bw`。`.bc`/`.ll` 扩展名可省，由配置里的 `LLorBC` 自动补齐（`true`→`.ll`，`false`→`.bc`）。

### 工具用法（由重要到次要）

#### `gclang` / `gclang++`：编译客方代码

在 `clang-18`/`clang++-18` 基础上加 `-fPIC -flto=full`，并把链接交给 `glld`。用它们（而非系统 clang）编译需要被 gyhcall 处理的客方代码。设好 `GYHCALL_CONFIG_DIR` 后，链接期 `lto-plugin` 会读该目录的 `gyhcall.config` 进行变换。

```
GYHCALL_CONFIG_DIR=work/qlib \
  _running/app/gclang++ -fPIC -flto=full -O2 -o work/exe src/*.cpp
```

#### `glld` / `lto-plugin.so` / `guest_rt.a`：链接与运行时

`glld` 封装 `ld.lld-18`，启动时自动 `--load-pass-plugin=lto-plugin.so`，并在末尾链接 `guest_rt.a`。通常不直接调用，由 `gclang` 经 `--ld-path` 驱动。`lto-plugin` 在 LTO 末段把可外联函数提取为 IR，写入 qlib 的 `guest/<uid>/allin1`；设环境变量 `GYHCALL_DEBUG_PLUGIN` 可让它在加载时停住以便调试器附加。

#### `outline`：外联不可卸载代码

输入 `allin1`，输出 `outlined`。是否启用取决于配置里的 `UsePFO`（开启时后续阶段读 `outlined`，否则直接读 `allin1`）。

```
_running/app/outline \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/allin1 \
  --o-ir work/qlib/guest/<uid>/outlined
```

#### `analyze`：生成符号表

分析 IR，产出符号表 `symtbl.json`，供 `take-out`/`chop-off` 使用。

```
_running/app/analyze \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/outlined \
  --o-st work/qlib/guest/<uid>/symtbl.json
```

#### `take-out`：取出主方函数

按符号表把主方函数抽到一份独立 IR（`host`）。

```
_running/app/take-out \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/outlined \
  --i-st work/qlib/guest/<uid>/symtbl.json \
  --o-ir work/qlib/guest/<uid>/host
```

#### `chop-off`：切除主方、保留客方

切除主方函数，留下客方 IR（`guest`）。

```
_running/app/chop-off \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/outlined \
  --i-st work/qlib/guest/<uid>/symtbl.json \
  --o-ir work/qlib/guest/<uid>/guest
```

#### `pick-up`：反推黑白名单

从一份 IR 推导出黑白名单 `BwList`（JSON），写回配置辅助筛选要处理的函数。

```
_running/app/pick-up \
  --i-c work/qlib/gyhcall.config \
  --i-ir work/qlib/guest/<uid>/allin1 \
  --o-bw work/bwlist.json
```

#### `hqlibs`：构建主机共享库

把每个 qlib 里的 `guest/<uid>/host.{bc,ll}` 编译为主机共享库 `host/<uid>.qso`，供运行时动态加载。

```
_running/app/hqlibs [--debug] [--with-ir] [--guest_bw <dir>] [--clang] work/qlib
# 默认 -O2；--debug 用 -g -Og；--with-ir 额外输出 .ll；
# --guest_bw <dir> 额外编译 guest_bw 的 .qso；--clang 指定 clang 路径
# 省略位置参数时改读 $GYHCALL_CONFIG_DIR
```

#### `config`：打印默认配置

打印默认 `gyhcall.config`（JSON），便于据此起草配置。

```
_running/app/config > work/qlib/gyhcall.config
```

#### `inspect`：查看全局符号

打印 IR 中指定全局符号的定义，找不到则输出 `NOT FOUND`。不读配置，仅需 `--i-ir`。

```
_running/app/inspect --i-ir work/qlib/guest/<uid>/outlined main foo
```

#### `funcnum`：统计函数数量

统计 IR 中函数数量：总数、原始函数、`__gyh_callback_*`、`__gyh_call_*` 及杂项。不读配置。

```
_running/app/funcnum --i-ir work/qlib/guest/<uid>/guest
```

#### `globhash`：比对全局哈希

比对两份 IR 的全局类型/函数哈希，报告碰撞与不一致（输出 `NORMAL`/`UNHASHABLE`/`ADDITIONAL`/`COLLISION`/`MISMATCH` 计数）。不读配置。

```
_running/app/globhash --i-ir base.ir cmp.ir
```

### 完整案例

下面把上面大部分工具串起来，从一份客方源码走到运行。准备一个工作目录 `work/`，其中 `work/qlib/` 存放配置与变换产物，`work/exe` 是最终客方可执行文件。

```bash
# 0. 起草配置：用 config 生成默认值，再把 UsePFO 改为 true 以启用 outline
_running/app/config > work/qlib/gyhcall.config
# （可选）若已有现成 IR，可用 pick-up 反推黑白名单写进配置
#   _running/app/pick-up --i-c work/qlib/gyhcall.config --i-ir ref.ir --o-bw work/bwlist.json

# 1. 编译客方程序：gclang++ 链接时 lto-plugin 读 gyhcall.config，
#    把可外联函数提取为 work/qlib/guest/<uid>/allin1
GYHCALL_CONFIG_DIR=work/qlib \
  _running/app/gclang++ -fPIC -flto=full -O2 -o work/exe src/*.cpp

# 2. 客方 IR 变换流水线（uid 取自 work/qlib/guest/ 下的子目录名）
U=work/qlib/guest/<uid>
_running/app/outline  --i-c work/qlib/gyhcall.config --i-ir $U/allin1   --o-ir $U/outlined
_running/app/analyze  --i-c work/qlib/gyhcall.config --i-ir $U/outlined --o-st $U/symtbl.json
_running/app/take-out --i-c work/qlib/gyhcall.config --i-ir $U/outlined --i-st $U/symtbl.json --o-ir $U/host
_running/app/chop-off --i-c work/qlib/gyhcall.config --i-ir $U/outlined --i-st $U/symtbl.json --o-ir $U/guest

# 3. （可选）检查变换后的客方 IR
_running/app/funcnum --i-ir $U/guest

# 4. 构建主机共享库：读取 work/qlib/guest/<uid>/host.{bc,ll}，产出 work/qlib/host/<uid>.qso
_running/app/hqlibs work/qlib

# 5. 运行：gclang 产出的客方程序必须用 coMulator 修改过的 QEMU 运行
#    （由 _run/build-qemu 构建，位于 _running/qemu/，如 qemu-aarch64 / qemu-riscv64 / qemu-x86_64）。
#    设 GYHCALL_CONFIG_DIR 指向含 .qso 的配置目录；
#    设 GYHCALL_BREAKDOWN_FILE 收集模拟耗时/系统调用/客主调用等统计
mkdir -p work/run
GYHCALL_CONFIG_DIR=work/qlib \
GYHCALL_BREAKDOWN_FILE=work/run/breakdown.txt \
  _running/qemu/qemu-<arch> work/exe <args>
```

运行结束后，`work/run/breakdown.txt`（若生成）里每行 `name num` 对应 `emu_nsecs`/`syscalls`/`calls`/`callbacks`/`callback_returns` 等指标；客方程序的输出与原生版本对比即可判断正确性。
