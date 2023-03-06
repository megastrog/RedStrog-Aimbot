clang aim.c -Ofast -mfma -lX11 -lxdo -lm -o aim
strip --strip-unneeded aim
upx --best aim
sleep 1
./aim
