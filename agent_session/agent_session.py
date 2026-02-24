#!/usr/bin/env python3
"""
Agent Session

Handles voice agent session for a specific user in a room.
Processes audio data received from WebSocket Protoo Session.
"""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Optional, Any, Dict, TYPE_CHECKING
from ten_vad import TenVad
import numpy as np
from utils.model_manager import ASRModelManager
from config.config import Config
#from llm_client.llm_client import LLMClient
from llm_client.langchain_client import LangChainClient
import os
import datetime

if TYPE_CHECKING:
    from websocket_protoo.ws_protoo_session import WsProtooSession

def detect_speech_boundary(
    out_flag,
    state,
    min_start_frames=3,
    min_end_frames=5,
    frame_index=None,
):
    """
    仅用 out_flag 连续判断语音开始/结束。
    连续 n 个 1 触发开始，连续 m 个 0 触发结束。
    记录开始/结束的起始帧位置到 state['start_frame'] / state['end_frame']。
    返回 (start_frame, end_frame)。
    """
    start_frame = None
    end_frame = None

    if not state["in_speech"]:
        if out_flag == 1:
            if state["start_count"] == 0 and frame_index is not None:
                state["start_frame"] = frame_index
            state["start_count"] += 1
            if state["start_count"] >= min_start_frames:
                state["in_speech"] = True
                state["end_count"] = 0
                start_frame = state["start_frame"]
        else:
            state["start_count"] = 0
            state["start_frame"] = None
            state["segment_frames"] = []
    else:
        if out_flag == 0:
            if state["end_count"] == 0 and frame_index is not None:
                state["end_frame"] = frame_index
            state["end_count"] += 1
            if state["end_count"] >= min_end_frames:
                state["in_speech"] = False
                state["start_count"] = 0
                end_frame = state["end_frame"]
        else:
            state["end_count"] = 0
            state["end_frame"] = None

    return start_frame, end_frame

class AgentSession:
    """
    Agent Session for processing user audio data.
    
    Each agent session represents a single user in a room and handles
    audio processing and communication with that user through a WebSocket session.
    """
    
    def __init__(
        self,
        room_id: str,
        user_id: str,
        ws_session: object,
        config: Config,
        logger: Optional[logging.Logger] = None
    ) -> None:
        """
        Initialize Agent Session.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            ws_session (object): WebSocket Protoo Session object (typed as object to avoid circular imports)
            config (Config): Configuration instance
            logger (Optional[logging.Logger]): Logger instance
        """
        self.room_id = room_id
        self.user_id = user_id
        self.ws_session = ws_session  # Type: WsProtooSession
        self.config = config
        self.log = logger or logging.getLogger(f"agent_session.{room_id}.{user_id}")
        self.msg_index = 0
        self.conversation_id = None

        # Audio processing state
        self._audio_buffer = bytearray()
        self._is_processing = False
        self._closed = False
        self.sample_rate = 16000  # Default sample rate
        self.hop_size = 160  # 10 ms per frame
        self.threshold = 0.5
        self.ten_vad_instance = TenVad(self.hop_size, self.threshold)  # Create a TenVad instance

        # Background tasks
        self._processing_task: Optional[asyncio.Task] = None
        self.vad_state = {
            "in_speech": False,
            "start_count": 0,
            "end_count": 0,
            "start_frame": None,
            "end_frame": None,
            "segment_start": None,
            "segment_frames": [],
        }
        
        # Get shared funasr model instance
        self.recognize_model = ASRModelManager.get_instance().get_model()

        # Initialize LLM client by configuration
        llm_type = self.config.get("llm_config.llm_type", "qwen")
        modle_name = self.config.get("llm_config.model_name", "")
        self.llm_client = LangChainClient.create_from_type(
            llm_type=llm_type,
            api_key=os.getenv("LLM_API_KEY", ""),
            messages_max_length=20,
        )
    
    def start(self) -> None:
        """Start the agent session and begin processing."""
        if self._processing_task is not None and not self._processing_task.done():
            self.log.warning("Agent session already running")
            return
        
        self.log.info("Starting agent session")
        # Reset closed flag to allow restarts
        self._closed = False
        self._processing_task = asyncio.create_task(self._process_loop())
    
    async def stop(self) -> None:
        """Stop the agent session and cleanup resources."""
        if self._closed:
            return
        
        self.log.info("Stopping agent session")
        self._closed = True
        
        # Cancel processing task
        if self._processing_task is not None:
            self._processing_task.cancel()
            try:
                await self._processing_task
            except asyncio.CancelledError:
                pass
            self._processing_task = None
        
        # Clear audio buffer
        self._audio_buffer.clear()
        
        self.log.info("Agent session stopped")
    
    async def handle_audio_data(self, audio_data: bytes) -> None:
        """
        Handle incoming audio data from WebSocket session.
        
        Args:
            audio_data (bytes): Raw audio data
        """
        if self._closed:
            self.log.warning("Ignoring audio data - session is closed")
            return
        # write audio data to file for testing
        # with open(f"audio_{self.room_id}_{self.user_id}.pcm", "ab") as f:
        #     f.write(audio_data)
        #     f.close()

        # Add to buffer
        self._audio_buffer.extend(audio_data)
    
    def ten_vad_handle_audio_data(self, speech: bytes, loop: asyncio.AbstractEventLoop) -> None:
        """
        Handle incoming audio data using TenVad for voice activity detection.
        
        Args:
            audio_data (bytes): Raw audio data
        """
        if self._closed:
            self.log.warning("Ignoring audio data - session is closed")
            return
        try:
            # Assuming 16-bit PCM, 2 bytes per sample
            bytes_per_sample = 2
            frame_size_bytes = self.hop_size * bytes_per_sample
            
            # 遍历speech数据，按 frame_size_bytes 分割成帧
            for i in range(0, len(speech), frame_size_bytes):
                # 防治最后一帧不足 frame_size_bytes
                frame_bytes = speech[i:i + frame_size_bytes]
                if len(frame_bytes) != frame_size_bytes:
                    self.log.warning(f"Frame {i}: Incomplete frame with {len(frame_bytes)} bytes, padding with zeros")
                    frame_bytes = frame_bytes + b'\x00' * (frame_size_bytes - len(frame_bytes))
                
                # Convert to numpy array (int16)
                frame_np = np.frombuffer(frame_bytes, dtype=np.int16)
                frame_vector = frame_np.copy()
                
                # VAD检测
                # self.log.info(f"Frame {i}: Input Frame shape={frame_np.shape}")
                out_probability, out_flag = self.ten_vad_instance.process(frame_np)
                
                # Calculate frame index (relative to this chunk)
                frame_index = i // frame_size_bytes
                
                start_frame, end_frame = detect_speech_boundary(
                        out_flag,
                        self.vad_state,
                        min_start_frames=20,
                        min_end_frames=160,
                        frame_index=frame_index,
                    )
    
                # 如果还未进入说话状态，但开始计数，也要收集帧
                if not self.vad_state["in_speech"] and self.vad_state["start_count"] > 0:
                    self.vad_state["segment_frames"].append(frame_vector)
                
                if start_frame is not None:
                    self.log.info(f"Frame {i}: Speech Start (start_frame={start_frame})")
                    self.vad_state["segment_start"] = start_frame
                    asyncio.run_coroutine_threadsafe(self.send_conversation_start(), loop)
                
                # 如果在说话，持续收集音频帧
                if self.vad_state["in_speech"]:
                    self.vad_state["segment_frames"].append(frame_vector)
                
                if end_frame is not None:
                    self.log.info(f"Frame {i}: Speech End (end_frame={end_frame})")
                    seg_start = self.vad_state.get("segment_start")
                    if seg_start is not None and self.vad_state["segment_frames"]:
                        start_ms = seg_start * self.hop_size / self.sample_rate * 1000
                        end_ms = end_frame * self.hop_size / self.sample_rate * 1000
                        duration_ms = end_ms - start_ms
                        self.log.info(f"Segment: start_ms={start_ms:.2f}, end_ms={end_ms:.2f}, duration={duration_ms:.2f}ms")
                        asyncio.run_coroutine_threadsafe(self.send_conversation_end(), loop)
                        # 调用语音识别函数
                        recognized_text = self.recognize_speech(self.vad_state["segment_frames"], self.recognize_model)
                        self.log.info(f"Recognized Text: {recognized_text}")
                        asyncio.run_coroutine_threadsafe(self.send_recognized_text(recognized_text), loop)
                        asyncio.run_coroutine_threadsafe(self.send2llm(recognized_text), loop)
                        # 重置片段状态
                        self.vad_state["segment_start"] = None
                        self.vad_state["segment_frames"] = []
        except Exception as e:
            self.log.error(f"TenVad processing error: {e}")
    
    def recognize_speech(self, segment_frames, recognize_model):
        """
        使用 FunASR 非流式模型将音频帧转换为文字。
        输入：segment_frames - 音频帧列表（int16格式），recognize_model - 已加载的非流式模型
        输出：识别的文字
        """
        if not segment_frames:
            return ""
        
        # 拼接所有帧为完整音频
        segment_audio = np.concatenate(segment_frames).reshape(-1)
        
        # 将 int16 转换回 float32 [-1.0, 1.0] 范围
        segment_audio = segment_audio.astype(np.float32) / 32768.0
        
        # 使用非流式模型进行识别
        try:
            res = recognize_model.generate(
                input=segment_audio,
                batch_size_s=300,
                disable_pbar=True,
                without_punc=False,
                # 尝试其他参数
                lang="zh",
            )
            
            # 提取文字
            if res:
                if isinstance(res, list) and len(res) > 0:
                    if isinstance(res[0], dict):
                        text = res[0].get('text', '')
                    else:
                        text = str(res[0])
                elif isinstance(res, dict):
                    text = res.get('text', '')
                else:
                    text = str(res)
                
                return text.strip() if text else ""
        except Exception as e:
            self.log.error(f"识别错误: {e}")
            return ""
        
        return ""

    async def _process_loop(self) -> None:
        """
        Main processing loop for audio data.
        Continuously processes audio buffer.
        """
        self.log.info("Audio processing loop started")
        
        try:
            while not self._closed:
                if not self._is_processing and len(self._audio_buffer) > 0:
                    self._is_processing = True
                    await self._process_audio_chunk()
                    self._is_processing = False
                else:
                    # Wait a bit before checking again
                    await asyncio.sleep(0.1)
        except asyncio.CancelledError:
            self.log.info("Processing loop cancelled")
            raise
        except Exception as e:
            self.log.exception(f"Error in processing loop: {e}")
        finally:
            self.log.info("Audio processing loop stopped")
    
    async def _process_audio_chunk(self) -> None:
        """
        Process a chunk of audio data from the buffer.
        
        This method should be overridden or extended to implement
        actual audio processing logic (e.g., speech recognition, voice activity detection).
        """
        # 获取当前的音频数据并清空缓冲区
        audio_data = bytes(self._audio_buffer)
        self._audio_buffer.clear()
        
        # 在独立线程中运行CPU密集型的VAD处理，避免阻塞事件循环
        loop = asyncio.get_running_loop()
        self.log.debug(f"Process audio chunk: {len(audio_data)} bytes")

        await loop.run_in_executor(None, self.ten_vad_handle_audio_data, audio_data, loop)

    
    async def send_audio_response(self, audio_data: bytes) -> None:
        """
        Send audio response back to the client through WebSocket session.
        
        Args:
            audio_data (bytes): Audio data to send
        """
        if self._closed:
            self.log.warning("Cannot send audio - session is closed")
            return
        
        try:
            # Send as notification through WebSocket session
            await self.ws_session.send_notification(
                "audio_response",
                {
                    "audio": audio_data.hex(),  # Convert bytes to hex string for JSON
                    "roomId": self.room_id,
                    "userId": self.user_id
                }
            )
            self.log.debug(f"Sent {len(audio_data)} bytes of audio response")
        except Exception as e:
            self.log.error(f"Failed to send audio response: {e}")
    
    async def send_text_message(self, message: str) -> None:
        """
        Send text message to the client through WebSocket session.
        
        Args:
            message (str): Text message to send
        """
        if self._closed:
            self.log.warning("Cannot send message - session is closed")
            return
        
        try:
            await self.ws_session.send_notification(
                "text_message",
                {
                    "message": message,
                    "roomId": self.room_id,
                    "userId": self.user_id
                }
            )
            self.log.info(f"Sent text message: {message}")
        except Exception as e:
            self.log.error(f"Failed to send text message: {e}")
    
    async def request_data(self, method: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Send a request to the client and wait for response.
        
        Args:
            method (str): Request method name
            data (Optional[Dict[str, Any]]): Request data
            
        Returns:
            Dict[str, Any]: Response data
        """
        if self._closed:
            raise RuntimeError("Cannot send request - session is closed")
        
        try:
            response = await self.ws_session.send_request(method, data)
            self.log.debug(f"Request {method} completed: {response}")
            return response
        except Exception as e:
            self.log.error(f"Request {method} failed: {e}")
            raise
    
    @property
    def is_active(self) -> bool:
        """Check if the session is active."""
        return not self._closed
    
    def __repr__(self) -> str:
        """String representation of Agent Session."""
        return f"AgentSession(room={self.room_id}, user={self.user_id}, active={self.is_active})"
    
    async def send_conversation_start(self) -> None:
        """
        Send conversation start notification to the client.
        """
        import datetime
        ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]  # 精确到毫秒
        ms = datetime.datetime.now().timestamp() * 1000  # 毫秒时间戳
        self.conversation_id = f"{self.room_id}_{self.user_id}_{int(ms)}"
        json_message = {
            "type": "conversation.start",
            "roomId": self.room_id,
            "userId": self.user_id,
            "msgIndex": self.msg_index,
            "conversationId": self.conversation_id,
            "ts": ts,
            "ms": ms,
        }
        self.msg_index += 1

        await self.ws_session.send_notification("conversation.start", json_message)

    async def send_conversation_end(self) -> None:
        """
        Send conversation end notification to the client.
        """
        import datetime
        ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]  # 精确到毫秒
        ms = datetime.datetime.now().timestamp() * 1000  # 毫秒时间戳
        json_message = {
            "type": "conversation.end",
            "roomId": self.room_id,
            "userId": self.user_id,
            "msgIndex": self.msg_index,
            "conversationId": self.conversation_id,
            "ts": ts,
            "ms": ms,
        }
        self.msg_index += 1

        await self.ws_session.send_notification("conversation.end", json_message)

    async def send_recognized_text(self, text: str) -> None:
        """
        Send recognized text notification to the client.
        """
        try:
            ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]  # 精确到毫秒
            ms = datetime.datetime.now().timestamp() * 1000  # 毫秒时间戳
            json_message = {
                "type": "input.transcript",
                "roomId": self.room_id,
                "userId": self.user_id,
                "msgIndex": self.msg_index,
                "conversationId": self.conversation_id,
                "ts": ts,
                "ms": ms,
                "text": text,
            }
            self.msg_index += 1
            self.log.info(f"Send recognized text: %s", json.dumps(json_message, ensure_ascii=False))
            await self.ws_session.send_notification("input.transcript", json_message)
        except Exception as e:
            self.log.error(f"Failed to send recognized text: {e}")

    async def send2llm(self, text: str) -> None:
        """
        Send text to LLM for processing.
        """
        prompt = "请以纯文本格式回答，严禁使用 Markdown 语法, 如 **、#、-、` 等。不要返回任何表格或代码块，回复文字会被使用在语音中"
        text = f"{prompt}\n用户说: {text}\n请作为智能助手进行回答:"
        try:
            resp = await self.llm_client.send_request(text)
            if len(resp) == 0:
                return
            ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]  # 精确到毫秒
            ms = datetime.datetime.now().timestamp() * 1000  # 毫秒时间戳
            json_message = {
                "type": "response.text",
                "roomId": self.room_id,
                "userId": self.user_id,
                "msgIndex": self.msg_index,
                "conversationId": self.conversation_id,
                "ts": ts,
                "ms": ms,
                "text": resp,
            }
            self.msg_index += 1
            self.log.info(f"LLM Response: %s", json.dumps(json_message, ensure_ascii=False))
            await self.ws_session.send_notification("response.text", json_message)

            # send response.txt to tts in voice agent worker
            await self.ws_session.send_response_text2voiceagent_worker(self.room_id, self.user_id, resp)
        except Exception as e:
            self.log.error(f"Failed to send text to LLM: {e}")