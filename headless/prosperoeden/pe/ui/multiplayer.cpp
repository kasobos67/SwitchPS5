// SPDX-License-Identifier: GPL-3.0-or-later
#include "pe/ui/launcher.hpp"
#include <algorithm>
#include <charconv>

namespace pe::ui {
using Eden::Multiplayer::Phase;
using audio::Cue;
static constexpr int kVisibleRoomMembers = 8;


void Launcher::open_multiplayer() {
    room_connection_ = services_.room_connection();
    room_status_ = services_.room_status();
    room_row_ = 0;
    room_page_ = RoomPage::choice;
    open_modal(Modal::multiplayer);
}

void Launcher::press_multiplayer(Key key) {
    if (key == Key::circle) {
        room_edit_stop_ = true;
        room_directory_stop_ = true;
        room_directory_refresh_ = false;
        room_password_join_ = false;
        room_connection_.password.clear();
        if (room_page_ == RoomPage::choice) close_modal();
        else { room_page_ = RoomPage::choice; room_row_ = 0; message_.clear(); cue(Cue::back); }
        return;
    }
    if (room_edit_.valid()) return;
    if (key == Key::square && room_status_.phase != Phase::Idle && room_status_.phase != Phase::Failed) {
        services_.leave_room();
        room_connection_.password.clear();
        return;
    }
    if (room_page_ == RoomPage::choice && room_status_.phase != Phase::Connected) {
        if (key == Key::up || key == Key::down) {
            const int next = key == Key::down ? 1 : 0;
            if (next != room_row_) { room_row_ = next; cue(Cue::focus); }
        }
        if (key == Key::cross) {
            room_page_ = room_row_ == 0 ? RoomPage::public_rooms : RoomPage::private_room;
            room_row_ = 0;
            room_connection_ = services_.room_connection();
            if (room_page_ == RoomPage::public_rooms) {
                room_connection_.password.clear();
                refresh_public_rooms();
            }
            cue(Cue::open);
        }
        return;
    }
    if (room_status_.phase == Phase::Connected) {
        if (key == Key::up || key == Key::down)
            room_row_ = std::clamp(room_row_ + (key == Key::down ? 1 : -1), 0,
                std::max(0, static_cast<int>(room_status_.members.size()) - kVisibleRoomMembers));
        if (key == Key::cross) services_.leave_room();
        return;
    }
    if (room_page_ == RoomPage::public_rooms && room_status_.phase != Phase::Connecting &&
        room_status_.phase != Phase::Disconnecting) {
        if (key == Key::triangle) { refresh_public_rooms(); return; }
        if (key == Key::up || key == Key::down) {
            room_row_ = std::clamp(room_row_ + (key == Key::down ? 1 : -1), 0,
                                   static_cast<int>(room_directory_.rooms.size()));
            cue(Cue::focus);
        }
        if (key == Key::left || key == Key::right) { // a page at a time: public lists are long
            const int visible = room_status_.error.empty() ? 6 : 4;
            room_row_ = std::clamp(room_row_ + (key == Key::right ? visible : -visible), 0,
                                   static_cast<int>(room_directory_.rooms.size()));
            cue(Cue::focus);
        }
        if (key != Key::cross) return;
        if (room_row_ > 0) {
            const auto& room = room_directory_.rooms[room_row_ - 1];
            if (!room.compatible) { say(tr("This room uses a different protocol version."), true); return; }
            room_connection_.host = room.address;
            room_connection_.port = room.port;
            room_connection_.password.clear();
            if (!room.password) {
                if (!services_.connect_public_room(room_connection_))
                    say(tr("Could not start the connection or save its settings."), true);
                return;
            }
        }
        room_edit_field_ = room_row_ == 0 ? 2 : 3;
        room_password_join_ = room_edit_field_ == 3;
        room_edit_stop_ = false;
        try {
            room_edit_ = std::async(std::launch::async, [this, field = room_edit_field_,
                initial = room_edit_field_ == 2 ? room_connection_.nickname : std::string{}] {
                return services_.room_text(field, initial, room_edit_stop_);
            });
        } catch (const std::exception&) {
            room_password_join_ = false;
            say(tr("The keyboard could not be opened."), true);
        }
        return;
    }
    if (key == Key::up || key == Key::down) {
        room_row_ = (room_row_ + (key == Key::down ? 1 : 4)) % 5;
        cue(Cue::focus);
        return;
    }
    if (key != Key::cross) return;
    const bool idle = room_status_.phase == Phase::Idle || room_status_.phase == Phase::Failed;
    if (room_row_ == 4) {
        if (idle) {
            if (!services_.connect_room(room_connection_))
                say(tr("Could not start the connection or save its settings."), true);
        } else services_.leave_room();
        return;
    }
    if (!idle) {
        say(tr("Leave the room before changing connection details."), true);
        return;
    }
    const std::string fields[]{room_connection_.host, std::to_string(room_connection_.port),
                               room_connection_.nickname, room_connection_.password};
    room_edit_field_ = room_row_;
    room_edit_stop_ = false;
    try {
        room_edit_ = std::async(std::launch::async, [this, value = fields[room_row_], field = room_row_] {
            return services_.room_text(field, value, room_edit_stop_);
        });
    } catch (const std::exception&) {
        say(tr("The keyboard could not be opened."), true);
    }
}

void Launcher::update_multiplayer() {
    if (room_directory_request_.valid() &&
        room_directory_request_.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        Eden::Multiplayer::RoomDirectory result;
        try { result = room_directory_request_.get(); }
        catch (const std::exception&) { result.error = "Could not download public rooms."; }
        if (!room_directory_stop_.load()) {
            // While the list is on screen the cursor stays on the room it was on: a refresh
            // reorders rooms as players come and go. Elsewhere room_row_ belongs to another view.
            const bool browsing = modal_ == Modal::multiplayer && room_page_ == RoomPage::public_rooms &&
                room_status_.phase != Phase::Connected && room_status_.phase != Phase::Connecting &&
                room_status_.phase != Phase::Disconnecting;
            const auto key_of = [](const Eden::Multiplayer::PublicRoom& room) {
                return room.address + ":" + std::to_string(room.port);
            };
            std::string selected;
            if (browsing && room_row_ > 0 && room_row_ <= static_cast<int>(room_directory_.rooms.size()))
                selected = key_of(room_directory_.rooms[room_row_ - 1]);
            if (result.error.empty()) room_directory_ = std::move(result);
            else room_directory_.error = std::move(result.error);
            room_directory_loaded_ = true;
            if (browsing) {
                const int count = static_cast<int>(room_directory_.rooms.size());
                room_row_ = std::min(room_row_, count);
                for (int i = 0; !selected.empty() && i < count; ++i) {
                    if (key_of(room_directory_.rooms[i]) == selected) { room_row_ = i + 1; break; }
                }
            }
        }
        if (room_directory_refresh_ && modal_ == Modal::multiplayer && room_page_ == RoomPage::public_rooms)
            refresh_public_rooms();
    }
    if (modal_ == Modal::multiplayer) {
        auto next = services_.room_status();
        if (next.phase != room_status_.phase && room_page_ != RoomPage::choice) room_row_ = 0;
        room_status_ = std::move(next);
        if (room_status_.phase == Phase::Connected && room_page_ != RoomPage::choice)
            room_row_ = std::min(room_row_, std::max(0,
                static_cast<int>(room_status_.members.size()) - kVisibleRoomMembers));
    }
    if (!room_edit_.valid() || room_edit_.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
        return;
    std::optional<std::string> answer;
    try { answer = room_edit_.get(); }
    catch (const std::exception&) {
        room_password_join_ = false;
        say(tr("The keyboard could not be opened."), true);
        return;
    }
    if (!answer || room_edit_stop_ || modal_ != Modal::multiplayer) {
        room_password_join_ = false;
        return;
    }
    switch (room_edit_field_) {
    case 0: room_connection_.host = *answer; break;
    case 1: {
        unsigned port = 0;
        const auto result = std::from_chars(answer->data(), answer->data() + answer->size(), port);
        if (result.ec != std::errc{} || result.ptr != answer->data() + answer->size() || port == 0 || port > 65535)
            say(tr("Enter a UDP port between 1 and 65535."), true);
        else room_connection_.port = static_cast<std::uint16_t>(port);
        break;
    }
    case 2: room_connection_.nickname = *answer; break;
    case 3: room_connection_.password = *answer; break;
    }
    // Saved as typed, so the form survives closing the app before a join. A public room's
    // address and password are never saved: they would replace the private room.
    if (room_page_ == RoomPage::private_room) services_.remember_room(room_connection_, true);
    else if (room_edit_field_ == 2) services_.remember_room(room_connection_, false);
    if (room_password_join_) {
        room_password_join_ = false;
        if (!services_.connect_public_room(room_connection_))
            say(tr("Could not start the connection or save its settings."), true);
        room_connection_.password.clear();
    }
}

void Launcher::refresh_public_rooms() {
    if (room_directory_request_.valid()) {
        // Reopening while the previous cancellation completes must still fetch a fresh list.
        room_directory_refresh_ = room_directory_stop_.load();
        return;
    }
    room_directory_refresh_ = false;
    room_directory_stop_ = false;
    room_directory_.error.clear();
    message_.clear();
    try {
        room_directory_request_ = std::async(std::launch::async, [this] {
            return services_.public_rooms(room_directory_stop_);
        });
    } catch (const std::exception&) { room_directory_.error = "Could not start the public room request."; }
}

void Launcher::draw_public_rooms(Canvas& c) {
    const float left=548,right=1372,width=824;
    const bool loading=room_directory_request_.valid()&&!room_directory_stop_.load();
    const std::string notice=!message_.empty()?message_:!room_status_.error.empty()?room_status_.error:room_directory_.error;
    text(c,loading?"Refreshing...":std::to_string(room_directory_.rooms.size())+" public rooms",right,284,24,theme::kCopy,Align::right);
    Rect nick{left,338,width,72};plate(c,kRowPlate,nick,room_row_==0?1:0);
    text(c,"Nickname",left+36,383.5f,28,theme::kText);
    text_fit(c,room_connection_.nickname,right-36,383.5f,28,theme::kCopy,width-358,Align::right);
    const int visible=notice.empty()?5:4;const int count=static_cast<int>(room_directory_.rooms.size());
    const int first=std::clamp(room_row_-visible,0,std::max(0,count-visible));
    for(int i=first;i<std::min(count,first+visible);++i) {
        const auto& room=room_directory_.rooms[i];const float top=426+(i-first)*76.0f;
        plate(c,kRowPlate,{left,top,width,74},room_row_==i+1?1:0);
        text_fit(c,room.name,left+36,top+31,26,theme::kText,width-252);
        text_fit(c,std::to_string(room.players)+" / "+std::to_string(room.capacity),right-36,top+31,24,theme::kCopy,156,Align::right);
        std::string detail=room.game+(room.password?" | Password":"")+(room.compatible?"":" | Incompatible");
        text_fit(c,detail,left+36,top+62,22,theme::kCopy,width-72);
    }
    if(count==0)text(c,loading?"Loading public rooms...":"No public rooms found.",left+36,476,24,theme::kCopy);
    if(count>visible) {
        float height=visible*76.0f,thumb=std::max(28.0f,height*visible/count);
        c.list.rounded_rect({right+14,426,6,height},3,theme::kRule.with_alpha(.35f));
        c.list.rounded_rect({right+14,426+first*1.0f/(count-visible)*(height-thumb),6,thumb},3,theme::kMeta.with_alpha(.8f));
    }
    if(!notice.empty())text_block(c,notice,left+36,786,22,28,theme::kWarning,width-72,2);
    static constexpr Hint hints[]={{Pad::cross,"Select"},{Pad::triangle,"Refresh"},{Pad::circle,"Back"}};
    draw_hints_right(c,hints,3,right,878,theme::kCopy);
}
void Launcher::draw_multiplayer(Canvas& c,float open) {
    const float left=548,right=1372,width=824;
    c.list.push_opacity(open);glass(c,{500,150,920,780},0,theme::kPanel,theme::kPanelEdge,1);
    const bool connected=room_status_.phase==Phase::Connected;
    text(c,connected||room_page_==RoomPage::choice?"Multiplayer":room_page_==RoomPage::public_rooms?"Public rooms":"Private room",left,238,40,theme::kTitle);
    const char* phases[]={"Offline","Connecting...","Connected","Leaving...","Connection failed"};
    text(c,phases[std::clamp(static_cast<int>(room_status_.phase),0,4)],left,284,24,connected?theme::kLime:theme::kCopy);
    c.list.line(left,316,right,316,1,theme::kRule);
    const bool active=room_status_.phase==Phase::Connecting||room_status_.phase==Phase::Disconnecting;
    std::string notice=!message_.empty()?message_:room_status_.error;
    if(connected) {
        text_fit(c,room_status_.room,left,378,30,theme::kText,width);text(c,"Players",left,422,22,theme::kMeta);
        for(int i=0;i<6&&room_row_+i<static_cast<int>(room_status_.members.size());++i)text_fit(c,room_status_.members[room_row_+i],left+36,462+i*32.0f,26,theme::kCopy,width-72);
        plate_focus(c,kRowPlate,{left,734,width,72},1);text(c,"Disconnect",left+36,779.5f,28,theme::kText);
        static constexpr Hint hints[]={{Pad::cross,"Disconnect"},{Pad::circle,"Back"}};draw_hints_right(c,hints,2,right,878,theme::kCopy);
    } else if(room_page_==RoomPage::choice) {
        const char* labels[]={"Public rooms","Private room"};
        const char* help[]={"Browse the public directory and choose a room.","Connect using an address, port and password."};
        for(int i=0;i<2;++i){float y=344+i*128.0f;plate(c,kRowPlate,{left,y,width,112},room_row_==i?1:0);text(c,labels[i],left+36,y+41,28,theme::kText);text_fit(c,help[i],left+36,y+81,24,theme::kCopy,width-72);}
        text_block(c,"Join a room before launching a game, then choose Local Wireless in the game.",left+36,664,24,36,theme::kCopy,width-72,2);
        static constexpr Hint idle[]={{Pad::cross,"Select"},{Pad::circle,"Back"}};
        static constexpr Hint online[]={{Pad::cross,"Select"},{Pad::square,"Go offline"},{Pad::circle,"Back"}};
        draw_hints_right(c,active?online:idle,active?3:2,right,878,theme::kCopy);
    } else if(room_page_==RoomPage::public_rooms&&!active) {
        draw_public_rooms(c);notice.clear();
    } else {
        const char* labels[]={"Room address","UDP port","Nickname","Password",active?"Cancel connection":"Join room"};
        const std::string values[]={room_connection_.host,std::to_string(room_connection_.port),room_connection_.nickname,room_connection_.password.empty()?"":"********",""};
        for(int i=0;i<5;++i){float y=338+i*84.0f;plate(c,kRowPlate,{left,y,width,82},room_row_==i?1:0);float used=text_fit(c,values[i],right-36,baseline(y,82,26),26,theme::kCopy,width*.55f,Align::right);text_fit(c,labels[i],left+36,baseline(y,82,26),26,theme::kText,width-108-used);}
        static constexpr Hint hints[]={{Pad::cross,"Select"},{Pad::circle,"Back"}};draw_hints_right(c,hints,2,right,878,theme::kCopy);
    }
    if(!notice.empty())text_block(c,notice,left+36,786,22,28,theme::kWarning,width-72,2);
    c.list.line(left,826,right,826,1,theme::kRule);c.list.pop_opacity();
}

} // namespace pe::ui
