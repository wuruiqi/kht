@echo off
REM One-click build & deploy for kht (calls build.py)
python "%~dp0build.py" %*
if errorlevel 1 pause
