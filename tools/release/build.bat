@echo off
setlocal

cd /d "%~dp0"

where py >nul 2>&1
if not errorlevel 1 (
    py -3 "%~dp0pack.py" %*
    exit /b %errorlevel%
)

where python >nul 2>&1
if not errorlevel 1 (
    python "%~dp0pack.py" %*
    exit /b %errorlevel%
)

where python3 >nul 2>&1
if not errorlevel 1 (
    python3 "%~dp0pack.py" %*
    exit /b %errorlevel%
)

echo.
echo [ERROR] Python 3 was not found.
echo.
echo Install Python 3 from:
echo https://www.python.org/
echo.
exit /b 1
