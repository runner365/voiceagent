# voice agent server

import sys
import asyncio
from config.config import Config
from logger import setup_logger, get_logger
from agent_session.session_mgr import SessionMgr
from websocket_protoo.ws_protoo_server import WsProtooServer, ServerOptions
from utils.model_manager import ASRModelManager
from worker_mgr.worker_mgr import WorkerMgr
import threading

async def run_voice_agent(config: Config, worker_mgr: WorkerMgr):
    """
    Run voice agent server
    
    Args:
        config (Config): Configuration object
    """
    logger = get_logger()
    
    # Preload ASR model (blocking operation to ensure readiness)
    logger.info(f"Preloading ASR model, path: {config.funasr_model_dir}")
    try:
        ASRModelManager.get_instance().load_model(config.funasr_model_dir)
    except Exception as e:
        logger.error(f"Failed to preload ASR model: {e}")
        # Continue anyway? Or exit?
        # If model loading fails, the agent is useless.
        sys.exit(1)
    logger.info("ASR model preloaded successfully")
    # create session_mgr object
    session_mgr = SessionMgr(config=config, logger=logger)

    # Create server options from config
    server_opts = ServerOptions(
        host="0.0.0.0",
        port=config.protoo_port,
        cert_path=config.cert_path if config.ssl_enable else None,
        key_path=config.key_path if config.ssl_enable else None,
        subpath=f"/{config.subpath}" if not config.subpath.startswith("/") else config.subpath
    )
    
    # Create and start WebSocket Protoo Server
    logger.info(f"Creating WebSocket Protoo Server on port {server_opts.port}")
    logger.info(f"SSL Enabled: {config.ssl_enable}")
    logger.info(f"Subpath: {server_opts.subpath}")
    
    ws_server = WsProtooServer(
        options=server_opts,
        logger=logger,
        session_mgr=session_mgr,
        worker_mgr=worker_mgr
    )
    
    await ws_server.start()
    logger.info("WebSocket Protoo Server started successfully")
    
    try:
        # Keep running until interrupted
        while True:
            await asyncio.sleep(3600)
    except (asyncio.CancelledError, KeyboardInterrupt):
        logger.info("Shutting down Voice Agent...")
    finally:
        await ws_server.stop()
        logger.info("Voice Agent stopped")


def main():
    """
    Main entry point for voice agent
    """
    if len(sys.argv) < 2:
        print("Usage: python voice_agent.py <config_path>")
        sys.exit(1)
    
    config_path = sys.argv[1]
    
    # Load configuration using Config class
    config = Config(config_path)
    
    # Initialize logger
    setup_logger(config.log_level, config.log_path)
    logger = get_logger()
    
    # Log startup information
    logger.info("Voice Agent starting...")
    logger.info(f"Configuration loaded from: {config_path}")
    logger.info("Configuration details:")
    logger.info(config.dump())
    
    # call worker_mgr to start worker process
    worker_mgr = WorkerMgr(
        worker_bin=config.worker_bin,
        config_path=config.worker_config_path,
        logger=logger
    )
    # start worker process in new thread
    worker_mgr_thread = threading.Thread(target=worker_mgr.start)
    worker_mgr_thread.start()
    
    # Run the voice agent server
    try:
        asyncio.run(run_voice_agent(config, worker_mgr))
    except KeyboardInterrupt:
        logger.info("Voice Agent interrupted by user")
    except Exception as e:
        logger.error(f"Voice Agent encountered an error: {e}", exc_info=True)
        sys.exit(1)
    

if __name__ == "__main__":
    main()
