#pragma once
#include "../../client/client.hpp"
#include "../../core/core.hpp"
#include "../../core/logger.hpp"
#include "../../core/shared_chan.hpp"
#include "../../packet/game/core.hpp"
#include "../../server/server.hpp"
#include "../../utils/packet_utils.hpp"
#include "../../utils/text_parse.hpp"
#include "../parser/parser.hpp"
#include "command_handler.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <windows.h>

namespace fs = std::filesystem;
using namespace std::chrono;

// Returns empty string on failure.
static std::string get_home_dir() {
#ifdef _WIN32
  if (auto *up = std::getenv("USERPROFILE"))
    return up;
  if (auto *drive = std::getenv("HOMEDRIVE"))
    if (auto *path = std::getenv("HOMEPATH"))
      return std::string(drive) + path;
  return {};
#else
  return std::getenv("HOME") ? std::getenv("HOME") : std::string{};
#endif
}

// Ensure that ~/subdir exists. Throws on error.
bool ensure_dir_exists(const std::string &subdir) {
  auto home = get_home_dir();
  if (home.empty())
    throw std::runtime_error("Could not determine home directory");

  fs::path dir = fs::path(home) / subdir;
  if (fs::exists(dir)) {
    if (!fs::is_directory(dir))
      throw std::runtime_error(dir.string() + " exists but is not a directory");
  } else {
    fs::create_directories(dir);
  }
  return true;
}

template <typename... Args>
std::string format_string(const char *format, Args &&...args) {
  int size = std::snprintf(nullptr, 0, format, std::forward<Args>(args)...) +
             1; // +1 for null terminator
  if (size <= 0) {
    throw std::runtime_error("Error during formatting.");
  }

  std::unique_ptr<char[]> buf(new char[size]);

  int printed =
      std::snprintf(buf.get(), size, format, std::forward<Args>(args)...);
  if (printed < 0) {
    throw std::runtime_error("Error during formatting.");
  }

  return std::string(buf.get(), buf.get() + printed);
}

void to_vec_bytes(std::vector<char> &in, std::vector<std::byte> &out) {
  for (const auto byte : in) {
    out.emplace_back(static_cast<std::byte>(byte));
  }
}

void sendReelPacket(player::Player &p) {
  std::vector<char> b{
      '\x04', '\x00', '\x00', '\x00', '\x03', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\xc4', '\x0b', '\x00',
      '\x00', '\x00', '\x70', '\x1c', '\x45', '\x00', '\x40', '\xcc', '\x44',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x4f', '\x00', '\x00', '\x00', '\x34', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00'};

  std::vector<std::byte> ab{};
  to_vec_bytes(b, ab);
  std::ignore = p.send_packet(ab);
}

void sendDetoPacket(player::Player &p) {
  std::vector<char> b{
      '\x04', '\x00', '\x00', '\x00', '\x03', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x94', '\x15', '\x00',
      '\x00', '\x00', '\x50', '\x1c', '\x45', '\x00', '\x40', '\xcc', '\x44',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x4f', '\x00', '\x00', '\x00', '\x34', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00'};

  std::vector<std::byte> ab{};
  to_vec_bytes(b, ab);
  std::ignore = p.send_packet(ab);
};

void sendThrowPacket(player::Player &p) {
  std::vector<char> b{
      '\x04', '\x00', '\x00', '\x00', '\x03', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\xc4', '\x0b', '\x00',
      '\x00', '\x00', '\x90', '\x1c', '\x45', '\x00', '\x40', '\xcc', '\x44',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
      '\x00', '\x00', '\x00', '\x4f', '\x00', '\x00', '\x00', '\x34', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00'};

  std::vector<std::byte> ab{};
  to_vec_bytes(b, ab);
  std::ignore = p.send_packet(ab);
};

namespace extension::command_handler {
class CommandHandlerExtension final : public ICommandHandlerExtension {
  core::Core *core_;
  ChannelManager chan{};
  bool fast_drop = false;
  bool auto_fish = false;
  time_t last_event = 0;

public:
  explicit CommandHandlerExtension(core::Core *core) : core_{core} {}

  ~CommandHandlerExtension() override = default;

  void init() override {
    chan.add_channel("FishCaught", 1 << 16, true);
    chan.add_channel("FishThrowOrReel", 1 << 16, true);
    chan.add_channel("FishBlockChangeToSolid", 1 << 16, true);
    chan.add_channel("FishObstructed", 1 << 16, true);
    chan.add_channel("FishFull", 1 << 16, true);
    chan.add_channel("PlayerUpdate", 1000000 * 2, true);
    chan.add_channel("SendInventory", 1000000 * 2, true);
    chan.add_channel("ItemChange", 1000000 * 2, true);
    chan.add_channel("ModifyInventory", 1000000 * 2, true);

    std::thread([&]() {
      while (true) {
        if (!auto_fish) {
          last_event = time(NULL);
        }
        if (time(NULL) - last_event > 30) {
          sendThrowPacket(*core_->get_client()->get_player());
          last_event = time(NULL);
        }
        Sleep(500);
      }
    }).detach();

    core_->get_event_dispatcher().prependListener(
        core::EventType::Message, [this](const core::EventMessage &event) {
          TextParse textParse(event.get_message().get_raw(), "|");

          std::string command = textParse.get("text");
          std::cout << command << "\n";

          if (command.rfind("/fd") == 0) {
            fast_drop = !fast_drop;

            event.canceled = true;
          } else if (command.rfind("/fish") == 0) {
            auto_fish = !auto_fish;
            event.canceled = true;
          } else if (command.rfind("/ft") == 0) {
            auto_fish = true;
            sendThrowPacket(*core_->get_client()->get_player());
            last_event = time(NULL);
            event.canceled = true;
          }
        });
    core_->get_event_dispatcher().prependListener(
        core::EventType::Packet, [this](const core::EventPacket &pkt) {
          const packet::GameUpdatePacket &game_pkt = pkt.get_packet();
          if (game_pkt.type == packet::PacketType::PACKET_SEND_MAP_DATA &&
              pkt.from == core::EventFrom::FromServer) {
            ByteStream stream{(std::byte *)(pkt.get_data().data()),
                              pkt.get_data().size()};

            stream.skip(66);
            std::string world_name;
            stream.read(world_name);

            // TODO: world version
            stream.reset_ptr();
            ensure_dir_exists(".gtworlds");
            std::string home = get_home_dir();
            std::string path = format_string("%s\\.gtworlds\\%s.bin",
                                             home.c_str(), world_name.c_str());
            spdlog::info(format_string("Saving world to %s", path.c_str()));

            std::ofstream out(path, std::ios::binary);
            auto bytes = stream.get_data();
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      bytes.size());
            out.close();

            // Apparently you need elevated privileges to create a symlink in
            // Windows
            // CreateSymbolicLinkA(format_string("%s\\.gtworlds\\current",
            // home.c_str()).c_str(), path.c_str(), 0);
          } else if (game_pkt.type == packet::PacketType::PACKET_GONE_FISHIN) {
            // TODO: check if the event is targeted to myself
            chan.send_to("FishThrowOrReel");
          } else if (game_pkt.type == packet::PacketType::PACKET_STATE) {
            // {
            //     auto lock = std::scoped_lock{mutex_};
            //     tile_updates_.push_back(pkt.get_data());
            // }
            // tile_updates_not_empty_.notify_one();
            chan.send_to("PlayerUpdate", pkt.get_data());
          } else if (game_pkt.type ==
                     packet::PacketType::PACKET_SEND_INVENTORY_STATE) {
            chan.send_to("SendInventory", pkt.get_ext_data());
          } else if (game_pkt.type == packet::PacketType::PACKET_ITEM_CHANGE_OBJECT) {
            chan.send_to("ItemChange", pkt.get_data());
          } else if (game_pkt.type == packet::PacketType::PACKET_MODIFY_ITEM_INVENTORY) {
            chan.send_to("ModifyInventory", pkt.get_data());
          }
        });

    auto ext{core_->query_extension<IParserExtension>()};
    ext->get_event_dispatcher().appendListener(
        IParserExtension::EventType::CallFunction,
        [&](const IParserExtension::EventCallFunction &evt) {
          if (evt.from != core::EventFrom::FromServer) {
            return;
          }

          const packet::Variant evt_variant{evt.get_args()};
          std::string fn_name = evt.get_function_name();
          if (fn_name == "OnConsoleMessage") {
            std::string msg = evt_variant.get(1);
            if (msg == "The hole in the ice froze over!" ||
                msg == "The uranium reformed!") {
              chan.send_to("FishBlockChangeToSolid");
              if (auto_fish) {
                std::thread([&]() {
                  Sleep(1000);
                  sendDetoPacket(*core_->get_client()->get_player());
                  Sleep(700);
                  sendThrowPacket(*core_->get_client()->get_player());
                  last_event = time(NULL);
                }).detach();
              }
            }
          } else if (fn_name == "OnPlayPositioned") {
            std::string file = evt_variant.get(1);

            if (file == "audio/splash.wav") {
              chan.send_to("FishCaught");
              if (auto_fish) {
                std::thread([&]() {
                  Sleep(500);
                  sendReelPacket(*core_->get_client()->get_player());
                  Sleep(700);
                  sendThrowPacket(*core_->get_client()->get_player());
                  last_event = time(NULL);
                }).detach();
              }
            }
          } else if (fn_name == "OnTalkBubble") {
            std::string msg = evt_variant.get(2);
            if (msg == "You need to drill the ice before you can fish!" ||
                msg ==
                    "You need to detonate the uranium before you can fish!") {
              chan.send_to("FishObstructed");
            } else if (msg == "You can't fish here, find an emptier spot!") {
              chan.send_to("FishFull");
            }
          } else if (fn_name == "OnDialogRequest") {
            std::string req = evt_variant.get(1);
            if (req.contains("How many to drop") && fast_drop) {
              TextParse req_{req};
              std::string itemID = req_.get("embed_data", 1);
              std::string count = req_.get("add_text_input", 1);

              TextParse cmd_{};
              cmd_.add("itemID", {itemID});
              cmd_.add("count", {count});
              cmd_.add("dialog_name", {"drop_item"});
              cmd_.add("action", {"dialog_return"});

              std::string cmd = cmd_.get_raw();

              ByteStream s{};
              s.write<uint32_t>(2);
              s.write_data(cmd.c_str(), cmd.length() + 1);

              std::vector<std::byte> b{};
              s.read_vector(b, 4 + cmd.length() + 1);
              std::ignore =
                  core_->get_client()->get_player()->send_packet(b, 0);
              evt.canceled = true;
            } else if (req.contains("How many to `4destroy``")) {
              TextParse req_{req};
              std::string itemID = req_.get("embed_data", 1);
              // std::string count = req_.get("add_text_input", 1);

              TextParse cmd_{};
              cmd_.add("itemID", {itemID});
              cmd_.add("count", {"199"});
              cmd_.add("dialog_name", {"trash_item"});
              cmd_.add("action", {"dialog_return"});

              std::string cmd = cmd_.get_raw();

              ByteStream s{};
              s.write<uint32_t>(2);
              s.write_data(cmd.c_str(), cmd.length() + 1);

              std::vector<std::byte> b{};
              s.read_vector(b, 4 + cmd.length() + 1);
              std::ignore =
                  core_->get_client()->get_player()->send_packet(b, 0);
              evt.canceled = true;
            }
          } else if (fn_name == "OnConsoleMessage") {
            std::string msg = evt_variant.get(1);

            std::string home = get_home_dir();
            std::string path =
                format_string("%s\\.gtworlds\\messages.txt", home.c_str());
            std::ofstream out(path, std::ios_base::app);
            out.write(msg.data(), msg.length());
          } else if (fn_name == "OnTalkBubble") {
            std::string msg = evt_variant.get(2);
            if (msg.contains("bro fish more please!!!")) {
              auto_fish = true;
              sendThrowPacket(*core_->get_client()->get_player());
            }
          }
        });
  }

  void free() override { delete this; }
};
} // namespace extension::command_handler
