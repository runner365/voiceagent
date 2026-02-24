"""
使用LangChain 1.0实现的LLM客户端，与llm_client.py接口一致
"""
import asyncio
from typing import TypedDict, Dict
from langchain_openai import ChatOpenAI
from langchain_core.messages import HumanMessage, AIMessage
from langchain_core.prompts import ChatPromptTemplate

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
        "url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-plus",
    },
    "yuanbao": {
        "llm_type": "yuanbao",
        "url": "https://api.hunyuan.cloud.tencent.com/v1",
        "model": "hunyuan-turbo",
    },
    "openai": {
        "llm_type": "openai",
        "url": "https://api.openai.com/v1",
        "model": "gpt-3.5-turbo",
    },
    "deepseek": {
        "llm_type": "deepseek",
        "url": "https://api.deepseek.cn/v1",
        "model": "deepseek-turbo",
    },
}

class LangChainClient:
    def __init__(self, url, api_key, model_name, messages_max_length):
        self.url = url
        self.api_key = api_key
        self.model_name = model_name
        self.messages_max_length = messages_max_length
        self.messages = []
        
        # Initialize LangChain ChatOpenAI instance
        self.llm = ChatOpenAI(
            model=model_name,
            api_key=api_key,
            base_url=url
        )

    @classmethod
    def create_from_type(cls, llm_type: str, api_key: str, messages_max_length: int = 10) -> "LangChainClient":
        """
        Factory method to create LangChainClient from supported LLM type.
        
        Args:
            llm_type: The type of LLM (e.g., "qwen", "yuanbao")
            api_key: API key for the LLM service
            messages_max_length: Maximum number of messages to retain
            
        Returns:
            LangChainClient instance configured for the specified LLM type
            
        Raises:
            ValueError: If llm_type is not supported
            
        Example:
            client = LangChainClient.create_from_type("qwen", api_key="your_key")
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

        # Convert messages to LangChain format
        langchain_messages = []
        for msg in self.messages:
            if msg["role"] == "user":
                langchain_messages.append(HumanMessage(content=msg["content"]))
            elif msg["role"] == "assistant":
                langchain_messages.append(AIMessage(content=msg["content"]))

        # Send request using LangChain
        response = await self.llm.ainvoke(langchain_messages)
        
        # Get assistant response content
        assistant_content = response.content
        
        # Add assistant message to messages list
        assistant_message = {"role": "assistant", "content": assistant_content}
        self.messages.append(assistant_message)
        
        # Remove oldest message if exceeds max length
        if len(self.messages) > self.messages_max_length:
            self.messages.pop(0)
        
        # Return assistant message content
        return assistant_content