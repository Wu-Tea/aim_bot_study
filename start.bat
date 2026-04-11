@echo off
cd /d "%~dp0"
set VISION_PERF_LOG=1
set VISION_BENCH=1
python main.py --controller-mode gamepad
pause
