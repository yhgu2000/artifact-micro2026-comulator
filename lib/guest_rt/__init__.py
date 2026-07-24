from dataclasses import dataclass

from _run import Path, here, runps_async

HERE = here(__file__)


@dataclass(kw_only=True)
class Target:

    arch: str
    """客方架构名"""

    srcs: list[Path]
    """关联源码"""

    async def build(self, build_dir: Path, cc: Path, ar: Path) -> Path:
        """构建 guest_rt.a

        :param build_dir: 构建目录
        :param cc: 编译器
        :param ar: 归档工具
        :return: 构建后的 guest_rt.a
        """

        await runps_async(
            cc,
            "-c",
            "-fPIC",
            *self.srcs,
            cwd=build_dir,
            check=0,
        )
        ret = build_dir / "guest_rt.a"
        await runps_async(
            ar,
            "rcs",
            ret,
            *build_dir.glob("*.o"),
            cwd=build_dir,
            check=0,
        )
        return ret


mock = Target(arch="mock", srcs=[HERE / "debug.c", HERE / "mock.c"])
x86_64 = Target(arch="x86_64", srcs=[HERE / "debug.c", HERE / "x86_64.s"])
aarch64 = Target(arch="aarch64", srcs=[HERE / "debug.c", HERE / "aarch64.s"])
riscv64 = Target(arch="riscv64", srcs=[HERE / "debug.c", HERE / "riscv64.s"])
