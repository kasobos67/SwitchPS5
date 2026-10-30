// SwitchPS5 native settings. SPDX-License-Identifier: GPL-3.0-or-later
#include "pe/ui/launcher.hpp"
#include "pe/ui/stock_assets.hpp"
#include <algorithm>
#include <array>

namespace pe::ui {
namespace {
enum Category { profiles, video, performance, audio, accessibility, system, downloads, sync, count };
constexpr const char* categories[]={"Profiles","Video","Performance","Audio","Accessibility","System","Downloads","Save sync"};
constexpr const char* performance_help[]={
    "Compiles code used in earlier sessions as a game starts.",
    "Draws effects once their shaders are ready, reducing compilation pauses.",
    "Lower GPU accuracy can be faster, but graphics may be incorrect.",
    "Less exact floating-point math may affect some games.",
    "Less exact GPU transfers may affect graphics.",
    "Keeps GPU readback accurate. Disabling it may affect effects.",
    "Fewer invalidation checks may leave stale textures.",
    "Gives back memory left mostly empty. Can cause stutters in games that keep rewriting it; leave off unless memory runs short.",
    "Allows a game to run faster when the console has capacity. Games tied to their frame rate may run their whole simulation faster. 100% keeps the original pace."
};
struct Row {
    std::string label;
    int Preferences::*number=nullptr;
    bool Preferences::*flag=nullptr;
    std::vector<std::string> choices;
    std::string help;
    int action=0, bit=-1;
};
Row choice(const char* label,int Preferences::*field,std::vector<std::string> values,const char* help="") { return {label,field,nullptr,std::move(values),help}; }
Row toggle(const char* label,bool Preferences::*field,const char* help="") { return {label,nullptr,field,{"Off","On"},help}; }
Row button(const char* label,int action,const char* help="") { Row r; r.label=label;r.action=action;r.help=help;return r; }
std::vector<Row> rows(int kind,Services& services,const Preferences& prefs) {
    if(kind==0) return {
        choice("Renderer",&Preferences::renderer,{"OpenGL","Vulkan"}),
        choice("Output resolution",&Preferences::output,{"1080p","1440p","2160p"}),
        choice("Resolution",&Preferences::resolution,services.resolution_labels()),
        choice("Upscaling filter",&Preferences::filter,services.filter_labels()),
        choice("ASTC textures",&Preferences::astc,services.astc_labels(),"Full quality uses four times the graphics memory for these textures. It can run out above 1x."),
        choice("Post-processing",&Preferences::post_processing,services.post_processing_labels(),"Applies a visual preset to the game picture. Requires the Vulkan renderer."),
        choice("Refresh rate",&Preferences::refresh,{"60 Hz","120 Hz"},"A display that cannot show 120 Hz stays at 60 Hz."),
        toggle("FPS overlay",&Preferences::hud),button("Overlay options",1),
        toggle("Frame generation",&Preferences::frame_gen,"Adds generated frames between the game's own frames. Requires Vulkan."),
        choice("Frame gen method",&Preferences::frame_gen_method,{"Lossless Scaling","MihawkGen"},"Lossless Scaling requires Lossless.dll in the lossless folder. MihawkGen uses the bundled driver support."),
        choice("Frame gen target",&Preferences::frame_gen_target,{"Auto","60 Hz","90 Hz","120 Hz","144 Hz","240 Hz"}),
        choice("Frame gen multiplier",&Preferences::frame_gen_multiplier,{"2x","3x","4x"})};
    if(kind==1) return {
        toggle("Compile ahead",&Preferences::block_list,performance_help[0]),
        toggle("Asynchronous shaders",&Preferences::async_shaders,performance_help[1]),
        toggle("Faster GPU emulation",&Preferences::fast_gpu,performance_help[2]),
        toggle("Faster CPU emulation",&Preferences::unsafe_cpu,performance_help[3]),
        toggle("Faster DMA",&Preferences::unsafe_dma,performance_help[4]),
        toggle("Reactive flushing",&Preferences::reactive_flushing,performance_help[5]),
        toggle("Skip CPU invalidation",&Preferences::skip_invalidation,performance_help[6]),
        toggle("Compact memory",&Preferences::compact_memory,performance_help[7]),
        choice("Speed limit",&Preferences::speed_limit,{"100%","150%","200%","300%","Unlimited"},performance_help[8]),
        choice("Memory",&Preferences::memory_layout,{"4 GiB","6 GiB","8 GiB"},"The memory size reported to games. Some mods require 6 or 8 GiB. The default is 4 GiB.")};
    if(kind==2) {
        std::vector<std::string> levels;for(int i=0;i<=100;i+=10)levels.push_back(std::to_string(i)+"%");
        return {choice("Game volume",&Preferences::volume,levels),toggle("Mute",&Preferences::mute),choice("Menu sounds",&Preferences::menu_volume,levels)};
    }
    if(kind==3) return {toggle("Larger text",&Preferences::large_text,"Draws the menu's small text larger."),toggle("High contrast",&Preferences::high_contrast,"Solid surfaces and brighter text make the selection easier to see."),toggle("Reduce motion",&Preferences::reduce_motion,"Limits menu transitions and loading-screen motion.")};
    if(kind==4) return {toggle("Detailed logging",&Preferences::detailed_logging,"Records more information when investigating a problem."),toggle("Write logs at once",&Preferences::immediate_logs,"Keeps the last line before a crash, but games stutter. Applies from the next start.")};
    if(kind==5) return {toggle("Vibration",&Preferences::vibration),choice("Vibration strength",&Preferences::vibration_strength,{"25%","50%","75%","100%"}),button("Game button mapping",2),button("Menu buttons",3,"Choose which face button selects, goes back, or opens an action in the menus.")};
    if(kind==6) {
        std::vector<Row> r={toggle("Enable colors",&Preferences::hud_colors),choice("Size",&Preferences::hud_size,{"Regular","Medium","Large"}),choice("Corner",&Preferences::hud_corner,{"Top left","Top right","Bottom left","Bottom right"})};
        const char* labels[]={"Backend","FPS","Frame time","Speed","CPU","GPU","Memory","Ping","CPU by core","Game resolution"};
        for(int i=0;i<10;++i) { Row v;v.label=labels[i];v.bit=i;v.choices={"Off","On"};r.push_back(v); }return r;
    }
    (void)prefs;return {};
}
int value(const Row& r,const Preferences& p) {
    if(r.flag)return p.*r.flag ? 1:0;
    if(r.bit>=0)return (((p.hud_fields>>r.bit)&1)!=0) != (r.bit==8);
    if(!r.number)return 0;
    int v=p.*r.number;
    if(r.number==&Preferences::volume || r.number==&Preferences::menu_volume)v/=10;
    if(r.number==&Preferences::vibration_strength)v=v/25-1;
    return v;
}
void assign(const Row& r,Preferences& p,int v) {
    if(r.flag)p.*r.flag=v!=0;
    else if(r.bit>=0) { bool on=(v!=0)!=(r.bit==8);p.hud_fields=(p.hud_fields&~(1<<r.bit))|(on?(1<<r.bit):0); }
    else if(r.number) {
        if(r.number==&Preferences::volume || r.number==&Preferences::menu_volume)v*=10;
        if(r.number==&Preferences::vibration_strength)v=(v+1)*25;
        p.*r.number=v;
    }
}
}
int Launcher::dialog_kind(Modal modal) const {
    switch(modal) {case Modal::video:return 0;case Modal::performance:return 1;case Modal::audio:return 2;case Modal::accessibility:return 3;case Modal::diagnostics:return 4;case Modal::controls:return 5;case Modal::hud:return 6;default:return -1;}
}
Launcher::Modal Launcher::settings_modal() const {
    switch(settings_.selected) {case video:return Modal::video;case performance:return Modal::performance;case audio:return Modal::audio;case accessibility:return Modal::accessibility;case system:return Modal::diagnostics;default:return Modal::none;}
}
void Launcher::initialize_settings() { settings_.visible=count;settings_.pitch=68;settings_.wrap=false;settings_.reset(count,0); for(auto* v:{&video_rows_,&performance_rows_,&hud_rows_}){v->visible=7;v->pitch=80;v->wrap=false;}video_rows_.reset(dialog_rows(Modal::video),0);performance_rows_.reset(dialog_rows(Modal::performance),0);hud_rows_.reset(dialog_rows(Modal::hud),0); }
void Launcher::enter_settings_content() {
    settings_content_=true;option_=0;message_.clear();
    for(auto* view:{&video_rows_,&performance_rows_,&hud_rows_}) {view->visible=7;view->pitch=80;view->wrap=false;}
    video_rows_.reset(dialog_rows(Modal::video),0);performance_rows_.reset(dialog_rows(Modal::performance),0);hud_rows_.reset(dialog_rows(Modal::hud),0);cue(audio::Cue::focus);
}
void Launcher::open_controller_settings() {prefs_=services_.preferences();option_=0;open(Screen::controllers,true);}
void Launcher::press_controller_settings(Key key) {
    if(key==Key::circle){open(Screen::home,false);return;}
    modal_=Modal::controls;press_dialog(key);if(modal_==Modal::controls)modal_=Modal::none;
}
void Launcher::draw_controller_settings(Canvas& c) {
    draw_frame(c,"Controllers","");draw_dialog(c,Modal::controls,1);
    text(c,"Touchpad + L1: Return to HOME",750,790,24,theme::kCopy);
    text(c,"Touchpad + R1: Show or hide the FPS overlay",750,834,24,theme::kCopy);
    static constexpr Hint hints[]={{Pad::circle,"Back"},{Pad::cross,"OK"}};draw_footer(c,hints,2);
}
void Launcher::press_settings(Key key) {
    if(!settings_content_) {
        if(key==Key::circle){open(Screen::home,false);return;}
        if(key==Key::up||key==Key::down){if(settings_.move(key==Key::down?1:-1)){option_=0;message_.clear();cue(audio::Cue::focus);}return;}
        if(key==Key::cross||key==Key::right)enter_settings_content();return;
    }
    if(key==Key::circle){settings_content_=false;message_.clear();cue(audio::Cue::back);return;}
    auto editor=settings_modal();
    if(editor==Modal::none){if(key==Key::cross||key==Key::right){switch(settings_.selected){case profiles:open_profiles();break;case downloads:open_sources();break;case sync:open_sync_setup();break;default:break;}}return;}
    if(key==Key::left){settings_content_=false;cue(audio::Cue::back);return;}
    modal_=editor;press_dialog(key);if(modal_==editor)modal_=Modal::none;
}
int Launcher::dialog_rows(Modal modal) const {
    if(modal==Modal::game)return (services_.save_transfer_available()?9:8)+(library_.selected<static_cast<int>(games_.size())&&!games_[library_.selected].sources.empty()&&!games_[library_.selected].remote?1:0);
    return static_cast<int>(rows(dialog_kind(modal),services_,prefs_).size());
}
float Launcher::dialog_row_top(Modal modal,int row) const {
    float scroll=modal==Modal::video?video_rows_.scroll():modal==Modal::performance?performance_rows_.scroll():modal==Modal::hud?hud_rows_.scroll():0;
    return 228+row*80-scroll;
}
void Launcher::press_dialog(Key key) {
    const int kind=dialog_kind(modal_);auto r=rows(kind,services_,prefs_);if(r.empty())return;
    if(key==Key::circle){if(modal_==Modal::hud){modal_=modal_shown_=Modal::none;option_=8;cue(audio::Cue::back);}else close_modal();return;}
    auto* view=kind==0?&video_rows_:kind==1?&performance_rows_:kind==6?&hud_rows_:nullptr;
    if(key==Key::up||key==Key::down){if(view){view->move(key==Key::down?1:-1);option_=view->selected;}else option_=std::clamp(option_+(key==Key::down?1:-1),0,static_cast<int>(r.size())-1);message_.clear();cue(audio::Cue::focus);return;}
    if(key!=Key::cross&&key!=Key::left&&key!=Key::right)return;
    option_=std::clamp(option_,0,static_cast<int>(r.size())-1);const auto& row=r[option_];
    if(row.action){if(key==Key::left)return;if(row.action==1){hud_rows_.reset(dialog_rows(Modal::hud),0);open_modal(Modal::hud);}else if(row.action==2)open_mapping(false);else open_menu_mapping();return;}
    if(key==Key::cross&&row.number){open_settings_choice();return;}
    const auto before=prefs_;const int n=static_cast<int>(row.choices.size());if(!n)return;
    assign(row,prefs_,(value(row,prefs_)+(key==Key::left?-1:1)+n)%n);
    if(!save_preferences(true)){prefs_=before;cue(audio::Cue::error);}else {apply_look();cue(value(row,prefs_)?audio::Cue::toggle:audio::Cue::toggle_off);if(kind==5&&prefs_.vibration)services_.vibration_pulse(prefs_.vibration_strength);}
}
void Launcher::open_settings_choice() {
    setting_choice_category_=dialog_kind(modal_);setting_choice_option_=option_;
    const auto r=rows(setting_choice_category_,services_,prefs_);if(option_<0||option_>=static_cast<int>(r.size()))return;
    const auto& row=r[option_];setting_choice_rows_.visible=6;setting_choice_rows_.pitch=90;setting_choice_rows_.wrap=false;
    setting_choice_rows_.reset(static_cast<int>(row.choices.size()),value(row,prefs_));open_modal(Modal::settings_choice);
}
void Launcher::press_settings_choice(Key key) {
    if(key==Key::circle){close_modal();option_=setting_choice_option_;return;}
    if(key==Key::up||key==Key::down){setting_choice_rows_.move(key==Key::down?1:-1);cue(audio::Cue::focus);return;}
    if(key!=Key::cross)return;
    const auto r=rows(setting_choice_category_,services_,prefs_);const auto& row=r[setting_choice_option_];auto before=prefs_;
    assign(row,prefs_,setting_choice_rows_.selected);
    if(!save_preferences(true)){prefs_=before;cue(audio::Cue::error);return;}
    apply_look();close_modal();option_=setting_choice_option_;cue(audio::Cue::saved);
}
void Launcher::draw_settings_choice(Canvas& c,float open) {
    auto r=rows(setting_choice_category_,services_,prefs_);const auto& selected=r[setting_choice_option_];
    c.list.push_opacity(open);glass(c,{390,135,1140,810},0,theme::kPanel,theme::kPanelEdge,1);
    text(c,selected.label,450,212,36,theme::kTitle);c.list.push_clip({420,255,1080,550});
    for(int i=setting_choice_rows_.first_row();i<=setting_choice_rows_.last_row();++i){Rect row{450,264+i*90-setting_choice_rows_.scroll(),1020,88};plate(c,kRowPlate,row,i==setting_choice_rows_.selected?1:0);text(c,selected.choices[i],486,baseline(row.y,row.h,30),30,theme::kText);if(i==value(selected,prefs_))text(c,"✓",1420,baseline(row.y,row.h,30),30,theme::kLime,Align::right);}
    c.list.pop_clip();scrollbar(c,setting_choice_rows_,1484,264,538);
    if(!message_.empty())notice_block(c,message_,450,839,22,28,theme::kWarning,1020,1,true);
    static constexpr Hint hints[]={{Pad::circle,"Back"},{Pad::cross,"OK"}};draw_hints_right(c,hints,2,1470,897,theme::kText);c.list.pop_opacity();
}
void Launcher::draw_dialog(Canvas& c,Modal modal,float open) {
    c.list.push_opacity(open);const int kind=dialog_kind(modal);auto r=rows(kind,services_,prefs_);
    const bool scrolls=kind==0||kind==1||kind==6;const auto& view=kind==1?performance_rows_:kind==6?hud_rows_:video_rows_;
    const int first=scrolls?view.first_row():0;const int last=std::min(static_cast<int>(r.size())-1,scrolls?view.last_row():static_cast<int>(r.size())-1);
    c.list.push_clip({708,222,1110,568});
    for(int i=first;i<=last;++i){const auto& row=r[i];Rect rect{714,dialog_row_top(modal,i),1086,78};plate(c,kRowPlate,rect,(settings_content_||screen_==Screen::controllers||modal_==Modal::hud)&&i==option_?1:0);
        int v=value(row,prefs_);std::string shown=row.action?">":row.choices.empty()?"Unavailable":row.choices[std::clamp(v,0,static_cast<int>(row.choices.size())-1)];
        float used=text_shrink(c,shown,1764,baseline(rect.y,rect.h,28),28,row.flag&&v?theme::kLime:theme::kCopy,470,Align::right);
        text_shrink(c,row.label,750,baseline(rect.y,rect.h,28),28,theme::kText,1014-used-36);
    }
    c.list.pop_clip();if(scrolls)scrollbar(c,view,1821,228,558);
    const auto index=std::clamp(option_,0,std::max(0,static_cast<int>(r.size())-1));
    if(!r.empty())text_block(c,r[index].help,750,846,24,32,theme::kCopy,1008,3);
    c.list.pop_opacity();
}
void Launcher::draw_settings(Canvas& c) {
    draw_frame(c,"System Settings","");
    for(int i=0;i<count;++i){Rect r{156,209+i*68.0f,438,66};if(i==settings_.selected){if(!settings_content_)plate_focus(c,kRowPlate,r,1);c.list.rounded_rect({171,r.y+12,4,r.h-24},0,theme::kLime);}text(c,categories[i],188,baseline(r.y,r.h,30),30,i==settings_.selected?theme::kLime:theme::kText);}
    auto editor=settings_modal();
    if(editor!=Modal::none)draw_dialog(c,editor,1);
    else {
        Rect row{714,228,1086,88};plate(c,kRowPlate,row,settings_content_?1:0);
        const char* title=settings_.selected==profiles?"User Settings":settings_.selected==downloads?"Downloads":"Save sync";
        text(c,title,750,baseline(row.y,row.h,30),30,theme::kText);
        if(settings_.selected==profiles){for(std::size_t i=0;i<profiles_.size();++i){const auto a=presentation_.avatar(profiles_[i].name);profile_avatar(c,a.icon,a.color,{750+i*124.0f,382,88,88},true);if(profiles_[i].playing)c.list.ring(794+i*124.0f,426,49,3,theme::kLime);}}
        const char* copy=settings_.selected==profiles?"Each user keeps their own save data, settings and recent software.":settings_.selected==downloads?"Games on your network, downloaded when you play them.":"Each profile's save data on a server of its own, synced before and after a game.";
        text_block(c,copy,750,settings_.selected==profiles?550:390,24,36,theme::kCopy,1008,3);
    }
    if(!message_.empty())notice_block(c,message_,714,949,22,28,message_warning_?theme::kWarning:theme::kCopy,1086,1,message_warning_);
    static constexpr Hint hints[]={{Pad::circle,"Back"},{Pad::cross,"OK"}};draw_footer(c,hints,2);
}
} // namespace pe::ui
