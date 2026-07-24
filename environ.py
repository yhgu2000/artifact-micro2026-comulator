from _run import Configuration, Path, here
from _run._gyhcall import BwListModel, FuncBwModel
from data.cases import APPS
from lib import guest_rt

HERE = here(__file__)


class ENVIRON(Configuration):
    cmake_build_dir: Path = HERE.src.parent / "build"
    """CMake 构建目录"""
    cmake_build_type: str = "RelWithDebugInfo"
    """CMake 构建类型"""

    local_guest_rt = guest_rt.x86_64
    """本机的 guest_rt 版本"""

    EXE: "EXE"
    """外部可执行程序"""
    TEST: "TEST"
    """单元测试配置"""


class EXE(Configuration, _=ENVIRON):
    cp = "/bin/cp"
    rm = "/bin/rm"
    env = "/bin/env"

    git = "/bin/git"
    tar = "/bin/tar"
    wget = "/bin/wget"

    buildah = "/bin/buildah"
    skopeo = "/bin/skopeo"
    podman = "/bin/podman"

    cmake = "/bin/cmake"
    clang = "/bin/clang-18"
    clangpp = "/bin/clang++-18"
    ar = "/bin/ar"


class TEST(Configuration, _=ENVIRON):
    gyhcall_app_dir: Path = HERE.running_dir / "app"


try:
    from env import ENVIRON  # type: ignore # noqa
except ImportError:
    pass
