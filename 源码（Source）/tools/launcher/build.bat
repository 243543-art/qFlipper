@echo off
windres launcher.rc -o launcher_res.o
g++ -O2 -mwindows -municode launcher.cpp launcher_res.o -o qFlipper.exe -luser32 -static-libgcc -static-libstdc++

