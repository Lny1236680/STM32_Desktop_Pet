#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
桌宠 AI 云中转服务器 (FastAPI)
=================================
职责:
  1. 接收 ESP32 转发来的用户文本    POST /chat  {"text": "..."}
  2. 调用 DeepSeek API 生成 AI 回复 (中文)
  3. 返回回复给 ESP32             {"reply": "..."}
  4. 把中文文本转成语音            POST /tts   {"text": "..."} -> mp3 字节
     (用免费微软 edge-tts, 供 ESP32-S3 + I2S 功放播放)

部署 (在 Windows 电脑上运行, 供局域网内 ESP32 访问):
  pip install -r requirements.txt
  set DEEPSEEK_API_KEY=sk-你的Key
  uvicorn server:app --host 0.0.0.0 --port 8000

  注意:
    - 电脑防火墙需放行 8000 端口 (入站规则), ESP32 才能访问
    - 查电脑局域网 IP: ipconfig  (ESP32 固件 SERVER_URL 填这个 IP)
    - API Key 只放在服务器上, 不暴露给设备, 更安全
    - 未设 Key 时运行在"无Key模式": 返回固定中文测试回复, 用于先验证链路

本地自测:
  curl -X POST http://127.0.0.1:8000/chat -H "Content-Type: application/json" -d "{\"text\":\"你好\"}"
  curl -X POST http://127.0.0.1:8000/tts  -H "Content-Type: application/json" -d "{\"text\":\"你好呀主人\"}" -o reply.mp3

回复显示限制说明:
  OLED 在 AI 回复时显示随机表情(不做文字显示), 中文回复由 ESP32 播放语音,
  所以回复长度约束放宽到口语化的短句 (≤30字)。
"""

import os
import re
import random

import httpx
from fastapi import FastAPI, HTTPException
from fastapi.responses import Response
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

# ==================== 配置 (可通过环境变量覆盖) ====================

DEEPSEEK_API_KEY = os.environ.get("DEEPSEEK_API_KEY", "")
DEEPSEEK_URL     = "https://api.deepseek.com/chat/completions"   # DeepSeek 官方 API (OpenAI 兼容格式)
DEEPSEEK_MODEL   = os.environ.get("DEEPSEEK_MODEL", "deepseek-chat")

# TTS 配置: edge-tts 中文女声 (晓晓, 免费)
TTS_VOICE   = os.environ.get("TTS_VOICE", "zh-CN-XiaoxiaoNeural")
TTS_RATE    = os.environ.get("TTS_RATE", "+8%")   # 稍快一点, 更像桌宠

# 回复约束: 口语化中文短句, 便于语音播放
MAX_REPLY_LEN = 30

# 人设: 桌面宠物, 中文口语化短句
SYSTEM_PROMPT = (
    "你是一只可爱的桌面小宠物机器人, 住在一个小小的STM32桌宠里。"
    "请用中文口语化地、简短地回复主人的话。"
    "整条回复控制在30字以内, 语气活泼亲切, 可以偶尔带一点可爱。"
    "不要使用表情符号、不要用英文、不要换行。"
)

# 无Key模式下的固定测试回复 (链路验证用, 有Key后不再使用)
NO_KEY_REPLIES = [
    "你好呀主人, 我在呢",
    "好的好的, 我听懂啦",
    "嘿嘿, 主人找我有什么事呀",
    "我在我在, 主人请讲",
    "收到收到, 这就来陪你玩",
]

# ==================== FastAPI 应用 ====================

app = FastAPI(title="Desktop Pet AI Gateway")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class ChatRequest(BaseModel):
    text: str
    # 可选: 多轮对话历史 (未来扩展), 每项为 {"role": "user"/"assistant", "content": "..."}
    # 用 default_factory 避免 Pydantic v2 对可变默认值的报错
    history: list = Field(default_factory=list)


class TTSRequest(BaseModel):
    text: str


@app.get("/")
async def root():
    return {
        "status": "ok",
        "service": "desktop-pet-ai",
        "ai_configured": bool(DEEPSEEK_API_KEY),
    }


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.post("/chat")
async def chat(req: ChatRequest):
    text = req.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text is empty")

    if not DEEPSEEK_API_KEY:
        # 无Key模式: 返回固定中文测试回复, 方便先验证整条链路
        return {"reply": random.choice(NO_KEY_REPLIES)}

    # 组装对话: system 人设 + 最近历史 + 本次问题
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    for msg in req.history[-6:]:
        messages.append(msg)
    messages.append({"role": "user", "content": text})

    try:
        reply = await ask_deepseek(messages)
    except Exception as e:
        print(f"[ERROR] DeepSeek call failed: {e}")
        raise HTTPException(status_code=502, detail=f"AI upstream error: {e}")

    # 清理: 换行 -> 空格, 去表情符号, 截断到可显示/播放长度
    reply = re.sub(r"[\r\n]+", " ", reply).strip()
    # 去 emoji 及变化选择符/组合符号 (避免 TTS 读出乱码、OLED 残留字符)
    reply = re.sub(r"[\U0001F000-\U0001FAFF☀-➿︎️‍]", "", reply).strip()
    reply = reply[:MAX_REPLY_LEN]

    return {"reply": reply}


@app.post("/tts")
async def tts(req: TTSRequest):
    """把中文文本转成 mp3 语音, 返回音频字节 (ESP32 拉取后播放)"""
    text = req.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text is empty")

    try:
        # edge-tts 是异步库, 在此导入并调用
        import edge_tts
        communicate = edge_tts.Communicate(text, TTS_VOICE, rate=TTS_RATE)
        audio = b""
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                audio += chunk["data"]
    except ImportError:
        raise HTTPException(
            status_code=500,
            detail="edge-tts 未安装, 请先 pip install edge-tts",
        )
    except Exception as e:
        print(f"[ERROR] TTS failed: {e}")
        raise HTTPException(status_code=500, detail=f"TTS error: {e}")

    if not audio:
        raise HTTPException(status_code=500, detail="TTS returned empty audio")

    return Response(
        content=audio,
        media_type="audio/mpeg",
        headers={"Content-Disposition": "inline; filename=reply.mp3"},
    )


async def ask_deepseek(messages):
    """调用 DeepSeek API (OpenAI 兼容格式)"""
    headers = {
        "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
        "Content-Type": "application/json",
    }
    payload = {
        "model": DEEPSEEK_MODEL,
        "messages": messages,
        "max_tokens": 80,
        "temperature": 0.7,
        "stream": False,
    }
    async with httpx.AsyncClient(timeout=25.0) as client:
        resp = await client.post(DEEPSEEK_URL, json=payload, headers=headers)
        resp.raise_for_status()
        data = resp.json()
    return data["choices"][0]["message"]["content"].strip()
