@rem Gradle wrapper script for Windows
@rem Auto-downloads Gradle if not present

@if "%DEBUG%"=="" @echo off

set "DIRNAME=%~dp0"
set "APP_BASE_NAME=%~n0"
set "APP_HOME=%DIRNAME%"
set "DEFAULT_JVM_OPTS=-Xmx2048m -Dfile.encoding=UTF-8"

set "JAVA_EXE=java.exe"
where %JAVA_EXE% >NUL 2>NUL
if %ERRORLEVEL% equ 0 goto :execute

echo ERROR: JAVA_HOME is not set and no 'java' command could be found on PATH.
exit /b 1

:execute
set "CLASSPATH=%APP_HOME%\gradle\wrapper\gradle-wrapper.jar"

"%JAVA_EXE%" %DEFAULT_JVM_OPTS% -classpath "%CLASSPATH%" org.gradle.wrapper.GradleWrapperMain %*

:end
