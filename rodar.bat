@echo off
echo ==== Compilando o Banco de Dados ====
gcc servidor.c -o servidor.exe
gcc cliente.c -o cliente.exe

echo.
echo ==== Compilacao Finzalizada ====
echo Iniciando o Servidor de background e aguardando 1 segundo...
start "Servidor" cmd /c "servidor.exe & pause"

timeout /t 1 > nul

echo.
echo Iniciando o Cliente...
cliente.exe

echo.
echo.
echo ===== BANCO DE DADOS (Persistencia TXT) =====
type banco.txt
echo =============================================

pause
