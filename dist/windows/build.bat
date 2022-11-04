
rem copy ..\..\..\build-fdc-sds-qt6-Desktop_Qt_6_4_0_MinGW_64_bit-MinSizeRel\fdc-sds-qt6.exe packages\com.deltecent.fdcplus\data\"FDC+ Serial Server.exe"
copy ..\..\..\build-fdc-sds-qt6-Desktop_Qt_6_4_0_MinGW_64_bit-Debug\fdc-sds-qt6.exe packages\com.deltecent.fdcplus\data\"FDC+ Serial Server.exe"

windeployqt "packages\com.deltecent.fdcplus\data\FDC+ Serial Server.exe"

C:\Qt\Tools\QtInstallerFramework\4.4\bin\binarycreator --offline-only -c config\config.xml -p packages "FDC+ Serial Server Installer.exe"

scp "packages\com.deltecent.fdcplus\data\FDC+ Serial Server.zip" plesk.deltecent.com:
scp "FDC+ Serial Server Installer.exe" plesk.deltecent.com:
