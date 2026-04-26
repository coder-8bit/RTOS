#!/usr/bin/env python3
"""
test_os.py
----------
Test harness mô phỏng profile Event API demo trên Renode.

Mục tiêu của script:
1. Build firmware.
2. Chạy Renode headless và validate UART log cho:
   - WaitEvent block Consumer khi chưa có event.
   - SetEvent từ Producer đánh thức Consumer.
   - GetEvent đọc đúng event mask.
   - ClearEvent xóa đúng bit đã xử lý.
3. Smoke test GDB attach vào Renode, cài hardware breakpoint và đọc symbol debug.
"""

from __future__ import annotations

from pathlib import Path
import os
import re
import socket
import subprocess
import sys
import time
from typing import List, Sequence, Tuple

# Xác định đường dẫn thư mục hiện tại để gọi path linh hoạt mọi nơi.
PROJECT_ROOT = Path(__file__).resolve().parent
BUILD_DIR = Path(os.environ.get("OS_TEST_BUILD_DIR", PROJECT_ROOT / "build" / "os_test"))
ELF_PATH = Path(os.environ.get("OS_TEST_ELF", BUILD_DIR / "os_test.elf"))
RENODE_SCRIPT = Path(os.environ.get("OS_TEST_RENODE_SCRIPT", PROJECT_ROOT / "renode.resc"))

# Xác định file chạy ảo hoá Renode. Ở MacOS thường nằm trong app bundle.
RENODE_BIN = os.environ.get("RENODE_BIN")
if not RENODE_BIN:
    default_renode = Path("/Applications/Renode.app/Contents/MacOS/renode")
    RENODE_BIN = str(default_renode if default_renode.exists() else "renode")

# Regular Expression (Biểu thức chính quy) để bóc tách thời gian (Tick) từ UART Log.
UART_EVENT_RE = re.compile(r"\[T=(?P<tick>\d+)\]\s+(?P<message>.+)$")

BOOT_PROFILE = "[BOOT] PROFILE=EVENT_API_DEMO"
BOOT_APIS = "[BOOT] APIs=WaitEvent SetEvent ClearEvent GetEvent"
BOOT_FLOW = "[BOOT] Task_EventConsumer waits before producer runs"
CONSUMER_WAIT_RX = "[CONSUMER] WaitEvent(EVENT_RX_DONE)"
PRODUCER_SET_RX = "[PRODUCER] SetEvent(EVENT_RX_DONE)"
CONSUMER_GET_RX = "[CONSUMER] GetEvent after RX mask=0x00000001"
CONSUMER_CLEAR_RX = "[CONSUMER] GetEvent after ClearEvent(RX) mask=0x00000000"
CONSUMER_WAIT_TIMEOUT = "[CONSUMER] WaitEvent(EVENT_TIMEOUT)"
PRODUCER_RESUMED = "[PRODUCER] resumed after consumer blocked again"
PRODUCER_SET_TIMEOUT = "[PRODUCER] SetEvent(EVENT_TIMEOUT)"
CONSUMER_GET_TIMEOUT = "[CONSUMER] GetEvent after TIMEOUT mask=0x00000002"
CONSUMER_CLEAR_TIMEOUT = "[CONSUMER] GetEvent after ClearEvent(TIMEOUT) mask=0x00000000"
CONSUMER_DONE = "[CONSUMER] Event demo complete"
PRODUCER_DONE = "[PRODUCER] producer complete"


def rel_path(path: Path) -> str:
    """Trả về relative path cho file để chèn vào biến config của renode."""
    try:
        return path.resolve().relative_to(PROJECT_ROOT.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def build_temp_resc(log_path: Path, run_for: str) -> Path:
    """
    Sinh ra file cấu hình giả lập '.resc' tạm thời chứa:
    - Đường dẫn chỏ vào Firmware đuôi .elf
    - Tham số chỉ định xuất output Log Analyzer ra file vật lý.
    - Thời gian ảo hoá chỉ chạy đúng run_for giây.
    """
    resc_text = RENODE_SCRIPT.read_text(encoding="utf-8")
    temp_resc = BUILD_DIR / f"renode_{log_path.stem}.resc"
    filtered_lines = []

    for line in resc_text.splitlines():
        stripped = line.strip()
        # Chèn đè đường dẫn firmware
        if stripped.startswith("$elf_path ?="):
            filtered_lines.append(f"$elf_path ?= @{ELF_PATH.resolve().as_posix()}")
        # Ghi log ra file thay vì console
        elif stripped.startswith("$gpio_log_path ?="):
            filtered_lines.append(f"$gpio_log_path ?= @{log_path.resolve().as_posix()}")
        # Tắt các config server debug vì ta chỉ muốn test tự động không GUI
        elif stripped.startswith("$gdb_port ?="):
            continue
        elif stripped.startswith("machine StartGdbServer"):
            continue
        elif stripped == 'echo "Renode ready for GDB connection on configured port. Use \'start\' to begin simulation."':
            continue
        else:
            filtered_lines.append(line)

    # Chèn lệnh ra lệnh cho CPU Board bắt đầu chạy ngầm "run_for" (s)
    filtered_lines.append(f'emulation RunFor "{run_for}"')
    filtered_lines.append("quit")
    temp_resc.write_text("\n".join(filtered_lines) + "\n", encoding="utf-8")
    return temp_resc


def run_cmd(cmd: Sequence[str], *, env: dict | None = None, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    """Hàm chạy shell command thay thế cho os.system, gom stdout và bắt error."""
    return subprocess.run(
        list(cmd),
        cwd=PROJECT_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )


def build_firmware() -> None:
    """
    Bước 1: Gọi makefile C/C++ để compile code thành hệ nhị phân (bin/elf).
    Kiểm tra triệt để tiến trình GCC.
    """
    proc_clean = run_cmd(["make", "clean"], timeout=120)
    if proc_clean.returncode != 0:
        raise RuntimeError(f"`make clean` failed:\n{proc_clean.stdout}")

    proc_build = run_cmd(["make", "-j4", "all"], timeout=240)
    if proc_build.returncode != 0:
        raise RuntimeError(f"Build failed:\n{proc_build.stdout}")

    if not ELF_PATH.exists():
        raise RuntimeError(f"ELF not found after build: {ELF_PATH}")


def run_renode(run_for: str) -> str:
    """
    Bước 2: Nạp Firmware lên giả lập Renode bằng thư viện dòng lệnh.
    """
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    log_path = BUILD_DIR / "renode_event_api.log"
    temp_resc = build_temp_resc(log_path, run_for)

    renode_cmd = [
        RENODE_BIN,
        "--console",    # Chạy ở Terminal background
        "--disable-gui",# Không render giao diện đồ hoạ Board
        "-e",
        f"i @{rel_path(temp_resc)}", # include file kịch bản
    ]

    proc = run_cmd(renode_cmd, timeout=60)
    if proc.returncode != 0:
        raise RuntimeError(f"Renode failed:\n{proc.stdout}")

    if not log_path.exists():
        raise RuntimeError(f"Renode log not found: {log_path}")

    # Đọc kết quả file Log do Renode nhả ra
    return log_path.read_text(encoding="utf-8", errors="replace")


def extract_events(log_text: str) -> List[Tuple[int, str]]:
    """
    Bước 3: Tách dữ liệu UART raw chứa đầy các ký tự rác hoặc thông tin không liên quan,
    trích rút ra chuẩn tuple: (Tick Time, Thông điệp Log)
    """
    events: List[Tuple[int, str]] = []
    last_event: Tuple[int, str] | None = None

    for line in log_text.splitlines():
        if "usart1:" not in line:
            continue
        match = UART_EVENT_RE.search(line)
        if not match:
            continue
        event = (int(match.group("tick")), match.group("message").strip())
        
        # Renode thi thoảng quét 1 symbol 2 lần nên ta lọc bỏ line trùng.
        if event != last_event:
            events.append(event)
            last_event = event

    return events


def find_message_index(texts: Sequence[str], message: str, start: int = 0) -> int:
    """Tìm dòng thông báo trong chuỗi Log."""
    for idx in range(start, len(texts)):
        if texts[idx] == message:
            return idx
    raise AssertionError(f"Khong tim thay message: {message}")


def require_order(events: Sequence[Tuple[int, str]], messages: Sequence[str]) -> List[int]:
    """Xác nhận rằng một chuỗi thông báo nhất định bắt buộc tuân thủ đúng thứ tự in."""
    texts = [text for _, text in events]
    indices: List[int] = []
    start = 0
    for message in messages:
        idx = find_message_index(texts, message, start)
        indices.append(idx)
        start = idx + 1
    return indices


def find_message_tick(events: Sequence[Tuple[int, str]], message: str, occurrence: int = 1) -> int:
    """Moi ra cột mốc Thời gian (Tick hệ thống) của một Log nhất định."""
    count = 0
    for tick, text in events:
        if text == message:
            count += 1
            if count == occurrence:
                return tick
    raise AssertionError(f"Khong tim thay occurrence #{occurrence} cua message: {message}")


def require_tick(events: Sequence[Tuple[int, str]], message: str, expected_tick: int, occurrence: int = 1) -> int:
    actual_tick = find_message_tick(events, message, occurrence)
    if actual_tick != expected_tick:
        raise AssertionError(
            f"Sai tick cho occurrence #{occurrence} cua '{message}': expected={expected_tick}, actual={actual_tick}"
        )
    return actual_tick


def validate_event_api(events: List[Tuple[int, str]]) -> str:
    """
    Validate đủ 4 ý chính:
    - Consumer gọi WaitEvent trước khi Producer phát event.
    - SetEvent(EVENT_RX_DONE) đánh thức Consumer và GetEvent thấy bit 0.
    - ClearEvent(EVENT_RX_DONE) xóa bit về 0.
    - Chu kỳ thứ hai với EVENT_TIMEOUT đi qua cùng flow.
    """
    indices = require_order(
        events,
        [
            BOOT_PROFILE,
            BOOT_APIS,
            BOOT_FLOW,
            CONSUMER_WAIT_RX,
            PRODUCER_SET_RX,
            CONSUMER_GET_RX,
            CONSUMER_CLEAR_RX,
            CONSUMER_WAIT_TIMEOUT,
            PRODUCER_RESUMED,
            PRODUCER_SET_TIMEOUT,
            CONSUMER_GET_TIMEOUT,
            CONSUMER_CLEAR_TIMEOUT,
            CONSUMER_DONE,
            PRODUCER_DONE,
        ],
    )

    if not (indices[3] < indices[4] < indices[5] < indices[6]):
        raise AssertionError("RX flow sai thu tu WaitEvent -> SetEvent -> GetEvent -> ClearEvent")
    if not (indices[7] < indices[9] < indices[10] < indices[11]):
        raise AssertionError("TIMEOUT flow sai thu tu WaitEvent -> SetEvent -> GetEvent -> ClearEvent")

    return (
        "Event API OK: Consumer block bang WaitEvent, Producer SetEvent danh thuc, "
        "GetEvent doc dung mask 0x1/0x2, ClearEvent xoa ve 0."
    )


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_port(port: int, timeout_s: float = 10.0) -> None:
    deadline = time.monotonic() + timeout_s
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(0.1)
    raise RuntimeError(f"GDB server port {port} khong san sang: {last_error}")


def build_temp_gdb_resc(log_path: Path, port: int) -> Path:
    resc_text = RENODE_SCRIPT.read_text(encoding="utf-8")
    temp_resc = BUILD_DIR / "renode_gdb_smoke.resc"
    filtered_lines = []

    for line in resc_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("$elf_path ?="):
            filtered_lines.append(f"$elf_path ?= @{ELF_PATH.resolve().as_posix()}")
        elif stripped.startswith("$gpio_log_path ?="):
            filtered_lines.append(f"$gpio_log_path ?= @{log_path.resolve().as_posix()}")
        elif stripped.startswith("$gdb_port ?="):
            filtered_lines.append(f"$gdb_port ?= {port}")
        elif stripped == 'echo "Renode ready for GDB connection on configured port. Use \'start\' to begin simulation."':
            continue
        else:
            filtered_lines.append(line)

    temp_resc.write_text("\n".join(filtered_lines) + "\n", encoding="utf-8")
    return temp_resc


def run_gdb_smoke() -> str:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    port = pick_free_port()
    log_path = BUILD_DIR / "renode_gdb_smoke.log"
    temp_resc = build_temp_gdb_resc(log_path, port)

    renode_cmd = [
        RENODE_BIN,
        "--console",
        "--disable-gui",
    ]
    renode_proc = subprocess.Popen(
        renode_cmd,
        cwd=PROJECT_ROOT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        if renode_proc.stdin is None:
            raise RuntimeError("Khong mo duoc stdin cho Renode console")
        renode_proc.stdin.write(f"i @{rel_path(temp_resc)}\n")
        renode_proc.stdin.flush()
        wait_for_port(port)
        gdb_cmd = [
            "arm-none-eabi-gdb",
            "-q",
            str(ELF_PATH),
            "-ex", "set pagination off",
            "-ex", "set confirm off",
            "-ex", f"target remote :{port}",
            "-ex", "hbreak Task_EventConsumer",
            "-ex", "hbreak SetEvent",
            "-ex", "info breakpoints",
            "-ex", "p/x &g_current",
            "-ex", "p/x &g_next",
            "-ex", "detach",
            "-ex", "quit",
        ]
        gdb_proc = run_cmd(gdb_cmd, timeout=40)
        if gdb_proc.returncode != 0:
            raise RuntimeError(f"GDB smoke failed:\n{gdb_proc.stdout}")
        if ("Task_EventConsumer" not in gdb_proc.stdout) or ("SetEvent" not in gdb_proc.stdout) or ("Hardware assisted breakpoint" not in gdb_proc.stdout):
            raise RuntimeError(f"GDB khong attach/doc duoc symbol can thiet:\n{gdb_proc.stdout}")
        return f"GDB attached vao Renode port {port}, cai hardware breakpoint Task_EventConsumer/SetEvent va doc symbol g_current/g_next"
    finally:
        if renode_proc.poll() is None:
            if renode_proc.stdin is not None:
                try:
                    renode_proc.stdin.write("quit\n")
                    renode_proc.stdin.flush()
                except BrokenPipeError:
                    pass
            renode_proc.terminate()
            try:
                renode_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                renode_proc.kill()
                renode_proc.wait(timeout=5)


def main() -> int:
    print("\n=== Test Event API Demo (Renode/GDB Automation) ===")
    try:
        print("1. Bien dich Firmware (Make)...")
        build_firmware()
        print("   OK")

        print("2. Chay gia lap OS tren MCU bang Renode...")
        log_text = run_renode(run_for="0.32")
        events = extract_events(log_text)
        if not events:
            raise RuntimeError("Khong bat duoc su kien UART nao!")
        print("   OK")

        print("3. Validate WaitEvent/SetEvent/ClearEvent/GetEvent tu log...")
        summary = validate_event_api(events)
        print(f"   PASS: {summary}\n")

        print("4. GDB smoke attach vao Renode...")
        gdb_summary = run_gdb_smoke()
        print(f"   PASS: {gdb_summary}\n")
        return 0
    except (RuntimeError, AssertionError, subprocess.TimeoutExpired, FileNotFoundError) as exc:
        print(f"\nFAILED: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
