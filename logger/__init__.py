import logging
import logging.handlers
import os
from typing import Optional


# Global logger instance
_logger: Optional[logging.Logger] = None


def setup_logger(log_level: str = 'info', log_path: str = 'server0.log') -> logging.Logger:
    """
    Setup and configure the global logger
    
    Args:
        log_level (str): Log level ('debug', 'info', 'warn', 'error')
        log_path (str): Path to the log file
        
    Returns:
        logging.Logger: Configured logger instance
    """
    global _logger
    
    # Create logger
    _logger = logging.getLogger('voice_agent')
    _logger.handlers.clear()  # Clear existing handlers
    _logger.propagate = False # Prevent propagation to root logger (which might have console handlers)
    
    # Convert string log level to logging level
    level_map = {
        'debug': logging.DEBUG,
        'info': logging.INFO,
        'warn': logging.WARNING,
        'warning': logging.WARNING,
        'error': logging.ERROR,
    }
    
    log_level_upper = log_level.lower()
    numeric_level = level_map.get(log_level_upper, logging.INFO)
    _logger.setLevel(numeric_level)
    
    # Create formatter
    formatter = logging.Formatter(
        fmt='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    # Create directory for log file if it doesn't exist
    log_dir = os.path.dirname(log_path)
    if log_dir and not os.path.exists(log_dir):
        os.makedirs(log_dir, exist_ok=True)
    
    # File handler with rotation (only file output, no console)
    file_handler = logging.handlers.RotatingFileHandler(
        log_path,
        maxBytes=10 * 1024 * 1024,  # 10MB
        backupCount=5,
        encoding='utf-8'
    )
    file_handler.setFormatter(formatter)
    _logger.addHandler(file_handler)
    
    return _logger


def get_logger() -> logging.Logger:
    """
    Get the global logger instance
    
    Returns:
        logging.Logger: Global logger instance
        
    Raises:
        RuntimeError: If logger has not been initialized
    """
    if _logger is None:
        raise RuntimeError("Logger not initialized. Call setup_logger() first.")
    return _logger
