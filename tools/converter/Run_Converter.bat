@echo off
cd /d "%~dp0"
echo Checking environment...

if not exist node_modules (
    echo First run detected. Installing dependencies...
    echo This may take a few minutes like downloading Chrome and Puppeteer...
    call npm install
    if errorlevel 1 (
        echo Error installing dependencies. Please ensure Node.js is installed.
        pause
        exit /b
    )
)

echo Starting Book32 Converter...
echo Placing your .epub files in the "books" folder...
echo.

if not exist books (
    mkdir books
    echo Created "books" folder. Please drop your files there and run this again.
    pause
    exit
)

node index.js
echo.
echo Conversion complete! Check the "output" folder.
pause
