#!/usr/bin/env python3
"""
test_os.py
----------
Test harness mô phỏng kịch bản Non-Preemptive trên nền tảng Renode.

File này chứa một Script tự động hóa cực kỳ hữu ích để bạn có thể tham khảo
khi muốn đẩy Firmware Bare-metal lên một máy ảo giả lập thay vì nạp vào Board thật.

Mục tiêu của script:
1. Gọi shell OS thực thi câu lệnh biên dịch (Make).
2. Tạo cấu hình máy ảo Renode nhúng Firmware vừa biên dịch.
3. Kích hoạt giả lập chạy trong một khoảng thời gian (Virtual Time) nhất định.
4. Bắt toàn bộ dữ liệu Console in ra trên cổng Serial (UART1) của MCU.
5. Phân tích ngữ nghĩa log để Đánh giá (Assert) sự cắt ngang (Preempt) thành công.
"""

from __future__ import annotations

from pathlib import Path
import os
import re
import subprocess
import sys
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
    log_path = BUILD_DIR / "renode_non_preemptive.log"
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


def validate_non_preemptive(events: List[Tuple[int, str]]) -> str:
    """
    Bước 4: Logic Đánh giá cho Non-Preemptive Scheduling.
    Khẳng định rằng Task Low chạy tận tới khi Terminate (kết thúc trọn vẹn,
    in xong log), thì Task Priority High mới được nhường CPU chạy.
    """
    # Khẳng định trật tự logic: High bắt buộc xuất hiện SAU đoạn block của Low.
    require_order(
        events,
        [
            "[BOOT] OS_SCHED_MODE_DEFAULT=NON_PREEMPTIVE",
            "[BOOT] Khoi tao phan cung thanh cong.",
            "[NP_LOW] Task Low Bat Dau (Dang chiem dung CPU)",
            "[NP_LOW] Task Low Tiep Tuc va Ket Thuc (Sau khi Alarm High no, nhung van KHONG bi cat ngang)",
            "[NP_HIGH] => Task High Nhay Vao (Chi duoc chay sau khi Task Low ket thuc!)",
        ],
    )

    # Khẳng định thời gian định lượng
    start_tick = find_message_tick(events, "[NP_LOW] Task Low Bat Dau (Dang chiem dung CPU)")
    end_tick = find_message_tick(events, "[NP_LOW] Task Low Tiep Tuc va Ket Thuc (Sau khi Alarm High no, nhung van KHONG bi cat ngang)")
    urgent_tick = find_message_tick(events, "[NP_HIGH] => Task High Nhay Vao (Chi duoc chay sau khi Task Low ket thuc!)")

    if not (start_tick < end_tick <= urgent_tick):
        raise AssertionError(
            f"Non-Preemptive sai thu tu tick: start={start_tick}, end={end_tick}, urgent={urgent_tick}"
        )

    return f"Task_NP_Low chay tron ven tu tick {start_tick} den {end_tick}, roi Task_NP_High moi duoc chay o tick {urgent_tick}!"


def main() -> int:
    print("\n=== Test Non-Preemptive (Renode Automation) ===")
    try:
        print("1. Bien dich Firmware (Make)...")
        build_firmware()
        print("   OK")

        print("2. Chay gia lap OS tren MCU bang Renode...")
        log_text = run_renode(run_for="0.2") # Lặp trình ảo hoá 200 milliseconds.
        events = extract_events(log_text)
        if not events:
            raise RuntimeError("Khong bat duoc su kien UART nao!")
        print("   OK")

        print("3. Validate (Truy van) ket qua Non-Preemptive tu log...")
        summary = validate_non_preemptive(events)
        print(f"   PASS: {summary}\n")
        return 0
    except (RuntimeError, AssertionError, subprocess.TimeoutExpired, FileNotFoundError) as exc:
        print(f"\nFAILED: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
