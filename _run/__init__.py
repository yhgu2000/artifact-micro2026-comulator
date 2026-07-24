"""项目元框架"""

PROJECT = "gyhcall"
VERSION = "v2"


# ==================================================================================== #
import asyncio as aio
import inspect
import logging
import os
import pickle
import random
import re
import shlex
import shutil
import subprocess as subp
import sys
import tempfile
from abc import ABC, abstractmethod
from contextvars import ContextVar
from copy import deepcopy
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from io import BufferedReader, BufferedWriter, IOBase, StringIO, TextIOBase
from pathlib import Path
from types import EllipsisType, NotImplementedType
from typing import (
    Any,
    AsyncIterable,
    Callable,
    Coroutine,
    Generator,
    Iterable,
    Literal,
    NamedTuple,
    Optional,
    TypeVar,
    Union,
    overload,
)

_T = TypeVar("_T")


# ==================================================================================== #
NAMING_RULE = re.compile(r"[a-z](?:(?:[a-z0-9]|-(?!-)){0,62}[a-z0-9])?")  # RFC 1035
"""一般性的命名规则"""
assert NAMING_RULE.fullmatch(PROJECT) and NAMING_RULE.fullmatch(VERSION)


def naming(name: str, capital: bool | None = False, hyphen="-") -> str:
    """生成命名用字符串

    :param naming: 命名字符串, 只能由字母数字和最多一种标点符号构成
    :param capital: False-全小写, True-全大写, None-首字母大写
    :param hyphen: 连字符
    :return: 格式化后的字符串, 不合法时返回空字符串。
    """

    sep = ""
    for i in map(ord, set(name)):
        if 64 <= i <= 90 or 97 <= i <= 122 or 48 <= i <= 57:
            continue
        if sep != "":
            return ""
        sep = chr(i)
    if sep == "":
        parts = [name]
    else:
        parts = name.split(sep)
    if capital is None:
        parts = [i[:1].upper() + i[1:].lower() for i in parts]
    elif capital:
        parts = [i.upper() for i in parts]
    else:
        parts = [i.lower() for i in parts]
    return hyphen.join(parts)


# TODO UCN 正则式生成


def utctime() -> float:
    """获取当前的 UTC 时间戳"""

    return datetime.now(timezone.utc).timestamp()


def timetag(dt: datetime | EllipsisType | None = None, us=False) -> str:
    """时间标签, 由数字和减号组成

    :param dt: 时间对象, 默认使用当前时间, `...` 使用 UTC
    :param us: 是否包含微秒
    :return: 形如 `20250301-102204-094879`
    """

    if dt is None:
        dt = datetime.now()
    elif dt is ...:
        dt = datetime.now(timezone.utc)
    fmt = "%Y%m%d-%H%M%S"
    if us:
        fmt += "-%f"
    return dt.strftime(fmt)


# ==================================================================================== #
PROJECT_DIR = Path(__file__).resolve().parent.parent
"""项目根路径"""
RUNNING_DIR = PROJECT_DIR / "_running"
"""项目脚本运行存储目录"""
LOGGING_DIR = PROJECT_DIR / "_logging"
"""项目脚本运行日志目录, 每次启动唯一"""
PROJVER_KEY = naming(PROJECT, True, "_") + "_" + naming(VERSION, True, "_")
"""项目版本环境变量前缀"""


class Here:

    project_dir = PROJECT_DIR
    running_dir = RUNNING_DIR
    logging_dir = LOGGING_DIR

    def __init__(self, relpath: Path) -> None:
        self.__relpath = relpath

    def __str__(self) -> str:
        """获取这里的绝对路径字符串"""
        return os.path.abspath((self.project_dir / self.__relpath).parent)

    def __call__(self, *path: str | Path) -> str:
        """获取 *path 在这里的绝对路径字符串"""
        return os.path.abspath(os.path.join(str(self), *path))

    def __truediv__(self, path: str | Path) -> Path:
        """获取 *path 在这里的路径"""
        return (self.project_dir / self.__relpath).parent / path

    @property
    def src(self) -> Path:
        """获取当前源文件的绝对路径"""
        return self.project_dir / self.__relpath

    @property
    def run_dir(self) -> Path:
        """获取模块对应的运行目录"""
        return self.running_dir / self.__relpath

    @property
    def log_dir(self) -> Path:
        """获取模块对应的日志目录"""
        return self.logging_dir / self.__relpath

    def __path(
        self,
        dir: Path,
        *path: str | Path,
        rm=False,
        mp=False,
        md=False,
        uni=False,
        unipre="",
        unisuf="",
    ) -> Path:
        ret = dir.joinpath(*path).absolute()
        ret = Path(os.path.normpath(ret))
        if uni:
            ret /= (
                unipre
                + timetag(us=True)
                + "".join(random.sample("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 4))
                + unisuf
            )
        if rm:
            shutil.rmtree(ret, ignore_errors=True)
        if mp:
            ret.parent.mkdir(parents=True, exist_ok=True)
        if md:
            ret.mkdir(parents=True, exist_ok=True)
            ret = ret.resolve()
        return ret

    def running(
        self,
        *path: str | Path,
        rm=False,
        mp=False,
        md=False,
        uni=False,
        unipre="",
        unisuf="",
    ) -> Path:
        """获取 *path 在运行目录中的绝对路径

        :param rm: 是否删除目标
        :param mp: 是否创建父目录
        :param md: 是否创建目标为目录
        :param uni: 在路径最后追加唯一文件名
        :param unipre: 唯一文件名的前缀
        :param unisuf: 唯一文件名的后缀
        :return: 绝对路径
        """
        return self.__path(
            self.run_dir,
            *path,
            rm=rm,
            mp=mp,
            md=md,
            uni=uni,
            unipre=unipre,
            unisuf=unisuf,
        )

    def logging(
        self,
        *path: str | Path,
        rm=False,
        mp=False,
        md=False,
        uni=False,
        unipre="",
        unisuf="",
    ) -> Path:
        """获取 *path 在日志目录中的绝对路径

        :param rm: 是否删除目标
        :param mp: 是否创建父目录
        :param md: 是否创建目标为目录
        :param uni: 在路径最后追加唯一文件名
        :param unipre: 唯一文件名的前缀
        :param unisuf: 唯一文件名的后缀
        :return: 绝对路径
        """
        return self.__path(
            self.log_dir,
            *path,
            rm=rm,
            mp=mp,
            md=md,
            uni=uni,
            unipre=unipre,
            unisuf=unisuf,
        )


def here(__file__) -> Here:
    """获取这里的路径访问器

    :param __file__: 当前文件路径
    :return: 路径访问器
    """
    return Here(Path(os.path.normpath(os.path.relpath(__file__, PROJECT_DIR))))


def init_dirs(*, _logdir_suffix="") -> None:
    """初始化全局目录, 只应在入口处被调用一次"""
    global RUNNING_DIR, LOGGING_DIR

    if v := os.environ.get(PROJVER_KEY + "_RUNNING_DIR"):
        RUNNING_DIR = Path(v)
    else:
        RUNNING_DIR.mkdir(parents=True, exist_ok=True)
        os.environ[PROJVER_KEY + "_RUNNING_DIR"] = str(RUNNING_DIR)
    Here.running_dir = RUNNING_DIR

    if v := os.environ.get(PROJVER_KEY + "_LOGGING_DIR"):
        LOGGING_DIR = Path(v)
    else:
        LOGGING_DIR /= timetag() + _logdir_suffix
        LOGGING_DIR.mkdir(parents=True, exist_ok=True)
        os.environ[PROJVER_KEY + "_LOGGING_DIR"] = str(LOGGING_DIR)
    Here.logging_dir = LOGGING_DIR


ROOT = Here(Path())
HERE = here(__file__)


# ==================================================================================== #
class LogFormatter(logging.Formatter):

    def format(self, rec: logging.LogRecord) -> str:
        # cat 和 tags 是我们额外定义的字段, 因此要容忍非预期的错误!
        cat = getattr(rec, "cat", "")
        if type(cat) is not str:
            cat = ""
        tags = getattr(rec, "tags", {})
        if type(tags) is not dict:
            tags = {}

        # 需要保证写入日志的可解析性
        sio = StringIO()
        body = super().format(rec)

        # 元信息头
        timestamp = datetime.fromtimestamp(rec.created).isoformat()
        thread = rec.thread or ""
        task_name = getattr(rec, "taskName", "")
        head = (
            f"\n{rec.levelno} {rec.name!r} {cat!r} ["
            f"{rec.pathname!r}:{rec.lineno} | "
            f"{timestamp} {thread}-{task_name!r}"
            f"] {len(tags)} {len(body)}\n"
        )
        sio.write(head)

        # 标签键值对
        for k, v in tags.items():
            try:
                s = str(k)
            except Exception:
                s = str(id(k))
            sio.write(repr(s))
            sio.write("=")
            try:
                s = str(v)
            except Exception:
                s = str(id(v))
            sio.write(repr(s))
            sio.write("\n")

        # 日志主体
        sio.write(body)
        return sio.getvalue()


class LogParser:
    """对应于 Formatter 输出风格的日志解析器"""

    def parse_io(self, io: IOBase) -> Generator[logging.LogRecord, None, None]:
        raise NotImplementedError("TODO")

    def parse_text(self, text: str) -> Generator[logging.LogRecord, None, None]:
        raise NotImplementedError("TODO")

    def parse_file(self, path: Path) -> Generator[logging.LogRecord, None, None]:
        with open(path, "r") as f:
            for i in self.parse_io(f):
                yield i

    def filter(self, rec: logging.LogRecord) -> logging.LogRecord | None:
        """过滤器, 在子类中重写以在解析中过滤日志记录

        :param rec: 日志记录
        :return: 过滤后的日志记录, 或 None 表示丢弃
        """
        return rec


def module_logger(__spec__, __file__) -> logging.Logger:
    """获取当前模块的日志器

    :param __spec__: 如果不为主模块, 用作日志器名
    :param __file__: 如果为主模块, 将其相对路径用作日志名
    :return: 日志器
    """
    if __spec__ is None or __spec__.name == "__main__":
        name = os.path.normpath(os.path.relpath(__file__, PROJECT_DIR))
    else:
        name = __spec__.name
    return logging.getLogger(name)


def init_logging(
    name: str,
    level: int | None = None,
    stderr: bool | None = None,
    unilog: bool | None = None,
    envkey=PROJVER_KEY + "_RUNLOG",
) -> Path | None:
    """初始化日志模块, 只应在程序入口处被调用一次

    :param name: 日志名, 用于命名文件
    :param level: 日志输出级别
    :param stderr: 是否打印日志到标准错误输出, 而不是重定向到文件
    :param unilog: 不向索引文件名中加入进程号, 使多进程共用一个日志文件
    :param envkey: 保存默认参数的环境变量名, 用于跨进程传递日志配置
    :return: 日志文件路径, 使用标准输出时为 None
    """

    assert NAMING_RULE.fullmatch(name), name
    default = os.environ.get(envkey, None)
    if default:
        parts = default.split()
        if level is None:
            level = eval(parts[0])
        if stderr is None:
            stderr = eval(parts[1])
        if unilog is None:
            unilog = eval(parts[2])
    else:
        if level is None:
            level = logging.INFO
        if stderr is None:
            stderr = False
        if unilog is None:
            unilog = False
        default = f"{level} {stderr} {unilog}"
        os.environ[envkey] = default

    if stderr:
        handler: logging.Handler = logging.StreamHandler()
        ret: Path | None = None
    else:
        if unilog:
            ret = LOGGING_DIR / f"{name}.log"
        else:
            ret = LOGGING_DIR / f"{name}.{os.getpid()}.log"
        handler = logging.FileHandler(filename=ret)

    handler.setFormatter(LogFormatter())
    logging.root.addHandler(handler)
    logging.root.setLevel(level)

    LOGGER.log(99, "LOGFILE SETUP: level=%s index=%s", logging.root.level, ret)
    return ret


LOGGER = module_logger(__spec__, __file__)


# ==================================================================================== #
class Progress:

    class GlobalVar:

        def set(self, x):
            self.x = x

        def get(self):
            return self.x

    def __init__(self, cxtvarname: str) -> None:
        # self.__cur: ContextVar[Watcher] = ContextVar(cxtvarname)
        self.__cur = Progress.GlobalVar()
        self.__cur.set(Silence())

    def push(self, watcher: "Watcher") -> None:
        """使用新的进度监视器

        :param watcher: 进度监视器实例
        """

        assert watcher._Watcher__prev is watcher, watcher._Watcher__prev
        watcher._Watcher__prev = self.__cur.get()
        self.__cur.set(watcher)
        watcher.pushed()

    def pop(self) -> Optional["Watcher"]:
        """恢复前一个进度监视器

        :return: 返回被 pop 的进度监视器, 如果已经没有监视器则返回 None
        """

        cur = self.__cur.get()
        prev = cur._Watcher__prev
        if cur is prev:
            return None
        cur._Watcher__prev = cur
        self.__cur.set(prev)
        cur.popped()
        return cur

    @staticmethod
    def push_Silence(**kwargs) -> None:
        PROGRESS.push(Silence(**kwargs))

    @staticmethod
    def push_PrintTree(out=sys.stderr, flush=False) -> None:
        PROGRESS.push(PrintTree(out, flush))

    # ============================================================================ #

    def __call__(
        self,
        msg: Optional[Any] = None,
        logger: Union[logging.Logger, Callable, None] = None,
        total: Union[int, EllipsisType, None, NotImplementedType] = NotImplemented,
    ) -> Any:
        """圆括号（调用）语法提示状态和进度总量

        ```
        PROGRESS("msg")                 # 提示状态
        PROGRESS("msg", total=100)      # 提示状态, 同时设置进度总量
        PROGRESS("msg", LOGGER.error)   # 提示状态, 同时记录日志
        PROGRESS(total=...)             # 设置进度总量为未知
        PROGRESS("msg", LOGGER, 100)    # 完全形式
        ```

        :param msg: 提示消息, None 指代空消息
        :param log: 日志记录器
        :param total: 进度总量, Ellipsis(...) 指代未知, None 取消设置总量
        :return: 返回 msg
        """

        if logger is not None:
            if total is NotImplemented:
                progress_info = ""
            elif total is None:
                progress_info = "PROGRESS TOTAL RESET\n"
            elif total is ...:
                progress_info = "PROGRESS TOTAL UNKNOWN\n"
            else:
                progress_info = f"PROGRESS TOTAL {total}\n"
            if isinstance(logger, logging.Logger):
                logger.info("%s%s", progress_info, msg, stacklevel=2)
            else:
                logger("%s%s", progress_info, msg, stacklevel=2)
        if total is NotImplemented:
            self.__cur.get().desc(msg)
        else:
            self.__cur.get().total(total, msg)
        return msg

    class WithEnter:

        def __init__(self, prog, msg) -> None:
            self.prog = prog
            self.msg = msg

        def __enter__(self):
            self.prog._Progress__cur.get().enter(self.msg)
            del self.msg  # 尽早释放资源
            return self

        def __exit__(self, exc_type, exc_value, traceback):
            self.prog._Progress__cur.get().exit(exc_value)

    def __getitem__(
        self,
        arg: Union[Any, tuple[Optional[Any], Union[logging.Logger, Callable]]],
    ) -> "Progress.WithEnter":
        """方括号语法语法进入子过程

        ```
        with PROGRESS["msg"]:           # 同时提示状态
        with PROGRESS["msg", LOGGER]:   # 同时提示状态和记录日志
        ```

        不建议在异步函数中使用 with, 这样可能导致层级嵌套的混乱
        """

        if type(arg) is tuple:
            msg, logger = arg
        else:
            msg, logger = arg, None
        if logger is not None:
            if isinstance(logger, logging.Logger):
                logger.info("PROGRESS ENTER\n%s", msg, stacklevel=2)
            else:
                logger("PROGRESS ENTER\n%s", msg, stacklevel=2)
        return Progress.WithEnter(self, msg)

    def __enter__(self) -> "Progress":
        """使用空消息进入子过程"""

        self.__cur.get().enter(None)
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.__cur.get().exit(exc_value)

    def __add__(self, rel: Union[int, tuple[Any, int]]) -> "Progress":
        """加运算符 + 用于步进进度

        ```
        PROGRESS + 3   # 步进 3
        PROGRESS + ("msg", 3)  # 步进 3, 同时提示状态
        ```

        :param msg: 提示消息
        """

        if type(rel) is tuple:
            msg, rel = rel
            self.__cur.get().step(rel, msg)
        elif type(rel) is int:
            self.__cur.get().step(rel, None)
        else:
            raise TypeError(rel)
        return self

    def __iadd__(self, abs: Union[int, tuple[Any, int]]) -> "Progress":
        """加等于运算符 += 用于跳到进度

        ```
        PROGRESS += 10  # 跳到 10
        PROGRESS += ("msg", 10)  # 跳到 10, 同时提示状态
        ```
        """

        if type(abs) is tuple:
            msg, abs = abs
            self.__cur.get().jump(abs, msg)
        elif type(abs) is int:
            self.__cur.get().jump(abs, None)
        else:
            raise TypeError(abs)
        return self

    class ForIter:

        def __init__(self, prog, seq: Iterable) -> None:
            self.prog = prog
            self.seq = seq

        def __iter__(self):
            self.it = self.seq.__iter__()
            del self.seq  # 尽早释放资源
            return self

        def __next__(self):
            i = self.it.__next__()
            self.prog._Progress__cur.get().step(1, None)
            return i

        def __aiter__(self):
            self.ait = self.seq.__aiter__()
            del self.seq
            return self

        async def __anext__(self):
            i = await self.ait.__anext__()
            self.prog._Progress__cur.get().step(1, None)
            return i

    @overload
    def __matmul__(self, seq: Iterable[_T]) -> Iterable[_T]: ...

    @overload
    def __matmul__(self, seq: AsyncIterable[_T]) -> AsyncIterable[_T]: ...

    def __matmul__(self, seq):
        """矩阵乘法运算符 @ 用于包装迭代器, 同时自动设置总量

        ```
        for i in PROGRESS @ range(100):
        ```

        :param seq: 可迭代对象
        :return: 迭代器
        """

        self.__cur.get().total(
            len(seq) if hasattr(seq, "__len__") else None,  # type: ignore
            f"@{type(seq).__name__}<{id(seq)}>",
        )
        return Progress.ForIter(self, seq)

    @overload
    def __mod__(self, seq: Iterable[_T]) -> Iterable[_T]: ...

    @overload
    def __mod__(self, seq: AsyncIterable[_T]) -> AsyncIterable[_T]: ...

    def __mod__(self, seq):
        """取模运算符 % 用于包装迭代器

        ```
        for i in PROGRESS % range(100):
        ```

        :param seq: 可迭代对象
        :return: 迭代器
        """

        return Progress.ForIter(self, seq)


class Watcher(ABC):
    """进度监控接口类"""

    _Watcher__prev: "Watcher"

    def __init__(self):
        self.__prev = self

    @abstractmethod
    def desc(self, msg: Optional[Any]) -> None:
        """描述当前状态

        :param msg: 提示消息, None 指代空消息
        """

    @abstractmethod
    def total(self, num: Union[int, EllipsisType, None], msg: Optional[Any]) -> None:
        """设置进度总量

        :param num: 进度总量, Ellipsis(...) 指代未知, None 取消设置总量
        :param msg: 提示消息, None 指代空消息
        """

    @abstractmethod
    def step(self, rel: int, msg: Optional[Any]) -> None:
        """将进度推进一个相对值 rel

        :param rel: 推进的相对值
        :param msg: 提示消息, None 指代空消息
        """

    @abstractmethod
    def jump(self, abs: int, msg: Optional[Any]) -> None:
        """将进度跳到一个绝对值 abs

        :param abs: 跳到的绝对值
        :param msg: 提示消息, None 指代空消息
        """

    @abstractmethod
    def enter(self, msg: Optional[Any]) -> None:
        """进入子过程

        :param msg: 提示消息, None 指代空消息
        """

    @abstractmethod
    def exit(self, exc: Optional[Exception]) -> None:
        """退出子过程

        :param exc: 如果因异常退出则为异常对象
        """

    @abstractmethod
    def pushed(self) -> None:
        """进度监视器被 push 时调用"""

    @abstractmethod
    def popped(self) -> None:
        """进度监视器被 pop 时调用"""


class Silence(Watcher):
    """什么都不做的静音类"""

    def desc(self, msg) -> None:
        pass

    def total(self, total, msg) -> None:
        pass

    def step(self, rel, msg) -> None:
        pass

    def jump(self, abs, msg) -> None:
        pass

    def enter(self, msg) -> None:
        pass

    def exit(self, exc) -> None:
        pass

    def pushed(self) -> None:
        pass

    def popped(self) -> None:
        pass


class PrintTree(Watcher):
    """使用 print() 打印进度树"""

    @dataclass
    class StackFrame:
        start_time: datetime
        last_time: datetime
        total: Union[int, EllipsisType] = ...
        current: Optional[int] = None

    def __init__(self, out=sys.stderr, flush=False) -> None:
        super().__init__()
        self.__out = out
        self.__flush = flush
        self.__stack = list[PrintTree.StackFrame]()

    __SIDE_BAR_WIDTH = 6

    @staticmethod
    def __time_tag(seconds: float) -> str:
        width = PrintTree.__SIDE_BAR_WIDTH

        s = f"{seconds:.2f}".rstrip("0").rstrip(".")
        if len(s) <= width:
            return s.rjust(width)

        minute = seconds / 60
        s = f"{minute:.2f}".rstrip("0").rstrip(".") + "m"
        if len(s) <= width:
            return s.rjust(width)

        hour = minute / 60
        s = f"{hour:.2f}".rstrip("0").rstrip(".") + "h"
        if len(s) <= width:
            return s.rjust(width)

        day = hour / 24
        s = f"{day:.2f}".rstrip("0").rstrip(".") + "d"
        if len(s) <= width:
            return s.rjust(width)

        return "*" * width

    def __indent(self) -> str:
        stack = self.__stack
        assert stack, self
        frame = stack[-1]

        now = datetime.now()
        time_tag = self.__time_tag((now - frame.last_time).total_seconds())
        frame.last_time = now

        level = len(stack)
        return time_tag + " |" * level

    def __indent_lines(self, text: str) -> Optional[str]:
        width = self.__SIDE_BAR_WIDTH

        lines = text.split("\n")
        if len(lines) == 1:
            return None
        prefix = "\n" + " " * width + " |" * len(self.__stack) + "  "
        return prefix + prefix.join(lines) + prefix  # 换行开头, 结尾无换行

    def __message(self, msg) -> str:
        stack = self.__stack
        assert stack, self
        frame = stack[-1]

        text = str("" if msg is None else msg)
        text = self.__indent_lines(text) or text

        first_line = self.__indent()
        if frame.current is None:
            first_line += "- "
        elif frame.total is ...:
            first_line += "-[" + str(frame.current) + "] "
        else:
            total = str(frame.total)
            current = str(frame.current).rjust(len(total), ",")
            first_line += f"-[{current}/{total}] "

        return first_line + text + "\n"

    def __print(self, txt: str) -> None:
        self.__out.write(txt)
        if self.__flush:
            self.__out.flush()

    def desc(self, msg) -> None:
        if not self.__stack:
            self.__print(f"{msg}\n")
            return
        self.__print(self.__message(msg))

    def total(self, total, msg) -> None:
        stack = self.__stack
        if not stack:
            self.__print(f"*{total}: {msg}\n")
            return
        frame = stack[-1]

        if total is None:
            frame.total = ...
            frame.current = None
        else:
            frame.total = total
            frame.current = 0
        self.desc(msg)

    def step(self, rel, msg) -> None:
        stack = self.__stack
        if not stack:
            self.__print(f"+{rel}: {msg}\n")
            return
        frame = stack[-1]

        frame.current = (frame.current or 0) + rel
        self.desc(msg)

    def jump(self, abs, msg) -> None:
        stack = self.__stack
        if not stack:
            self.__print(f"={abs}: {msg}\n")
            return
        frame = stack[-1]

        frame.current = abs
        self.desc(msg)

    def enter(self, msg) -> None:
        stack = self.__stack
        if not stack:
            self.__print("-" * (PrintTree.__SIDE_BAR_WIDTH + 2) + "\n")
        first_line = self.__indent() + "--- "

        now = datetime.now()
        frame = PrintTree.StackFrame(now, now)
        stack.append(frame)

        text = str(msg or "")
        text = self.__indent_lines(text) or text

        self.__print(first_line + text + "\n")

    def exit(self, exc) -> None:
        stack = self.__stack
        assert stack, self
        frame = stack.pop()

        level = len(stack)
        indent = " " * PrintTree.__SIDE_BAR_WIDTH + " |" * level
        time_cost = str(datetime.now() - frame.start_time)
        line = indent + " \\" + time_cost + "/"
        if exc is not None:
            line += " " + type(exc).__name__
        self.__print(line + "\n")

    def pushed(self) -> None:
        stack = self.__stack
        self.__print("-" * (PrintTree.__SIDE_BAR_WIDTH + 2) + "\n")
        now = datetime.now()
        stack.append(PrintTree.StackFrame(now, now))

    def popped(self) -> None:
        stack = self.__stack
        self.__print("-" * (PrintTree.__SIDE_BAR_WIDTH + 2) + "\n")
        stack.clear()


PROGRESS = Progress("PROGRESS._Progress__cur")
"""全局进度监视器, 面向用户的进度监视服务点"""


def __run__(
    __spec__, __file__, *, log_level=1, log_stderr=None, log_unilog=None
) -> tuple[Here, Here, logging.Logger, Progress]:
    """项目脚本的初始化引言

    :param __spec__: 传入 __spec__
    :param __file__: 传入 __file__
    :return ROOT: 根目录路径访问器
    :return HERE: 脚本的路径访问器
    :return LOGGER: 日志对象
    :return PROGRESS: 进度对象
    """
    sys.path = [os.path.realpath(i) for i in sys.path]
    cmdname = Path(__file__).name
    init_dirs(_logdir_suffix="_" + cmdname)
    flog = init_logging(cmdname, log_level, log_stderr, log_unilog)
    if flog:
        for _ in range(3):
            print(flog, file=sys.stderr, flush=True)
    return ROOT, here(__file__), module_logger(__spec__, __file__), PROGRESS


# ==================================================================================== #


@dataclass
class RunPs:
    cmd: list[str]
    "命令行"
    cwd: Optional[str] = None
    "工作目录, `None` 使用自动路径"
    env: dict[str, str] = field(default_factory=lambda: {})
    "补充的环境变量"
    in_: Union[str, Literal[False], None] = False
    "标准输入, `str` 重定向到文件；`False` 关闭；`None` 继承当前"
    out: Union[str, bool, None] = True
    "标准输出, `True` 使用自动路径, 其余同 in_"
    err: Union[str, bool, None] = True
    "标准错误输出, 同 out"
    exec: Optional[str] = None
    "可执行文件路径, 为 None 时使用 cmd 首项"
    envs: Optional[dict[str, str]] = None
    "完整的环境变量集, 默认使用 os.environ"

    class InOutErr(NamedTuple):
        in_: Union[BufferedReader, int, None]
        out: Union[BufferedWriter, int, None]
        err: Union[BufferedWriter, int, None]

        def __enter__(self) -> "RunPs.InOutErr":
            return self

        def __exit__(self, type, value, traceback) -> None:
            if isinstance(self.in_, BufferedReader):
                self.in_.close()
            if isinstance(self.out, BufferedWriter):
                self.out.close()
            if self.err is not self.out and isinstance(self.err, BufferedWriter):
                self.err.close()

    SILENCE = InOutErr(
        aio.subprocess.DEVNULL, aio.subprocess.DEVNULL, aio.subprocess.DEVNULL
    )

    class Failed(Exception):

        def __init__(self, code: int, runps: "RunPs"):
            super().__init__(code, runps)

        def __str__(self) -> str:
            return f"{self.args[0]}\n" + self.args[1].format()

    def prepare(self, autodir: str, stdout="stdout", stderr="stderr") -> InOutErr:
        """准备执行: 创建工作目录, 标准输出和标准错误输出文件

        :param autodir: 自动路径的目录, 会按需创建
        :param stdout: 标准输出文件名, 默认 "stdout"
        :param stderr: 标准错误输出文件名, 默认 "stderr"
        :return: 最终执行的模型, 以及打开好的标准输入输出组
        """

        if self.cwd is None:
            self.cwd = str(autodir)

        infile: Union[BufferedReader, int, None]
        infile_close: Callable = lambda: None
        if self.in_:
            infile = open(self.in_, "rb")
            infile_close = infile.close
        elif self.in_ is False:
            infile = aio.subprocess.DEVNULL
        else:
            infile = None

        outfile: Union[BufferedWriter, int, None]
        outfile_close: Callable = lambda: None
        if self.out:
            if self.out is True:
                self.out = f"{autodir}/{stdout}"
            os.makedirs(os.path.dirname(self.out), exist_ok=True)
            try:
                outfile = open(self.out, "wb")
                outfile_close = outfile.close
            except OSError:
                infile_close()
                raise
        elif self.out is False:
            outfile = aio.subprocess.DEVNULL
        else:
            outfile = None

        errfile: Union[BufferedWriter, int, None]
        if self.err == self.out:
            errfile = outfile
        elif self.err:
            if self.err is True:
                self.err = f"{autodir}/{stderr}"
            os.makedirs(os.path.dirname(self.err), exist_ok=True)
            try:
                errfile = open(self.err, "wb")
            except OSError:
                infile_close()
                outfile_close()
                raise
        elif self.err is False:
            errfile = aio.subprocess.DEVNULL
        else:
            errfile = None

        return RunPs.InOutErr(infile, outfile, errfile)

    def run(self, ioe: InOutErr, **kwargs) -> subp.CompletedProcess:
        """使用 subprocess.run 运行, self 必须已经准备好

        :param ioe: self.prepare 的返回结果
        :param kwargs: 传递给 run 的其它参数
        """

        if self.cwd:
            os.makedirs(self.cwd, exist_ok=True)
        if self.envs is None:
            envs = dict(os.environ.items())
        else:
            envs = self.envs.copy()
        envs.update(self.env)
        with ioe as (stdin, stdout, stderr):
            return subp.run(
                self.cmd,
                cwd=self.cwd,
                env=envs,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
                executable=self.exec,
                **kwargs,
            )

    def popen(self, ioe: InOutErr, **kwargs) -> subp.Popen:
        """使用 subprocess.Popen 启动子进程, self 必须已经准备好

        :param ioe: self.prepare 的返回结果
        :param kwargs: 传递给 Popen 的其它参数
        """

        if self.cwd:
            os.makedirs(self.cwd, exist_ok=True)
        if self.envs is None:
            envs = dict(os.environ.items())
        else:
            envs = self.envs.copy()
        envs.update(self.env)
        with ioe as (stdin, stdout, stderr):
            return subp.Popen(
                self.cmd,
                cwd=self.cwd,
                env=self.envs,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
                executable=self.exec,
                **kwargs,
            )

    async def start(self, ioe: InOutErr, **kwargs) -> aio.subprocess.Process:
        """使用 aio.create_subprocess_exec 启动子进程, self 必须已经准备好

        :param ioe: self.prepare 的返回结果
        :param kwargs: 传递给 create_subprocess_exec 的其它参数
        """

        if self.cwd:
            os.makedirs(self.cwd, exist_ok=True)
        if self.envs is None:
            envs = dict(os.environ.items())
        else:
            envs = self.envs.copy()
        envs.update(self.env)
        with ioe as (stdin, stdout, stderr):
            return await aio.create_subprocess_exec(
                *self.cmd,
                cwd=self.cwd,
                env=envs,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
                **kwargs,
            )

    def format(self) -> str:
        """格式化为可读文本"""

        sio = StringIO()
        sio.write(f"CMD: {shlex.join(self.cmd)}")
        if self.cwd:
            sio.write(f"\nCWD: {shlex.quote(self.cwd)}")
        if self.env:
            sio.write(f"\nENV: {self.env}")
        if self.in_:
            sio.write(f"\nIN_: {shlex.quote(self.in_)}")
        if self.out:
            msg = shlex.quote(self.out) if isinstance(self.out, str) else str(self.out)
            sio.write(f"\nOUT: {msg}")
        if self.err:
            msg = shlex.quote(self.err) if isinstance(self.err, str) else str(self.err)
            sio.write(f"\nERR: {msg}")
        if self.exec:
            sio.write(f"\nEXEC: {shlex.quote(self.exec)}")
        if self.envs is not None:
            sio.write(f"\nENVS: {self.envs}")
        return sio.getvalue()

    def format_bash(self) -> str:
        """格式化为 bash 命令行"""

        sio = StringIO()
        if self.cwd:
            sio.write(f"cd {shlex.quote(self.cwd)} && ")
        envs = self.envs.copy() if self.envs is not None else {}
        envs.update(self.env)
        for k, v in envs.items():
            sio.write(f"{k}={shlex.quote(v)} ")
        if self.exec:
            sio.write(f"exec -a {shlex.quote(self.cmd[0])} ")
            sio.write(f"{self.exec} ")
            sio.write(shlex.join(self.cmd[1:]))
        else:
            sio.write(shlex.join(self.cmd))
        if self.in_:
            sio.write(f" < {shlex.quote(self.in_)}")
        if self.out is True:
            self.out = "stdout"
        if type(self.out) is str:
            sio.write(f" > {shlex.quote(self.out)}")
        if self.err is True:
            self.err = "stderr"
        if type(self.err) is str:
            sio.write(f" 2> {shlex.quote(self.err)}")
        return sio.getvalue()


def runps(
    *cmd: str | Path,
    cwd: str | Path | None = None,
    env: dict[str, Any] = {},
    stdin: str | Path | None = None,
    stdout: str | Path | None = None,
    stderr: str | Path | None = None,
    envs: dict[str, Any] | None = None,
    exec: str | Path | None = None,
    level=logging.INFO,
    check: int | None = None,
    logger=LOGGER,
    logstlv=2,
    timeout: float | None = None,
) -> tuple[Path, Path, timedelta, int]:
    """运行子进程, 同时记录日志

    :param cmd: 命令行
    :param cwd: 工作目录, 默认使用自动路径
    :param env: 额外的环境变量
    :param stdin: 标准输入重定向, None 使用 /dev/null
    :param stdout: 标准输出重定向, None 时使用自动路径
    :param stderr: 标准错误输出重定向, None 时使用自动路径
    :param envs: 完整的环境变量, 默认使用 os.environ
    :param exec: 可执行文件路径, 为 None 时使用 cmd 首项
    :param level: 日志级别, 默认 INFO
    :param check: 检查返回码为指定的值, 默认不检查
    :param logger: 日志器
    :param logstlv: 日志栈层级
    :param timeout: 超时时限, 单位（秒）
    :return out: 标准输出文件路径
    :return err: 标准错误文件路径
    :return time: 运行时间
    :return exit: 返回码
    """

    runps = RunPs(
        cmd=list(map(str, cmd)),
        cwd=str(cwd) if cwd else None,
        env={k: str(v) for k, v in env.items()},
        in_=False if stdin is None else str(stdin),
        out=True if stdout is None else str(stdout),
        err=True if stderr is None else str(stderr),
        exec=None if exec is None else str(exec),
        envs=None if envs is None else {k: str(v) for k, v in envs.items()},
    )
    inouterr = runps.prepare(str(HERE.logging(uni=True)))
    run_info = PROGRESS(runps.format())

    timing = datetime.now()
    if v := os.getenv(PROJVER_KEY + "_DRY_RUNPS"):
        ps = None
        returncode = int(v)
    else:
        ps = runps.run(inouterr, timeout=timeout)
        returncode = ps.returncode
    time_cost = datetime.now() - timing

    logger.log(
        level, "%s (%s)\n%s", returncode, time_cost, run_info, stacklevel=logstlv
    )
    if check is not None and check != returncode:
        raise RunPs.Failed(returncode, runps)
    return Path(runps.out), Path(runps.err), time_cost, returncode  # type: ignore


async def runps_async(
    *cmd: str | Path,
    cwd: str | Path | None = None,
    env: dict[str, Any] = {},
    stdin: str | Path | None = None,
    stdout: str | Path | None = None,
    stderr: str | Path | None = None,
    envs: dict[str, Any] | None = None,
    exec: str | Path | None = None,
    level=logging.INFO,
    check: int | None = None,
    logger=LOGGER,
    logstlv=2,
    timeout: float | None = None,
) -> tuple[Path, Path, timedelta, int]:
    """异步运行子进程, 同时记录日志

    :param cmd: 命令行
    :param cwd: 工作目录, 默认使用自动路径, 不存在时创建
    :param env: 额外的环境变量
    :param stdin: 标准输入重定向, None 使用 /dev/null
    :param stdout: 标准输出重定向, None 时使用自动路径
    :param stderr: 标准错误输出重定向, None 时使用自动路径
    :param envs: 完整的环境变量, 默认使用 os.environ
    :param exec: 可执行文件路径, 为 None 时使用 cmd 首项
    :param level: 日志级别, 默认 INFO
    :param check: 检查返回码为指定的值, 默认不检查
    :param logger: 日志器
    :param logstlv: 日志栈层级
    :param timeout: 超时时限, 单位（秒）
    :return out: 标准输出文件路径
    :return err: 标准错误文件路径
    :return time: 运行时间
    :return exit: 返回码
    """

    runps = RunPs(
        cmd=list(map(str, cmd)),
        cwd=str(cwd) if cwd else None,
        env={k: str(v) for k, v in env.items()},
        in_=False if stdin is None else str(stdin),
        out=True if stdout is None else str(stdout),
        err=True if stderr is None else str(stderr),
        exec=None if exec is None else str(exec),
        envs=None if envs is None else {k: str(v) for k, v in envs.items()},
    )
    inouterr = runps.prepare(str(HERE.logging(uni=True)))
    run_info = PROGRESS(runps.format())  # TODO 打印源文件行号

    timing = datetime.now()
    if v := os.getenv(PROJVER_KEY + "_DRY_RUNPS"):
        ps = None
        returncode = int(v)
    else:
        ps = await runps.start(inouterr)
        try:
            returncode = await aio.wait_for(ps.wait(), timeout)
        except TimeoutError:
            ps.kill()
            returncode = -1
    time_cost = datetime.now() - timing

    logger.log(
        level, "%s (%s)\n%s", returncode, time_cost, run_info, stacklevel=logstlv
    )
    if check is not None and check != returncode:
        raise RunPs.Failed(returncode, runps)
    return Path(runps.out), Path(runps.err), time_cost, returncode  # type: ignore


async def runpy(func: Callable[..., _T], *args, **kwargs) -> _T:
    ps = await aio.create_subprocess_exec(
        sys.executable,
        "-c",
        f"""import sys
sys.path = {sys.path!r}"""
        """
import pickle
data = sys.stdin.buffer.read()
func, args, kwargs = pickle.loads(data)
try:
    ret = (True, func(*args, **kwargs))
except Exception as e:
    ret = (False, f"{type(e)}: {e}")
sys.stdout.buffer.write(pickle.dumps(ret))
sys.stdout.buffer.flush()
""",
        stdin=aio.subprocess.PIPE,
        stdout=aio.subprocess.PIPE,
    )
    input = pickle.dumps((func, args, kwargs))
    stdout, _ = await ps.communicate(input)
    ok, ret = pickle.loads(stdout)
    if not ok:
        raise RuntimeError(ret)
    return ret


async def runpy_async(func: Callable[..., _T], *args, **kwargs) -> _T:
    ps = await aio.create_subprocess_exec(
        sys.executable,
        "-c",
        f"""import sys
sys.path = {sys.path!r}"""
        """
import asyncio
import pickle
data = sys.stdin.buffer.read()
func, args, kwargs = pickle.loads(data)
try:
    ret = (True, asyncio.run(func(*args, **kwargs)))
except Exception as e:
    ret = (False, f"{type(e)}: {e}")
sys.stdout.buffer.write(pickle.dumps(ret))
sys.stdout.buffer.flush()
""",
        stdin=aio.subprocess.PIPE,
        stdout=aio.subprocess.PIPE,
    )
    input = pickle.dumps((func, args, kwargs))
    stdout, _ = await ps.communicate(input)
    ok, ret = pickle.loads(stdout)
    if not ok:
        raise RuntimeError(ret)
    return ret


# ==================================================================================== #
class ConfigurationMeta(type):

    def __new__(cls, name, bases, attrs, **kwargs):
        # 在子类继承时自动拷贝父类的所有成员变量, 确保子类修改成员不会影响父类
        memo = {}
        for base in bases:
            if not isinstance(base, ConfigurationMeta):
                continue
            for k, v in base.__dict__.items():
                # 忽略以下划线开头的成员和 attrs 中已经覆盖的成员
                if k.startswith("_") or k in attrs:
                    continue

                # copy.deepcopy 不会拷贝类, 使用我们定义的拷贝方法
                if isinstance(v, ConfigurationMeta):
                    attrs[k] = v.__deepcopy__(memo)
                else:
                    try:
                        attrs[k] = deepcopy(v, memo)
                    except TypeError:
                        # 方法, 类方法等, 如果不可拷贝就算了
                        pass

        new_cls = super().__new__(cls, name, bases, attrs)

        # 子类可以通过下划线关键字参数注册到所属类, 而不必再写另外的赋值语句
        if (nest := kwargs.pop("_", None)) is not None:
            assert isinstance(nest, ConfigurationMeta), nest
            setattr(nest, name, new_cls)

        # 如果子类有异步配置项, 则重载 __getattribute__ 使在调用 __ainit__
        # 前访问其上的配置项时报错.
        ainits = set[str]()
        for k, v in attrs.items():
            if k.startswith("_"):
                continue
            if isinstance(v, ConfigurationMeta.__AINIT__):
                ainits.add(k)
        if ainits:
            setattr(new_cls, "__getattribute__", ConfigurationMeta.__ainit_getattr__)

        return new_cls

    def __copy__(cls) -> "ConfigurationMeta":
        return ConfigurationMeta(
            cls.__name__,
            cls.__bases__,
            {k: v for k, v in cls.__dict__.items() if not k.startswith("_")},
        )

    def __deepcopy__(cls, memo: dict | None = None) -> "ConfigurationMeta":
        if memo is None:
            memo = {}
        if v := memo.get(cls):
            return v
        new_cls = memo[v] = ConfigurationMeta(cls.__name__, cls.__bases__, {})
        for k, v in cls.__dict__.items():
            if k.startswith("_"):
                continue
            if isinstance(v, ConfigurationMeta):
                setattr(new_cls, k, v.__deepcopy__(memo))
            else:
                try:
                    new_attr = deepcopy(v, memo)
                except TypeError:
                    new_attr = v
                setattr(new_cls, k, new_attr)
        return new_cls

    def __overlay__(cls, other: "ConfigurationMeta") -> "ConfigurationMeta":
        assert isinstance(other, ConfigurationMeta), other
        for k, v in cls.__dict__.items():
            if k.startswith("_") or not hasattr(other, k):
                continue
            attr = getattr(other, k)
            if isinstance(v, ConfigurationMeta):
                v.__overlay__(attr)
            else:
                setattr(cls, k, attr)
        return cls  # type: ignore

    class __AINIT__:

        def __init__(self, val: Coroutine):
            # super().__init__("异步配置项需要调用 ainit 方法以初始化")
            self.__val = val

        def __del__(self):
            self.__val.close()

    def __rmatmul__(cls, coro: Coroutine[_T, None, None]) -> _T:
        return ConfigurationMeta.__AINIT__(coro)  # type: ignore

    async def __ainit__(cls) -> "ConfigurationMeta":
        if cls.__getattribute__ is not ConfigurationMeta.__ainit_getattr__:
            return cls  # type: ignore
        delattr(cls, "__getattribute__")

        async with aio.TaskGroup() as tg:
            for k, v in cls.__dict__.items():
                if not k.startswith("_") and isinstance(v, ConfigurationMeta.__AINIT__):
                    tg.create_task(ConfigurationMeta.__ainit_setattr__(cls, k))
                elif (
                    isinstance(v, ConfigurationMeta)
                    and v.__getattribute__ is not ConfigurationMeta.__ainit_getattr__
                ):
                    tg.create_task(v.__ainit__())

        return cls  # type: ignore

    @staticmethod
    def __ainit_getattr__(cls, name):
        attr = super(cls).__getattribute__(name)
        if not name.startswith("_") and isinstance(attr, ConfigurationMeta.__AINIT__):
            raise AttributeError(f"异步配置项 {cls.__name__}.{name} 未初始化")
        return attr

    @staticmethod
    async def __ainit_setattr__(cls, name):
        setattr(cls, name, await getattr(cls, name))

    def __markdown__(cls, tio: TextIOBase) -> "ConfigurationMeta":
        from pprint import pformat
        from textwrap import indent

        tio.write(f"# `{inspect.getfile(cls)}`: {cls.__qualname__}\n")

        bfs = [(cls, cls.__qualname__)]
        bfs_next = list[tuple[ConfigurationMeta, str]]()
        level = 2
        walked = dict[ConfigurationMeta, str]()

        while bfs:
            for conf, path in bfs:
                if old_path := walked.get(conf):
                    tio.write(f"{'#' * level} [{path}](#{old_path})\n")
                    continue
                walked[conf] = path

                tio.write(f"\n{'#' * level} <a id=\"{path}\">{path}</a>\n\n")
                for k, v in sorted(
                    filter(lambda x: not x[0].startswith("_"), conf.__dict__.items())
                ):
                    if isinstance(v, ConfigurationMeta):
                        next_path = path + "." + k
                        if (link_to := walked.get(v)) is None:
                            link_to = next_path
                            bfs_next.append((v, next_path))
                        tio.write(f"- *`{k}`* [`{link_to}`](#{link_to})\n")
                    else:
                        s = pformat(v)
                        if len(k) + len(s) < 60 and "\n" not in s:
                            tio.write(f"- **`{k}`** - `{v}`\n")
                        else:
                            tio.write(
                                f"- **`{k}`**\n\n  ```\n{indent(s, "  ")}\n  ```\n"
                            )
            bfs, bfs_next = bfs_next, []
            level += 1

        tio.write("\n")
        return cls  # type: ignore


class Configuration(metaclass=ConfigurationMeta):
    """基于 Python 类的配置定义框架

    1.  使用 Python 的类语法来定义配置项

        ```python
        class CONFIG(Configuration):
            log_level:int = 1
        ```

        设计上所有配置项都不得以下划线开头, 这些名字被保留,
        可以用于临时变量等用途.

    2.  支持配置类的 (深) 复制和覆盖

        ```python
        class CONFIG(Configuration): ...

        a = CONFIG.__copy__()  # 浅复制
        b = CONFIG.__deepcopy__()  # 深复制

        class NEW_CONFIG(CONFIG): ...
        a.__overlay__(NEW_CONFIG)  # 覆盖, 递归更新 a 中的同名配置
        ```

        配置类被继承时, 子类会深复制父类的所有配置项,
        从而避免对子配置类的修改错误地影响到父配置类.

    3.  嵌套地定义子配置模块, 也可将其外联

        ```python
        class CONFIG(Configuration):

            class NESTED_1(Configuration):
                ...

            NESTED_2: "NESTED_2"  # 类型注解非必要, 但建议使用以便静态代码分析

        class NESTED_2(Configuration, _=CONFIG):
            NESTED_1 = CONFIG.NESTED_1  # 允许配置类在多处被引用以及循环引用
        ```

    4.  使用异步协程的返回值作为配置项

        用 `coro() @ Configuration` 的写法来声明配置项:

        ```python
        async def _foo() -> int: ...

        class CONFIG(Configuration):
            x:int = _foo() @ Configuration
        ```

        然后再用 `await CONFIG.__ainit__()` 来递归初始化所有异步配置项.
        在未初始化前访问类中的异步配置项会引发 `AttributeError` 异常.

    5.  打印成 Markdown 格式以便于查看配置内容

        ```python
        CONFIG.__markdown__(sys.stdout)
        ```
    """


class Poision:
    """毒值类"""

    def __init__(self, msg: str) -> None:
        self.__msg = msg

    def __raise(self, *args, **kwargs):
        raise RuntimeError(self.__msg)

    __dir__ = __hash__ = __raise
    __getattr__ = __delattr__ = __raise
    __eq__ = __ne__ = __lt__ = __le__ = __gt__ = __ge__ = __raise
    __format__ = __str__ = __repr__ = __raise


class Cached(Exception):

    def __init__(self, path: str | Path, temp: bool | None = False) -> None:
        """
        :param path: 目标缓存路径
        :param temp: 不为 None 时使用临时路径, True 时创建临时目录
        """
        from filelock import AsyncFileLock

        super().__init__()
        self.path = Path(path)
        self.lock = AsyncFileLock(str(self.path) + ".lock")
        self.temp = temp

    def __call__(self, enable=True) -> Path:
        if enable and self.path.exists():
            raise self

        parent = self.path.parent
        parent.mkdir(parents=True, exist_ok=True)
        if self.temp is True:
            self._temp = tempfile.mkdtemp(None, self.path.name + ".", dir=parent)
        elif self.temp is False:
            self._temp = tempfile.mktemp("", self.path.name + ".", dir=parent)
        else:
            return self.path
        return Path(self._temp)

    async def __aenter__(self) -> "Cached":
        await self.lock.acquire()
        return self

    async def __aexit__(self, exc_type, exc_value, traceback):
        from shutil import rmtree

        try:
            await self.lock.release()
            Path(self.lock.lock_file).unlink(missing_ok=True)
            if exc_value is self:
                return True
            if self.temp is not None and exc_value is None:
                rmtree(self.path, ignore_errors=True)
                Path(self._temp).rename(self.path)
                del self._temp
                return
        finally:
            if hasattr(self, "_temp"):
                try:
                    Path(self._temp).unlink(missing_ok=True)
                except IsADirectoryError:
                    rmtree(self._temp, ignore_errors=True)
