@echo off
chcp 65001 >nul 2>&1
title DocxCompressor - Instalare si Pornire
color 0A

echo.
echo  ╔══════════════════════════════════════════════╗
echo  ║         DocxCompressor - Setup               ║
echo  ║   Compresor de documente Word pentru ASE      ║
echo  ╚══════════════════════════════════════════════╝
echo.

:: Check if Python is installed
echo  [1/3] Verificare Python...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo  ❌ Python nu este instalat!
    echo.
    echo  Descarca Python de pe: https://www.python.org/downloads/
    echo  IMPORTANT: La instalare, bifeaza "Add Python to PATH"!
    echo.
    pause
    exit /b 1
)

for /f "tokens=2 delims= " %%v in ('python --version 2^>^&1') do (
    echo  ✓ Python %%v detectat
)

:: Install dependencies
echo.
echo  [2/3] Instalare dependente (Flask, Pillow)...
echo.
pip install -r "%~dp0requirements.txt" --quiet --disable-pip-version-check
if %errorlevel% neq 0 (
    echo.
    echo  ⚠ Eroare la instalare. Incercam cu --user...
    pip install -r "%~dp0requirements.txt" --user --quiet --disable-pip-version-check
)
echo.
echo  ✓ Dependente instalate

:: Start the server
echo.
echo  [3/3] Pornire server web...
echo.
echo  ══════════════════════════════════════════════
echo    Aplicatia se deschide in browser la:
echo    👉 http://localhost:5000
echo.
echo    Pentru a opri serverul, inchide fereastra
echo    sau apasa Ctrl+C
echo  ══════════════════════════════════════════════
echo.

cd /d "%~dp0"
python app.py

pause
