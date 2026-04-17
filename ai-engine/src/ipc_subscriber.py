import json
import time
import logging
from typing import Generator, Optional
from dataclasses import dataclass
import win32file
import win32pipe
import pywintypes
from config import PipeConfig
logger = logging.getLogger("neuropace.ai.ipc_sub")
ERROR_FILE_NOT_FOUND = 2
ERROR_PIPE_BUSY = 231
ERROR_BROKEN_PIPE = 109
ERROR_NO_DATA = 232
ERROR_MORE_DATA = 234
@dataclass
class SubscriberStats:
    """Runtime statistics for the IPC subscriber."""
    frames_received: int = 0
    bytes_received: int = 0
    reconnect_count: int = 0
    parse_errors: int = 0
    last_frame_timestamp_us: int = 0
class NdjsonBuffer:
    """
    Accumulates raw bytes from the pipe and yields complete JSON lines.
    Named Pipes in PIPE_TYPE_BYTE mode do not guarantee message boundaries.
    This buffer handles partial reads by accumulating data and splitting on
    newline characters.
    """
    def __init__(self) -> None:
        self._buffer: str = ""
    def feed(self, data: str) -> list[dict]:
        """
        Feed raw string data into the buffer.
        Returns a list of parsed JSON objects (one per complete line).
        """
        self._buffer += data
        messages: list[dict] = []
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            line = line.strip()
            if not line:
                continue
            try:
                messages.append(json.loads(line))
            except json.JSONDecodeError as e:
                logger.warning("JSON parse error: %s (line: %.100s...)", e, line)
        if len(self._buffer) > 1_000_000:
            logger.error("NDJSON buffer overflow — clearing (1MB of unparsed data)")
            self._buffer = ""
        return messages
    def reset(self) -> None:
        self._buffer = ""
class IpcSubscriber:
    """
    Named Pipe client that reads TelemetryFrame data from the Telemetry module.
    Usage:
        subscriber = IpcSubscriber(pipe_config)
        for frame in subscriber.stream():
            process(frame)
    """
    def __init__(self, config: PipeConfig = PipeConfig()) -> None:
        self._config = config
        self._handle: Optional[int] = None
        self._buffer = NdjsonBuffer()
        self._connected = False
        self.stats = SubscriberStats()
    @property
    def connected(self) -> bool:
        return self._connected
    def connect(self) -> bool:
        """
        Attempt to connect to the telemetry Named Pipe.
        Returns True on success, False on failure.
        """
        if self._connected:
            return True
        try:
            self._handle = win32file.CreateFile(
                self._config.telemetry_pipe,
                win32file.GENERIC_READ,
                0,                          
                None,                       
                win32file.OPEN_EXISTING,    
                0,                          
                None,                       
            )
            self._connected = True
            self._buffer.reset()
            logger.info("Connected to telemetry pipe: %s", self._config.telemetry_pipe)
            return True
        except pywintypes.error as e:
            if e.winerror == ERROR_FILE_NOT_FOUND:
                logger.debug("Telemetry pipe not found — server not running yet")
            elif e.winerror == ERROR_PIPE_BUSY:
                logger.debug("Telemetry pipe busy — all instances in use")
            else:
                logger.warning("Pipe connect error: [%d] %s", e.winerror, e.strerror)
            self._connected = False
            return False
    def disconnect(self) -> None:
        """Close the pipe handle."""
        if self._handle is not None:
            try:
                win32file.CloseHandle(self._handle)
            except pywintypes.error:
                pass
            self._handle = None
        self._connected = False
        self._buffer.reset()
    def read_frames(self) -> list[dict]:
        """
        Read available data from the pipe and return parsed TelemetryFrame dicts.
        Returns an empty list if no data is available or on error.
        """
        if not self._connected or self._handle is None:
            return []
        try:
            _, bytes_avail, _ = win32pipe.PeekNamedPipe(self._handle, 0)
            if bytes_avail == 0:
                time.sleep(0.01) 
                return []
            hr, data = win32file.ReadFile(
                self._handle,
                min(bytes_avail, self._config.read_buffer_size),
            )
            if hr != 0:
                logger.warning("ReadFile returned non-zero HRESULT: %d", hr)
                self.disconnect()
                return []
            if not data:
                return []
            decoded = data.decode("utf-8", errors="replace")
            self.stats.bytes_received += len(data)
            frames = self._buffer.feed(decoded)
            self.stats.frames_received += len(frames)
            if frames:
                self.stats.last_frame_timestamp_us = frames[-1].get("timestamp_us", 0)
            return frames
        except pywintypes.error as e:
            if e.winerror in (ERROR_BROKEN_PIPE, ERROR_NO_DATA):
                self.disconnect()
            else:
                logger.error("Pipe read error: [%d] %s", e.winerror, e.strerror)
                self.disconnect()
            return []
    def stream(self) -> Generator[dict, None, None]:
        """
        Generator that yields TelemetryFrame dicts indefinitely.
        Automatically reconnects on pipe disconnection.
        Yields:
            dict: Parsed TelemetryFrame JSON object.
        """
        logger.info("Starting telemetry stream (pipe: %s)", self._config.telemetry_pipe)
        while True:
            if not self._connected:
                if not self.connect():
                    time.sleep(self._config.reconnect_interval_sec)
                    continue
            frames = self.read_frames()
            if frames:
                for frame in frames:
                    yield frame
            else:
                time.sleep(0.001)  
    def _handle_disconnect(self) -> None:
        """Handle a pipe disconnection event."""
        self.disconnect()
        self.stats.reconnect_count += 1
        logger.info(
            "Disconnected (total reconnects: %d, frames received: %d)",
            self.stats.reconnect_count,
            self.stats.frames_received,
        )
