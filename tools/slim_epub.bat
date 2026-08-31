@echo off
setlocal enabledelayedexpansion

rem Arrasta ficheiros .epub para cima deste .bat, ou corre-o sem argumentos
rem para processar tudo o que estiver na pasta "epubs" ao lado do script.
rem
rem Sem "cd" para a pasta do script: isso faria um argumento relativo passado
rem na linha de comandos resolver contra tools\ em vez da pasta onde o
rem utilizador esta. O script Python e sempre referido por caminho absoluto.
set SLIM=%~dp0slim_epub.py

rem O Python instalado da Store chama-se python.exe; o do python.org tambem
rem responde ao lancador "py", que e mais fiavel quando ha varias versoes.
set PYTHON=
where py >nul 2>&1 && set PYTHON=py
if not defined PYTHON (
    where python >nul 2>&1 && set PYTHON=python
)
if not defined PYTHON (
    echo ERRO: Python nao encontrado no PATH.
    echo Instala em https://www.python.org/downloads/ e marca
    echo "Add Python to PATH" durante a instalacao.
    echo.
    pause
    exit /b 1
)

rem Escolhe aqui o que manter (por omissao remove tudo: imagens, fontes,
rem CSS, JS e media -- nenhuma delas usada pelo leitor). --keep-cover fica
rem ligado por omissao porque o Book32 ja mostra a capa real na biblioteca;
rem o custo em tamanho e so o da propria capa. Outras opcoes uteis:
rem   set SLIM_ARGS=--keep-cover --keep-images
rem   set SLIM_ARGS=--keep-cover --keep-fonts
rem   set SLIM_ARGS=  (sem opcoes, remove tudo incluindo a capa)
set SLIM_ARGS=--keep-cover

if "%~1"=="" goto :pasta

rem Arrastar ficheiros passa-os como argumentos. Um a um, para que um EPUB
rem partido nao impeca os restantes de serem processados.
echo Book32 - a remover imagens, fontes e outros recursos nao suportados...
echo.
for %%F in (%*) do (
    %PYTHON% "%SLIM%" %SLIM_ARGS% "%%~fF"
)
echo.
echo Concluido. Envia os ficheiros .slim.epub para o Book32 em
echo http://book32.local/send
echo.
pause
exit /b 0

:pasta
cd /d "%~dp0"
if not exist epubs (
    mkdir epubs
    echo Criada a pasta "epubs".
    echo Coloca la os teus ficheiros .epub e corre isto outra vez,
    echo ou arrasta os ficheiros directamente para cima deste .bat.
    echo.
    pause
    exit /b 0
)

dir /b "epubs\*.epub" >nul 2>&1
if errorlevel 1 (
    echo A pasta "epubs" esta vazia.
    echo Coloca la os teus ficheiros .epub e corre isto outra vez.
    echo.
    pause
    exit /b 0
)

echo Book32 - a remover imagens, fontes e outros recursos nao suportados...
echo.
for %%F in ("epubs\*.epub") do (
    echo %%~nxF| findstr /i /c:".slim.epub" >nul || %PYTHON% "%SLIM%" %SLIM_ARGS% "%%~fF"
)
echo.
echo Concluido. Envia os ficheiros .slim.epub para o Book32 em
echo http://book32.local/send
echo.
pause
