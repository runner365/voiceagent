import yaml
import os
from typing import Any, Optional


class Config:
    """
    Configuration class for loading and managing voice agent settings from YAML
    """
    
    def __init__(self, config_path: str = None):
        """
        Initialize Config and load configuration from YAML file
        
        Args:
            config_path (str): Path to the config.yaml file
        """
        self._config_data = {}
        
        if config_path:
            self.load(config_path)
    
    def load(self, config_path: str) -> None:
        """
        Load configuration from YAML file
        
        Args:
            config_path (str): Path to the config.yaml file
            
        Raises:
            FileNotFoundError: If config file does not exist
            yaml.YAMLError: If YAML parsing fails
        """
        if not os.path.exists(config_path):
            raise FileNotFoundError(f"Config file not found: {config_path}")
        
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                self._config_data = yaml.safe_load(f) or {}
        except yaml.YAMLError as e:
            raise yaml.YAMLError(f"Failed to parse YAML file: {e}")
    
    def get(self, key: str, default: Any = None) -> Any:
        """
        Get a configuration value by key (supports nested keys with dot notation)
        
        Args:
            key (str): Configuration key (e.g., 'log.log_level' or 'protoo_server.port')
            default (Any): Default value if key not found
            
        Returns:
            Any: Configuration value or default
        """
        keys = key.split('.')
        value = self._config_data
        
        for k in keys:
            if isinstance(value, dict):
                value = value.get(k)
                if value is None:
                    return default
            else:
                return default
        
        return value if value is not None else default
    
    # Log configuration properties
    @property
    def log_level(self) -> str:
        """Get log level"""
        return self.get('log.log_level', 'info')
    
    @property
    def log_path(self) -> str:
        """Get log file path"""
        return self.get('log.log_path', 'server0.log')
    
    @property
    def log_config(self) -> dict:
        """Get entire log configuration"""
        return self.get('log', {})
    
    # Protoo server configuration properties
    @property
    def protoo_port(self) -> int:
        """Get protoo server port"""
        return self.get('protoo_server.port', 5555)
    
    @property
    def ssl_enable(self) -> bool:
        """Check if SSL is enabled"""
        return self.get('protoo_server.ssl_enable', False)
    
    @property
    def cert_path(self) -> str:
        """Get SSL certificate path"""
        return self.get('protoo_server.cert_path', '')
    
    @property
    def key_path(self) -> str:
        """Get SSL key path"""
        return self.get('protoo_server.key_path', '')
    
    @property
    def subpath(self) -> str:
        """Get websocket subpath"""
        return self.get('protoo_server.subpath', 'voiceagent')
    
    @property
    def protoo_server_config(self) -> dict:
        """Get entire protoo server configuration"""
        return self.get('protoo_server', {})
    
    # General configuration access
    @property
    def config_data(self) -> dict:
        """Get entire configuration data"""
        return self._config_data

    @property
    def funasr_model_dir(self) -> str:
        """Get FunASR model directory"""
        return self.get('funasr_config.model_dir', './models/funasr')
    
    @property
    def llm_config(self) -> dict:
        """Get entire LLM configuration"""
        return self.get('llm_config', {})

    @property
    def llm_type(self) -> str:
        """Get the configured LLM type"""
        return self.get('llm_config.llm_type', 'qwen')

    @property
    def llm_model_name(self) -> str:
        """Get the configured LLM model name"""
        return self.get('llm_config.model_name', '')
    """
    worker_config:
        worker_bin: "./objs/voiceagent"
        config_path: "./src/transcode.yaml"
    """
    
    @property
    def worker_config(self) -> dict:
        """Get entire worker configuration"""
        return self.get('worker_config', {})
    
    @property
    def worker_bin(self) -> str:
        """Get worker binary path"""
        return self.get('worker_config.worker_bin', './objs/voiceagent')
    
    @property
    def worker_config_path(self) -> str:
        """Get worker config path"""
        return self.get('worker_config.config_path', './src/transcode.yaml')

    def dump(self) -> str:
        """
        Return all configuration information as a formatted string
        
        Returns:
            str: Formatted configuration information
        """
        output = ["=" * 60]
        output.append("Configuration Information")
        output.append("=" * 60)
        
        # Log configuration section
        output.append("\n[Log Configuration]")
        output.append(f"  Log Level: {self.log_level}")
        output.append(f"  Log Path: {self.log_path}")
        
        # Protoo server configuration section
        output.append("\n[Protoo Server Configuration]")
        output.append(f"  Port: {self.protoo_port}")
        output.append(f"  SSL Enabled: {self.ssl_enable}")
        output.append(f"  Certificate Path: {self.cert_path}")
        output.append(f"  Key Path: {self.key_path}")
        output.append(f"  Subpath: {self.subpath}")
        
        # Raw configuration data
        output.append("\n[Raw Configuration Data]")
        output.append(yaml.dump(self._config_data, default_flow_style=False))

        # funasr configuration section
        output.append("\n[FunASR Configuration]")
        output.append(f"  Model Dir: {self.funasr_model_dir}")
        
        # LLM configuration section
        if self.llm_config:
            output.append("\n[LLM Configuration]")
            output.append(f"  Type: {self.llm_type}")
            output.append(f"  Model Name: {self.llm_model_name}")
        
        # worker configuration section
        output.append("\n[Worker Configuration]")
        output.append(f"  Worker Bin: {self.worker_bin}")
        output.append(f"  Config Path: {self.worker_config_path}")
        
        output.append("=" * 60)
        
        return "\n".join(output)
    
    def __repr__(self) -> str:
        """String representation of Config"""
        return f"Config({self._config_data})"
