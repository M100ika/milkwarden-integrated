#!/usr/bin/env python3
"""
Milkwarden Control (mwctl) — GUI-обёртка над Telnet CLI слейва ESP32.
Для техников на выезде: подключиться к весам по IP и нажимать кнопки
вместо ввода команд руками. Только стандартная библиотека Python.

Запуск:  python mwctl_gui.py   (или двойной клик по run_mwctl.bat на Windows)
"""

import queue
import socket
import threading
import tkinter as tk
from tkinter import messagebox, scrolledtext, ttk

TELNET_PORT = 23
SOCK_TIMEOUT = 5


# ─── Telnet-клиент ──────────────────────────────────────────────────────────

class TelnetClient:
    def __init__(self, on_event):
        # on_event(kind, payload) вызывается из фонового потока — только putnет в очередь
        self.on_event = on_event
        self.sock = None
        self.connected = False
        self._reader = None

    def connect(self, host, port=TELNET_PORT):
        try:
            s = socket.create_connection((host, port), timeout=SOCK_TIMEOUT)
        except OSError as e:
            self.on_event("status", (False, f"Не удалось подключиться: {e}"))
            return
        s.settimeout(None)
        self.sock = s
        self.connected = True
        self.on_event("status", (True, f"Подключено к {host}:{port}"))
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self):
        sock = self.sock
        while self.connected:
            try:
                data = sock.recv(4096)
            except OSError:
                break
            if not data:
                break
            self.on_event("line", data.decode("utf-8", errors="replace"))
        was_connected = self.connected
        self.connected = False
        if was_connected:
            self.on_event("status", (False, "Соединение разорвано"))

    def send(self, cmd):
        if not self.connected or not self.sock:
            self.on_event("line", "\n[!] Нет соединения — команда не отправлена.\n")
            return
        try:
            self.sock.sendall((cmd + "\n").encode("utf-8"))
        except OSError as e:
            self.on_event("status", (False, f"Ошибка отправки: {e}"))

    def disconnect(self):
        self.connected = False
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None


# ─── GUI ────────────────────────────────────────────────────────────────────

class MwctlApp:
    def __init__(self, root):
        self.root = root
        root.title("Milkwarden Control — весы (slave ESP32)")
        root.geometry("980x720")

        self.msg_queue = queue.Queue()
        self.client = TelnetClient(lambda kind, payload: self.msg_queue.put((kind, payload)))

        self._build_top_bar()
        self._build_notebook()
        self._build_console()

        self.root.after(50, self._poll_queue)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ── Верхняя панель: подключение ─────────────────────────────────────────
    def _build_top_bar(self):
        bar = ttk.Frame(self.root, padding=8)
        bar.pack(fill="x")

        ttk.Label(bar, text="IP весов:", font=("Segoe UI", 11)).pack(side="left")
        self.ip_entry = ttk.Entry(bar, width=18, font=("Segoe UI", 11))
        self.ip_entry.pack(side="left", padx=6)
        self.ip_entry.insert(0, "192.168.")
        self.ip_entry.bind("<Return>", lambda e: self.connect())

        self.connect_btn = ttk.Button(bar, text="Подключиться", command=self.connect)
        self.connect_btn.pack(side="left", padx=4)
        self.disconnect_btn = ttk.Button(bar, text="Отключиться", command=self.disconnect, state="disabled")
        self.disconnect_btn.pack(side="left", padx=4)

        self.status_var = tk.StringVar(value="Не подключено")
        self.status_lbl = ttk.Label(bar, textvariable=self.status_var, foreground="red", font=("Segoe UI", 11, "bold"))
        self.status_lbl.pack(side="left", padx=16)

    # ── Вкладки с кнопками ──────────────────────────────────────────────────
    def _build_notebook(self):
        nb = ttk.Notebook(self.root)
        nb.pack(fill="x", padx=8, pady=4)

        self._tab_system(self._new_tab(nb, "Система"))
        self._tab_measure(self._new_tab(nb, "Измерение"))
        self._tab_calib(self._new_tab(nb, "Калибровка"))
        self._tab_corner(self._new_tab(nb, "Углы"))
        self._tab_diag(self._new_tab(nb, "Диагностика"))
        self._tab_rfid(self._new_tab(nb, "RFID"))
        self._tab_beam(self._new_tab(nb, "Луч/Сессия"))
        self._tab_valve(self._new_tab(nb, "Клапан"))
        self._tab_network(self._new_tab(nb, "Сеть"))

    def _new_tab(self, nb, title):
        frame = ttk.Frame(nb, padding=10)
        nb.add(frame, text=title)
        return frame

    # helper: простая кнопка без параметров
    def _btn(self, parent, row, col, label, cmd, confirm=None, colspan=1):
        def action():
            if confirm and not messagebox.askyesno("Подтверждение", confirm):
                return
            self.send_cmd(cmd)
        ttk.Button(parent, text=label, command=action).grid(
            row=row, column=col, columnspan=colspan, sticky="ew", padx=4, pady=3)

    # helper: строка "команда <значение>" с полем ввода
    def _param_row(self, parent, row, label, cmd_prefix, default=""):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=3)
        entry = ttk.Entry(parent, width=12)
        entry.grid(row=row, column=1, sticky="w", padx=4, pady=3)
        if default:
            entry.insert(0, default)

        def action():
            val = entry.get().strip()
            self.send_cmd(f"{cmd_prefix} {val}" if val else cmd_prefix)

        ttk.Button(parent, text="Отправить", command=action).grid(row=row, column=2, padx=4, pady=3)

    # ── Система ──────────────────────────────────────────────────────────
    def _tab_system(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "Статус (status)", "status")
        self._btn(f, 0, 1, "Сохранить настройки (save)", "save")
        self._btn(f, 0, 2, "Список команд (help)", "help")
        self._btn(f, 1, 0, "ID устройства (devid)", "devid")
        self._btn(f, 1, 1, "OTA-обновление (update)", "update",
                  confirm="Запустить обновление прошивки по WiFi?")
        self._btn(f, 1, 2, "Перезагрузка (reboot)", "reboot",
                  confirm="Перезагрузить весы?")

        ttk.Separator(f, orient="horizontal").grid(row=2, column=0, columnspan=3, sticky="ew", pady=8)
        ttk.Label(f, text="Установить номер устройства (1-4):").grid(row=3, column=0, sticky="w", padx=4)
        self.setid_var = tk.StringVar(value="1")
        ttk.Combobox(f, textvariable=self.setid_var, values=["1", "2", "3", "4"],
                     width=4, state="readonly").grid(row=3, column=1, sticky="w", padx=4)
        ttk.Button(f, text="Применить (setid)",
                   command=lambda: self.send_cmd(f"setid {self.setid_var.get()}")
                   ).grid(row=3, column=2, padx=4, pady=3)

    # ── Измерение ────────────────────────────────────────────────────────
    def _tab_measure(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "Старт измерения (start)", "start")
        self._btn(f, 0, 1, "Стоп измерения (stop)", "stop")
        self._btn(f, 0, 2, "Обнулить весы (tare)", "tare")
        ttk.Separator(f, orient="horizontal").grid(row=1, column=0, columnspan=3, sticky="ew", pady=8)
        self._param_row(f, 2, "Кол-во сэмплов (1-64):", "samples", "15")
        ttk.Separator(f, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=8)
        self._btn(f, 4, 0, "Автозеро ВКЛ", "autozero on")
        self._btn(f, 4, 1, "Автозеро ВЫКЛ", "autozero off")
        self._param_row(f, 5, "Порог автозеро, г:", "az_thr", "10")
        self._param_row(f, 6, "Время удержания, мс:", "az_time", "8000")

    # ── Калибровка ───────────────────────────────────────────────────────
    def _tab_calib(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        ttk.Label(f, text="1) Снять груз с весов, нажать «Нулевая точка».",
                  font=("Segoe UI", 10, "italic")).grid(row=0, column=0, columnspan=3, sticky="w", pady=2)
        self._btn(f, 1, 0, "Нулевая точка (cal_tare)", "cal_tare", colspan=3)
        ttk.Label(f, text="2) Положить эталонный груз, указать его вес и отправить.",
                  font=("Segoe UI", 10, "italic")).grid(row=2, column=0, columnspan=3, sticky="w", pady=2)
        self._param_row(f, 3, "Вес эталонного груза, г:", "cal_weight")
        ttk.Label(f, text="3) Проверить результат и сохранить.",
                  font=("Segoe UI", 10, "italic")).grid(row=4, column=0, columnspan=3, sticky="w", pady=2)
        self._btn(f, 5, 0, "Текущий коэффициент (factor)", "factor")
        self._btn(f, 5, 1, "Сохранить (save)", "save")
        ttk.Separator(f, orient="horizontal").grid(row=6, column=0, columnspan=3, sticky="ew", pady=8)
        ttk.Label(f, text="Ручная установка коэффициента (редко нужно):").grid(row=7, column=0, sticky="w")
        self._param_row(f, 8, "Коэффициент (factor):", "calib")

    # ── Углы платформы ───────────────────────────────────────────────────
    def _tab_corner(self, f):
        for i in range(4):
            f.columnconfigure(i, weight=1)
        ttk.Label(f, text="Встать по очереди в каждый угол платформы и нажать соответствующую кнопку:",
                  font=("Segoe UI", 10, "italic")).grid(row=0, column=0, columnspan=4, sticky="w", pady=4)
        self._btn(f, 1, 0, "FL (перед-лево)", "corner_test FL")
        self._btn(f, 1, 1, "FR (перед-право)", "corner_test FR")
        self._btn(f, 1, 2, "BL (зад-лево)", "corner_test BL")
        self._btn(f, 1, 3, "BR (зад-право)", "corner_test BR")
        ttk.Separator(f, orient="horizontal").grid(row=2, column=0, columnspan=4, sticky="ew", pady=8)
        self._btn(f, 3, 0, "Отчёт (corner_report)", "corner_report", colspan=2)
        self._btn(f, 3, 2, "Очистить (corner_clear)", "corner_clear", colspan=2)

    # ── Диагностика ──────────────────────────────────────────────────────
    def _tab_diag(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "Полная диагностика (diag)", "diag")
        self._btn(f, 0, 1, "Схема подключения (wiring)", "wiring")
        ttk.Separator(f, orient="horizontal").grid(row=1, column=0, columnspan=3, sticky="ew", pady=8)
        self._param_row(f, 2, "Сырые данные, N сэмплов:", "raw", "5")
        self._param_row(f, 3, "Тест шума, N сэмплов:", "noise", "30")
        ttk.Separator(f, orient="horizontal").grid(row=4, column=0, columnspan=3, sticky="ew", pady=8)
        ttk.Label(f, text="Усиление HX711 (менять только если посоветовал инженер):").grid(
            row=5, column=0, columnspan=3, sticky="w")
        self._btn(f, 6, 0, "gain 128", "gain 128")
        self._btn(f, 6, 1, "gain 64", "gain 64")
        self._btn(f, 6, 2, "gain 32", "gain 32")

    # ── RFID ─────────────────────────────────────────────────────────────
    def _tab_rfid(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "Сканировать (rfid scan)", "rfid scan")
        self._btn(f, 0, 1, "Мониторинг (rfid monitor)", "rfid monitor")
        self._btn(f, 0, 2, "Диагностика (rfid diag)", "rfid diag")
        self._btn(f, 1, 0, "Текущий тег (rfid status)", "rfid status")
        self._btn(f, 1, 1, "Скан ВКЛ (rfid start)", "rfid start")
        self._btn(f, 1, 2, "Скан ВЫКЛ (rfid stop)", "rfid stop")
        ttk.Separator(f, orient="horizontal").grid(row=2, column=0, columnspan=3, sticky="ew", pady=8)
        self._btn(f, 3, 0, "Текущая мощность", "rfid power")
        self._param_row(f, 4, "Установить мощность, dBm (10-33):", "rfid power")

    # ── Луч / Сессия ─────────────────────────────────────────────────────
    def _tab_beam(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "Состояние луча (beam)", "beam")
        self._btn(f, 0, 1, "Мониторинг 10с (beam monitor)", "beam monitor")
        self._param_row(f, 1, "Подтверждение \"ушла\", мс:", "beam confirm", "5000")
        self._param_row(f, 2, "Подтверждение \"пришла\", мс:", "beam arrive", "2000")
        ttk.Separator(f, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=8)
        self._btn(f, 4, 0, "Статус сессии (session)", "session")
        self._btn(f, 4, 1, "Сбросить сессию (session reset)", "session reset",
                  confirm="Принудительно сбросить текущую сессию доения?")
        self._btn(f, 4, 2, "Живой поток 60с (flow)", "flow")

    # ── Клапан ───────────────────────────────────────────────────────────
    def _tab_valve(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "Статус клапана (valve status)", "valve status", colspan=3)
        self._btn(f, 1, 0, "Открыть (valve open)", "valve open",
                  confirm="Открыть клапан вручную?")
        self._btn(f, 1, 1, "Закрыть (valve close)", "valve close")
        ttk.Separator(f, orient="horizontal").grid(row=2, column=0, columnspan=3, sticky="ew", pady=8)
        self._param_row(f, 3, "Задержка перед спреем, мс:", "valve delay", "180000")
        self._param_row(f, 4, "Длительность спрея, мс:", "valve duration", "10000")
        self._param_row(f, 5, "Пауза между спреями, мс:", "valve cooldown", "240000")

    # ── Сеть ─────────────────────────────────────────────────────────────
    def _tab_network(self, f):
        for i in range(3):
            f.columnconfigure(i, weight=1)
        self._btn(f, 0, 0, "WiFi статус (wifi)", "wifi")
        self._btn(f, 0, 1, "ESP-NOW статус", "espnow status")
        self._btn(f, 0, 2, "ESP-NOW тест-пакет", "espnow test")
        self._btn(f, 1, 0, "Тест облака (cloud test)", "cloud test")
        self._btn(f, 1, 1, "Текущее время (time)", "time")
        self._btn(f, 1, 2, "Синхронизировать NTP", "ntp sync")

    # ── Консоль вывода + ручной ввод ─────────────────────────────────────
    def _build_console(self):
        frame = ttk.Frame(self.root, padding=(8, 4))
        frame.pack(fill="both", expand=True)

        self.console = scrolledtext.ScrolledText(
            frame, height=16, font=("Consolas", 10), bg="#111", fg="#0f0", insertbackground="#0f0")
        self.console.pack(fill="both", expand=True)
        self.console.configure(state="disabled")

        entry_frame = ttk.Frame(self.root, padding=8)
        entry_frame.pack(fill="x")
        ttk.Label(entry_frame, text="Своя команда:").pack(side="left")
        self.raw_entry = ttk.Entry(entry_frame)
        self.raw_entry.pack(side="left", fill="x", expand=True, padx=6)
        self.raw_entry.bind("<Return>", lambda e: self._send_raw())
        ttk.Button(entry_frame, text="Отправить", command=self._send_raw).pack(side="left")

    def _send_raw(self):
        cmd = self.raw_entry.get().strip()
        if cmd:
            self.send_cmd(cmd)
            self.raw_entry.delete(0, "end")

    # ── Действия ─────────────────────────────────────────────────────────
    def connect(self):
        host = self.ip_entry.get().strip()
        if not host:
            messagebox.showwarning("IP не указан", "Введите IP-адрес весов.")
            return
        self.status_var.set("Подключение...")
        self.status_lbl.configure(foreground="orange")
        threading.Thread(target=self.client.connect, args=(host,), daemon=True).start()

    def disconnect(self):
        self.client.disconnect()
        self.status_var.set("Не подключено")
        self.status_lbl.configure(foreground="red")
        self.connect_btn.configure(state="normal")
        self.disconnect_btn.configure(state="disabled")

    def send_cmd(self, cmd):
        self._append_console(f"\n>>> {cmd}\n")
        self.client.send(cmd)

    def _append_console(self, text):
        self.console.configure(state="normal")
        self.console.insert("end", text)
        self.console.see("end")
        self.console.configure(state="disabled")

    def _poll_queue(self):
        try:
            while True:
                kind, payload = self.msg_queue.get_nowait()
                if kind == "line":
                    self._append_console(payload)
                elif kind == "status":
                    connected, msg = payload
                    self.status_var.set(msg)
                    self.status_lbl.configure(foreground="green" if connected else "red")
                    self.connect_btn.configure(state="disabled" if connected else "normal")
                    self.disconnect_btn.configure(state="normal" if connected else "disabled")
        except queue.Empty:
            pass
        self.root.after(50, self._poll_queue)

    def _on_close(self):
        self.client.disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    try:
        ttk.Style().theme_use("vista")
    except tk.TclError:
        pass
    MwctlApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
