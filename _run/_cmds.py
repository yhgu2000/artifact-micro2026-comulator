import json
from dataclasses import dataclass
from typing import Literal

from environ import ENVIRON

from . import Path, runps_async


async def cp_rf(dst: str | Path, *src: str | Path, link=False, preserve=False) -> None:
    """异步执行 cp -rf

    :param dst: 目标目录
    :param src: 源目录
    :param link: 是否创建硬链接（-l 选项）
    :param preserve: 是否保留源文件的元数据（-p 选项）
    """

    await runps_async(
        ENVIRON.EXE.cp,
        "-rf",
        *(["-l"] if link else []),
        *(["-p"] if preserve else []),
        *map(str, src),
        str(dst),
        logstlv=3,
        check=0,
    )


async def rm_rf(*dst: str | Path) -> None:
    """异步执行 rm -rf

    :param dst: 目标目录
    """

    await runps_async(
        ENVIRON.EXE.rm,
        "-rf",
        *map(str, dst),
        logstlv=3,
        check=0,
    )


async def buildah_build(
    tag: str,
    dir: str | Path,
    file: str | Path | None = None,
    layers=True,
    nocache=False,
    volumes: list[tuple[str | Path, str | Path, Literal["rw", "ro"]]] = [],
) -> None:
    if file:
        file = Path(file).relative_to(dir)

    volume_options = []
    for host, ctnr, ro in volumes:
        volume_options.append("--volume")
        volume_options.append(f"{host}:{ctnr}:{ro}")
    await runps_async(
        ENVIRON.EXE.buildah,
        "build",
        *(["--layers"] if layers else []),
        *(["--no-cache"] if nocache else []),
        *volume_options,
        *["-t", tag],
        *(["-f", str(file)] if file else []),
        dir,
        cwd=dir,
        logstlv=3,
        check=0,
    )


async def buildah_push(img: str, tgt: str, tls=True) -> None:
    await runps_async(
        ENVIRON.EXE.buildah,
        "push",
        *([] if tls else ["--tls-verify=false"]),
        *[img, tgt],
        logstlv=3,
        check=0,
    )


@dataclass
class BuildahInspect:
    image_id: str


async def buildah_inspect(img: str) -> BuildahInspect:
    stdout, *_ = await runps_async(
        ENVIRON.EXE.buildah,
        "inspect",
        img,
        logstlv=3,
        check=0,
    )
    with open(stdout) as f:
        data = json.load(f)
    return BuildahInspect(
        image_id=data["FromImageID"],
    )


async def git_clone(
    cwd: str | Path,
    target_dir: str | Path,
    remote_url: str,
    branch: str | None = None,
    single_branch: bool | None = None,
) -> None:
    """异步执行 git clone

    :param cwd: 工作目录
    :param remote_url: 远程仓库
    :param branch: 分支
    :param kwargs: 其他参数, 参见 arun
    """

    await runps_async(
        ENVIRON.EXE.git,
        "clone",
        remote_url,
        *(["-b", branch] if branch else []),
        *(["--single-branch"] if single_branch else []),
        target_dir,
        cwd=cwd,
        logstlv=3,
        check=0,
    )


async def git_pull(
    cwd: str | Path, remote: str | None = None, branch: str | None = None
) -> None:
    """异步执行 git pull

    :param cwd: 工作目录
    :param remote: 远程仓库
    :param branch: 分支
    :param kwargs: 其他参数, 参见 arun
    """

    if branch:
        assert remote, "branch 参数需要 remote 参数"
    await runps_async(
        ENVIRON.EXE.git,
        "pull",
        *([remote] if remote else []),
        *([branch] if branch else []),
        cwd=cwd,
        logstlv=3,
        check=0,
    )


async def tar_cf_dir(out: str | Path, dir: str | Path) -> None:
    """异步执行 tar, 将目录里的所有文件打包, 但不包括目录本身

    :param out: 输出文件, 必须以 .tar / .tgz / .txz 结尾
    :param dir: 目录
    :param kwargs: 其他参数, 参见 arun
    """

    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.suffix == ".tar":
        opt = "-cf"
    elif out.suffix == ".tgz":
        opt = "-czf"
    elif out.suffix == ".txz":
        opt = "-cJf"
    else:
        raise ValueError(f"不支持的输出文件类型：{out!r}")
    await runps_async(
        ENVIRON.EXE.tar,
        *[opt, str(out)],
        *["-C", str(dir)],
        ".",
        logstlv=3,
        check=0,
    )


async def wget(url: str, out: str | Path) -> None:
    """异步执行 wget

    :param url: 下载链接
    :param out: 输出文件
    """

    await runps_async(
        ENVIRON.EXE.wget,
        "-c",
        *["-O", str(out)],
        url,
        logstlv=3,
        check=0,
    )
