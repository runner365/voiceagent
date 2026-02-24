from llm_client.langchain_client import LangChainClient
import os
# get api_key by env LLM_API_KEY
api_key = os.getenv("LLM_API_KEY")

print(api_key)

# 创建客户端实例
client = LangChainClient.create_from_type("qwen", api_key=api_key)

# 发送请求
async def main():
    response = await client.send_request("如何培养好的习惯，回答不用markdown格式，不要有特殊字符，回复文字会被使用在语音中")
    print(response)

# 运行异步函数
import asyncio
asyncio.run(main())
