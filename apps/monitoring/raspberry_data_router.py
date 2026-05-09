"""
Raspberry Pi data router.

Reads JSON lines from ESP32 master over UART.
Routes snap packets to in-memory state (Nextion), session packets to server API.
"""

import json
import time
import urllib.request
from typing import Any, Dict, Optional

try:
    import serial  # type: ignore
except ImportError:
    serial = None

# Static mapping: esp_id → stall number.
# Change values here to remap any ESP to any stall.
STALL_MAP: Dict[int, int] = {
    1: 1,
    2: 2,
    3: 3,
    4: 4,
}

SERVER_URL = "http://localhost:8000/api/session"


class DataRouter:
    def __init__(
        self,
        serial_port: str = "/dev/ttyUSB0",
        baudrate: int = 115200,
        server_url: str = SERVER_URL,
    ) -> None:
        self.server_url = server_url
        self.serial = self._open_serial(serial_port, baudrate)
        self.snap_state: Dict[int, Dict] = {}  # last snapshot per stall

    def _open_serial(self, port: str, baudrate: int):
        if serial is None:
            return None
        return serial.Serial(port, baudrate, timeout=2)

    def _read_line(self) -> Optional[str]:
        if self.serial:
            raw = self.serial.readline().decode("utf-8").strip()
        else:
            raw = input("Mock UART> ")
        return raw or None

    def _stall(self, esp_id: int) -> int:
        return STALL_MAP.get(esp_id, esp_id)

    def _handle_snap(self, pkt: Dict[str, Any]) -> None:
        stall = self._stall(pkt["id"])
        self.snap_state[stall] = pkt
        # TODO: push fields to Nextion component for stall N

    def _handle_session(self, pkt: Dict[str, Any]) -> None:
        stall = self._stall(pkt["id"])
        payload = {
            "stall":          stall,
            "esp_id":         pkt["id"],
            "rfid":           pkt.get("rfid", ""),
            "weight_initial": pkt["w_init"],
            "weight_final":   pkt["w_final"],
            "start_time":     pkt["t_start"],
            "end_time":       pkt["t_end"],
            "end_reason":     pkt["reason"],
        }
        self._post(payload)

    def _post(self, payload: Dict[str, Any]) -> None:
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            self.server_url,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        for attempt in range(3):
            try:
                with urllib.request.urlopen(req, timeout=5) as resp:
                    if resp.status < 300:
                        return
            except Exception:
                time.sleep(2 ** attempt)
        print("[ERROR] Failed to deliver session packet after 3 retries")

    def loop(self) -> None:
        while True:
            line = self._read_line()
            if not line:
                continue
            try:
                pkt = json.loads(line)
                pkt_type = pkt.get("type")
                if pkt_type == "snap":
                    self._handle_snap(pkt)
                elif pkt_type == "session":
                    self._handle_session(pkt)
                else:
                    print(f"[WARN] Unknown packet type: {pkt_type}")
            except json.JSONDecodeError as exc:
                print(f"[WARN] Bad JSON: {exc}")
            except KeyError as exc:
                print(f"[WARN] Missing field: {exc}")
            except Exception as exc:
                print(f"[ERROR] {exc}")


def main() -> None:
    router = DataRouter()
    router.loop()


if __name__ == "__main__":
    main()
