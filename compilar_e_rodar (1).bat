@echo off
rem ============================================================
rem  ERP Torra - compila e executa o programa.
rem  Basta dar dois cliques neste arquivo.
rem  Ele procura sozinho a libpq (MSYS2 ou PostgreSQL instalado)
rem  e usa o cabecalho e a biblioteca da MESMA origem.
rem ============================================================
cd /d "%~dp0"

rem Procura o main.c: nesta pasta ou em qualquer subpasta dela.
if exist main.c goto achou_main
for /r %%F in (main.c) do (
    if exist "%%F" (
        cd /d "%%~dpF"
        goto achou_main
    )
)
echo ERRO: nao achei o main.c nesta pasta nem nas subpastas.
echo Pasta atual: %CD%
pause
exit /b 1
:achou_main

set "GCC=C:\msys64\ucrt64\bin\gcc.exe"
if not exist "%GCC%" (
    echo ERRO: gcc nao encontrado em %GCC%
    echo Instale o MSYS2 ou ajuste o caminho do GCC no inicio deste arquivo.
    pause
    exit /b 1
)

rem O gcc do MSYS2 precisa da pasta bin dele no PATH. Sem isso ele nao acha
rem as proprias DLLs e falha SEM mostrar mensagem de erro.
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

"%GCC%" --version >nul 2>&1
if errorlevel 1 (
    echo ERRO: o gcc nao consegue iniciar. Codigo %errorlevel%
    echo Abra o terminal MSYS2 UCRT64 e rode: pacman -Syu
    pause
    exit /b 1
)

if not exist output mkdir output
set "BIN="

rem 1) libpq do MSYS2
call :tentar "C:/msys64/ucrt64/include" "C:/msys64/ucrt64/lib" "C:\msys64\ucrt64\bin"
if defined BIN goto compilado
call :tentar "C:/msys64/ucrt64/include/postgresql" "C:/msys64/ucrt64/lib" "C:\msys64\ucrt64\bin"
if defined BIN goto compilado

rem 2) PostgreSQL instalado no Windows (versoes mais novas primeiro)
for %%V in (18 17 16 15 14 13) do (
    if not defined BIN call :tentar "C:/Program Files/PostgreSQL/%%V/include" "C:/Program Files/PostgreSQL/%%V/lib" "C:\Program Files\PostgreSQL\%%V\bin"
)
if defined BIN goto compilado

echo.
echo ERRO: nao consegui compilar. Veja as mensagens acima.
echo Confira se o PostgreSQL esta instalado em C:\Program Files\PostgreSQL
echo ou se a libpq do MSYS2 esta instalada (no terminal MSYS2 UCRT64:
echo   pacman -S mingw-w64-ucrt-x86_64-postgresql ).
pause
exit /b 1

:compilado
echo.
echo Compilado com sucesso em output\main.exe
echo Iniciando o programa...
echo.
set "PATH=%BIN%;%PATH%"
output\main.exe
echo.
pause
exit /b 0

:tentar
rem %1 = pasta include   %2 = pasta lib   %3 = pasta bin (onde fica o libpq.dll)
set "CHK=%~1"
set "CHK=%CHK:/=\%"
if not exist "%CHK%\libpq-fe.h" exit /b 1
echo Tentando compilar com: %~1
"%GCC%" -Wall -Wextra -g3 main.c -o output\main.exe -I"%~1" -L"%~2" -lpq
if not errorlevel 1 goto tentar_ok
echo   gcc terminou com o codigo %errorlevel%
rem Segunda tentativa: liga direto no libpq.dll
if not exist "%~3\libpq.dll" exit /b 1
echo Tentando ligar direto no libpq.dll...
"%GCC%" -Wall -Wextra -g3 main.c -o output\main.exe -I"%~1" "%~3\libpq.dll"
if errorlevel 1 (
    echo   gcc terminou com o codigo %errorlevel%
    exit /b 1
)
:tentar_ok
set "BIN=%~3"
exit /b 0
