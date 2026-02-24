# -*- coding: utf-8 -*-
import os
import logging
from logger import setup_logger, get_logger
from threading import Lock
from funasr import AutoModel

class ASRModelManager:
    """
    Singleton manager for FunASR model to avoid reloading model for each session.
    """
    _instance = None
    _lock = Lock()

    def __init__(self):
        if ASRModelManager._instance is not None:
            raise RuntimeError("Use get_instance() to access ASRModelManager")
        self.model = None
        self.log = get_logger()

    @classmethod
    def get_instance(cls):
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def load_model(self, model_dir: str):
        """
        Load the FunASR model if not already loaded.
        """
        if model_dir is None:
            self.log.info("FunASR model dir is not config, skip loading model")
            return

        self.log.info("Loading FunASR model...")
        try:
            # Path resolution:
            # current file: voice_agent/utils/model_manager.py
            # model dir: voice_agent/funasr_models
            
            vad_model_path = os.path.join(model_dir, "fsmn-vad")
            punc_model_path = os.path.join(model_dir, "ct-punc-c")
            asr_model_path = os.path.join(model_dir, "paraformer-zh")

            self.log.info(f"VAD model path: {vad_model_path}")
            self.log.info(f"PUNC model path: {punc_model_path}")
            self.log.info(f"ASR model path: {asr_model_path}")
            
            # Check if model directories exist
            if not os.path.exists(model_dir):
                raise FileNotFoundError(f"Model directory not found: {model_dir}")
                
            self.model = AutoModel(
                model=asr_model_path,
                vad_model=vad_model_path,
                punc_model=punc_model_path,
                vad_kwargs={
                    "max_single_segment_time": 60000,
                }
            )
            self.log.info("FunASR model loaded successfully")
        except Exception as e:
            self.log.error(f"Failed to load FunASR model: {e}")
            raise

    def get_model(self, model_dir: str):
        """
        Get the loaded model instance. Loads it if necessary.
        """
        if self.model is None:
            self.load_model(model_dir)
        return self.model
