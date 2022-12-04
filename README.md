# ncpgen_mktb
A tool to generate an NCPatcher environment for Mario Kart DS projects that use Mario Kart Toolbox.

## Building

Tools:
 - Microsoft Visual Studio 2022
 - CMake 3.20

To build the project, the library [zeux/pugixml](https://github.com/zeux/pugixml) v1.13 is required. \
Create a `libs` folder at the same directory level that `source` is at. \
Download the library, and extract `pugixml-1.13.zip/pugixml-1.13` to `libs/pugixml`.

After that open the project in your preferred IDE, select the building toolkit (MSVC 2022 is recommended) and build it.

## Usage

This program should be called before and after NCPatcher runs.

Syntax:
ncpgen MODE XML_PATH
 - MODE = 0 for pre-built, 1 for post-build
 - XML_PATH = The path of the ROM XML project

Example compilation script:
```bat
REM compileasm.bat

@echo off
echo Generating NCPatcher environment...
ncpgen 0 rom/proj.xml
if %errorlevel% neq 0 goto end
cd src
echo Running NCPatcher...
ncpatcher --verbose
cd..
echo Cleaning NCPatcher environment...
ncpgen 1 rom/proj.xml
:end
```
