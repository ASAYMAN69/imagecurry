#!/bin/bash
convert "$1" -resize "900x900>" -quality 65 -define webp:method=6 -strip "$2"