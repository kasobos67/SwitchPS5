from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'headless/main.cpp').read_text()
a=s.index('        std::string selected_game;');b=s.index('        if (selected_game.empty()) return 0;',a);body=s[a:b]
code=r'''
#include <filesystem>
#include <string>
#include <vector>
#include <utility>
#include <system_error>
#include <stdexcept>
#include <cassert>
#define EDEN_DEV_ROM_ID "01009EA00B714000"
namespace Eden {std::vector<std::filesystem::directory_entry> entries;
auto ReadNativeDirectory(const char*,std::error_code&){return entries;}}
int menus=0;std::string SelectProsperoEdenGame(std::string&){++menus;return "menu";}
unsigned long long eden_game_title_id(const char*){return 0;}  // file-name match only
struct App{bool autoboot_pending=true;std::string launch_error;std::string relaunch_game;unsigned guest_fault_retries=0;
std::string development_id=EDEN_DEV_ROM_ID;
std::string select(){
'''+body+r'''
return selected_game;}};
int main(){
 Eden::entries.emplace_back(std::filesystem::path("Sample Quest [0100000000020000].nsp"));
 Eden::entries.emplace_back(std::filesystem::path("Demo Racer [0100000000010000].nsp"));
 App app;assert(app.select()=="/app0/assets/roms/Sample Quest [0100000000020000].nsp");
 assert(menus==0);assert(app.select()=="menu" && menus==1);
 // A guest-fault relaunch reuses the game without the menu and keeps its retry count.
 app.relaunch_game="again";app.guest_fault_retries=1;
 assert(app.select()=="again" && menus==1 && app.relaunch_game.empty() && app.guest_fault_retries==1);
 assert(app.select()=="menu" && menus==2 && app.guest_fault_retries==0);
 // dev-settings rom= overrides the build's development title.
 App other;other.development_id="0100000000010000";
 assert(other.select()=="/app0/assets/roms/Demo Racer [0100000000010000].nsp" && menus==2);
 Eden::entries.clear();App missing;try{missing.select();assert(false);}catch(const std::runtime_error&){}
 assert(missing.select()=="menu");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Actual development selection: exact title, first-launch only, missing ROM rejection, fault relaunch, title override PASS')