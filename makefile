
app:
	g++ main.cc  -std=c++14 -g -static  -I/home/gyl/tools/ffmpeg-static/target/include  -L/home/gyl/tools/ffmpeg-static/target/lib  -L/usr/local/lib -lavdevice -lavfilter -lavformat -lavcodec  -lpostproc -lswresample -lswscale -lavutil    -lxcb -lxcb-shm -lxcb-shape -lxcb-xfixes  -pthread  -lass -lvidstab -lm -lgomp -lpthread -lfreetype   -lz  -lvpx  -lopencore-amrwb  -lfdk-aac -lmp3lame -lopencore-amrnb -lopenjp2 -lopus -ltheoraenc -ltheoradec -logg -lvorbis -lvorbisenc -lx264  -lxvidcore    -pthread  -ldl -lpthread  -lX11 -static-libstdc++  -Wl,-Bstatic -lx265  -lnuma -lssl -lcrypto   -lavdevice -lavfilter -lavformat -lavcodec -lpostproc -lswresample -lswscale -lavutil  -lm -lxcb -lXau -lXdmcp -lxcb-shm -lxcb -lXau -lXdmcp -lxcb-shape -lxcb -lXau -lXdmcp -lxcb-xfixes -lxcb-render -lxcb-shape -lxcb -lXau -lXdmcp -L/usr/local/lib -Wl,-rpath,/usr/local/lib -Wl,--enable-new-dtags -lSDL2 -Wl,--no-undefined -lm -ldl -lpthread -lrt -pthread -lm -L/home/gyl/tools/ffmpeg-static/target/lib -lfribidi -L/home/gyl/tools/ffmpeg-static/target/lib -lass -lm -lharfbuzz -lglib-2.0 -pthread -lpcre -pthread -lfontconfig -lexpat -lfreetype -lexpat -lfribidi -lfreetype -lpng16 -lm -lz -lm -lz -L/home/gyl/tools/ffmpeg-static/target/lib -lvidstab -L/home/gyl/tools/ffmpeg-static/target/lib -lzimg -lstdc++ -ldl -L/home/gyl/tools/ffmpeg-static/target/lib -lfontconfig -lexpat -lfreetype -lexpat -lfreetype -lpng16 -lm -lz -lm -lz -L/home/gyl/tools/ffmpeg-static/target/lib -lfreetype -lpng16 -lm -lz -lm -lz -lm -lz -L/home/gyl/tools/ffmpeg-static/target/lib -lssl -ldl -lcrypto -ldl -L/home/gyl/tools/ffmpeg-static/target/lib -lrtmp -lz -lssl -ldl -lcrypto -ldl -L/home/gyl/tools/ffmpeg-static/target/lib -lvpx -lm -lpthread -L/home/gyl/tools/ffmpeg-static/target/lib -lvpx -lm -lpthread -L/home/gyl/tools/ffmpeg-static/target/lib -lvpx -lm -lpthread -L/home/gyl/tools/ffmpeg-static/target/lib -lvpx -lm -lpthread -L/home/gyl/tools/ffmpeg-static/target/lib -lwebpmux -lm -lwebp -lm -pthread -pthread -lm -lopencore-amrwb -lz -L/home/gyl/tools/ffmpeg-static/target/lib -lfdk-aac -lm -lmp3lame -lm -lopencore-amrnb -L/home/gyl/tools/ffmpeg-static/target/lib -lopenjp2 -lm -L/home/gyl/tools/ffmpeg-static/target/lib -lopus -lm -L/home/gyl/tools/ffmpeg-static/target/lib -lspeex -lm -ltheoraenc -ltheoradec -logg -lvo-amrwbenc -L/home/gyl/tools/ffmpeg-static/target/lib -lvorbis -lm -logg -L/home/gyl/tools/ffmpeg-static/target/lib -lvorbisenc -lvorbis -lm -logg -L/home/gyl/tools/ffmpeg-static/target/lib -lwebp -lm -pthread -L/home/gyl/tools/ffmpeg-static/target/lib -lx264 -lpthread -lm -L/home/gyl/tools/ffmpeg-static/target/lib -lx265 -lstdc++ -lm -lgcc_eh -lgcc -lgcc_eh -lgcc -lrt -ldl -lnuma -lxvidcore -lm -lm -lsoxr -lm -pthread -lm -lpthread -lm -lz

