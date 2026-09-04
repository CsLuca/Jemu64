@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0run_kernel_iec_e2e.ps1" %*
endlocal
