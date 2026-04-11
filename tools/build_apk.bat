@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: Build APK Script
:: Usage: build_apk.bat <project_path> [output_apk_path]
::   project_path   - path to the game project folder (contains project.json)
::   output_apk_path - (optional) where to copy the final APK
:: ============================================================================

set "ROOT=%~dp0.."
set "ANDROID_DIR=%ROOT%\android"
set "ASSETS_DIR=%ANDROID_DIR%\app\src\main\assets"
set "SHADER_SRC=%ROOT%\build\shaders"

:: Validate arguments
if "%~1"=="" (
    echo Usage: build_apk.bat ^<project_path^> [output_apk_path]
    exit /b 1
)

set "PROJECT_PATH=%~f1"
set "OUTPUT_PATH=%~f2"

if not exist "%PROJECT_PATH%\project.json" (
    echo [ERROR] project.json not found in: %PROJECT_PATH%
    exit /b 1
)

echo ============================================
echo  Building APK
echo  Project: %PROJECT_PATH%
echo ============================================

:: Step 1: Check SDK
echo.
echo [Step 1/5] Checking Android SDK...
if not exist "%ANDROID_DIR%\local.properties" (
    echo [ERROR] local.properties not found. Run setup_android_sdk.bat first.
    exit /b 1
)
echo [OK] SDK configured.

:: Step 2: Clean and bundle assets
echo.
echo [Step 2/5] Bundling game assets...
if exist "%ASSETS_DIR%" rmdir /s /q "%ASSETS_DIR%"
mkdir "%ASSETS_DIR%"

:: Copy project configs
copy "%PROJECT_PATH%\project.json" "%ASSETS_DIR%\" >nul 2>&1
copy "%PROJECT_PATH%\start_screen.json" "%ASSETS_DIR%\" >nul 2>&1
copy "%PROJECT_PATH%\music_selection.json" "%ASSETS_DIR%\" >nul 2>&1

:: Copy all assets (charts, audio, textures, images)
if exist "%PROJECT_PATH%\assets" (
    xcopy "%PROJECT_PATH%\assets" "%ASSETS_DIR%\assets" /E /I /Q /Y >nul
    echo   Copied project assets/
)

:: Step 3: Copy compiled shaders
echo.
echo [Step 3/5] Copying shaders...
mkdir "%ASSETS_DIR%\shaders" 2>nul
if exist "%SHADER_SRC%" (
    copy "%SHADER_SRC%\*.spv" "%ASSETS_DIR%\shaders\" >nul 2>&1
    echo   Copied %SHADER_SRC% -^> assets/shaders/
) else (
    echo [ERROR] No compiled shaders at %SHADER_SRC%
    echo         Build the desktop project first to compile shaders.
    exit /b 1
)

:: Step 4: Build APK
echo.
echo [Step 4/5] Building APK with Gradle...
cd /d "%ANDROID_DIR%"

:: Use java directly with gradle-wrapper.jar (gradlew.bat has path issues in bash)
java -classpath "%ANDROID_DIR%\gradle\wrapper\gradle-wrapper.jar" org.gradle.wrapper.GradleWrapperMain assembleDebug
if errorlevel 1 (
    echo.
    echo [ERROR] Gradle build failed!
    exit /b 1
)

:: Step 5: Output
set "APK_PATH=%ANDROID_DIR%\app\build\outputs\apk\debug\app-debug.apk"
if not exist "%APK_PATH%" (
    echo [ERROR] APK not found at expected path.
    exit /b 1
)

echo.
echo [Step 5/5] APK ready!

:: Copy to output location if specified
if not "%OUTPUT_PATH%"=="" (
    copy "%APK_PATH%" "%OUTPUT_PATH%" >nul
    echo   Copied to: %OUTPUT_PATH%
    set "APK_PATH=%OUTPUT_PATH%"
)

echo.
echo ============================================
echo  BUILD SUCCESSFUL!
echo  APK: %APK_PATH%
echo  Size:
for %%A in ("%APK_PATH%") do echo    %%~zA bytes
echo ============================================

exit /b 0
