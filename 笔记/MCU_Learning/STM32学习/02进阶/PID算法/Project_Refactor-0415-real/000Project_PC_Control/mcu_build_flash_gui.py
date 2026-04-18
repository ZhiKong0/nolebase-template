from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import threading
import traceback
from dataclasses import dataclass
from pathlib import Path
from queue import Empty, Queue
from tkinter import filedialog, messagebox, scrolledtext
import tkinter as tk
from tkinter import ttk
from typing import Callable, Optional, Sequence

try:
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - runtime fallback
    list_ports = None


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_PROJECT_ROOT = SCRIPT_DIR.parent
RUNTIME_DIR = Path(os.environ.get("LOCALAPPDATA", str(SCRIPT_DIR))) / "MCUBuildFlashGUI"
STATE_PATH = RUNTIME_DIR / "mcu_build_flash_gui.state.json"

METHOD_STLINK = "stlink"
METHOD_PYOCD = "pyocd"
METHOD_STCGAL = "stcgal"

METHOD_OPTIONS = [
    (METHOD_STLINK, "STM32CubeProgrammer / ST-LINK"),
    (METHOD_PYOCD, "pyOCD / DAPLink"),
    (METHOD_STCGAL, "stcgal / 串口"),
]
METHOD_LABELS = dict(METHOD_OPTIONS)

CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


@dataclass
class BuildArtifacts:
    project_root: Path
    project_file: Path
    objects_dir: Path
    build_log: Path
    raw_log: str
    hex_path: Optional[Path]
    axf_path: Optional[Path]
    source_latest: Optional[Path]
    hex_created: bool


def safe_read_text(path: Path) -> str:
    for encoding in ("utf-8", "gb18030", "cp936", "latin-1"):
        try:
            return path.read_text(encoding=encoding)
        except UnicodeDecodeError:
            continue
    return path.read_text(encoding="utf-8", errors="replace")


def find_first_existing(paths: Sequence[str]) -> str:
    for candidate in paths:
        if candidate and Path(candidate).exists():
            return candidate
    return ""


def find_executable(name: str) -> str:
    hits = os.environ.get("PATH", "").split(os.pathsep)
    suffixes = [""] if name.lower().endswith(".exe") else [".exe", ""]
    for folder in hits:
        if not folder:
            continue
        for suffix in suffixes:
            candidate = Path(folder) / f"{name}{suffix}"
            if candidate.exists():
                return str(candidate)
    return ""


def default_settings() -> dict[str, str]:
    python_home = Path(sys.executable).resolve().parent
    script_bin = python_home / "Scripts"
    project_root = DEFAULT_PROJECT_ROOT
    project_file = project_root / "project.uvprojx"
    if not project_file.exists():
        matches = sorted(project_root.glob("*.uvprojx"))
        if matches:
            project_file = matches[0]

    return {
        "project_root": str(project_root),
        "project_file": str(project_file),
        "target_name": "Target 1",
        "uv4_path": find_first_existing(
            [
                r"D:\keil\Keil-v5\Arm\UV4\UV4.exe",
                r"D:\Keil_v5\UV4\UV4.exe",
                find_executable("UV4"),
            ]
        ),
        "cube_cli_path": find_first_existing(
            [
                r"E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe",
                r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
                find_executable("STM32_Programmer_CLI"),
            ]
        ),
        "pyocd_path": find_first_existing(
            [
                str(script_bin / "pyocd.exe"),
                str(script_bin / "pyocd"),
                find_executable("pyocd"),
            ]
        ),
        "stcgal_path": find_first_existing(
            [
                str(script_bin / "stcgal.exe"),
                str(script_bin / "stcgal"),
                find_executable("stcgal"),
            ]
        ),
        "flash_method": METHOD_STLINK,
        "selected_port": "",
        "cube_freq": "4000",
        "cube_mode": "UR",
        "cube_reset": "HWrst",
        "pyocd_target": "stm32f103rc",
        "pyocd_freq": "100000",
        "pyocd_probe_uid": "",
        "pyocd_retries": "3",
        "stc_chip": "stc15",
        "stc_retries": "5",
    }


def load_settings() -> dict[str, str]:
    settings = default_settings()
    if STATE_PATH.exists():
        try:
            cached = json.loads(STATE_PATH.read_text(encoding="utf-8"))
            if isinstance(cached, dict):
                settings.update({k: str(v) for k, v in cached.items()})
        except (OSError, json.JSONDecodeError):
            pass
    return settings


def save_settings(settings: dict[str, str]) -> None:
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    STATE_PATH.write_text(
        json.dumps(settings, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def list_serial_port_choices() -> tuple[list[str], Optional[str]]:
    if list_ports is None:
        return [], None

    choices: list[str] = []
    preferred: Optional[str] = None
    ports = sorted(list(list_ports.comports()), key=lambda item: item.device)
    for port in ports:
        description = port.description or "未知设备"
        label = f"{port.device} | {description}"
        choices.append(label)
        upper_desc = description.upper()
        if preferred is None and ("CH340" in upper_desc or "USB-SERIAL" in upper_desc):
            preferred = label
    return choices, preferred


def port_label_to_name(label: str) -> str:
    return label.split("|", 1)[0].strip()


def method_display_value(method: str) -> str:
    return f"{method} | {METHOD_LABELS.get(method, method)}"


def method_from_display(value: str) -> str:
    return value.split("|", 1)[0].strip()


def quote_command(args: Sequence[str]) -> str:
    return subprocess.list2cmdline(list(args))


def run_command(
    args: Sequence[str],
    *,
    cwd: Path,
    logger: Callable[[str], None],
) -> tuple[int, str]:
    logger("")
    logger(f"$ {quote_command(args)}")
    try:
        process = subprocess.Popen(
            list(args),
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=CREATE_NO_WINDOW,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(f"命令不存在: {args[0]}") from exc

    output_lines: list[str] = []
    assert process.stdout is not None
    for raw_line in process.stdout:
        line = raw_line.rstrip()
        output_lines.append(line)
        logger(line)
    process.wait()
    logger(f"[exit={process.returncode}]")
    return process.returncode, "\n".join(output_lines)


def ensure_path_exists(path_str: str, label: str) -> Path:
    if not path_str.strip():
        raise RuntimeError(f"{label} 未设置")
    path = Path(path_str).expanduser()
    if not path.exists():
        raise RuntimeError(f"{label} 不存在: {path}")
    return path


def discover_project_file(project_root: Path, configured: str) -> Path:
    configured_path = Path(configured.strip())
    if configured_path.is_absolute() and configured_path.exists():
        return configured_path
    if configured_path and (project_root / configured_path).exists():
        return project_root / configured_path
    default_file = project_root / "project.uvprojx"
    if default_file.exists():
        return default_file
    uvprojx = sorted(project_root.glob("*.uvprojx"))
    if uvprojx:
        return uvprojx[0]
    uvproj = sorted(project_root.glob("*.uvproj"))
    if uvproj:
        return uvproj[0]
    raise RuntimeError(f"未找到 Keil 工程文件: {project_root}")


def newest_source_file(project_root: Path) -> Optional[Path]:
    candidates: list[Path] = []
    for folder_name in ("User", "Hardware", "System"):
        folder = project_root / folder_name
        if not folder.exists():
            continue
        candidates.extend(folder.rglob("*.c"))
        candidates.extend(folder.rglob("*.h"))
    if not candidates:
        return None
    return max(candidates, key=lambda item: item.stat().st_mtime)


def locate_output(objects_dir: Path, names: Sequence[str], patterns: Sequence[str]) -> Optional[Path]:
    for name in names:
        candidate = objects_dir / name
        if candidate.exists():
            return candidate
    for pattern in patterns:
        matches = sorted(objects_dir.glob(pattern))
        if matches:
            return matches[0]
    return None


def ensure_newer_than_source(artifact: Optional[Path], source: Optional[Path], label: str) -> None:
    if artifact is None or not artifact.exists():
        raise RuntimeError(f"{label} 不存在")
    if source is None:
        return
    if artifact.stat().st_mtime <= source.stat().st_mtime:
        raise RuntimeError(
            f"{label} 不是最新文件，请先重新编译: {artifact.name} <= {source.name}"
        )


def parse_first_probe_uid(text: str) -> Optional[str]:
    patterns = [
        re.compile(r"(?m)^\s*0\s+.+?\s+([0-9A-Za-z:_-]{6,})\s+\S+\s*$"),
        re.compile(r"(?m)^\s*\d+\s+.+?\s+([0-9A-Za-z:_-]{6,})\s+\S+\s*$"),
    ]
    for pattern in patterns:
        match = pattern.search(text)
        if match:
            return match.group(1)
    return None


def build_project(settings: dict[str, str], logger: Callable[[str], None]) -> BuildArtifacts:
    project_root = ensure_path_exists(settings["project_root"], "工程根目录")
    project_file = discover_project_file(project_root, settings["project_file"])
    uv4_path = ensure_path_exists(settings["uv4_path"], "Keil UV4 路径")
    objects_dir = project_root / "Objects"
    objects_dir.mkdir(exist_ok=True)
    build_log = objects_dir / "project.build_log.htm"

    logger(f"工程目录: {project_root}")
    logger(f"工程文件: {project_file}")
    logger(f"Target: {settings['target_name']}")

    run_command(
        [
            str(uv4_path),
            "-b",
            str(project_file),
            "-j0",
            "-t",
            settings["target_name"],
            "-o",
            str(build_log),
        ],
        cwd=project_root,
        logger=logger,
    )

    if not build_log.exists():
        raise RuntimeError(f"未生成 build log: {build_log}")

    raw_log = safe_read_text(build_log)
    if "0 Error(s)" not in raw_log:
        raise RuntimeError("编译失败: build log 中未检测到 0 Error(s)")

    lower_log = raw_log.lower()
    hex_created = "creating hex file" in lower_log
    hex_path = locate_output(objects_dir, ["project.hex"], ["*.hex"])
    axf_path = locate_output(objects_dir, ["project.axf", "project.elf"], ["*.axf", "*.elf"])
    source_latest = newest_source_file(project_root)

    if source_latest is not None:
        logger(f"最新源码: {source_latest.name}")
    if hex_path is not None and hex_path.exists():
        logger(f"HEX: {hex_path.name}")
    if axf_path is not None and axf_path.exists():
        logger(f"AXF/ELF: {axf_path.name}")

    if hex_created and hex_path is not None:
        ensure_newer_than_source(hex_path, source_latest, "HEX")
        logger("HEX 已更新。")
    elif hex_path is not None:
        logger("编译成功，但 build log 未显式报告 creating hex file。")

    return BuildArtifacts(
        project_root=project_root,
        project_file=project_file,
        objects_dir=objects_dir,
        build_log=build_log,
        raw_log=raw_log,
        hex_path=hex_path,
        axf_path=axf_path,
        source_latest=source_latest,
        hex_created=hex_created,
    )


def inspect_existing_artifacts(settings: dict[str, str], logger: Callable[[str], None]) -> BuildArtifacts:
    project_root = ensure_path_exists(settings["project_root"], "工程根目录")
    project_file = discover_project_file(project_root, settings["project_file"])
    objects_dir = project_root / "Objects"
    build_log = objects_dir / "project.build_log.htm"
    raw_log = safe_read_text(build_log) if build_log.exists() else ""
    hex_created = "creating hex file" in raw_log.lower()
    hex_path = locate_output(objects_dir, ["project.hex"], ["*.hex"])
    axf_path = locate_output(objects_dir, ["project.axf", "project.elf"], ["*.axf", "*.elf"])
    source_latest = newest_source_file(project_root)
    logger("跳过编译，直接检查现有产物。")
    return BuildArtifacts(
        project_root=project_root,
        project_file=project_file,
        objects_dir=objects_dir,
        build_log=build_log,
        raw_log=raw_log,
        hex_path=hex_path,
        axf_path=axf_path,
        source_latest=source_latest,
        hex_created=hex_created,
    )


def choose_pyocd_images(artifacts: BuildArtifacts, logger: Callable[[str], None]) -> list[Path]:
    images: list[Path] = []
    if artifacts.hex_path is not None and artifacts.hex_path.exists():
        ensure_newer_than_source(artifacts.hex_path, artifacts.source_latest, "HEX")
        images.append(artifacts.hex_path)
    if artifacts.axf_path is not None and artifacts.axf_path.exists():
        ensure_newer_than_source(artifacts.axf_path, artifacts.source_latest, "AXF/ELF")
        images.append(artifacts.axf_path)
    if not images:
        raise RuntimeError("未找到可用于 pyOCD 的 HEX 或 AXF/ELF 文件")
    logger("候选镜像: " + ", ".join(path.name for path in images))
    return images


def flash_stlink(
    settings: dict[str, str],
    artifacts: BuildArtifacts,
    logger: Callable[[str], None],
) -> None:
    cube_path = ensure_path_exists(settings["cube_cli_path"], "STM32CubeProgrammer CLI 路径")
    ensure_newer_than_source(artifacts.hex_path, artifacts.source_latest, "HEX")
    assert artifacts.hex_path is not None
    logger("使用 STM32CubeProgrammer + ST-LINK 烧录。")
    exit_code, _ = run_command(
        [
            str(cube_path),
            "-c",
            "port=SWD",
            f"freq={settings['cube_freq']}",
            f"mode={settings['cube_mode']}",
            f"reset={settings['cube_reset']}",
            "-w",
            str(artifacts.hex_path),
            "-v",
            "-rst",
        ],
        cwd=artifacts.project_root,
        logger=logger,
    )
    if exit_code != 0:
        raise RuntimeError("ST-LINK 烧录失败")


def flash_pyocd(
    settings: dict[str, str],
    artifacts: BuildArtifacts,
    logger: Callable[[str], None],
) -> None:
    pyocd_path = ensure_path_exists(settings["pyocd_path"], "pyOCD 路径")
    target_name = settings["pyocd_target"].strip()
    if not target_name:
        raise RuntimeError("pyOCD 目标芯片名未设置")

    logger("使用 pyOCD + DAPLink 烧录。")
    probe_uid = settings["pyocd_probe_uid"].strip()
    list_code, probe_text = run_command(
        [str(pyocd_path), "list", "--probes"],
        cwd=artifacts.project_root,
        logger=logger,
    )
    if list_code != 0:
        raise RuntimeError("pyOCD 探头枚举失败")
    if not probe_uid:
        probe_uid = parse_first_probe_uid(probe_text) or ""
        if probe_uid:
            logger(f"自动选中 Probe UID: {probe_uid}")
        else:
            logger("未从 probes 输出中解析到 Unique ID，将直接使用默认探头。")

    images = choose_pyocd_images(artifacts, logger)
    retries = max(1, int(settings["pyocd_retries"]))
    extra_uid_args = ["-u", probe_uid] if probe_uid else []
    success = False

    for attempt in range(1, retries + 1):
        logger("")
        logger(f"pyOCD 尝试 {attempt}/{retries}")
        status_args = [
            str(pyocd_path),
            "commander",
            "--no-config",
            *extra_uid_args,
            "-t",
            target_name,
            "-c",
            "status",
            "-c",
            "exit",
        ]
        run_command(status_args, cwd=artifacts.project_root, logger=logger)

        for image in images:
            erase_args = [
                str(pyocd_path),
                "erase",
                "--chip",
                "--no-config",
                *extra_uid_args,
                "-t",
                target_name,
                "-M",
                "under-reset",
                "-f",
                settings["pyocd_freq"],
            ]
            erase_code, _ = run_command(erase_args, cwd=artifacts.project_root, logger=logger)
            if erase_code != 0:
                logger(f"整片擦除失败，跳过镜像 {image.name}")
                continue

            load_args = [
                str(pyocd_path),
                "load",
                "--no-config",
                *extra_uid_args,
                "-t",
                target_name,
                "-M",
                "under-reset",
                "-f",
                settings["pyocd_freq"],
                "-e",
                "sector",
                str(image),
            ]
            load_code, _ = run_command(load_args, cwd=artifacts.project_root, logger=logger)
            if load_code == 0:
                reset_args = [
                    str(pyocd_path),
                    "reset",
                    "--no-config",
                    *extra_uid_args,
                    "-t",
                    target_name,
                ]
                run_command(reset_args, cwd=artifacts.project_root, logger=logger)
                success = True
                break
        if success:
            break
        if attempt < retries:
            logger("本次失败，请给目标板断电后重新上电，再继续下一次尝试。")

    if not success:
        raise RuntimeError("pyOCD 烧录失败，已达到最大重试次数")


def flash_stcgal(
    settings: dict[str, str],
    artifacts: BuildArtifacts,
    logger: Callable[[str], None],
) -> None:
    stcgal_path = ensure_path_exists(settings["stcgal_path"], "stcgal 路径")
    ensure_newer_than_source(artifacts.hex_path, artifacts.source_latest, "HEX")
    assert artifacts.hex_path is not None

    selected_port = settings["selected_port"].strip()
    if not selected_port:
        raise RuntimeError("stcgal 烧录必须选择 COM 口")

    retries = max(1, int(settings["stc_retries"]))
    logger("使用 stcgal 串口烧录。")
    for attempt in range(1, retries + 1):
        logger("")
        logger(f"stcgal 尝试 {attempt}/{retries}")
        logger("请在命令启动后给目标板断电后重新上电。")
        exit_code, _ = run_command(
            [
                str(stcgal_path),
                "-P",
                settings["stc_chip"],
                "-p",
                selected_port,
                str(artifacts.hex_path),
            ],
            cwd=artifacts.project_root,
            logger=logger,
        )
        if exit_code == 0:
            return
    raise RuntimeError("stcgal 烧录失败，已达到最大重试次数")


class MCUBuildFlashApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("MCU 编译烧录工具")

        self.settings = load_settings()
        self.log_queue: Queue[tuple[str, str]] = Queue()
        self.worker_thread: Optional[threading.Thread] = None

        self.var_project_root = tk.StringVar(value=self.settings["project_root"])
        self.var_project_file = tk.StringVar(value=self.settings["project_file"])
        self.var_target_name = tk.StringVar(value=self.settings["target_name"])
        self.var_uv4_path = tk.StringVar(value=self.settings["uv4_path"])
        self.var_cube_cli = tk.StringVar(value=self.settings["cube_cli_path"])
        self.var_pyocd = tk.StringVar(value=self.settings["pyocd_path"])
        self.var_stcgal = tk.StringVar(value=self.settings["stcgal_path"])
        self.var_flash_method = tk.StringVar(value=method_display_value(self.settings["flash_method"]))
        self.var_selected_port = tk.StringVar(value=self.settings["selected_port"])
        self.var_cube_freq = tk.StringVar(value=self.settings["cube_freq"])
        self.var_cube_mode = tk.StringVar(value=self.settings["cube_mode"])
        self.var_cube_reset = tk.StringVar(value=self.settings["cube_reset"])
        self.var_pyocd_target = tk.StringVar(value=self.settings["pyocd_target"])
        self.var_pyocd_freq = tk.StringVar(value=self.settings["pyocd_freq"])
        self.var_pyocd_probe_uid = tk.StringVar(value=self.settings["pyocd_probe_uid"])
        self.var_pyocd_retries = tk.StringVar(value=self.settings["pyocd_retries"])
        self.var_stc_chip = tk.StringVar(value=self.settings["stc_chip"])
        self.var_stc_retries = tk.StringVar(value=self.settings["stc_retries"])
        self.var_method_hint = tk.StringVar()
        self.var_compact_summary = tk.StringVar()
        self.var_status = tk.StringVar(value="就绪")

        self.method_combo: Optional[ttk.Combobox] = None
        self.port_combo: Optional[ttk.Combobox] = None
        self.button_build_only: Optional[ttk.Button] = None
        self.button_flash_only: Optional[ttk.Button] = None
        self.button_build_flash: Optional[ttk.Button] = None
        self.button_toggle_log: Optional[ttk.Button] = None
        self.log_inline_text: Optional[scrolledtext.ScrolledText] = None
        self.log_frame: Optional[ttk.LabelFrame] = None
        self.advanced_popup: Optional[tk.Toplevel] = None
        self.stlink_frame: Optional[ttk.Frame] = None
        self.pyocd_frame: Optional[ttk.Frame] = None
        self.stc_frame: Optional[ttk.Frame] = None
        self.log_buffer: list[str] = []
        self.log_panel_visible = False
        self.default_window_width = 460
        self.default_window_height = 225
        self.expanded_window_height = 560
        self.var_log_button_text = tk.StringVar(value="显示日志")

        self._build_ui()
        self._bind_shortcuts()
        self.refresh_ports(initial=True)
        self.update_method_frame()
        self.update_compact_summary()
        self._fit_initial_window()
        self.root.after(120, self.process_log_queue)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_ui(self) -> None:
        style = ttk.Style()
        if "clam" in style.theme_names():
            style.theme_use("clam")
        palette_bg = "#f3f6fb"
        palette_card = "#ffffff"
        palette_border = "#d7dfeb"
        palette_text = "#162033"
        palette_muted = "#617187"
        palette_accent = "#2563eb"
        palette_accent_hover = "#1d4ed8"
        self.root.configure(bg=palette_bg)
        style.configure("App.TFrame", background=palette_bg)
        style.configure("Card.TFrame", background=palette_card)
        style.configure("Card.TLabelframe", background=palette_card, bordercolor=palette_border, relief="solid")
        style.configure("Card.TLabelframe.Label", background=palette_card, foreground=palette_text, font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Title.TLabel", background=palette_bg, foreground=palette_text, font=("Microsoft YaHei UI", 12, "bold"))
        style.configure("Sub.TLabel", background=palette_bg, foreground=palette_muted, font=("Microsoft YaHei UI", 9))
        style.configure("CardLabel.TLabel", background=palette_card, foreground=palette_text, font=("Microsoft YaHei UI", 9))
        style.configure("Hint.TLabel", background=palette_card, foreground=palette_muted, font=("Microsoft YaHei UI", 9))
        style.configure("Primary.TButton", background=palette_accent, foreground="#ffffff", padding=(12, 5), borderwidth=0)
        style.map("Primary.TButton", background=[("active", palette_accent_hover), ("pressed", palette_accent_hover)])
        style.configure("TButton", padding=(8, 4))

        container = ttk.Frame(self.root, padding=(8, 8, 8, 6), style="App.TFrame")
        container.pack(fill="both", expand=True)
        container.columnconfigure(0, weight=1)
        container.rowconfigure(3, weight=0)
        container.rowconfigure(4, weight=0)
        container.rowconfigure(5, weight=1)

        header = ttk.Frame(container, style="App.TFrame")
        header.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        ttk.Label(header, text="MCU 编译烧录工具", style="Title.TLabel").pack(anchor="w")

        summary_frame = ttk.LabelFrame(container, text="当前配置", padding=8, style="Card.TLabelframe")
        summary_frame.grid(row=1, column=0, sticky="ew")
        summary_frame.columnconfigure(0, weight=1)
        ttk.Label(
            summary_frame,
            textvariable=self.var_compact_summary,
            style="CardLabel.TLabel",
            wraplength=400,
            justify="left",
        ).grid(row=0, column=0, sticky="w")
        ttk.Label(
            summary_frame,
            text="工程目录、方式、COM 和参数都在“高级设置”里修改。快捷键: Alt+B 编译, Alt+N 烧录。",
            style="Hint.TLabel",
            wraplength=400,
        ).grid(row=1, column=0, sticky="w", pady=(6, 0))

        action_frame = ttk.LabelFrame(container, text="执行", padding=8, style="Card.TLabelframe")
        action_frame.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        top_buttons = ttk.Frame(action_frame, style="Card.TFrame")
        top_buttons.grid(row=0, column=0, sticky="w")
        self.button_build_flash = ttk.Button(top_buttons, text="编译并烧录", style="Primary.TButton", command=lambda: self.start_workflow(build=True, flash=True))
        self.button_build_flash.pack(side="left")
        self.button_build_only = ttk.Button(top_buttons, text="仅编译", command=lambda: self.start_workflow(build=True, flash=False))
        self.button_build_only.pack(side="left", padx=(8, 0))
        self.button_flash_only = ttk.Button(top_buttons, text="仅烧录", command=lambda: self.start_workflow(build=False, flash=True))
        self.button_flash_only.pack(side="left", padx=(8, 0))

        bottom_buttons = ttk.Frame(action_frame, style="Card.TFrame")
        bottom_buttons.grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Button(bottom_buttons, text="高级设置", command=self.open_advanced_popup).pack(side="left")
        self.button_toggle_log = ttk.Button(bottom_buttons, textvariable=self.var_log_button_text, command=self.open_log_popup)
        self.button_toggle_log.pack(side="left", padx=(8, 0))

        footer = ttk.Frame(container, style="App.TFrame")
        footer.grid(row=4, column=0, sticky="ew", pady=(6, 0))
        footer.columnconfigure(0, weight=1)
        ttk.Label(footer, textvariable=self.var_status, anchor="w", style="Status.TLabel").grid(row=0, column=0, sticky="ew")

        self.log_frame = ttk.LabelFrame(container, text="执行日志", padding=8, style="Card.TLabelframe")
        self.log_frame.columnconfigure(0, weight=1)
        self.log_frame.rowconfigure(0, weight=1)

        self.log_inline_text = scrolledtext.ScrolledText(
            self.log_frame,
            wrap="word",
            font=("Consolas", 10),
            height=11,
            state="disabled",
            relief="flat",
            borderwidth=0,
        )
        self.log_inline_text.grid(row=0, column=0, sticky="nsew")

    def _bind_shortcuts(self) -> None:
        bindings = (
            ("<Alt-b>", self._on_shortcut_build),
            ("<Alt-B>", self._on_shortcut_build),
            ("<Alt-n>", self._on_shortcut_flash),
            ("<Alt-N>", self._on_shortcut_flash),
        )
        for sequence, handler in bindings:
            self.root.bind_all(sequence, handler, add="+")

    def _on_shortcut_build(self, event: Optional[tk.Event] = None) -> str:
        self.start_workflow(build=True, flash=False)
        return "break"

    def _on_shortcut_flash(self, event: Optional[tk.Event] = None) -> str:
        self.start_workflow(build=False, flash=True)
        return "break"

    def _fit_initial_window(self) -> None:
        self.root.update_idletasks()
        width = max(self.default_window_width, self.root.winfo_reqwidth())
        height = max(self.default_window_height, self.root.winfo_reqheight())
        self.default_window_width = width
        self.default_window_height = height
        self.expanded_window_height = max(self.expanded_window_height, height + 300)
        self.root.minsize(width, height)
        self.root.geometry(f"{width}x{height}")

    def _add_entry_row(
        self,
        parent: ttk.LabelFrame,
        row: int,
        label: str,
        variable: tk.StringVar,
        *,
        browse_dir: Optional[Callable[[], None]] = None,
        browse_file: Optional[Callable[[], None]] = None,
    ) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(0, 8), pady=4)
        entry = ttk.Entry(parent, textvariable=variable)
        entry.grid(row=row, column=1, sticky="ew", pady=4)
        if browse_dir or browse_file:
            ttk.Button(parent, text="浏览", command=browse_dir or browse_file).grid(
                row=row, column=2, sticky="w", padx=(8, 18), pady=4
            )

    def choose_directory(self, target: tk.StringVar) -> None:
        start = target.get().strip() or str(DEFAULT_PROJECT_ROOT)
        selected = filedialog.askdirectory(initialdir=start)
        if selected:
            target.set(selected)
            candidate = Path(selected) / "project.uvprojx"
            if candidate.exists():
                self.var_project_file.set(str(candidate))
            self.update_compact_summary()

    def choose_file(self, target: tk.StringVar, patterns: list[tuple[str, str]]) -> None:
        current = target.get().strip()
        initial_dir = str(Path(current).parent) if current else str(DEFAULT_PROJECT_ROOT)
        selected = filedialog.askopenfilename(initialdir=initial_dir, filetypes=patterns)
        if selected:
            target.set(selected)
            self.update_compact_summary()

    def refresh_ports(self, initial: bool = False) -> None:
        labels, preferred = list_serial_port_choices()
        current = self.var_selected_port.get().strip()
        if self.port_combo is not None:
            self.port_combo["values"] = labels

        if labels:
            if current in labels:
                self.var_selected_port.set(current)
            elif current and not initial:
                self.var_selected_port.set(current)
            else:
                self.var_selected_port.set(preferred or labels[0])
        elif not initial:
            self.var_selected_port.set("")
            messagebox.showwarning("COM 口", "没有检测到可用串口。")
        self.update_compact_summary()

    def update_method_frame(self) -> None:
        method = method_from_display(self.var_flash_method.get().strip()) or METHOD_STLINK
        method_label = METHOD_LABELS.get(method, method)
        hints = {
            METHOD_STLINK: "当前方式使用 ST-LINK / SWD，不依赖 COM 口；COM 口会保留为串口记录值。",
            METHOD_PYOCD: "当前方式使用 pyOCD / DAPLink，不依赖 COM 口；若有多个探头，可填写 Probe UID。",
            METHOD_STCGAL: "当前方式使用 stcgal 串口烧录，必须选择正确的 COM 口，并按提示断电上电。",
        }
        self.var_method_hint.set(f"已选择: {method_label}。{hints.get(method, '')}")

        for frame in (self.stlink_frame, self.pyocd_frame, self.stc_frame):
            if frame is not None and frame.winfo_exists():
                frame.grid_remove()

        if method == METHOD_STLINK:
            if self.stlink_frame is not None and self.stlink_frame.winfo_exists():
                self.stlink_frame.grid()
        elif method == METHOD_PYOCD:
            if self.pyocd_frame is not None and self.pyocd_frame.winfo_exists():
                self.pyocd_frame.grid()
        else:
            if self.stc_frame is not None and self.stc_frame.winfo_exists():
                self.stc_frame.grid()
        self.update_compact_summary()

    def update_compact_summary(self) -> None:
        method = method_from_display(self.var_flash_method.get().strip()) or METHOD_STLINK
        method_label = METHOD_LABELS.get(method, method)
        project_name = Path(self.var_project_root.get().strip() or DEFAULT_PROJECT_ROOT).name
        port_value = self.var_selected_port.get().strip() or "未选择"
        if "|" in port_value:
            port_value = port_label_to_name(port_value)
        extra = ""
        if method == METHOD_STLINK:
            extra = f"SWD {self.var_cube_freq.get().strip() or '-'} kHz"
        elif method == METHOD_PYOCD:
            extra = f"{self.var_pyocd_target.get().strip() or '-'} / {self.var_pyocd_freq.get().strip() or '-'} Hz"
        elif method == METHOD_STCGAL:
            extra = f"{self.var_stc_chip.get().strip() or '-'} / 重试 {self.var_stc_retries.get().strip() or '-'}"
        self.var_compact_summary.set(
            f"项目: {project_name}\n方式: {method_label} | COM: {port_value}\n参数: {extra}"
        )

    def open_advanced_popup(self) -> None:
        if self.advanced_popup is not None and self.advanced_popup.winfo_exists():
            self.advanced_popup.deiconify()
            self.advanced_popup.lift()
            self.advanced_popup.focus_force()
            return

        popup = tk.Toplevel(self.root)
        popup.title("高级设置")
        popup.geometry("760x520")
        popup.minsize(720, 460)
        popup.transient(self.root)
        self.advanced_popup = popup

        frame = ttk.Frame(popup, padding=10, style="App.TFrame")
        frame.pack(fill="both", expand=True)
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(2, weight=1)

        general_frame = ttk.LabelFrame(frame, text="基础配置", padding=10, style="Card.TLabelframe")
        general_frame.grid(row=0, column=0, sticky="ew")
        general_frame.columnconfigure(1, weight=1)
        general_frame.columnconfigure(3, weight=1)

        ttk.Label(general_frame, text="工程目录", style="CardLabel.TLabel").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(general_frame, textvariable=self.var_project_root).grid(row=0, column=1, columnspan=3, sticky="ew", pady=4)
        ttk.Button(general_frame, text="浏览", command=lambda: self.choose_directory(self.var_project_root)).grid(row=0, column=4, sticky="e", padx=(8, 0), pady=4)

        ttk.Label(general_frame, text="方式", style="CardLabel.TLabel").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=4)
        self.method_combo = ttk.Combobox(
            general_frame,
            textvariable=self.var_flash_method,
            state="readonly",
            values=[method_display_value(key) for key, _ in METHOD_OPTIONS],
            width=22,
        )
        self.method_combo.grid(row=1, column=1, sticky="ew", pady=4)
        self.method_combo.bind("<<ComboboxSelected>>", lambda _event: self.update_method_frame())

        ttk.Label(general_frame, text="COM", style="CardLabel.TLabel").grid(row=1, column=2, sticky="w", padx=(12, 8), pady=4)
        self.port_combo = ttk.Combobox(
            general_frame,
            textvariable=self.var_selected_port,
            state="readonly",
            width=20,
        )
        self.port_combo.grid(row=1, column=3, sticky="ew", pady=4)
        ttk.Button(general_frame, text="刷新", command=self.refresh_ports).grid(row=1, column=4, sticky="e", padx=(8, 0), pady=4)

        params_frame = ttk.LabelFrame(frame, text="当前方式参数", padding=10, style="Card.TLabelframe")
        params_frame.grid(row=1, column=0, sticky="ew", pady=(10, 0))
        params_frame.columnconfigure(1, weight=1)
        params_frame.columnconfigure(3, weight=1)

        ttk.Label(
            params_frame,
            textvariable=self.var_method_hint,
            style="Hint.TLabel",
            wraplength=680,
        ).grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 8))

        self.stlink_frame = ttk.Frame(params_frame, style="Card.TFrame")
        self.pyocd_frame = ttk.Frame(params_frame, style="Card.TFrame")
        self.stc_frame = ttk.Frame(params_frame, style="Card.TFrame")
        for frame_item in (self.stlink_frame, self.pyocd_frame, self.stc_frame):
            frame_item.grid(row=1, column=0, columnspan=4, sticky="ew")
            frame_item.columnconfigure(1, weight=1)
            frame_item.columnconfigure(3, weight=1)

        ttk.Label(self.stlink_frame, text="SWD 频率(kHz)", style="CardLabel.TLabel").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.stlink_frame, textvariable=self.var_cube_freq).grid(row=0, column=1, sticky="ew", padx=(0, 12), pady=4)
        ttk.Label(self.stlink_frame, text="连接模式", style="CardLabel.TLabel").grid(row=0, column=2, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.stlink_frame, textvariable=self.var_cube_mode).grid(row=0, column=3, sticky="ew", pady=4)
        ttk.Label(self.stlink_frame, text="复位方式", style="CardLabel.TLabel").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.stlink_frame, textvariable=self.var_cube_reset).grid(row=1, column=1, sticky="ew", pady=4)

        ttk.Label(self.pyocd_frame, text="目标芯片", style="CardLabel.TLabel").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.pyocd_frame, textvariable=self.var_pyocd_target).grid(row=0, column=1, sticky="ew", padx=(0, 12), pady=4)
        ttk.Label(self.pyocd_frame, text="SWD 频率(Hz)", style="CardLabel.TLabel").grid(row=0, column=2, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.pyocd_frame, textvariable=self.var_pyocd_freq).grid(row=0, column=3, sticky="ew", pady=4)
        ttk.Label(self.pyocd_frame, text="Probe UID", style="CardLabel.TLabel").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.pyocd_frame, textvariable=self.var_pyocd_probe_uid).grid(row=1, column=1, sticky="ew", padx=(0, 12), pady=4)
        ttk.Label(self.pyocd_frame, text="重试次数", style="CardLabel.TLabel").grid(row=1, column=2, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.pyocd_frame, textvariable=self.var_pyocd_retries).grid(row=1, column=3, sticky="ew", pady=4)

        ttk.Label(self.stc_frame, text="芯片型号", style="CardLabel.TLabel").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.stc_frame, textvariable=self.var_stc_chip).grid(row=0, column=1, sticky="ew", padx=(0, 12), pady=4)
        ttk.Label(self.stc_frame, text="重试次数", style="CardLabel.TLabel").grid(row=0, column=2, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(self.stc_frame, textvariable=self.var_stc_retries).grid(row=0, column=3, sticky="ew", pady=4)

        advanced_frame = ttk.LabelFrame(frame, text="工具链", padding=10, style="Card.TLabelframe")
        advanced_frame.grid(row=2, column=0, sticky="nsew", pady=(10, 0))
        advanced_frame.columnconfigure(1, weight=1)

        self._add_entry_row(
            advanced_frame,
            0,
            "Keil 工程文件",
            self.var_project_file,
            browse_file=lambda: self.choose_file(
                self.var_project_file,
                [("Keil Project", "*.uvprojx *.uvproj"), ("All Files", "*.*")],
            ),
        )
        self._add_entry_row(advanced_frame, 1, "Target 名", self.var_target_name)
        self._add_entry_row(
            advanced_frame,
            2,
            "UV4.exe",
            self.var_uv4_path,
            browse_file=lambda: self.choose_file(
                self.var_uv4_path,
                [("UV4", "UV4.exe"), ("Executable", "*.exe"), ("All Files", "*.*")],
            ),
        )
        self._add_entry_row(
            advanced_frame,
            3,
            "CubeProgrammer",
            self.var_cube_cli,
            browse_file=lambda: self.choose_file(
                self.var_cube_cli,
                [("STM32 Programmer", "STM32_Programmer_CLI.exe"), ("Executable", "*.exe"), ("All Files", "*.*")],
            ),
        )
        self._add_entry_row(
            advanced_frame,
            4,
            "pyocd",
            self.var_pyocd,
            browse_file=lambda: self.choose_file(
                self.var_pyocd,
                [("pyOCD", "pyocd.exe"), ("Executable", "*.exe"), ("All Files", "*.*")],
            ),
        )
        self._add_entry_row(
            advanced_frame,
            5,
            "stcgal",
            self.var_stcgal,
            browse_file=lambda: self.choose_file(
                self.var_stcgal,
                [("stcgal", "stcgal.exe"), ("Executable", "*.exe"), ("All Files", "*.*")],
            ),
        )

        button_row = ttk.Frame(frame, style="App.TFrame")
        button_row.grid(row=3, column=0, sticky="ew", pady=(10, 0))
        def _close_popup() -> None:
            self.method_combo = None
            self.port_combo = None
            self.stlink_frame = None
            self.pyocd_frame = None
            self.stc_frame = None
            self.advanced_popup = None
            popup.destroy()

        ttk.Button(button_row, text="保存设置", command=self.persist_settings).pack(side="left")
        ttk.Button(button_row, text="关闭", command=_close_popup).pack(side="left", padx=(8, 0))
        self.refresh_ports(initial=True)
        self.update_method_frame()
        popup.protocol("WM_DELETE_WINDOW", _close_popup)

    def open_log_popup(self) -> None:
        self.toggle_log_panel()

    def toggle_log_panel(self) -> None:
        self.log_panel_visible = not self.log_panel_visible
        if self.log_panel_visible:
            self.show_log_panel()
        else:
            self.hide_log_panel()

    def _resize_root_height(self, target_height: int) -> None:
        self.root.update_idletasks()
        width = max(self.root.winfo_width(), self.default_window_width)
        target_height = max(target_height, self.root.winfo_reqheight())
        x = self.root.winfo_x()
        y = self.root.winfo_y()
        self.root.geometry(f"{width}x{target_height}+{x}+{y}")

    def show_log_panel(self) -> None:
        if self.log_frame is None:
            return
        self.log_frame.grid(row=5, column=0, sticky="nsew", pady=(8, 0))
        self.var_log_button_text.set("隐藏日志")
        self._resize_root_height(self.expanded_window_height)
        self._refresh_log_widgets()

    def hide_log_panel(self) -> None:
        if self.log_frame is not None:
            self.log_frame.grid_remove()
        self.var_log_button_text.set("显示日志")
        self._resize_root_height(self.default_window_height)

    def persist_settings(self) -> None:
        save_settings(self.collect_settings())
        self.update_compact_summary()
        self.var_status.set("设置已保存")

    def collect_settings(self) -> dict[str, str]:
        selected_port = self.var_selected_port.get().strip()
        if "|" in selected_port:
            selected_port = port_label_to_name(selected_port)

        return {
            "project_root": self.var_project_root.get().strip(),
            "project_file": self.var_project_file.get().strip(),
            "target_name": self.var_target_name.get().strip(),
            "uv4_path": self.var_uv4_path.get().strip(),
            "cube_cli_path": self.var_cube_cli.get().strip(),
            "pyocd_path": self.var_pyocd.get().strip(),
            "stcgal_path": self.var_stcgal.get().strip(),
            "flash_method": method_from_display(self.var_flash_method.get().strip()),
            "selected_port": selected_port,
            "cube_freq": self.var_cube_freq.get().strip(),
            "cube_mode": self.var_cube_mode.get().strip(),
            "cube_reset": self.var_cube_reset.get().strip(),
            "pyocd_target": self.var_pyocd_target.get().strip(),
            "pyocd_freq": self.var_pyocd_freq.get().strip(),
            "pyocd_probe_uid": self.var_pyocd_probe_uid.get().strip(),
            "pyocd_retries": self.var_pyocd_retries.get().strip(),
            "stc_chip": self.var_stc_chip.get().strip(),
            "stc_retries": self.var_stc_retries.get().strip(),
        }

    def _set_text_widget(self, widget: scrolledtext.ScrolledText, content: str) -> None:
        widget.configure(state="normal")
        widget.delete("1.0", "end")
        widget.insert("end", content)
        widget.see("end")
        widget.configure(state="disabled")

    def _refresh_log_widgets(self) -> None:
        content = "\n".join(self.log_buffer)
        if content:
            content += "\n"
        if self.log_inline_text is not None and self.log_inline_text.winfo_exists():
            self._set_text_widget(self.log_inline_text, content)

    def clear_log(self) -> None:
        self.log_buffer.clear()
        self._refresh_log_widgets()

    def append_log(self, text: str) -> None:
        self.log_buffer.append(text)
        self._refresh_log_widgets()

    def queue_log(self, message: str) -> None:
        self.log_queue.put(("log", message))

    def queue_status(self, message: str) -> None:
        self.log_queue.put(("status", message))

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for button in (self.button_build_only, self.button_flash_only, self.button_build_flash):
            if button is not None:
                button.configure(state=state)

    def start_workflow(self, *, build: bool, flash: bool) -> None:
        if self.worker_thread and self.worker_thread.is_alive():
            messagebox.showinfo("执行中", "已有任务正在执行，请等待完成。")
            return

        settings = self.collect_settings()
        save_settings(settings)
        self.clear_log()
        action = "编译并烧录" if build and flash else "仅编译" if build else "仅烧录"
        self.var_status.set(f"{action} 开始")
        self.set_busy(True)

        self.worker_thread = threading.Thread(
            target=self._workflow_worker,
            args=(settings, build, flash),
            daemon=True,
        )
        self.worker_thread.start()

    def _workflow_worker(self, settings: dict[str, str], build: bool, flash: bool) -> None:
        try:
            action = "编译并烧录" if build and flash else "仅编译" if build else "仅烧录"
            self.queue_status(f"{action} 进行中")
            self.queue_log(f"开始: {action}")
            self.queue_log(f"烧录方式: {METHOD_LABELS.get(settings['flash_method'], settings['flash_method'])}")
            if settings["selected_port"]:
                self.queue_log(f"COM 口: {settings['selected_port']}")

            artifacts = build_project(settings, self.queue_log) if build else inspect_existing_artifacts(settings, self.queue_log)

            if flash:
                method = settings["flash_method"]
                if method == METHOD_STLINK:
                    flash_stlink(settings, artifacts, self.queue_log)
                elif method == METHOD_PYOCD:
                    flash_pyocd(settings, artifacts, self.queue_log)
                elif method == METHOD_STCGAL:
                    flash_stcgal(settings, artifacts, self.queue_log)
                else:
                    raise RuntimeError(f"未知烧录方式: {method}")

            self.log_queue.put(("success", action))
        except Exception as exc:  # pragma: no cover - GUI path
            self.queue_log("")
            self.queue_log(f"ERROR: {exc}")
            self.queue_log(traceback.format_exc().rstrip())
            self.log_queue.put(("error", str(exc)))
        finally:
            self.log_queue.put(("done", ""))

    def process_log_queue(self) -> None:
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "log":
                    self.append_log(payload)
                elif kind == "status":
                    self.var_status.set(payload)
                elif kind == "success":
                    self.var_status.set(f"{payload} 完成")
                elif kind == "error":
                    self.var_status.set("执行失败")
                    messagebox.showerror("失败", payload)
                elif kind == "done":
                    self.set_busy(False)
        except Empty:
            pass
        finally:
            self.root.after(120, self.process_log_queue)

    def on_close(self) -> None:
        if self.worker_thread and self.worker_thread.is_alive():
            if not messagebox.askyesno("确认退出", "任务仍在执行中，确定退出吗？"):
                return
        save_settings(self.collect_settings())
        self.root.destroy()


def main() -> None:
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    os.chdir(RUNTIME_DIR)
    root = tk.Tk()
    MCUBuildFlashApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
