/* Same section bounds used by ps5-opengl/native-app/ps5-pie.ld. */
PROVIDE(__eh_frame_start = ADDR(.eh_frame));
PROVIDE(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));
PROVIDE(__eh_frame_hdr_start = ADDR(.eh_frame_hdr));
PROVIDE(__eh_frame_hdr_end = ADDR(.eh_frame_hdr) + SIZEOF(.eh_frame_hdr));
/* The app's code, for the crash report (headless/crash_report.cpp). */
PROVIDE(__eden_text_start = ADDR(.text));
PROVIDE(__eden_text_end = ADDR(.text) + SIZEOF(.text));
