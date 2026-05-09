"""
Raspberry Pi data router.

Listens to UART from ESP32 (see flowchart #5), validates JSON packets,
and forwards them to the server API with retries and QoS metadata.
"""

import json
import time
import urllib.request
from dataclasses import dataclass
from typing import Any, Dict, Optional

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover
    serial = None


@dataclass
class ValidationError(Exception):
    field: str
    message: str


class DataRouter:
    def __init__(
        self,
        serial_port: str = "/dev/ttyUSB0",
        baudrate: int = 115200,
        server_url: str = "http://localhost:8000/api/add_sensor_data",
    ) -> None:
        self.server_url = server_url
        self.serial = self._open_serial(serial_port, baudrate)

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

    def validate_packet(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        required = [
            "device_id",
            "turbidity_idx",
            "tds_ms_cm",
            "vacuum_kpa",
            "milk_temp_c",
            "timestamp",
        ]
        for key in required:
            if key not in payload:
                raise ValidationError(key, "is required")
        return payload

    def forward_to_server(self, payload: Dict[str, Any]) -> None:
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
            except Exception as exc:
                time.sleep(2 ** attempt)
        raise RuntimeError("Failed to deliver packet after retries")

    def loop(self) -> None:
        while True:
            line = self._read_line()
            if not line:
                continue
            try:
                payload = json.loads(line)
                validated = self.validate_packet(payload)
                self.forward_to_server(validated)
            except ValidationError as err:
                print(f"[WARN] Field {err.field}: {err.message}")
            except Exception as exc:
                print(f"[ERROR] {exc}")


def main() -> None:
    router = DataRouter()
    router.loop()


if __name__ == "__main__":
    main()
