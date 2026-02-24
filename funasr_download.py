import os
from modelscope.hub.snapshot_download import snapshot_download
from funasr import AutoModel

# 预下载模型到指定本地目录
model_dirs = {
    "paraformer-zh": "./funasr_models/paraformer-zh",
    "paraformer-zh-streaming": "./funasr_models/paraformer-zh-streaming",
    "fsmn-vad": "./funasr_models/fsmn-vad",
    "ct-punc-c": "./funasr_models/ct-punc-c"
}

# 模型映射（优先使用 FunASR 模型名称）
models = [
    ("paraformer-zh", "damo/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-pytorch", "核心ASR模型（离线）", False),
    ("paraformer-zh-streaming", None, "核心ASR模型（流式）", True),  # None 表示使用 FunASR 自动下载
    ("fsmn-vad", "damo/speech_fsmn_vad_zh-cn-16k-common-pytorch", "VAD模型", False),
    ("ct-punc-c", "damo/punc_ct-transformer_zh-cn-common-vocab272727-pytorch", "标点模型", False),
]

def is_model_exists(model_path):
    """
    检查模型是否已存在（通过检查关键文件）
    
    Args:
        model_path: 模型目录路径
        
    Returns:
        bool: 模型是否存在
    """
    if not os.path.exists(model_path):
        return False
    
    # 检查关键文件
    key_files = ["model.pt", "config.yaml", "configuration.json"]
    for key_file in key_files:
        if os.path.exists(os.path.join(model_path, key_file)):
            return True
    
    return False


# 下载模型
for model_name, model_id, description, use_funasr in models:
    local_dir = model_dirs[model_name]
    
    # 检查模型是否已存在
    if is_model_exists(local_dir):
        print(f"✓ {description} 已存在: {local_dir}")
        continue
    
    print(f"⬇️  正在下载 {description}...")
    try:
        if use_funasr:
            # 使用 FunASR 自动下载机制（会自动放在 ~/.cache/funasr/models/ 中）
            print(f"   使用 FunASR 下载机制...")
            temp_model = AutoModel(model=model_name)
            # 复制模型到本地目录
            import shutil
            if os.path.exists(temp_model.model_path):
                os.makedirs(local_dir, exist_ok=True)
                for item in os.listdir(temp_model.model_path):
                    src = os.path.join(temp_model.model_path, item)
                    dst = os.path.join(local_dir, item)
                    if os.path.isdir(src):
                        if os.path.exists(dst):
                            shutil.rmtree(dst)
                        shutil.copytree(src, dst)
                    else:
                        shutil.copy2(src, dst)
                print(f"✓ {description} 下载完成: {local_dir}\n")
            else:
                print(f"✗ {description} 下载失败: 无法获取模型路径\n")
        else:
            # 使用 ModelScope 直接下载
            snapshot_download(
                model_id,
                cache_dir=None,
                local_dir=local_dir
            )
            print(f"✓ {description} 下载完成: {local_dir}\n")
    except Exception as e:
        print(f"✗ {description} 下载失败: {e}\n")

print("✓ 模型下载流程完成！")