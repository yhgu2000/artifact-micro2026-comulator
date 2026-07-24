import asyncio as aio
from dataclasses import dataclass

from _run import Path, RunPs, here, runps_async
from _run._gyhcall import Config, BwListModel

HERE = here(__file__)


@dataclass(kw_only=True)
class Target:

    async def build(self, build_dir: Path, gclang: Path, config: Config) -> Path:
        """
        :param build_dir: 构建目录
        :param gclang: 我们的 gclang 编译器
        :param variant: 变体
        :return: 发布目录, 里面包含 guest_bw.so 和主侧 IR
        """

        qlib_dir = build_dir / "qlib"
        qlib_dir.mkdir(parents=True)

        config = config.model_copy()
        config.BwList = BwListModel(FCPcmChopOthers=True)
        with open(qlib_dir / "gyhcall.config", "w") as f:
            f.write(config.model_dump_json(indent=2))

        async with aio.TaskGroup() as tg:
            for musl_mod in ["math", "complex", "string"]:
                build_src_dir = build_dir / musl_mod
                build_src_dir.mkdir(parents=True)
                tg.create_task(
                    runps_async(
                        gclang,
                        *("-c", "-w", "-O2", "-nostdinc"),
                        *("-I", HERE / "include"),
                        "-D_GNU_SOURCE",
                        *(HERE / musl_mod).glob("*.c"),
                        env={"GYHCALL_CONFIG_DIR": str(qlib_dir)},
                        cwd=build_src_dir,
                        check=0,
                    )
                )
            tg.create_task(
                runps_async(
                    gclang,
                    *("-c", "-w", "-O2"),
                    *("-o", build_dir / "malloc.o"),
                    HERE / "malloc.c",
                    env={"GYHCALL_CONFIG_DIR": str(qlib_dir)},
                    check=0,
                )
            )

        for musl_mod in ["math", "complex", "string"]:
            with open(build_dir / f"{musl_mod}.list", "w") as f:
                for o in (build_dir / musl_mod).glob("*.o"):
                    print(o, file=f)

        dist_dir = build_dir / "dist"
        dist_dir.mkdir(parents=True)
        await runps_async(
            gclang,
            *("-shared", "-O2", "-o", dist_dir / "guest_bw.so"),
            build_dir / "malloc.o",
            f"@{build_dir / 'math.list'}",
            f"@{build_dir / 'complex.list'}",
            f"@{build_dir / 'string.list'}",
            env={"GYHCALL_CONFIG_DIR": str(qlib_dir)},
            check=0,
        )

        guests = list((qlib_dir / "guest").glob("*"))
        assert len(guests) == 1
        qlib_uid = guests[0].name
        host_ir = next(guests[0].glob("host.*"))
        (dist_dir / f"{qlib_uid}{host_ir.suffix}").hardlink_to(host_ir)

        return dist_dir

    def debug(self, build_dir: Path, gclang: Path) -> str:
        """
        :param build_dir: 构建目录
        :param gclang: 我们的 gclang 编译器
        :return: 调试器
        """

        return RunPs(
            cmd=[
                str(gclang),
                *map(str, ("-shared", "-o", build_dir / "guest_bw.so")),
                str(build_dir / "malloc.o"),
                f"@{build_dir / 'math.list'}",
                f"@{build_dir / 'complex.list'}",
                f"@{build_dir / 'string.list'}",
            ],
            env={
                "GYHCALL_CONFIG_DIR": str(build_dir / "qlib"),
                "GYHCALL_DEBUG_PLUGIN": "",
            },
            out=False,
            err=False,
        ).format_bash()


TARGET = Target()
