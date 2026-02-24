#!/usr/bin/env python3
"""
Session Manager

Manages multiple AgentSession instances for different users in different rooms.
"""
from __future__ import annotations

import base64
import logging
from typing import Dict, Optional, TYPE_CHECKING

from .agent_session import AgentSession
from config.config import Config

if TYPE_CHECKING:
    from websocket_protoo.ws_protoo_session import WsProtooSession


class SessionMgr:
    """
    Manager for AgentSession objects.
    
    Manages multiple agent sessions indexed by room_id and user_id.
    Handles audio data distribution to appropriate sessions.
    """
    
    def __init__(self, config: Config, logger: Optional[logging.Logger] = None) -> None:
        """
        Initialize Session Manager.
        
        Args:
            config (Config): Configuration instance
            logger (Optional[logging.Logger]): Logger instance
        """
        self.config = config
        self.log = logger or logging.getLogger("session_mgr")
        
        # Store sessions with key format: "{room_id}_{user_id}"
        self._sessions: Dict[str, AgentSession] = {}
        
        self.log.info("Session Manager initialized")
    
    def _make_key(self, room_id: str, user_id: str) -> str:
        """
        Create session key from room_id and user_id.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            
        Returns:
            str: Session key
        """
        return f"{room_id}_{user_id}"
    
    def get_or_create_session(
        self,
        room_id: str,
        user_id: str,
        ws_session: object = None
    ) -> AgentSession:
        """
        Get an existing session or create a new one if not found.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            ws_session (object): WebSocket Protoo Session object (required if creating new session)
            
        Returns:
            AgentSession: Existing or newly created session object
            
        Raises:
            ValueError: If session doesn't exist and ws_session is not provided
        """
        key = self._make_key(room_id, user_id)
        
        # Return existing session if found
        if key in self._sessions:
            self.log.debug(f"Returning existing session: {key}")
            return self._sessions[key]
        
        # Create new session if ws_session provided
        if ws_session is None:
            raise ValueError(f"Session not found and ws_session not provided: {key}")
        
        session = AgentSession(room_id, user_id, ws_session, self.config, logger=self.log)
        self._sessions[key] = session
        session.start()
        self.log.info(f"Created session: {key}")
        return session
    
    def create_session(
        self,
        room_id: str,
        user_id: str,
        ws_session: object
    ) -> AgentSession:
        """
        Create and register a new agent session.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            ws_session (object): WebSocket Protoo Session object
            
        Returns:
            AgentSession: Created session object
            
        Raises:
            ValueError: If session already exists for this room_id and user_id
        """
        key = self._make_key(room_id, user_id)
        
        if key in self._sessions:
            raise ValueError(f"Session already exists: {key}")
        
        session = AgentSession(room_id, user_id, ws_session, logger=self.log)
        self._sessions[key] = session
        
        self.log.info(f"Created session: {key}")
        return session
    
    def get_session(self, room_id: str, user_id: str) -> Optional[AgentSession]:
        """
        Get an existing agent session.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            
        Returns:
            Optional[AgentSession]: Session object or None if not found
        """
        key = self._make_key(room_id, user_id)
        return self._sessions.get(key)
    
    async def remove_session(self, room_id: str, user_id: str) -> bool:
        """
        Remove and cleanup an agent session.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            
        Returns:
            bool: True if session was removed, False if not found
        """
        key = self._make_key(room_id, user_id)
        
        if key not in self._sessions:
            return False
        
        session = self._sessions.pop(key)
        await session.stop()
        
        self.log.info(f"Removed session: {key}")
        return True
    
    async def input_audio_data(
        self,
        room_id: str,
        user_id: str,
        data_base64: str,
        codec: str,
        ws_session: object = None,
    ) -> None:
        """
        Input audio data to a specific session.
        
        Decodes base64-encoded audio data and sends it to the appropriate
        agent session for processing.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            data_base64 (str): Base64-encoded audio data
            
        Raises:
            ValueError: If room_id/user_id not found or invalid base64
            RuntimeError: If session is not active
        """
        session = self.get_or_create_session(room_id, user_id, ws_session=ws_session)
        
        if session is None:
            raise ValueError(f"Session not found: {room_id}_{user_id}")
        
        if not session.is_active:
            raise RuntimeError(f"Session is not active: {room_id}_{user_id}")
        
        try:
            # Decode base64 to PCM bytes
            audio_data = base64.b64decode(data_base64)
            
            self.log.debug(f"Input audio data to {room_id}_{user_id}: {len(audio_data)} bytes")
            
            # Pass audio data to session
            await session.handle_audio_data(audio_data)
            
        except base64.binascii.Error as e:
            raise ValueError(f"Invalid base64 encoding: {e}")
        except Exception as e:
            self.log.error(f"Error processing audio data: {e}")
            raise
    
    async def start_session(self, room_id: str, user_id: str) -> None:
        """
        Start audio processing for a session.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            
        Raises:
            ValueError: If session not found
        """
        session = self.get_session(room_id, user_id)
        
        if session is None:
            raise ValueError(f"Session not found: {room_id}_{user_id}")
        
        session.start()
        self.log.info(f"Started session: {room_id}_{user_id}")
    
    async def stop_session(self, room_id: str, user_id: str) -> None:
        """
        Stop audio processing for a session.
        
        Args:
            room_id (str): Room identifier
            user_id (str): User identifier
            
        Raises:
            ValueError: If session not found
        """
        session = self.get_session(room_id, user_id)
        
        if session is None:
            raise ValueError(f"Session not found: {room_id}_{user_id}")
        
        await session.stop()
        self.log.info(f"Stopped session: {room_id}_{user_id}")
    
    def list_sessions(self) -> Dict[str, AgentSession]:
        """
        List all active sessions.
        
        Returns:
            Dict[str, AgentSession]: Copy of sessions dictionary
        """
        return dict(self._sessions)
    
    async def shutdown(self) -> None:
        """
        Shutdown all active sessions.
        
        Stops and removes all registered sessions.
        """
        self.log.info(f"Shutting down {len(self._sessions)} sessions")
        
        for key in list(self._sessions.keys()):
            session = self._sessions.pop(key)
            try:
                await session.stop()
            except Exception as e:
                self.log.error(f"Error stopping session {key}: {e}")
        
        self.log.info("Session Manager shutdown complete")
    
    def __repr__(self) -> str:
        """String representation of Session Manager."""
        return f"SessionMgr(sessions={len(self._sessions)})"
