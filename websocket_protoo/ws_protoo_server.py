#!/usr/bin/env python3
"""
WebSocket Protoo Server

Starts a WebSocket server listening on configurable host/port. Upon a new
connection, creates a WsProtooSession to handle protoo-like JSON messages.
Optionally supports TLS when given certificate and key paths.
"""
from __future__ import annotations

import asyncio
import logging
import ssl
from dataclasses import dataclass
from typing import Any, Dict, Optional, Set, TYPE_CHECKING

import websockets
from worker_mgr.worker_mgr import WorkerMgr

from .ws_protoo_session import WsProtooSession

if TYPE_CHECKING:
    from agent_session import SessionMgr


@dataclass
class ServerOptions:
    host: str = "0.0.0.0"
    port: int = 8443
    cert_path: Optional[str] = None
    key_path: Optional[str] = None
    subpath: str = "/"


class WsProtooServer:
    """Manage WebSocket listener and connected sessions.
    """

    def __init__(self, options: ServerOptions, 
                 logger: Optional[logging.Logger] = None,
                 session_mgr: Optional[object] = None,
                 worker_mgr: Optional[WorkerMgr] = None) -> None:
        self.opts = options
        self.log = logger or logging.getLogger("ws_protoo_server")
        self.session_mgr = session_mgr  # Type: SessionMgr
        self._server: Optional[websockets.server.Serve] = None
        self._sessions: Set[WsProtooSession] = set()
        self.worker_mgr = worker_mgr

    async def start(self) -> None:
        ssl_ctx = self._maybe_build_ssl_context(self.opts.cert_path, self.opts.key_path)
        self._server = await websockets.serve(
            self._on_connect,
            host=self.opts.host,
            port=self.opts.port,
            ssl=ssl_ctx,
            ping_interval=30,
            ping_timeout=20,
            max_queue=32,
        )
        self.log.info("WebSocket server listening on %s:%d%s",
                      self.opts.host, self.opts.port, " (TLS)" if ssl_ctx else "")

    async def stop(self) -> None:
        # Close all sessions
        for s in list(self._sessions):
            await s.close()
        self._sessions.clear()

        # Stop server
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        self.log.info("WebSocket server stopped")

    def unregister(self, session: WsProtooSession) -> None:
        self._sessions.discard(session)

    async def _on_connect(self, websocket: websockets.WebSocketServerProtocol) -> None:
        # Compatibility with different websockets versions
        path = getattr(websocket, 'path', None)
        if path is None and hasattr(websocket, 'request'):
             path = websocket.request.path

        if self.opts.subpath and path != self.opts.subpath:
            self.log.warning("Rejecting connection: unexpected path %s (expected %s)", path, self.opts.subpath)
            # 关闭连接（握手后立即断开）
            await websocket.close(code=1008, reason="invalid path")
            return

        peer = self._format_peer(websocket)
        session = WsProtooSession(websocket, self, peer, logger=self.log, session_mgr=self.session_mgr, worker_mgr=self.worker_mgr)
        self._sessions.add(session)
        self.log.info("Client connected: %s (path=%s)", peer, path)
        try:
            await session.run()
        finally:
            self.unregister(session)
            self.log.info("Client disconnected: %s", peer)

    async def broadcast_notification(self, method: str, data: Optional[Dict[str, Any]] = None) -> None:
        # Fire-and-forget best effort broadcast
        coros = [s.send_notification(method, data) for s in list(self._sessions)]
        if coros:
            await asyncio.gather(*coros, return_exceptions=True)

    @staticmethod
    def _maybe_build_ssl_context(cert_path: Optional[str], key_path: Optional[str]) -> Optional[ssl.SSLContext]:
        if not cert_path or not key_path:
            return None
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=cert_path, keyfile=key_path)
        return ctx

    @staticmethod
    def _format_peer(ws: websockets.WebSocketServerProtocol) -> str:
        try:
            host, port, *_ = ws.remote_address  # type: ignore[misc]
            return f"{host}:{port}"
        except Exception:
            return "unknown"


async def run_server_forever(opts: ServerOptions) -> None:
    """Convenience helper to start server and block forever (Ctrl+C to stop).
    """
    logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(levelname)s: %(message)s")
    server = WsProtooServer(opts)
    await server.start()
    try:
        while True:
            await asyncio.sleep(3600)
    except (asyncio.CancelledError, KeyboardInterrupt):
        pass
    finally:
        await server.stop()
