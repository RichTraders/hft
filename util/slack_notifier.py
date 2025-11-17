#!/usr/bin/env python3
"""
HFT Service Monitor & Slack Notifier

Supports both Supervisor eventlistener and systemd D-Bus monitoring.

Usage:
  # Supervisor mode (as eventlistener)
  ./slack_notifier.py supervisor

  # Systemd mode (monitor via D-Bus)
  ./slack_notifier.py systemd [--service hft-engine.service]
"""

import os
import sys
import json
import argparse
import requests
from abc import ABC, abstractmethod
from typing import Optional


class SlackNotifier:
    """Slack notification handler"""

    def __init__(self, webhook_url: Optional[str] = None):
        self.webhook_url = webhook_url or os.environ.get("SLACK_WEBHOOK_URL")
        if not self.webhook_url:
            print("[WARN] SLACK_WEBHOOK_URL not set. Notifications will be logged only.", file=sys.stderr)

    def send(self, text: str, fallback: bool = True) -> bool:
        """Send message to Slack

        Args:
            text: Message text
            fallback: If True, print to stderr when webhook is not configured

        Returns:
            True if sent successfully, False otherwise
        """
        if not self.webhook_url:
            if fallback:
                print(f"[SLACK] {text}", file=sys.stderr)
            return False

        try:
            resp = requests.post(
                self.webhook_url,
                json={"text": text},
                timeout=5
            )
            if resp.status_code >= 300:
                print(f"[ERROR] Slack webhook failed: {resp.status_code} {resp.text}", file=sys.stderr)
                return False
            return True
        except Exception as e:
            print(f"[ERROR] Slack webhook exception: {e}", file=sys.stderr)
            return False


class Monitor(ABC):
    """Abstract base class for service monitors"""

    def __init__(self, notifier: SlackNotifier):
        self.notifier = notifier

    @abstractmethod
    def run(self):
        """Run the monitor loop"""
        pass


class SupervisorEventListener(Monitor):
    """Supervisor eventlistener protocol handler"""

    EVENTS_OF_INTEREST = {
        "PROCESS_STATE_EXITED",
        "PROCESS_STATE_FATAL",
        "PROCESS_STATE_BACKOFF",
        "PROCESS_STATE_STOPPED",
    }

    EVENT_EMOJIS = {
        "PROCESS_STATE_EXITED": lambda expected: "🔁" if expected == "1" else "💥",
        "PROCESS_STATE_FATAL": "🛑",
        "PROCESS_STATE_BACKOFF": "⚠️",
        "PROCESS_STATE_STOPPED": "⏹️",
    }

    @staticmethod
    def parse_headers(line: str) -> dict:
        """Parse supervisor header line

        Example:
            "ver:3 server:supervisor serial:16 pool:slack_notifier poolserial:1 eventname:PROCESS_STATE_EXITED len:192"
        """
        parts = [kv.split(":", 1) for kv in line.strip().split()]
        return {k: v for k, v in parts if len(k) > 0 and len(v) > 0}

    def handle_event(self, header: str, payload: str):
        """Handle supervisor event and send notification if needed"""
        hdrs = self.parse_headers(header)

        # Parse payload
        if "\n" in payload:
            kvline, _data = payload.split("\n", 1)
        else:
            kvline, _data = payload, ""

        fields = self.parse_headers(kvline.replace(" ", "\n"))

        event = hdrs.get("eventname", "UNKNOWN")
        pname = fields.get("processname", "?")
        gname = fields.get("groupname", "?")
        fstate = fields.get("from_state", "?")
        pid = fields.get("pid", "?")
        expected = fields.get("expected", "0")

        # Send notification for events of interest
        if event in self.EVENTS_OF_INTEREST:
            emoji_fn = self.EVENT_EMOJIS.get(event, "ℹ️")
            emoji = emoji_fn(expected) if callable(emoji_fn) else emoji_fn

            text = (
                f"{emoji} *{event}*  "
                f"`{gname}:{pname}` pid={pid} from_state={fstate} expected={expected}"
            )
            self.notifier.send(text)

    def run(self):
        """Run supervisor eventlistener loop"""
        print("[INFO] Starting Supervisor eventlistener mode", file=sys.stderr)

        while True:
            # Send READY signal
            sys.stdout.write("READY\n")
            sys.stdout.flush()

            # Read header
            header = sys.stdin.readline()
            if not header:
                break

            hdrs = self.parse_headers(header)
            length = int(hdrs.get("len", "0"))

            # Read payload
            payload = sys.stdin.read(length) if length > 0 else ""

            # Handle event
            try:
                self.handle_event(header, payload)
            except Exception as e:
                print(f"[ERROR] Event handling failed: {e}", file=sys.stderr)

            # Send RESULT
            sys.stdout.write("RESULT 2\nOK")
            sys.stdout.flush()


class SystemdDBusMonitor(Monitor):
    """Systemd D-Bus service monitor"""

    STATE_EMOJIS = {
        "failed": "💥",
        "inactive": "⏹️",
        "activating": "🔄",
        "deactivating": "🔄",
    }

    def __init__(self, notifier: SlackNotifier, service_name: str = "hft-engine.service"):
        super().__init__(notifier)
        self.service_name = service_name
        self.previous_state = None

        try:
            from gi.repository import GLib
            from pydbus import SystemBus
            self.GLib = GLib
            self.SystemBus = SystemBus
        except ImportError:
            print("[ERROR] pydbus and GLib are required for systemd mode", file=sys.stderr)
            print("[ERROR] Install: pip install pydbus PyGObject", file=sys.stderr)
            sys.exit(1)

    def on_properties_changed(self, interface, changed, invalidated):
        """Callback for D-Bus PropertiesChanged signal"""
        if "ActiveState" in changed:
            new_state = changed["ActiveState"]

            # Skip initial state
            if self.previous_state is None:
                self.previous_state = new_state
                print(f"[INFO] Initial state: {new_state}", file=sys.stderr)
                return

            # State changed
            if new_state != self.previous_state:
                print(f"[INFO] State changed: {self.previous_state} -> {new_state}", file=sys.stderr)

                # Send notification
                emoji = self.STATE_EMOJIS.get(new_state, "ℹ️")
                text = (
                    f"{emoji} *Systemd Service State Changed*\n"
                    f"Service: `{self.service_name}`\n"
                    f"Previous: `{self.previous_state}` → New: `{new_state}`"
                )
                self.notifier.send(text)

                self.previous_state = new_state

        if "SubState" in changed:
            sub_state = changed["SubState"]
            if sub_state == "failed":
                # Get failure details
                try:
                    bus = self.SystemBus()
                    systemd = bus.get(".systemd1")
                    unit_path = systemd.GetUnit(self.service_name)
                    unit = bus.get(".systemd1", unit_path)

                    result = unit.Get("org.freedesktop.systemd1.Service", "Result")
                    exit_code = unit.Get("org.freedesktop.systemd1.Service", "ExecMainStatus")

                    text = (
                        f"🛑 *Service Failed*\n"
                        f"Service: `{self.service_name}`\n"
                        f"Result: `{result}`\n"
                        f"Exit Code: `{exit_code}`"
                    )
                    self.notifier.send(text)
                except Exception as e:
                    print(f"[WARN] Failed to get failure details: {e}", file=sys.stderr)

    def run(self):
        """Run systemd D-Bus monitor loop"""
        print(f"[INFO] Starting Systemd D-Bus monitor for {self.service_name}", file=sys.stderr)

        try:
            bus = self.SystemBus()
            systemd = bus.get(".systemd1")

            # Get unit path
            try:
                unit_path = systemd.GetUnit(self.service_name)
            except Exception as e:
                print(f"[ERROR] Service not found: {self.service_name}", file=sys.stderr)
                print(f"[ERROR] {e}", file=sys.stderr)
                sys.exit(1)

            # Get unit proxy
            unit = bus.get(".systemd1", unit_path)

            # Get initial state
            self.previous_state = unit.Get("org.freedesktop.systemd1.Unit", "ActiveState")
            print(f"[INFO] Current state: {self.previous_state}", file=sys.stderr)

            # Subscribe to PropertiesChanged signal
            unit.PropertiesChanged.connect(self.on_properties_changed)

            # Send startup notification
            text = (
                f"✅ *Systemd Monitor Started*\n"
                f"Service: `{self.service_name}`\n"
                f"State: `{self.previous_state}`"
            )
            self.notifier.send(text)

            # Run GLib main loop
            loop = self.GLib.MainLoop()
            print("[INFO] Monitoring... (Press Ctrl+C to stop)", file=sys.stderr)
            loop.run()

        except KeyboardInterrupt:
            print("\n[INFO] Stopped by user", file=sys.stderr)
        except Exception as e:
            print(f"[ERROR] Monitor failed: {e}", file=sys.stderr)
            sys.exit(1)


class MonitorFactory:
    """Factory for creating monitor instances"""

    _monitors = {
        "supervisor": SupervisorEventListener,
        "systemd": SystemdDBusMonitor,
    }

    @classmethod
    def register(cls, mode: str, monitor_class: type):
        """Register a new monitor type

        Args:
            mode: Monitor mode name (e.g., 'supervisor', 'systemd')
            monitor_class: Monitor class (must inherit from Monitor)
        """
        if not issubclass(monitor_class, Monitor):
            raise TypeError(f"{monitor_class} must inherit from Monitor")
        cls._monitors[mode] = monitor_class

    @classmethod
    def create(cls, mode: str, notifier: SlackNotifier, **kwargs) -> Monitor:
        """Create a monitor instance

        Args:
            mode: Monitor mode ('supervisor' or 'systemd')
            notifier: SlackNotifier instance
            **kwargs: Additional arguments for the monitor (e.g., service_name)

        Returns:
            Monitor instance

        Raises:
            ValueError: If mode is not supported
        """
        monitor_class = cls._monitors.get(mode)
        if not monitor_class:
            raise ValueError(f"Unknown monitor mode: {mode}. Available: {list(cls._monitors.keys())}")

        # Create monitor with appropriate arguments
        if mode == "systemd":
            service_name = kwargs.get("service_name", "hft-engine.service")
            return monitor_class(notifier, service_name)
        else:
            return monitor_class(notifier)

    @classmethod
    def available_modes(cls) -> list:
        """Get list of available monitor modes"""
        return list(cls._monitors.keys())


def main():
    parser = argparse.ArgumentParser(
        description="HFT Service Monitor & Slack Notifier",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Supervisor mode
  ./slack_notifier.py supervisor

  # Systemd mode
  ./slack_notifier.py systemd
  ./slack_notifier.py systemd --service hft-engine.service

Environment Variables:
  SLACK_WEBHOOK_URL    Slack webhook URL for notifications
        """
    )

    parser.add_argument(
        "mode",
        choices=MonitorFactory.available_modes(),
        help=f"Monitor mode: {', '.join(MonitorFactory.available_modes())}"
    )

    parser.add_argument(
        "--service",
        default="hft-engine.service",
        help="Systemd service name to monitor (default: hft-engine.service)"
    )

    parser.add_argument(
        "--webhook-url",
        help="Slack webhook URL (overrides SLACK_WEBHOOK_URL env)"
    )

    args = parser.parse_args()

    # Create notifier
    notifier = SlackNotifier(webhook_url=args.webhook_url)

    # Create and run monitor using factory
    try:
        monitor = MonitorFactory.create(
            mode=args.mode,
            notifier=notifier,
            service_name=args.service
        )
        monitor.run()
    except ValueError as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
