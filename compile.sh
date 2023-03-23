clang aim.c -Ofast -mfma -lX11 -lxdo -pthread -lm -o aim
strip --strip-unneeded aim
upx --best aim
clang aim.c -DENABLE_SHIFT -Ofast -mfma -lX11 -lxdo -pthread -lm -o aim_shift
strip --strip-unneeded aim_shift
upx --best aim_shift
clang aim.c -DEFFICIENT_SCAN -Ofast -mfma -lX11 -lxdo -pthread -lm -o aim_triggerbot_fastscan
strip --strip-unneeded aim_triggerbot_fastscan
upx --best aim_triggerbot_fastscan
sleep 1
./aim