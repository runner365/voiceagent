import subprocess
import logging
import time
import json

class WorkerMgr:
    def __init__(self, worker_bin: str, config_path: str, logger: logging):
        # cmd: {worker_bin} {config_path}
        self.worker_bin = worker_bin
        self.config_path = config_path
        self.logger = logger

        self.worker_process = None
        self.alive_ms = 0
        self.session = None
        self.user2session = {}

    def start(self):
        cmd = f"{self.worker_bin} {self.config_path}"
        self.worker_process = subprocess.Popen(
            cmd,
            shell=True,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.logger.info(f"start worker process: {cmd}")
        # 监听worker进程的stdout和stderr
        self._listen_worker_process()

    def _listen_worker_process(self):
        # 监听worker进程的stdout和stderr
        while self.worker_process.poll() is None:
            stdout_line = self.worker_process.stdout.readline().decode().strip()
            if stdout_line:
                self.logger.info(f"worker stdout: {stdout_line}")
                print(stdout_line)  # 发送到stdout

            stderr_line = self.worker_process.stderr.readline().decode().strip()
            if stderr_line:
                self.logger.error(f"worker stderr: {stderr_line}")

        # 监听完成后，检查worker进程是否退出
        if self.worker_process.returncode != 0:
            self.logger.error(f"worker process exit with code: {self.worker_process.returncode}")

        # 5s后，重新启动worker进程
        self.logger.info("restart worker process after 5s")
        time.sleep(5)
        self.start()

    def keepalive(self, now_ms: int, session: object):
        self.logger.info(f"keepalive worker: {now_ms}")
        self.alive_ms = now_ms
        self.session = session

    async def _handle_opus_data(self, room_id: str, user_id: str, opus_base64: str, session: object):
        """Handle opus data from client."""
        if self.session is None:
            self.logger.error("session is None, can not send opus data")
            return
        # user_id in sfu -> websocket session to the sfu
        self.user2session[user_id] = session
        try:
            data = {
                "type": "opus_data",
                "roomId": room_id,
                "userId": user_id,
                "opus_base64": opus_base64,
            }
            await self.session.send_notification("opus_data", data)
        except Exception as e:
            self.logger.error(f"send opus data error: {e}")

    async def send_response_text2worker(self, room_id: str, user_id: str, resp_text: str):
        """Send response text to worker."""
        self.logger.info(f"send response text to worker: room_id={room_id}, user_id={user_id}, resp_text={resp_text}")

        try:
            data = {
                "type": "response.text",
                "roomId": room_id,
                "userId": user_id,
                "text": resp_text,
            }
            self.logger.info(f"send response text to worker: %s", json.dumps(data))
            await self.session.send_notification("response.text", data)
        except Exception as e:
            self.logger.error(f"send response text error: {e}")
    def get_session(self, user_id: str):
        return self.user2session.get(user_id, None)
