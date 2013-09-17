@echo off
set JAVA_HOME=C:\Tools\JDK

set jarBaseName=jleaker.jar
set soBaseName=jleaker.dll
set jar="%CD%\..\ant\dist\%jarBaseName%"

if not exist %jar% (
	set jar="%CD%\%jarBaseName%"
)
set lib="%CD%\..\src\cpp\jleaker_vsto\Debug\%soBaseName%"
set conf="%CD%\..\conf"

if not exist %lib% (
	set lib="%CD%\%soBaseName%"
)

if not exist %conf% (
	set conf="%CD%\conf"
)

%JAVA_HOME%\jre\bin\java.exe -cp %jar%;%JAVA_HOME%\lib\tools.jar com.intel.swiss.sws.netstar.application.jleaker.main.JLeaker --lib-path %lib% --conf-path %conf% %*