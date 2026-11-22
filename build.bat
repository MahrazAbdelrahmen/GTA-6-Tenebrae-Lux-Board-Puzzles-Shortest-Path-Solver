@echo off
rem Build Tenebrae Lux with the w64devkit gcc that ships next to raylib
set RAYLIB=C:\raylib\raylib\src
C:\raylib\w64devkit\bin\gcc.exe tenebrae.c puzzle.c -o tenebrae.exe -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -I%RAYLIB% -L%RAYLIB% -lraylib -lopengl32 -lgdi32 -lwinmm
if %errorlevel%==0 echo Built tenebrae.exe
