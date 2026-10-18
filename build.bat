@echo off
REM Local build wrapper. Requires a configured Visual Studio and WDK shell.
REM build.bat          -- build all
REM build.bat clean    -- clean

setlocal
set ROOT=%~dp0
cd /d %ROOT%

if "%1"=="clean" (
    del /q driver\*.obj driver\*.sys driver\*.pdb 2>nul
    del /q control\*.obj control\*.sys control\*.pdb 2>nul
    del /q tool\*.obj tool\*.exe 2>nul
    del /q *.sys *.exe 2>nul
    goto :eof
)

REM Prepare the optional offsets header when it is absent.
if not exist "driver\winload_offsets.h" (
    echo [*] running winload scanner...
    where py >nul 2>nul && set PY=py
    if not defined PY where python >nul 2>nul && set PY=python
    if not defined PY (
        echo [-] no python. create driver\winload_offsets.h manually or install python.
        goto :scanner_done
    )
    %PY% tools\winload_scanner.py C:\Windows\System32\winload.exe --out driver\winload_offsets.h
    if errorlevel 1 (
        echo [-] scanner failed, writing stub...
        (
            echo // stub, scanner failed
            echo #pragma once
            echo #define WL_LIST_HEAD_RVA  0
            echo #define WL_LDR_DLLBASE_OFF 0x30
            echo #define WL_LDR_SIZEOFIMAGE_OFF 0x40
            echo #define WL_BUILD_ID "unknown"
        ) > driver\winload_offsets.h
    )
)
:scanner_done

echo [*] building driver
nmake /f Makefile dark_rvbbit.sys
if errorlevel 1 goto :err

echo [*] building ctl
nmake /f Makefile dark_rvbbit_ctl.sys
if errorlevel 1 goto :err

echo [*] building tool
nmake /f Makefile rvbbitctl.exe
if errorlevel 1 goto :err

REM Use a local signing certificate when one is available.
if exist "certs\dark_rvbbit.pfx" (
    signtool sign /fd sha256 /f certs\dark_rvbbit.pfx /p "%CERT_PASS%" dark_rvbbit.sys
    signtool sign /fd sha256 /f certs\dark_rvbbit.pfx /p "%CERT_PASS%" dark_rvbbit_ctl.sys
    signtool sign /fd sha256 /f certs\dark_rvbbit.pfx /p "%CERT_PASS%" rvbbitctl.exe
) else (
    echo [!] no cert, skipping sign
)

REM The separate UEFI component requires an EDK II environment.
if exist "%EDK2%\" (
    pushd uefi
    build -a X64 -t VS2022 -p hv_dxe.inf
    popd
) else (
    echo [!] EDK2 not set, skipping uefi
)

echo [*] done.
goto :eof

:err
echo [-] build failed.
exit /b 1