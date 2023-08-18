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

#pragma once

#include <third_party/joy_linux/rbuf/joy.hh>
#include <third_party/joy_linux/rbuf/joy_linux_config.hh>
#include <third_party/joy_linux/rbuf/joy_feedback.hh>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <linux/input.h>

#include "ark/pipeline/forward.hh"
#include "ark/pipeline/stage.hh"
#include "ark/pipeline/config.hh"

#include "channels.hh"

namespace joy_linux {

/// \brief Opens, reads from and publishes joystick events
class Joystick : public ark::pipeline::Stage
{
private:
    bool sticky_buttons_;
    bool default_trig_val_;
    std::string joy_dev_;
    std::string joy_dev_name_;
    std::string joy_dev_ff_;
    double deadzone_;
    double autorepeat_rate_;    // in Hz.  0 for no repeat.
    double coalesce_interval_;  // Defaults to 100 Hz rate limit.
    int event_count_;
    int pub_count_;
    ark::pipeline::PublisherPtr<joy_linux::rbuf::Joy> pub_;

    int ff_fd_;
    int joy_fd_;
    struct ff_effect joy_effect_;
    bool update_feedback_;
    // Here because we want to reset it on device close.
    rbuf::Joy joy_msg;
    std::thread read_thread_;
    bool running_ = true;

    /*! \brief Returns the device path of the first joystick that matches joy_name.
    *         If no match is found, an empty string is returned.
    */
    std::string get_dev_by_joy_name(const std::string & joy_name);

public:
    explicit Joystick(std::string name = "Joystick");

    ~Joystick();

    void set_feedback(const std::shared_ptr<const rbuf::JoyFeedbackArray>& msg);

    void initialize(ark::pipeline::StageInterface& interfaces) override;

    void timer();

    ///
    /// Indicates that the system has settled and you should begin any threads
    /// or processing.
    ///
    void start(ark::pipeline::StageInterface &interface) override;
};

} // namespace joy_linux
