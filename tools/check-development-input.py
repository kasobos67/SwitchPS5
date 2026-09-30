from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
code=r'''
#include "development_input.h"
#include "radio_input.h"
extern "C" uint64_t SDL_GetTicks64(void) { return 0; }
#include <sstream>
#include <cassert>
int main(){
 auto ui = ps5::pad::neutral_data(); ui.connected=1; ui.buttons=0x4000;
 radio_input_development_sample(&ui); radio_input_event_t event{};
 assert(radio_input_next(&event) && event.key==RADIO_INPUT_CROSS && event.pressed);
 radio_input_development_sample(&ui); assert(!radio_input_next(&event));
 ui.buttons=0; radio_input_development_sample(&ui);
 assert(radio_input_next(&event) && event.key==RADIO_INPUT_CROSS && !event.pressed);
 radio_input_development_sample(nullptr); assert(!radio_input_next(&event));
 Eden::DevelopmentInput p;assert(!p.Sample(0));
 std::istringstream a("1 8192 100");assert(p.Read(a,1000));assert(p.Sample(1000)->buttons==8192);
 assert(p.Sample(1099)->buttons==8192);assert(p.Sample(1100)->buttons==0);assert(!p.Sample(1101));
 for(auto s:{"1 8192 100","0 8192 100","2 65536 100","2 8192 0","2 8192 3001","oops"}){
  std::istringstream in(s);assert(!p.Read(in,1200));
 }
 std::istringstream chord("2 1049600 200");assert(p.Read(chord,2000));auto v=p.Sample(2000);assert(v && ps5::pad::is_usable(*v));assert(v->buttons==(ps5::pad::kButtonTouchPad|ps5::pad::kButtonL1));assert(p.Sample(2200)->buttons==0);
 std::istringstream st("3 0 500 128 0 200 128");assert(p.Read(st,3000));auto w=p.Sample(3000);
 assert(w->left_stick.x==128&&w->left_stick.y==0&&w->right_stick.x==200&&w->right_stick.y==128);
 auto e=p.Sample(3500);assert(e->left_stick.y==128&&e->right_stick.x==128);assert(!p.Sample(3501));
 std::istringstream bad("4 0 100 256 0 0 0");assert(!p.Read(bad,4000));
 std::istringstream plain("4 8192 100");assert(p.Read(plain,4000));assert(p.Sample(4000)->left_stick.x==128);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['gcc','-DEDEN_DEV_ROM_ID=1','-ffunction-sections','-fsanitize=address,undefined','-c',str(r/'headless/prosperoeden/radio_input.c'),'-o',str(p/'radio.o')],check=True)
 subprocess.run(['g++','-std=c++20','-DEDEN_DEV_ROM_ID=1','-Wl,--gc-sections','-fsanitize=address,undefined','-I'+str(r/'headless/prosperoeden'),str(p/'radio.o'),'-I'+str(r/'headless'),'-I'+str(r.parent/'ps5-native-gamepad-input-research/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Development pulses: expiry/release, monotonic commands, invalid masks/durations, sticks and actual menu chord PASS')