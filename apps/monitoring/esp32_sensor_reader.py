"""
ESP32 sensor acquisition script.

Collects Turbidity, EC/TDS, and Vacuum readings, normalizes them, and
sends JSON packets over UART to Raspberry Pi exactly as described in the
flowcharts (data acquisition + exchange pipeline).
"""

import json
import random
import time
from dataclasses import dataclass, asdict
from typing import Dict

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover - serial not available in tests
    serial = None


@dataclass
class SensorSample:
    """Normalized payload ready for transmission."""

    device_id: str
    turbidity_idx: float
    tds_ms_cm: float
    vacuum_kpa: float
    milk_temp_c: float
    humidity_pct: float
    timestamp: float


class SensorReader:
    """Reads sensors (mocked) and pushes JSON packages to Raspberry Pi."""

    def __init__(self, port: str = "/dev/ttyUSB0", baudrate: int = 115200) -> None:
        self.port = port
        self.baudrate = baudrate
        self.serial = self._open_serial()

    def _open_serial(self):
        if serial is None:
            return None
        return serial.Serial(self.port, self.baudrate, timeout=2)

    def read_turbidity(self) -> float:
        """Return turbidity index (0-100%) via SEN0189 ADC conversion."""
        return round(random.uniform(40, 90), 2)

    def read_tds(self) -> float:
        """Return milk conductivity (mS/cm) from TDS module."""
        return round(random.uniform(3.0, 5.5), 2)

    def read_vacuum(self) -> float:
        """Read vacuum sensor mapped from 0.5–4.5 V to -100..0 kPa."""
        voltage = random.uniform(0.6, 4.2)
        return round((voltage - 4.5) / 4.0 * 100.0, 2)

    def read_temp(self) -> float:
        return round(random.uniform(34.5, 37.5), 2)

    def read_humidity(self) -> float:
        return round(random.uniform(45.0, 70.0), 1)

    def collect_sample(self) -> SensorSample:
        return SensorSample(
            device_id="esp32-s3-01",
            turbidity_idx=self.read_turbidity(),
            tds_ms_cm=self.read_tds(),
            vacuum_kpa=self.read_vacuum(),
            milk_temp_c=self.read_temp(),
            humidity_pct=self.read_humidity(),
            timestamp=time.time(),
        )

    def send_packet(self, sample: SensorSample) -> None:
        packet = json.dumps(asdict(sample))
        if self.serial:
            self.serial.write(packet.encode("utf-8") + b"\n")
        else:
            print(packet)

    def run(self, interval: float = 2.0) -> None:
        """Continuously samples sensors and transmits data."""
        while True:
            sample = self.collect_sample()
            self.send_packet(sample)
            time.sleep(interval)


def main() -> None:
    reader = SensorReader()
    reader.run()


if __name__ == "__main__":
    main()
