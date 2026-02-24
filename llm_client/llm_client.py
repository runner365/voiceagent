"""
写一个llm_client类，与大模型服务器交互，实现发送请求和接收响应的功能
1. 实现发送请求的方法，包括请求参数的设置和请求的发送
2. 实现接收响应的方法，包括解析响应数据和处理异常情况
3. 构造函数输入: url, api_key, model_name, messages_max_length
4. 发送和接收消息的json格式请参考openai的标准格式
"""
import asyncio
import json
import aiohttp
from typing import TypedDict, Dict

# ========================================================
# LLM Type Configuration
# ========================================================
class LLMTypeInfo(TypedDict):
    """LLM type information structure."""
    llm_type: str
    url: str
    model: str

# Supported LLM types and their configurations
SUPPORTED_LLM_TYPES: Dict[str, LLMTypeInfo] = {
    "qwen": {
        "llm_type": "qwen",
        "url": "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
        "model": "qwen-plus",
    },
    "yuanbao": {
        "llm_type": "yuanbao",
        "url": "https://api.hunyuan.cloud.tencent.com/v1/chat/completions",
        "model": "hunyuan-turbo",
    },
    "openai": {
        "llm_type": "openai",
        "url": "https://api.openai.com/v1/chat/completions",
        "model": "gpt-3.5-turbo",
    },
    "deepseek": {
        "llm_type": "deepseek",
        "url": "https://api.deepseek.cn/v1/chat/completions",
        "model": "deepseek-turbo",
    },
}

class LLMClient:
    def __init__(self, url, api_key, model_name, messages_max_length):
        self.url = url
        self.api_key = api_key
        self.model_name = model_name
        self.messages_max_length = messages_max_length
        self.messages = []

    @classmethod
    def create_from_type(cls, llm_type: str, api_key: str, messages_max_length: int = 10) -> "LLMClient":
        """
        Factory method to create LLMClient from supported LLM type.
        
        Args:
            llm_type: The type of LLM (e.g., "qwen", "yuanbao")
            api_key: API key for the LLM service
            messages_max_length: Maximum number of messages to retain
            
        Returns:
            LLMClient instance configured for the specified LLM type
            
        Raises:
            ValueError: If llm_type is not supported
            
        Example:
            client = LLMClient.create_from_type("qwen", api_key="your_key")
        """
        if llm_type not in SUPPORTED_LLM_TYPES:
            supported = ", ".join(SUPPORTED_LLM_TYPES.keys())
            raise ValueError(f"Unsupported LLM type: {llm_type}. Supported types: {supported}")
        
        llm_info = SUPPORTED_LLM_TYPES[llm_type]
        return cls(
            url=llm_info["url"],
            api_key=api_key,
            model_name=llm_info["model"],
            messages_max_length=messages_max_length
        )

    async def send_request(self, text: str):
        # Add user message to messages list
        self.messages.append({"role": "user", "content": text})
        
        # Remove oldest message if exceeds max length
        if len(self.messages) > self.messages_max_length:
            self.messages.pop(0)

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.api_key}"
        }

        payload = {
            "model": self.model_name,
            "messages": self.messages
        }

        async with aiohttp.ClientSession() as session:
            async with session.post(self.url, headers=headers, json=payload) as response:
                if response.status != 200:
                    raise Exception(f"Request failed with status {response.status}")
                
                response_data = await response.json()
                
                # Parse response according to OpenAI format
                if "choices" not in response_data or not response_data["choices"]:
                    raise ValueError("Invalid response format: 'choices' field missing or empty")
                
                # Find assistant message in choices
                for choice in response_data["choices"]:
                    if choice.get("message", {}).get("role") == "assistant":
                        assistant_message = choice["message"]
                        # Add assistant message to messages list
                        self.messages.append(assistant_message)
                        # Remove oldest message if exceeds max length
                        if len(self.messages) > self.messages_max_length:
                            self.messages.pop(0)
                        # Return assistant message content
                        # Format: {"role": "assistant", "content": "Assistant's response text"}
                        # Example: {"role": "assistant", "content": "Hello, how can I help you?"}
                        return assistant_message["content"]
                
                raise ValueError("No assistant message found in response choices")
    

