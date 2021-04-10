#!/bin/bash
gcc app.c $(pkg-config --cflags --libs libftdi1) -o vp53app
#read -p "Press enter to continue"
