#!/usr/bin/env python3
"""
WebSocket Protoo Session

Implements a per-connection session that speaks a minimal subset of the
"protoo"-like JSON protocol described in ws_design:

Message types (all JSON objects):

- request:
  {
    "request": true,
    "id": 12345678,
    "method": "chatmessage",
    "data": {"type": "text", "value": "Hi there!"}
  }

- successful response:
  {
    "response": true,
    "id": 12345678,
    "ok": true,
    "data": {"foo": "lalala"}
  }

- error response:
  {
    "response": true,
    "id": 12345678,
    "ok": false,
    "errorCode": 123,
    "errorReason": "Something failed"
  }

- notification:
  {
    "notification": true,
    "method": "chatmessage",
    "data": {"foo": "bar"}
  }
"""
from __future__ import annotations

import asyncio
import json
import logging
import random
from typing import Any, Dict, Optional, TYPE_CHECKING

import websockets
import time

from worker_mgr.worker_mgr import WorkerMgr

if TYPE_CHECKING:
    from ..msu.msg_mgr import MsuManager
    from agent_session import SessionMgr
    # Imported only for type checking to avoid circular import at runtime
    from .ws_protoo_server import WsProtooServer


class WsProtooSession:
    """A single WebSocket connection speaking protoo-style JSON messages."""

    def __init__(self, websocket: websockets.WebSocketServerProtocol, server: "WsProtooServer", peer: str,
                 logger: Optional[logging.Logger] = None,
                 session_mgr: Optional[object] = None,
                 worker_mgr: Optional[WorkerMgr] = None) -> None:
        self.websocket = websocket
        self.server = server
        self.peer = peer
        self.log = logger or logging.getLogger("ws_protoo_session")
        self.session_mgr = session_mgr  # Type: SessionMgr
        self._recv_task: Optional[asyncio.Task[None]] = None
        self._closed = asyncio.Event()
        # track participations: room_id -> set of user_ids this session represents
        # a single session may join multiple rooms and may represent different
        # user ids in each room (e.g., SFU scenarios)
        self.participations: Dict[str, set[str]] = {}
        # Track pending requests: req_id -> asyncio.Future for response
        self._pending_requests: Dict[int, asyncio.Future] = {}
        # Log the remote peer address so operators can see which endpoint connected.
        # Message kept concise and consistent with other session logs.

        self.worker_mgr = worker_mgr
        try:
            self.log.info("New protoo session connected from %s", self.peer)
        except Exception:
            # Logging must not throw during construction
            pass

        

    async def run(self) -> None:
        """Enter receive loop until connection is closed."""
        self.log.info("Session started: %s", self.peer)
        try:
            async for raw in self.websocket:
                await self._on_message(raw)
        except websockets.ConnectionClosedOK:
            self.log.info("Connection closed (OK): %s", self.peer)
        except websockets.ConnectionClosedError as e:
            self.log.warning("Connection closed (error) %s: %s", self.peer, e)
        except Exception as e:
            self.log.exception("Unhandled error in session %s: %s", self.peer, e)
        finally:
            await self.close()

    async def close(self) -> None:
        if not self._closed.is_set():
            self._closed.set()
            try:
                await self.websocket.close()
            except Exception:
                pass
            # unregister at server
            try:
                self.server.unregister(self)
            except Exception:
                pass
            self.log.info("Session closed: %s", self.peer)

    async def _on_message(self, raw: Any) -> None:
        # websockets yields str for text frames by default
        if not isinstance(raw, str):
            self.log.debug("Ignoring non-text frame from %s", self.peer)
            return
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            self.log.debug("Invalid JSON from %s: %r", self.peer, raw[:200])
            return
        if not isinstance(msg, dict):
            self.log.debug("Ignoring non-object JSON from %s", self.peer)
            return

        # Dispatch by top-level flags
        if msg.get("request") is True:
            await self._handle_request(msg)
        elif msg.get("response") is True:
            await self._handle_response(msg)
        elif msg.get("notification") is True:
            await self._handle_notification(msg)
        else:
            self.log.debug("Unknown message shape from %s: %s", self.peer, msg)

    async def _handle_request(self, msg: Dict[str, Any]) -> None:
        req_id = msg.get("id")
        method = msg.get("method")
        data = msg.get("data")

        if not isinstance(req_id, (int, str)):
            await self.send_response_error(req_id, 400, "Invalid id")
            return
        if not isinstance(method, str):
            await self.send_response_error(req_id, 400, "Invalid method")
            return

        try:
            if method == "echo":
                # Echo back the data received
                try:
                    if not isinstance(data, dict):
                        await self.send_response_error(req_id, 400, "Invalid data")
                        return
                    type_str = data.get("type")
                    if not isinstance(type_str, str):
                        await self.send_response_error(req_id, 400, "Invalid type")
                        return
                    ts = data.get("ts")
                    if not isinstance(ts, int):
                        await self.send_response_error(req_id, 400, "Invalid ts")
                        return
                    self.log.info(f"echo data: {data}")
                    if type_str == "voiceagent_worker":
                        self.worker_mgr.keepalive(ts, self)
                    await self.send_response_ok(req_id, {"echo": data})
                except Exception as e:
                    self.log.exception("Error handling echo request: %s", e)
                    await self.send_response_error(req_id, 500, "Internal error")

            elif method == "register":
                self.log.info(f"register data: {data}")
                msu_id = data.get("id") if isinstance(data, dict) else None
                logging.info("Register request: id=%s", msu_id)
                # validate id
                if not isinstance(msu_id, str) or not msu_id:
                    await self.send_response_error(req_id, 400, "Invalid id")
                    return

                # MSU registration not supported without msu_manager
                self.log.warning("MSU registration requested but no msu_manager configured")
                await self.send_response_error(req_id, 501, "MSU registration not supported")

            elif method == "join":
                roomId = data.get("roomId") if isinstance(data, dict) else None
                userId = data.get("userId") if isinstance(data, dict) else None
                userName = data.get("userName") if isinstance(data, dict) else None
                audience = data.get("audience")
                if not isinstance(audience, bool):
                    audience = False

                logging.info("User join request: roomId=%s, userId=%s, userName=%s, audience=%s", roomId, userId, userName, audience)
                # Room join not supported without room_manager
                self.log.debug("Room join requested but no room_manager configured")
                await self.send_response_error(req_id, 501, "Room join not supported")
            else:
                await self.send_response_error(req_id, 404, f"Unknown method: {method}")
        except Exception as e:
            self.log.exception("Error handling request %s: %s", method, e)
            await self.send_response_error(req_id, 500, "Internal error")

    async def send_response_ok(self, req_id: Any, data: Optional[Dict[str, Any]] = None) -> None:
        payload = {"response": True, "id": req_id, "ok": True, "data": data or {}}
        await self._send_json(payload)

    async def send_response_error(self, req_id: Any, code: int, reason: str) -> None:
        payload = {
            "response": True,
            "id": req_id,
            "ok": False,
            "errorCode": int(code),
            "errorReason": str(reason),
        }
        await self._send_json(payload)

    async def send_notification(self, method: str, data: Optional[Dict[str, Any]] = None) -> None:
        payload = {"notification": True, "method": method, "data": data or {}}
        await self._send_json(payload)

    async def _handle_response(self, msg: Dict[str, Any]) -> None:
        """Handle response messages from peer."""
        req_id = msg.get("id")
        if req_id not in self._pending_requests:
            self.log.debug("Received response for unknown request id: %s", req_id)
            return
        
        future = self._pending_requests.get(req_id)
        if future and not future.done():
            if msg.get("ok") is True:
                future.set_result(msg.get("data", {}))
            else:
                error_code = msg.get("errorCode", 0)
                error_reason = msg.get("errorReason", "Unknown error")
                future.set_exception(Exception(f"Request failed: [{error_code}] {error_reason}"))

    async def send_request(self, method: str, data: Optional[Dict[str, Any]] = None, timeout: float = 10.0) -> Dict[str, Any]:
        """Send a request to the peer and wait for response."""
        req_id = random.randint(10000000, 99999999)
        payload = {"request": True, "id": req_id, "method": method, "data": data or {}}
        
        # Create future to wait for response
        future: asyncio.Future = asyncio.Future()
        self._pending_requests[req_id] = future
        
        try:
            await self._send_json(payload)
            # Wait for response with timeout
            result = await asyncio.wait_for(future, timeout=timeout)
            return result
        except asyncio.TimeoutError:
            raise TimeoutError(f"Request {method} (id={req_id}) timed out after {timeout}s")
        finally:
            # Clean up pending request
            self._pending_requests.pop(req_id, None)

    async def _handle_opus_data(self, room_id: str, user_id: str, opus_base64: str) -> None:
        """Handle opus data from client in sfu """
        self.log.debug(f"websocket handle opus data from sfu room_id={room_id}, user_id={user_id}, opus_base64={opus_base64}")
        if self.worker_mgr:
            await self.worker_mgr._handle_opus_data(room_id, user_id, opus_base64, self)

    async def _handle_notification(self, msg: Dict[str, Any]) -> None:
        """Process client-sent notifications.

        Current behavior:
        - Log the notification with method and peer.
        - Optionally rebroadcast to other peers (disabled by default).
        """
        method = msg.get("method")
        data = msg.get("data")
        if not isinstance(method, str):
            self.log.error("Invalid notification method from %s: %r", self.peer, method)
            return
        # Log notification
        room_id = data.get("roomId")
        user_id = data.get("userId")
        self.log.debug("receive notification roomId:%s, userId:%s, method:%s", room_id, user_id, method)

        if method == "input_audio_buffer.append":
            audio_base64 = data.get("audio")
            if not isinstance(room_id, str) or not isinstance(user_id, str) or not isinstance(audio_base64, str):
                self.log.error("Invalid audio buffer notification from %s: %s", self.peer, data)
                return
            codec = data.get("codec")
            if not isinstance(codec, str):
                self.log.error("Invalid audio buffer notification from %s: %s", self.peer, data)
                return
            if codec == "opus":
                # handle opus data from client in sfu
                await self._handle_opus_data(room_id, user_id, audio_base64)
            else:
                self.log.error("Unsupported codec in audio buffer notification from %s: %s", self.peer, codec)
            # await self.session_mgr.input_audio_data(room_id, user_id, audio_base64, codec, self)
            # Here you can add code to process the audio buffer if needed
        elif method == "sfuheartbeat":
            self.log.info("Received sfuheartbeat from %s: %s", self.peer, json.dumps(data))
        elif method == "pcm_data":
            pcm_base64 = data.get("msg")
            if not isinstance(room_id, str) or not isinstance(user_id, str) or not isinstance(pcm_base64, str):
                self.log.error("Invalid pcm data notification from %s: %s", self.peer, data)
                return
            # handle pcm data from voice agent worker to client
            await self.handle_pcm_data(room_id, user_id, pcm_base64)
        elif method == "tts_opus_data":
            # handle tts opus data from client in sfu
            tts_opus_base64 = data.get("msg")
            if not isinstance(room_id, str) or not isinstance(user_id, str) or not isinstance(tts_opus_base64, str):
                self.log.error("Invalid tts opus data notification from %s: %s", self.peer, data)
                return
            # self.log.info("handle tts opus data:%s", json.dumps(data))
            task_index = data.get("taskIndex")
            if not isinstance(task_index, int):
                self.log.error("Invalid tts opus data notification from %s: %s", self.peer, data)
                return
            await self._handle_tts_opus_data(room_id, user_id, tts_opus_base64, task_index)
        else:
            self.log.error("Unhandled notification method from %s: %s", self.peer, method)
    async def _send_json(self, obj: Dict[str, Any]) -> None:
        try:
            await self.websocket.send(json.dumps(obj))
        except Exception as e:
            self.log.debug("Send failed to %s: %s", self.peer, e)

    async def handle_pcm_data(self, room_id: str, user_id: str, pcm_base64: str) -> None:
        """Send pcm data from voice agent worker to client."""
        self.log.debug("handle pcm data from voice agent worker: room_id=%s, user_id=%s, pcm_base64 len=%d", room_id, user_id, len(pcm_base64))
        session = self.worker_mgr.get_session(user_id)
        if session is None:
            self.log.error("session is None, can not send pcm data")
            return
        await self.session_mgr.input_audio_data(room_id, user_id, pcm_base64, "pcm", session)

    async def send_response_text2voiceagent_worker(self, room_id: str, user_id: str, resp_text: str) -> None:
        """Send response text to voice agent worker."""
        self.log.info("send response text to voice agent worker: room_id=%s, user_id=%s, resp_text=%s", room_id, user_id, resp_text)
        if self.worker_mgr is None:
            self.log.error("worker_mgr is None, can not send response text")
            return
        await self.worker_mgr.send_response_text2worker(room_id, user_id, resp_text)

    async def _handle_tts_opus_data(self, room_id: str, user_id: str, tts_opus_base64: str, task_index: int) -> None:
        """Handle tts opus data from client in sfu."""
        self.log.debug("handle tts opus data from client in sfu: room_id=%s, user_id=%s, tts_opus_base64 len=%d", room_id, user_id, len(tts_opus_base64))
        session = self.worker_mgr.get_session(user_id)
        if session is None:
            self.log.error("session is None, can not send tts opus data")
            return
        now_ms = int(time.time() * 1000)
        conversation_id = str(task_index)
        
        data = {
            "type": "tts_opus_data",
            "roomId": room_id,
            "userId": user_id,
            "msg": tts_opus_base64,
            "ms": now_ms,
            "conversationId": conversation_id
        }
        await session.send_notification("tts_opus_data", data)