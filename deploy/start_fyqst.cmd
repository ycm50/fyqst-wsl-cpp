@echo off
title FYQST - start the game
echo.
echo   Opening WSL ... keep the WSL window open while playing.
echo.
start "" %SystemRoot%\System32\wsl.exe -d Debian
echo   Waiting for game services (about 20 seconds) ...
ping -n 20 127.0.0.1 >nul
start "" http://localhost:81/
echo.
echo   Game  : http://localhost:81/
echo   GM    : http://localhost:81/gm/gm.php        (GM code 123456)
echo   CDK   : http://localhost:81/cdk/index.php    (admin / 123456)
echo.
echo   Keep the WSL window open. Closing it stops all services.
echo.
ping -n 6 127.0.0.1 >nul
