@echo off
cd /d "%~dp0"
start "" "microsoft-edge:http://localhost:8766/watch-live.html"
python watch-live-server.py
pause
