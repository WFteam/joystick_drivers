/*
 * joy_linux_node
 * Copyright (c) 2009, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <ORGANIZATION> nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// \author: Blaise Gassend

#include "joy_linux_node.hh"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

namespace joy_linux {

std::string Joystick::get_dev_by_joy_name(const std::string & joy_name)
{
  const char path[] = "/dev/input";  // no trailing / here
  struct dirent * entry;
  struct stat stat_buf;

  DIR * dev_dir = opendir(path);
  if (dev_dir == nullptr) {
    return "";
  }

  while ((entry = readdir(dev_dir)) != nullptr) {
    // filter entries
    if (strncmp(entry->d_name, "js", 2) != 0) {  // skip device if it's not a joystick
      continue;
    }
    std::string current_path = std::string(path) + "/" + entry->d_name;
    if (stat(current_path.c_str(), &stat_buf) == -1) {
      continue;
    }
    if (!S_ISCHR(stat_buf.st_mode)) {  // input devices are character devices, skip other
      continue;
    }

    // get joystick name
    int joy_fd = open(current_path.c_str(), O_RDONLY);
    if (joy_fd == -1) {
      continue;
    }

    char current_joy_name[128];
    if (ioctl(joy_fd, JSIOCGNAME(sizeof(current_joy_name)), current_joy_name) < 0) {
      strncpy(current_joy_name, "Unknown", sizeof(current_joy_name));
    }

    close(joy_fd);

    if (strcmp(current_joy_name, joy_name.c_str()) == 0) {
      closedir(dev_dir);
      return current_path;
    }
  }

  closedir(dev_dir);
  return "";
}

Joystick::Joystick(std::string name)
: ark::pipeline::Stage(std::move(name)), ff_fd_(-1)
{}

Joystick::~Joystick()
{
  running_ = false;
  if (read_thread_.joinable())
  {
    read_thread_.join();
  }
}

void Joystick::set_feedback(const std::shared_ptr<const rbuf::JoyFeedbackArray>& msg)
{
  if (ff_fd_ == -1) {
    return;  // we arent ready yet
  }

  size_t size = msg->array.size();
  for (size_t i = 0; i < size; i++) {
    // process each feedback
    if (msg->array[i].type == joy_linux::rbuf::FeedbackType::TYPE_RUMBLE && ff_fd_ != -1) {  // TYPE_RUMBLE
      // if id is zero, thats low freq, 1 is high
      joy_effect_.direction = 0;  // down
      joy_effect_.type = FF_RUMBLE;
      if (msg->array[i].id == 0) {
        joy_effect_.u.rumble.strong_magnitude =
          (static_cast<float>(1 << 15)) * msg->array[i].intensity;
      } else {
        joy_effect_.u.rumble.weak_magnitude =
          (static_cast<float>(1 << 15)) * msg->array[i].intensity;
      }

      joy_effect_.replay.length = 1000;
      joy_effect_.replay.delay = 0;

      update_feedback_ = true;
    }
  }
}


void Joystick::initialize(ark::pipeline::StageInterface& interfaces)
{
  // Parameters
  pub_ = interfaces.add_publisher<rbuf::Joy>(channel_names::JOY_CHANNEL);

  interfaces.add_subscriber(ark::pipeline::SubscriberConfiguration<rbuf::JoyFeedbackArray>{
      .channel_name = channel_names::JOY_FEEDBACK_CHANNEL,
      .maximum_queue_depth = 10,
      .callback =
          [this](const std::shared_ptr<const rbuf::JoyFeedbackArray>& msg) {
              set_feedback(msg);
          },
  });
  auto config = ark::pipeline::get_stage_config<rbuf::JoyLinuxConfig>(interfaces);

  joy_dev_ = config.dev;
  joy_dev_name_ = config.dev_name;
  joy_dev_ff_ = config.dev_ff;
  deadzone_ = config.deadzone;
  autorepeat_rate_ = config.autorepeat_rate;
  coalesce_interval_ = config.coalesce_interval;
  default_trig_val_ = config.default_trig_val;
  sticky_buttons_ = config.sticky_buttons;

  // Checks on parameters
  if (!joy_dev_name_.empty()) {
    std::string joy_dev_path = get_dev_by_joy_name(joy_dev_name_);
    if (!joy_dev_path.empty()) {
      joy_dev_ = joy_dev_path;
    }
  }

  if (deadzone_ >= 1) {
    deadzone_ /= 32767;
  }

  if (deadzone_ > 0.9) {
    deadzone_ = 0.9;
  }

  if (deadzone_ < 0) {
    deadzone_ = 0;
  }

  if (autorepeat_rate_ < 0) {
    autorepeat_rate_ = 0;
  }

  if (coalesce_interval_ < 0) {
    coalesce_interval_ = 0;
  }

  event_count_ = 0;
  pub_count_ = 0;
  joy_fd_ = open(joy_dev_.c_str(), O_RDONLY);
  if (!joy_dev_ff_.empty()) {
    ff_fd_ = open(joy_dev_ff_.c_str(), O_RDWR);
    /* Set the gain of the device*/
    int gain = 100;           /* between 0 and 100 */
    struct input_event ie;      /* structure used to communicate with the driver */

    ie.type = EV_FF;
    ie.code = FF_GAIN;
    ie.value = 0xFFFFUL * gain / 100;

    if (write(ff_fd_, &ie, sizeof(ie)) == -1) {
    }

    joy_effect_.id = -1;
    joy_effect_.direction = 0;  // down
    joy_effect_.type = FF_RUMBLE;
    joy_effect_.u.rumble.strong_magnitude = 0;
    joy_effect_.u.rumble.weak_magnitude = 0;
    joy_effect_.replay.length = 1000;
    joy_effect_.replay.delay = 0;

    // upload the effect
    // FIXME: check the return value here
    ioctl(ff_fd_, EVIOCSFF, &joy_effect_);
  }
}

void Joystick::start(ark::pipeline::StageInterface & /*unused*/)
{
    read_thread_ = std::thread([this]() {
      while (running_)
      {
        timer();
      }
    });
}

void Joystick::timer() {
  // Parameter conversions
  double autorepeat_interval = 1 / autorepeat_rate_;
  double scale = -1. / (1. - deadzone_) / 32767.;
  double unscaled_deadzone = 32767. * deadzone_;
  bool tv_set = false;
  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  double val;  // Temporary variable to hold event values

  js_event event;
  fd_set set;
  auto joy_fd = joy_fd_;
  FD_ZERO(&set);
  FD_SET(joy_fd, &set);

  int select_out = select(joy_fd + 1, &set, nullptr, nullptr, &tv);
  if (select_out == -1) {
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return;
  }

  // play the rumble effect (can probably do this at lower rate later)
  if (ff_fd_ != -1) {
    struct input_event start;
    start.type = EV_FF;
    start.code = joy_effect_.id;
    start.value = 3;
    if (write(ff_fd_, (const void *) &start, sizeof(start)) == -1) {
      return;  // fd closed
    }

    // upload the effect
    if (update_feedback_ == true) {
      // FIXME: check the return value here.
      ioctl(ff_fd_, EVIOCSFF, &joy_effect_);
      update_feedback_ = false;
    }
  }

  if (FD_ISSET(joy_fd, &set)) {
    if (read(joy_fd, &event, sizeof(js_event)) == -1 && errno != EAGAIN) {
      return;  // Joystick is probably closed. Definitely occurs.
    }

    joy_msg.stamp = std::chrono::steady_clock::now();
    event_count_++;
    switch (event.type) {
      case JS_EVENT_BUTTON:
      case JS_EVENT_BUTTON | JS_EVENT_INIT:
        if (event.number >= joy_msg.buttons.size()) {
          size_t old_size = joy_msg.buttons.size();
          joy_msg.buttons.resize(event.number + 1);
          for (size_t i = old_size; i < joy_msg.buttons.size(); i++) {
            joy_msg.buttons[i] = 0.0;
          }
        }
        if (sticky_buttons_) {
          if (event.value == 1) {
            joy_msg.buttons[event.number] = 1 - joy_msg.buttons[event.number];
          }
        } else {
          joy_msg.buttons[event.number] = (event.value ? 1 : 0);
        }
        break;
      case JS_EVENT_AXIS:
      case JS_EVENT_AXIS | JS_EVENT_INIT:
        val = event.value;
        if (event.number >= joy_msg.axes.size()) {
          size_t old_size = joy_msg.axes.size();
          joy_msg.axes.resize(event.number + 1);
          for (size_t i = old_size; i < joy_msg.axes.size(); i++) {
            joy_msg.axes[i] = 0.0;
          }
        }
        if (default_trig_val_) {
          // Allows deadzone to be "smooth"
          if (val > unscaled_deadzone) {
            val -= unscaled_deadzone;
          } else if (val < -unscaled_deadzone) {
            val += unscaled_deadzone;
          } else {
            val = 0;
          }
          joy_msg.axes[event.number] = val * scale;
        } else {
          if (!(event.type & JS_EVENT_INIT)) {
            val = event.value;
            if (val > unscaled_deadzone) {
              val -= unscaled_deadzone;
            } else if (val < -unscaled_deadzone) {
              val += unscaled_deadzone;
            } else {
              val = 0;
            }
            joy_msg.axes[event.number] = val * scale;
          }
        }
        break;
      default:
        break;
    }
  }

    // Assume that all the JS_EVENT_INIT messages have arrived already.
    // This should be the case as the kernel sends them along as soon as
    // the device opens.
    joy_msg.stamp = std::chrono::steady_clock::now();
    size_t old_btn_size = joy_msg.buttons.size();
    joy_msg.buttons.resize(5);
    for (size_t i = old_btn_size; i < joy_msg.buttons.size(); i++) {
        joy_msg.buttons[i] = 0.0;
    }
    size_t old_axes_size = joy_msg.axes.size();
    joy_msg.axes.resize(8);
    for (size_t i = old_axes_size; i < joy_msg.axes.size(); i++) {
        joy_msg.axes[i] = 0.0;
    }


    pub_->push(joy_msg);

    tv_set = false;
    pub_count_++;

  // If nothing is going on, start a timer to do autorepeat.
  if (!tv_set && autorepeat_rate_ > 0) {
    tv.tv_sec = trunc(autorepeat_interval);
    tv.tv_usec = (autorepeat_interval - tv.tv_sec) * 1e6;
    tv_set = true;
  }

  if (!tv_set) {
    tv.tv_sec = 1;
    tv.tv_usec = 0;
  }
}

} // namespace joy_linux
