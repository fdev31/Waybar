#include "modules/privacy/privacy.hpp"

#include <fmt/core.h>
#include <json/value.h>
#include <pipewire/pipewire.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "AModule.hpp"
#include "gtkmm/image.h"
#include "modules/privacy/privacy_item.hpp"

namespace waybar::modules::privacy {

using util::PipewireBackend::PRIVACY_NODE_TYPE_AUDIO_INPUT;
using util::PipewireBackend::PRIVACY_NODE_TYPE_AUDIO_OUTPUT;
using util::PipewireBackend::PRIVACY_NODE_TYPE_NONE;
using util::PipewireBackend::PRIVACY_NODE_TYPE_VIDEO_INPUT;

Privacy::Privacy(const std::string& id, const Json::Value& config, const std::string& pos)
    : AModule(config, "privacy", id),
      nodes_screenshare(),
      nodes_audio_in(),
      nodes_audio_out(),
      visibility_conn(),
      box_(Gtk::ORIENTATION_HORIZONTAL, 0) {
  box_.set_name(name_);

  event_box_.add(box_);

  // Icon Spacing
  if (config_["icon-spacing"].isUInt()) {
    iconSpacing = config_["icon-spacing"].asUInt();
  }
  box_.set_spacing(iconSpacing);

  // Icon Size
  if (config_["icon-size"].isUInt()) {
    iconSize = config_["icon-size"].asUInt();
  }

  // Transition Duration
  if (config_["transition-duration"].isUInt()) {
    transition_duration = config_["transition-duration"].asUInt();
  }

  // Initialize each privacy module
  Json::Value modules = config_["modules"];
  // Add Screenshare and Mic usage as default modules if none are specified
  if (!modules.isArray() || modules.size() == 0) {
    modules = Json::Value(Json::arrayValue);
    for (auto& type : {"screenshare", "audio-in"}) {
      Json::Value obj = Json::Value(Json::objectValue);
      obj["type"] = type;
      modules.append(obj);
    }
  }
  for (uint i = 0; i < modules.size(); i++) {
    const Json::Value& module_config = modules[i];
    if (!module_config.isObject() || !module_config["type"].isString()) continue;
    const std::string type = module_config["type"].asString();
    if (type == "screenshare") {
      auto item =
          Gtk::make_managed<PrivacyItem>(module_config, PRIVACY_NODE_TYPE_VIDEO_INPUT,
                                         &nodes_screenshare, pos, iconSize, transition_duration);
      box_.add(*item);
    } else if (type == "audio-in") {
      auto item =
          Gtk::make_managed<PrivacyItem>(module_config, PRIVACY_NODE_TYPE_AUDIO_INPUT,
                                         &nodes_audio_in, pos, iconSize, transition_duration);
      box_.add(*item);
    } else if (type == "audio-out") {
      auto item =
          Gtk::make_managed<PrivacyItem>(module_config, PRIVACY_NODE_TYPE_AUDIO_OUTPUT,
                                         &nodes_audio_out, pos, iconSize, transition_duration);
      box_.add(*item);
    }
  }

  backend = util::PipewireBackend::PipewireBackend::getInstance();
  backend->privacy_nodes_changed_signal_event.connect(
      sigc::mem_fun(*this, &Privacy::onPrivacyNodesChanged));

  dp.emit();
}

void Privacy::onPrivacyNodesChanged() {
  mutex_.lock();
  nodes_audio_out.clear();
  nodes_audio_in.clear();
  nodes_screenshare.clear();

  for (auto& node : backend->privacy_nodes) {
    switch (node.second->state) {
      case PW_NODE_STATE_RUNNING:
        switch (node.second->type) {
          case PRIVACY_NODE_TYPE_VIDEO_INPUT:
            nodes_screenshare.push_back(node.second);
            break;
          case PRIVACY_NODE_TYPE_AUDIO_INPUT:
            nodes_audio_in.push_back(node.second);
            break;
          case PRIVACY_NODE_TYPE_AUDIO_OUTPUT:
            nodes_audio_out.push_back(node.second);
            break;
          case PRIVACY_NODE_TYPE_NONE:
            continue;
        }
        break;
      default:
        break;
    }
  }

  mutex_.unlock();
  dp.emit();
}

auto Privacy::update() -> void {
  mutex_.lock();
  bool screenshare = !nodes_screenshare.empty();
  bool audio_in = !nodes_audio_in.empty();
  bool audio_out = !nodes_audio_out.empty();

  for (Gtk::Widget* widget : box_.get_children()) {
    PrivacyItem* module = dynamic_cast<PrivacyItem*>(widget);
    if (!module) continue;
    switch (module->privacy_type) {
      case util::PipewireBackend::PRIVACY_NODE_TYPE_VIDEO_INPUT:
        module->set_in_use(screenshare);
        break;
      case util::PipewireBackend::PRIVACY_NODE_TYPE_AUDIO_INPUT:
        module->set_in_use(audio_in);
        break;
      case util::PipewireBackend::PRIVACY_NODE_TYPE_AUDIO_OUTPUT:
        module->set_in_use(audio_out);
        break;
      case util::PipewireBackend::PRIVACY_NODE_TYPE_NONE:
        break;
    }
  }
  mutex_.unlock();

  // Hide the whole widget if none are in use
  bool is_visible = screenshare || audio_in || audio_out;
  if (is_visible != event_box_.get_visible()) {
    // Disconnect any previous connection so that it doesn't get activated in
    // the future, hiding the module when it should be visible
    visibility_conn.disconnect();
    if (is_visible) {
      event_box_.set_visible(true);
    } else {
      // Hides the widget when all of the privacy_item revealers animations
      // have finished animating
      visibility_conn = Glib::signal_timeout().connect(
          sigc::track_obj(
              [this] {
                mutex_.lock();
                bool screenshare = !nodes_screenshare.empty();
                bool audio_in = !nodes_audio_in.empty();
                bool audio_out = !nodes_audio_out.empty();
                mutex_.unlock();
                event_box_.set_visible(screenshare || audio_in || audio_out);
                return false;
              },
              *this),
          transition_duration);
    }
  }

  // Call parent update
  AModule::update();
}

}  // namespace waybar::modules::privacy
