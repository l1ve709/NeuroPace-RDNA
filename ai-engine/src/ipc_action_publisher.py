from __future__ import annotations
import json
import logging
import threading
import time
from dataclasses import dataclass
from typing import Optional
import win32file
import win32pipe
import win32event
import win32security
import pywintypes
from config import PipeConfig
logger = logging.getLogger("neuropace.ai.ipc_pub")
ERROR_BROKEN_PIPE = 109
ERROR_NO_DATA = 232
ERROR_PIPE_NOT_CONNECTED = 233
ERROR_IO_PENDING = 997
ERROR_PIPE_CONNECTED = 535
@dataclass
class PublisherStats:
    """Runtime statistics for a single pipe publisher."""
    messages_published: int = 0
    bytes_written: int = 0
    client_connects: int = 0
    client_disconnects: int = 0
    write_errors: int = 0
    write_timeouts: int = 0
class PipeServer:
    """
    A single Named Pipe server instance that accepts one client at a time.
    For multi-client support, create multiple PipeServer instances with
    the same pipe name (Windows supports overlapping pipe instances).

    Both the accept path and the write path use overlapped (asynchronous)
    I/O so that stop() can interrupt a pending ConnectNamedPipe and a slow
    client can never block the publisher thread indefinitely.
    """
    #: How long a single publish() may wait for a stalled client (ms).
    WRITE_TIMEOUT_MS: int = 100

    def __init__(
        self,
        pipe_name: str,
        buffer_size: int = 65536,
        label: str = "pipe",
    ) -> None:
        self._pipe_name = pipe_name
        self._buffer_size = buffer_size
        self._label = label
        self._handle: Optional[int] = None
        self._connected = False
        self._lock = threading.Lock()
        self._accept_thread: Optional[threading.Thread] = None
        self._running = False
        self._shutdown_event: Optional[int] = None
        self._write_event: Optional[int] = None
        self._write_ov = None
        self.stats = PublisherStats()
    @property
    def connected(self) -> bool:
        with self._lock:
            return self._connected
    @property
    def pipe_name(self) -> str:
        return self._pipe_name
    def start(self) -> None:
        """Start the background thread that waits for client connections."""
        if self._running:
            return
        self._running = True
        self._shutdown_event = win32event.CreateEvent(None, True, False, None)
        self._accept_thread = threading.Thread(
            target=self._accept_loop,
            name=f"pipe-{self._label}",
            daemon=True,
        )
        self._accept_thread.start()
        logger.info("[%s] Pipe server started: %s", self._label, self._pipe_name)
    def stop(self) -> None:
        """Stop the server and disconnect any client."""
        self._running = False
        if self._shutdown_event is not None:
            win32event.SetEvent(self._shutdown_event)
        self._disconnect()
        if self._accept_thread and self._accept_thread.is_alive():
            self._accept_thread.join(timeout=3.0)
        thread_alive = self._accept_thread is not None and self._accept_thread.is_alive()
        self._accept_thread = None
        if self._shutdown_event is not None and not thread_alive:
            try:
                win32file.CloseHandle(self._shutdown_event)
            except pywintypes.error:
                pass
            self._shutdown_event = None
        logger.info("[%s] Pipe server stopped", self._label)
    def publish(self, data: dict) -> bool:
        """
        Publish a JSON message to the connected client using an overlapped
        write with a bounded timeout (a stalled client cannot block us).
        Args:
            data: Dictionary to serialize as JSON.
        Returns:
            True if data was written successfully, False otherwise.
        """
        with self._lock:
            if not self._connected or self._handle is None:
                return False
            payload = json.dumps(data, separators=(",", ":")) + "\n"
            payload_bytes = payload.encode("utf-8")
            if self._write_overlapped(payload_bytes):
                self.stats.messages_published += 1
                self.stats.bytes_written += len(payload_bytes)
                return True
            return False

    def _write_overlapped(self, payload_bytes: bytes) -> bool:
        """
        Overlapped WriteFile bounded by WRITE_TIMEOUT_MS.
        Must be called with self._lock held. On failure the client is
        marked disconnected and its handles are released.
        """
        try:
            win32event.ResetEvent(self._write_event)
            hr, _ = win32file.WriteFile(self._handle, payload_bytes, self._write_ov)
        except pywintypes.error as e:
            hr = e.winerror
        if hr == ERROR_IO_PENDING:
            wait = win32event.WaitForMultipleObjects(
                [self._write_event], False, self.WRITE_TIMEOUT_MS
            )
            if wait != win32event.WAIT_OBJECT_0:
                self.stats.write_timeouts += 1
                logger.warning(
                    "[%s] Write timed out after %dms - disconnecting slow client",
                    self._label,
                    self.WRITE_TIMEOUT_MS,
                )
                try:
                    win32file.CancelIo(self._handle)
                except pywintypes.error:
                    pass
                try:
                    win32file.GetOverlappedResult(self._handle, self._write_ov, True)
                except pywintypes.error:
                    pass
                self._connected = False
                self._cleanup_handle()
                return False
            try:
                win32file.GetOverlappedResult(self._handle, self._write_ov, False)
                return True
            except pywintypes.error as e:
                hr = e.winerror
        if hr == 0:
            return True  # completed synchronously
        if hr in (ERROR_BROKEN_PIPE, ERROR_NO_DATA, ERROR_PIPE_NOT_CONNECTED):
            logger.info("[%s] Client disconnected during write", self._label)
        else:
            logger.warning("[%s] Write error: [%d]", self._label, hr)
        self.stats.write_errors += 1
        self._connected = False
        self._cleanup_handle()
        return False
    def _accept_loop(self) -> None:
        """
        Background loop: create pipe → wait for client → serve until disconnect → repeat.
        """
        sa = win32security.SECURITY_ATTRIBUTES()
        sa.bInheritHandle = 0
        sa.SECURITY_DESCRIPTOR = win32security.ConvertStringSecurityDescriptorToSecurityDescriptor(
            "D:(A;;GA;;;WD)", win32security.SDDL_REVISION_1
        )
        while self._running:
            try:
                handle = win32pipe.CreateNamedPipe(
                    self._pipe_name,
                    win32pipe.PIPE_ACCESS_OUTBOUND | win32file.FILE_FLAG_OVERLAPPED,
                    (
                        win32pipe.PIPE_TYPE_BYTE
                        | win32pipe.PIPE_READMODE_BYTE
                        | win32pipe.PIPE_WAIT
                    ),
                    win32pipe.PIPE_UNLIMITED_INSTANCES,
                    self._buffer_size,
                    0,      
                    0,      
                    sa,   
                )
            except pywintypes.error as e:
                logger.error(
                    "[%s] CreateNamedPipe failed: [%d] %s",
                    self._label, e.winerror, e.strerror,
                )
                time.sleep(1.0)
                continue
            logger.debug("[%s] Waiting for client connection...", self._label)
            ov = pywintypes.OVERLAPPED()
            ov.hEvent = win32event.CreateEvent(None, True, False, None)
            connected = False
            try:
                result = win32pipe.ConnectNamedPipe(handle, ov)
                if result == ERROR_IO_PENDING:
                    handles = [ov.hEvent]
                    if self._shutdown_event is not None:
                        handles.append(self._shutdown_event)
                    wait = win32event.WaitForMultipleObjects(
                        handles, False, win32event.INFINITE
                    )
                    if wait != win32event.WAIT_OBJECT_0:
                        # stop() requested while waiting for a client
                        try:
                            win32file.CloseHandle(handle)
                        except pywintypes.error:
                            pass
                        return  # finally closes ov.hEvent
                    win32file.GetOverlappedResult(handle, ov, False)
                    connected = True
                elif result in (None, 0) or result == ERROR_PIPE_CONNECTED:
                    # Completed synchronously / client already connected.
                    connected = True
                else:
                    logger.warning(
                        "[%s] ConnectNamedPipe returned %d", self._label, result
                    )
            except pywintypes.error as e:
                if e.winerror == ERROR_PIPE_CONNECTED:
                    connected = True
                else:
                    if self._running:
                        logger.warning(
                            "[%s] ConnectNamedPipe error: [%d] %s",
                            self._label, e.winerror, e.strerror,
                        )
            finally:
                try:
                    win32file.CloseHandle(ov.hEvent)
                except pywintypes.error:
                    pass
            if not connected:
                try:
                    win32file.CloseHandle(handle)
                except pywintypes.error:
                    pass
                if self._running:
                    time.sleep(0.1)
                    continue
                break
            with self._lock:
                self._handle = handle
                self._write_event = win32event.CreateEvent(None, True, False, None)
                self._write_ov = pywintypes.OVERLAPPED()
                self._write_ov.hEvent = self._write_event
                self._connected = True
                self.stats.client_connects += 1
            logger.info(
                "[%s] Client connected (total connects: %d)",
                self._label, self.stats.client_connects,
            )
            while self._running and self.connected:
                time.sleep(0.1)
            self._disconnect()
            self.stats.client_disconnects += 1
    def _disconnect(self) -> None:
        """Disconnect the current client and close the pipe handle."""
        with self._lock:
            self._connected = False
            self._cleanup_handle()
    def _cleanup_handle(self) -> None:
        """Close the pipe/write handles (must be called with lock held)."""
        if self._handle is not None:
            try:
                win32pipe.DisconnectNamedPipe(self._handle)
            except pywintypes.error:
                pass
            try:
                win32file.CloseHandle(self._handle)
            except pywintypes.error:
                pass
            self._handle = None
        if self._write_event is not None:
            try:
                win32file.CloseHandle(self._write_event)
            except pywintypes.error:
                pass
            self._write_event = None
        self._write_ov = None
class IpcActionPublisher:
    """
    Manages two Named Pipe servers:
      1. Action pipe    → sends ActionCommand to Actuator
      2. Prediction pipe → sends PredictionResult to Dashboard
    Usage:
        publisher = IpcActionPublisher(pipe_config)
        publisher.start()
        result = predictor.predict(features)
        publisher.publish_action(result.to_dict())
        publisher.publish_prediction(result.to_dict())
    """
    def __init__(self, config: PipeConfig = PipeConfig()) -> None:
        self._config = config
        self._action_server = PipeServer(
            pipe_name=config.action_pipe,
            buffer_size=config.write_buffer_size,
            label="action",
        )
        self._prediction_server = PipeServer(
            pipe_name=config.prediction_pipe,
            buffer_size=config.write_buffer_size,
            label="prediction",
        )
    def start(self) -> None:
        """Start both pipe servers."""
        self._action_server.start()
        self._prediction_server.start()
    def stop(self) -> None:
        """Stop both pipe servers."""
        self._action_server.stop()
        self._prediction_server.stop()
    def publish_action(self, data: dict) -> bool:
        """Publish an ActionCommand to the Actuator pipe."""
        return self._action_server.publish(data)
    def publish_prediction(self, data: dict) -> bool:
        """Publish a PredictionResult to the Dashboard pipe."""
        return self._prediction_server.publish(data)
    @property
    def action_connected(self) -> bool:
        return self._action_server.connected
    @property
    def prediction_connected(self) -> bool:
        return self._prediction_server.connected
