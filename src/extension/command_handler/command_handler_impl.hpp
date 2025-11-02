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
#include <glm/glm.hpp>

namespace fs = std::filesystem;
using namespace std::chrono;

// Returns empty string on failure.
static std::string get_home_dir()
{
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
bool ensure_dir_exists(const std::string &subdir)
{
  auto home = get_home_dir();
  if (home.empty())
    throw std::runtime_error("Could not determine home directory");

  fs::path dir = fs::path(home) / subdir;
  if (fs::exists(dir))
  {
    if (!fs::is_directory(dir))
      throw std::runtime_error(dir.string() + " exists but is not a directory");
  }
  else
  {
    fs::create_directories(dir);
  }
  return true;
}

template <typename... Args>
std::string format_string(const char *format, Args &&...args)
{
  int size = std::snprintf(nullptr, 0, format, std::forward<Args>(args)...) +
             1; // +1 for null terminator
  if (size <= 0)
  {
    throw std::runtime_error("Error during formatting.");
  }

  std::unique_ptr<char[]> buf(new char[size]);

  int printed =
      std::snprintf(buf.get(), size, format, std::forward<Args>(args)...);
  if (printed < 0)
  {
    throw std::runtime_error("Error during formatting.");
  }

  return std::string(buf.get(), buf.get() + printed);
}

void to_vec_bytes(std::vector<char> &in, std::vector<std::byte> &out)
{
  for (const auto byte : in)
  {
    out.emplace_back(static_cast<std::byte>(byte));
  }
}

void sendReelPacket(player::Player &p)
{
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

void sendDetoPacket(player::Player &p)
{
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

void sendThrowPacket(player::Player &p)
{
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

struct Player
{
  std::string type;
  std::string avatar;
  uint32_t net_id;
  std::string online_id;
  std::string e_id;
  std::string ip;
  std::string col_rect;
  std::string title_icon;
  uint32_t m_state;
  uint32_t user_id;
  bool invisible;
  std::string name;
  std::string country;
  float x;
  float y;
};

class World
{
  std::unordered_map<uint32_t, Player> players{};

public:
  uint32_t my_net_id = 0;
  float my_x = 0;
  float my_y = 0;
  int build_range = 2;
  int punch_range = 2;

  void reset()
  {
    players.clear();
    my_net_id = 0;
    my_x = 0;
    my_y = 0;
    build_range = 2;
    punch_range = 2;
  }

  void remove(uint32_t id)
  {
    auto p = players.find(id);
    if (p != players.end())
    {
      players.erase(p);
    }
  }

  std::optional<Player> get(uint32_t id)
  {
    auto p = players.find(id);
    if (p != players.end())
    {
      return p->second;
    }

    return {};
  }
};

int randrange(int min, int max)
{
  return rand() % (max - min + 1) + min;
}

struct Block
{
  int x, y;
  bool destroyed;
};

void write_tank(const packet::GameUpdatePacket &pkt, ByteStream<std::byte> &s)
{
  s.write<uint8_t>(static_cast<uint8_t>(pkt.type));
  s.write(pkt.object_type);
  s.write(pkt.jump_count);
  s.write(pkt.animation_type);
  s.write(pkt.net_id);
  s.write(pkt.target_net_id);
  s.write<uint32_t>(static_cast<uint32_t>(pkt.flags.value));
  s.write(pkt.float_var);
  s.write(pkt.value);
  s.write(pkt.vec_x);
  s.write(pkt.vec_y);
  s.write(pkt.vec2_x);
  s.write(pkt.vec2_y);
  s.write(pkt.particle_rot);
  s.write(pkt.int_x);
  s.write(pkt.int_y);
  s.write(pkt.data_size);
}

namespace extension::command_handler
{
  class CommandHandlerExtension final : public ICommandHandlerExtension
  {
    core::Core *core_;
    ChannelManager chan{};
    bool fast_drop = false;
    bool fast_recycle = false;
    bool auto_fish = false;
    time_t last_event = 0;
    World world{};
    bool record_block = false;
    std::vector<Block> blocks{};
    int32_t block_auto_id = -1;
    bool auto_break = false;
    std::mutex auto_mutex;

    void console_log(const char *fmt, ...)
    {
      char buffer[2048];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buffer, sizeof(buffer), fmt, args);
      va_end(args);

      buffer[sizeof(buffer) - 1] = '\0';

      TextParse text_parse{};
      text_parse.add("action", {"log"});
      text_parse.add("msg", {buffer});

      ByteStream<std::byte> s{};
      s.write<uint32_t>(packet::NET_MESSAGE_GAME_MESSAGE);
      s.write(text_parse.get_raw(), false);
      s.write<char>(0);

      for (const auto &key_value : text_parse.get_key_values())
      {
        spdlog::info("\t{}", key_value);
      }

      core_->get_server()->get_player()->send_packet(s.get_data());
    }

    void send_tile_change_request(int px, int py, int x, int y, uint32_t id)
    {
      packet::GameUpdatePacket pkt{};
      pkt.type = packet::PACKET_TILE_CHANGE_REQUEST;
      int player_tile_x = floorf((float)px / 32.0);
      int player_tile_y = floorf((float)py / 32.0);
      pkt.vec_x = (float)px; // player pos (in pixel)
      pkt.vec_y = (float)py;
      pkt.int_x = (float)x; // target tile (in tile)
      pkt.int_y = (float)y;
      pkt.value = id;

      int range = world.build_range;
      if (id == 18)
      {
        range = world.punch_range;
      }
      if (fabsf(player_tile_x - x) <= range && fabsf(player_tile_y - y) <= range)
      {
        ByteStream<std::byte> s{};
        s.write<uint32_t>(packet::NET_MESSAGE_GAME_PACKET);
        write_tank(pkt, s);
        s.write<char>(0);

        spdlog::info("Send tile change request: {} {} {} {} {}", pkt.vec_x, pkt.vec_y, pkt.int_x, pkt.int_y, pkt.value);

        core_->get_client()->get_player()->send_packet(s.get_data());

        pkt.flags.on_placed = true;
        if (player_tile_x > x)
        {
          pkt.flags.rotate_left = true;
        }

        pkt.type = packet::PACKET_STATE;

        ByteStream<std::byte> s2{};
        s2.write<uint32_t>(packet::NET_MESSAGE_GAME_PACKET);
        write_tank(pkt, s2);
        s2.write('\x00');

        core_->get_client()->get_player()->send_packet(s2.get_data());
      }
    }

    void on_particle_effect(uint32_t id, float x, float y)
    {
      packet::game::OnParticleEffect pkt{};
      pkt.id = id;
      pkt.x = x;
      pkt.y = y;

      packet::PacketHelper::send(pkt, *core_->get_server()->get_player());
    }

  public:
    explicit CommandHandlerExtension(core::Core *core) : core_{core} {}

    ~CommandHandlerExtension() override = default;

    void init() override
    {
      chan.add_channel("FishCaught", 1 << 16, true);
      chan.add_channel("FishThrowOrReel", 1 << 16, true);
      chan.add_channel("FishBlockChangeToSolid", 1 << 16, true);
      chan.add_channel("FishObstructed", 1 << 16, true);
      chan.add_channel("FishFull", 1 << 16, true);
      chan.add_channel("PlayerUpdate", 1000000 * 2, true);
      chan.add_channel("SendInventory", 1000000 * 2, true);
      chan.add_channel("ItemChange", 1000000 * 2, true);
      chan.add_channel("ModifyInventory", 1000000 * 2, true);

      std::thread([&]()
                  {
      while (true) {
        if (!auto_fish) {
          last_event = time(NULL);
        }
        if (time(NULL) - last_event > 30) {
          sendThrowPacket(*core_->get_client()->get_player());
          last_event = time(NULL);
        }
        Sleep(500);
      } })
          .detach();

      // put thread
      std::thread([&]()
                  {
        while (true) {
          if (!auto_break) {
            Sleep(500);
            continue;
          }

        check_again:
          for (const auto &block : blocks) {
            if (!block.destroyed) {
              Sleep(100);
              goto check_again;
            }
          }

          auto_mutex.lock();
          for (auto &block : blocks) {
            if (block.destroyed) {
              send_tile_change_request(world.my_x, world.my_y, block.x, block.y, block_auto_id);
              block.destroyed = false;
              Sleep(200 + randrange(-50, 50));
            }
          }
          auto_mutex.unlock();
        } })
          .detach();

      // break thread
      std::thread([&]()
                  {
      while (true) {
        if (!auto_break) {
          Sleep(500);
          continue;
        }

        auto_mutex.lock();
        for (const auto &block : blocks) {
          if (!block.destroyed) {
            send_tile_change_request(world.my_x, world.my_y, block.x, block.y, 18);
            Sleep(200 + randrange(-50, 50));
          }
        }
        auto_mutex.unlock();
        Sleep(125 + randrange(-10, 10));
      } })
          .detach();

      core_->get_event_dispatcher().prependListener(
          core::EventType::Message, [this](const core::EventMessage &event)
          {
          TextParse textParse(event.get_message().get_raw(), "|");

          std::string command = textParse.get("text");
          std::cout << command << "\n";

          if (command.rfind("/fd") == 0) {
            fast_drop = !fast_drop;
            console_log("fd is now %s", fast_drop ? "on" : "off");

            event.canceled = true;
          } else if (command.rfind("/fr") == 0) {
            fast_recycle = !fast_recycle;
            console_log("fr is now %s", fast_recycle ? "on" : "off");
            event.canceled = true;
          } else if (command.rfind("/fish") == 0) {
            auto_fish = !auto_fish;
            console_log("fish is now %s", auto_fish ? "on" : "off");
            event.canceled = true;
          } else if (command.rfind("/rec") == 0) {
            if (!record_block) {
              blocks.clear();
            }
            record_block = !record_block;
            console_log("rec is now %s", record_block ? "on" : "off");
            if (!record_block) {
              console_log("Saved sequence with %d point", blocks.size());
            }
            event.canceled = true;
          } else if (command.rfind("/bid") == 0) {
            try {
              block_auto_id = std::stoi(command.substr(strlen("/bid")));
              console_log("rec is now %s", record_block ? "on" : "off");
            } catch (std::invalid_argument const &ex) {
              console_log("Invalid id");
            }
            event.canceled = true;
          } else if (command.rfind("/br") == 0) {
            if (!auto_break && block_auto_id == -1) {
              console_log("Set block id first");
            } else if (!auto_break && blocks.size() == 0) {
              console_log("No block position recorded (/rec)");
            } else {
              auto_break = !auto_break;
              console_log("br is now %s", auto_break ? "on" : "off");
            }
            event.canceled = true;
          } else if (command.rfind("/ft") == 0) {
            auto_fish = true;
            sendThrowPacket(*core_->get_client()->get_player());
            last_event = time(NULL);
            event.canceled = true;
          }  else if (command.rfind("/test") == 0) {
            send_tile_change_request(floorf(world.my_x), floorf(world.my_y), world.my_x + 1, world.my_y, 18);
            event.canceled = true;
          } else if (command.rfind("/info") == 0) {
            console_log("%f %f, netid=%d, build_range=%d, punch_range=%d", world.my_x, world.my_y, world.my_net_id, world.build_range, world.punch_range);
            event.canceled = true;
          } else if (command.rfind("/particle") == 0) {
            try {
              int particle_id = std::stoi(command.substr(strlen("/particle")));
              on_particle_effect(static_cast<uint32_t>(particle_id), world.my_x, world.my_y);
            } catch (std::invalid_argument const &ex) {
              console_log("Invalid particle id");
            }
            event.canceled = true;
          } });
      core_->get_event_dispatcher().prependListener(
          core::EventType::Packet, [this](const core::EventPacket &pkt)
          {
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
          } else if (game_pkt.type == packet::PacketType::PACKET_TILE_CHANGE_REQUEST && pkt.from == core::EventFrom::FromServer) {
            auto it = std::find_if(blocks.begin(), blocks.end(), [game_pkt](const Block &b) {
              return b.x == game_pkt.int_x && b.y == game_pkt.int_y;
            });
            if (it != blocks.end()) {
              if (game_pkt.value == 18) {  // remove tile
                it[0].destroyed = true;
              } else { // else place
                it[0].destroyed = false;
              }
            }
          } else if (game_pkt.type == packet::PacketType::PACKET_SET_CHARACTER_STATE) {
            world.build_range = game_pkt.jump_count - 126;
            world.punch_range = game_pkt.animation_type - 126;
          } else if (game_pkt.type == packet::PacketType::PACKET_STATE && pkt.from == core::EventFrom::FromClient) {
            world.my_x = game_pkt.vec_x;
            world.my_y = game_pkt.vec_y;

            auto p = world.get(world.my_net_id);
            if (p) {
              (*p).x = game_pkt.vec_x;
              (*p).y = game_pkt.vec_y;
            }

            if (record_block && game_pkt.flags.on_punched) {
              Block b{};
              b.x = game_pkt.int_x;
              b.y = game_pkt.int_y;
              blocks.push_back(b);
              console_log("recorded block at (%d, %d)", game_pkt.int_x, game_pkt.int_y);
            }
            chan.send_to("PlayerUpdate", pkt.get_data());
          } else if (game_pkt.type ==
                     packet::PacketType::PACKET_SEND_INVENTORY_STATE) {
            chan.send_to("SendInventory", pkt.get_ext_data());
          } else if (game_pkt.type == packet::PacketType::PACKET_ITEM_CHANGE_OBJECT) {
            chan.send_to("ItemChange", pkt.get_data());
          } else if (game_pkt.type == packet::PacketType::PACKET_MODIFY_ITEM_INVENTORY) {
            chan.send_to("ModifyInventory", pkt.get_data());
          } });

      auto ext{core_->query_extension<IParserExtension>()};
      ext->get_event_dispatcher().appendListener(
          IParserExtension::EventType::CallFunction,
          [&](const IParserExtension::EventCallFunction &evt)
          {
            if (evt.from != core::EventFrom::FromServer)
            {
              return;
            }

            const packet::Variant evt_variant{evt.get_args()};
            std::string fn_name = evt.get_function_name();
            if (fn_name == "OnConsoleMessage")
            {
              std::string msg = evt_variant.get(1);
              if (msg == "The hole in the ice froze over!" ||
                  msg == "The uranium reformed!")
              {
                chan.send_to("FishBlockChangeToSolid");
                if (auto_fish)
                {
                  std::thread([&]()
                              {
                  Sleep(1000);
                  sendDetoPacket(*core_->get_client()->get_player());
                  Sleep(700);
                  sendThrowPacket(*core_->get_client()->get_player());
                  last_event = time(NULL); })
                      .detach();
                }
              }
            }
            else if (fn_name == "OnPlayPositioned")
            {
              std::string file = evt_variant.get(1);

              if (file == "audio/splash.wav")
              {
                chan.send_to("FishCaught");
                if (auto_fish)
                {
                  std::thread([&]()
                              {
                  Sleep(500);
                  sendReelPacket(*core_->get_client()->get_player());
                  Sleep(700);
                  sendThrowPacket(*core_->get_client()->get_player());
                  last_event = time(NULL); })
                      .detach();
                }
              }
            }
            else if (fn_name == "OnTalkBubble")
            {
              std::string msg = evt_variant.get(2);
              if (msg == "You need to drill the ice before you can fish!" ||
                  msg ==
                      "You need to detonate the uranium before you can fish!")
              {
                chan.send_to("FishObstructed");
              }
              else if (msg == "You can't fish here, find an emptier spot!")
              {
                chan.send_to("FishFull");
              }
            }
            else if (fn_name == "OnDialogRequest")
            {
              std::string req = evt_variant.get(1);
              if (req.contains("How many to drop") && fast_drop)
              {
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
                Sleep(100 + randrange(-50, 50));
                std::ignore =
                    core_->get_client()->get_player()->send_packet(b, 0);
                evt.canceled = true;
              }
              else if (req.contains("How many to `4destroy``") && fast_recycle)
              {
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
                Sleep(100 + randrange(-50, 50));
                std::ignore =
                    core_->get_client()->get_player()->send_packet(b, 0);
                evt.canceled = true;
              }
            }
            else if (fn_name == "OnConsoleMessage")
            {
              std::string msg = evt_variant.get(1);

              std::string home = get_home_dir();
              std::string path =
                  format_string("%s\\.gtworlds\\messages.txt", home.c_str());
              std::ofstream out(path, std::ios_base::app);
              out.write(msg.data(), msg.length());
            }
            else if (fn_name == "OnSetPos")
            {
              auto pos = evt_variant.get<glm::vec2>(1);
              world.my_x = pos.x;
              world.my_y = pos.y;
            }
            else if (fn_name == "OnSpawn")
            {
              std::string kv = evt_variant.get(1);
              TextParse req{kv};
              if (req.contains("type"))
              {
                world.my_net_id = req.get<uint32_t>("netID");
              }
              else
              {
                Player p;
                p.type = req.get("spawn");
                p.avatar = req.get("avatar");
                p.net_id = req.get<uint32_t>("netID");
                p.online_id = req.get("onlineID");
                p.e_id = req.get("onlineID");
                p.ip = req.get("ip");
                p.col_rect = req.get("col_rect");
                p.title_icon = req.get("title_icon");
                p.m_state = req.get<uint32_t>("mstate");
                p.user_id = req.get<uint32_t>("userID");
                p.invisible = bool(req.get<uint32_t>("invis"));
                p.name = req.get("name");
                p.country = req.get("country");
                p.x = 0;
                p.y = 0;

                if (req.contains("posXY"))
                {
                  std::string pos = req.get("posXY");
                  size_t sep = pos.find('|');
                  p.x = std::stof(pos.substr(0, sep));
                  p.y = std::stof(pos.substr(sep + 1));
                }
              }
            }
            else if (fn_name == "OnRemove")
            {
              std::string kv = evt_variant.get(1);
              TextParse req{kv};
              world.remove(req.get<uint32_t>("netID"));
            }
            else if (fn_name == "OnTalkBubble")
            {
              std::string msg = evt_variant.get(2);
              if (msg.contains("bro fish more please!!!"))
              {
                auto_fish = true;
                sendThrowPacket(*core_->get_client()->get_player());
              }
            }
            else if (fn_name == "OnRequestWorldSelectMenu")
            {
              if (auto_fish)
              {
                console_log("fs is turned off");
                auto_fish = false;
              }

              if (auto_break)
              {
                console_log("br is turned off");
                auto_break = false;
              }
              world.reset();
            }
          });
    }

    void free() override { delete this; }
  };
} // namespace extension::command_handler
