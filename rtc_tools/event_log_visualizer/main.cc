/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <iostream>

#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "rtc_base/flags.h"
#include "rtc_tools/event_log_visualizer/analyzer.h"
#include "rtc_tools/event_log_visualizer/plot_base.h"
#include "rtc_tools/event_log_visualizer/plot_python.h"
#include "test/field_trial.h"
#include "test/testsupport/fileutils.h"

DEFINE_string(plot_profile,
              "default",
              "A profile that selects a certain subset of the plots. Currently "
              "defined profiles are \"all\", \"none\", \"sendside_bwe\","
              "\"receiveside_bwe\" and \"default\"");

DEFINE_bool(plot_incoming_packet_sizes,
            false,
            "Plot bar graph showing the size of each incoming packet.");
DEFINE_bool(plot_outgoing_packet_sizes,
            false,
            "Plot bar graph showing the size of each outgoing packet.");
DEFINE_bool(plot_incoming_packet_count,
            false,
            "Plot the accumulated number of packets for each incoming stream.");
DEFINE_bool(plot_outgoing_packet_count,
            false,
            "Plot the accumulated number of packets for each outgoing stream.");
DEFINE_bool(plot_audio_playout,
            false,
            "Plot bar graph showing the time between each audio playout.");
DEFINE_bool(plot_audio_level,
            false,
            "Plot line graph showing the audio level of incoming audio.");
DEFINE_bool(plot_incoming_sequence_number_delta,
            false,
            "Plot the sequence number difference between consecutive incoming "
            "packets.");
DEFINE_bool(
    plot_incoming_delay_delta,
    false,
    "Plot the difference in 1-way path delay between consecutive packets.");
DEFINE_bool(plot_incoming_delay,
            true,
            "Plot the 1-way path delay for incoming packets, normalized so "
            "that the first packet has delay 0.");
DEFINE_bool(plot_incoming_loss_rate,
            true,
            "Compute the loss rate for incoming packets using a method that's "
            "similar to the one used for RTCP SR and RR fraction lost. Note "
            "that the loss rate can be negative if packets are duplicated or "
            "reordered.");
DEFINE_bool(plot_incoming_bitrate,
            true,
            "Plot the total bitrate used by all incoming streams.");
DEFINE_bool(plot_outgoing_bitrate,
            true,
            "Plot the total bitrate used by all outgoing streams.");
DEFINE_bool(plot_incoming_stream_bitrate,
            true,
            "Plot the bitrate used by each incoming stream.");
DEFINE_bool(plot_outgoing_stream_bitrate,
            true,
            "Plot the bitrate used by each outgoing stream.");
DEFINE_bool(plot_simulated_receiveside_bwe,
            false,
            "Run the receive-side bandwidth estimator with the incoming rtp "
            "packets and plot the resulting estimate.");
DEFINE_bool(plot_simulated_sendside_bwe,
            false,
            "Run the send-side bandwidth estimator with the outgoing rtp and "
            "incoming rtcp and plot the resulting estimate.");
DEFINE_bool(plot_network_delay_feedback,
            true,
            "Compute network delay based on sent packets and the received "
            "transport feedback.");
DEFINE_bool(plot_fraction_loss_feedback,
            true,
            "Plot packet loss in percent for outgoing packets (as perceived by "
            "the send-side bandwidth estimator).");
DEFINE_bool(plot_pacer_delay,
            false,
            "Plot the time each sent packet has spent in the pacer (based on "
            "the difference between the RTP timestamp and the send "
            "timestamp).");
DEFINE_bool(plot_timestamps,
            false,
            "Plot the rtp timestamps of all rtp and rtcp packets over time.");
DEFINE_bool(plot_audio_encoder_bitrate_bps,
            false,
            "Plot the audio encoder target bitrate.");
DEFINE_bool(plot_audio_encoder_frame_length_ms,
            false,
            "Plot the audio encoder frame length.");
DEFINE_bool(
    plot_audio_encoder_packet_loss,
    false,
    "Plot the uplink packet loss fraction which is sent to the audio encoder.");
DEFINE_bool(plot_audio_encoder_fec, false, "Plot the audio encoder FEC.");
DEFINE_bool(plot_audio_encoder_dtx, false, "Plot the audio encoder DTX.");
DEFINE_bool(plot_audio_encoder_num_channels,
            false,
            "Plot the audio encoder number of channels.");
DEFINE_bool(plot_audio_jitter_buffer,
            false,
            "Plot the audio jitter buffer delay profile.");
DEFINE_bool(plot_ice_candidate_pair_config,
            false,
            "Plot the ICE candidate pair config events.");
DEFINE_bool(plot_ice_connectivity_check,
            false,
            "Plot the ICE candidate pair connectivity checks.");

DEFINE_string(
    force_fieldtrials,
    "",
    "Field trials control experimental feature code which can be forced. "
    "E.g. running with --force_fieldtrials=WebRTC-FooFeature/Enabled/"
    " will assign the group Enabled to field trial WebRTC-FooFeature. Multiple "
    "trials are separated by \"/\"");
DEFINE_string(wav_filename,
              "",
              "Path to wav file used for simulation of jitter buffer");
DEFINE_bool(help, false, "prints this message");

DEFINE_bool(show_detector_state,
            false,
            "Show the state of the delay based BWE detector on the total "
            "bitrate graph");

DEFINE_bool(show_alr_state,
            false,
            "Show the state ALR state on the total bitrate graph");

DEFINE_bool(
    print_triage_notifications,
    false,
    "Print triage notifications, i.e. a list of suspicious looking events.");

void SetAllPlotFlags(bool setting);


int main(int argc, char* argv[]) {
  std::string program_name = argv[0];
  std::string usage =
      "A tool for visualizing WebRTC event logs.\n"
      "Example usage:\n" +
      program_name + " <logfile> | python\n" + "Run " + program_name +
      " --help for a list of command line options\n";

  // Parse command line flags without removing them. We're only interested in
  // the |plot_profile| flag.
  rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, false);
  if (strcmp(FLAG_plot_profile, "all") == 0) {
    SetAllPlotFlags(true);
  } else if (strcmp(FLAG_plot_profile, "none") == 0) {
    SetAllPlotFlags(false);
  } else if (strcmp(FLAG_plot_profile, "sendside_bwe") == 0) {
    SetAllPlotFlags(false);
    FLAG_plot_outgoing_packet_sizes = true;
    FLAG_plot_outgoing_bitrate = true;
    FLAG_plot_outgoing_stream_bitrate = true;
    FLAG_plot_simulated_sendside_bwe = true;
    FLAG_plot_network_delay_feedback = true;
    FLAG_plot_fraction_loss_feedback = true;
  } else if (strcmp(FLAG_plot_profile, "receiveside_bwe") == 0) {
    SetAllPlotFlags(false);
    FLAG_plot_incoming_packet_sizes = true;
    FLAG_plot_incoming_delay_delta = true;
    FLAG_plot_incoming_delay = true;
    FLAG_plot_incoming_loss_rate = true;
    FLAG_plot_incoming_bitrate = true;
    FLAG_plot_incoming_stream_bitrate = true;
    FLAG_plot_simulated_receiveside_bwe = true;
  } else if (strcmp(FLAG_plot_profile, "default") == 0) {
    // Do nothing.
  } else {
    rtc::Flag* plot_profile_flag = rtc::FlagList::Lookup("plot_profile");
    RTC_CHECK(plot_profile_flag);
    plot_profile_flag->Print(false);
  }
  // Parse the remaining flags. They are applied relative to the chosen profile.
  rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, true);

  if (argc != 2 || FLAG_help) {
    // Print usage information.
    std::cout << usage;
    if (FLAG_help)
      rtc::FlagList::Print(nullptr, false);
    return 0;
  }

  webrtc::test::SetExecutablePath(argv[0]);
  webrtc::test::InitFieldTrialsFromString(FLAG_force_fieldtrials);

  std::string filename = argv[1];

  webrtc::ParsedRtcEventLog parsed_log;

  if (!parsed_log.ParseFile(filename)) {
    std::cerr << "Could not parse the entire log file." << std::endl;
    std::cerr << "Proceeding to analyze the first "
              << parsed_log.GetNumberOfEvents() << " events in the file."
              << std::endl;
  }

  webrtc::plotting::EventLogAnalyzer analyzer(parsed_log);
  std::unique_ptr<webrtc::plotting::PlotCollection> collection(
      new webrtc::plotting::PythonPlotCollection());

  if (FLAG_plot_incoming_packet_sizes) {
    analyzer.CreatePacketGraph(webrtc::PacketDirection::kIncomingPacket,
                               collection->AppendNewPlot());
  }
  if (FLAG_plot_outgoing_packet_sizes) {
    analyzer.CreatePacketGraph(webrtc::PacketDirection::kOutgoingPacket,
                               collection->AppendNewPlot());
  }
  if (FLAG_plot_incoming_packet_count) {
    analyzer.CreateAccumulatedPacketsGraph(
        webrtc::PacketDirection::kIncomingPacket, collection->AppendNewPlot());
  }
  if (FLAG_plot_outgoing_packet_count) {
    analyzer.CreateAccumulatedPacketsGraph(
        webrtc::PacketDirection::kOutgoingPacket, collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_playout) {
    analyzer.CreatePlayoutGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_level) {
    analyzer.CreateAudioLevelGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_incoming_sequence_number_delta) {
    analyzer.CreateSequenceNumberGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_incoming_delay_delta) {
    analyzer.CreateIncomingDelayDeltaGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_incoming_delay) {
    analyzer.CreateIncomingDelayGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_incoming_loss_rate) {
    analyzer.CreateIncomingPacketLossGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_incoming_bitrate) {
    analyzer.CreateTotalBitrateGraph(webrtc::PacketDirection::kIncomingPacket,
                                     collection->AppendNewPlot(),
                                     FLAG_show_detector_state,
                                     FLAG_show_alr_state);
  }
  if (FLAG_plot_outgoing_bitrate) {
    analyzer.CreateTotalBitrateGraph(webrtc::PacketDirection::kOutgoingPacket,
                                     collection->AppendNewPlot(),
                                     FLAG_show_detector_state,
                                     FLAG_show_alr_state);
  }
  if (FLAG_plot_incoming_stream_bitrate) {
    analyzer.CreateStreamBitrateGraph(webrtc::PacketDirection::kIncomingPacket,
                                      collection->AppendNewPlot());
  }
  if (FLAG_plot_outgoing_stream_bitrate) {
    analyzer.CreateStreamBitrateGraph(webrtc::PacketDirection::kOutgoingPacket,
                                      collection->AppendNewPlot());
  }
  if (FLAG_plot_simulated_receiveside_bwe) {
    analyzer.CreateReceiveSideBweSimulationGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_simulated_sendside_bwe) {
    analyzer.CreateSendSideBweSimulationGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_network_delay_feedback) {
    analyzer.CreateNetworkDelayFeedbackGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_fraction_loss_feedback) {
    analyzer.CreateFractionLossGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_timestamps) {
    analyzer.CreateTimestampGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_pacer_delay) {
    analyzer.CreatePacerDelayGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_encoder_bitrate_bps) {
    analyzer.CreateAudioEncoderTargetBitrateGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_encoder_frame_length_ms) {
    analyzer.CreateAudioEncoderFrameLengthGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_encoder_packet_loss) {
    analyzer.CreateAudioEncoderPacketLossGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_encoder_fec) {
    analyzer.CreateAudioEncoderEnableFecGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_encoder_dtx) {
    analyzer.CreateAudioEncoderEnableDtxGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_encoder_num_channels) {
    analyzer.CreateAudioEncoderNumChannelsGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_audio_jitter_buffer) {
    std::string wav_path;
    if (FLAG_wav_filename[0] != '\0') {
      wav_path = FLAG_wav_filename;
    } else {
      wav_path = webrtc::test::ResourcePath(
          "audio_processing/conversational_speech/EN_script2_F_sp2_B1", "wav");
    }
    analyzer.CreateAudioJitterBufferGraph(wav_path, 48000,
                                          collection->AppendNewPlot());
  }

  if (FLAG_plot_ice_candidate_pair_config) {
    analyzer.CreateIceCandidatePairConfigGraph(collection->AppendNewPlot());
  }
  if (FLAG_plot_ice_connectivity_check) {
    analyzer.CreateIceConnectivityCheckGraph(collection->AppendNewPlot());
  }

  collection->Draw();

  if (FLAG_print_triage_notifications) {
    analyzer.CreateTriageNotifications();
    analyzer.PrintNotifications(stderr);
  }

  return 0;
}


void SetAllPlotFlags(bool setting) {
  FLAG_plot_incoming_packet_sizes = setting;
  FLAG_plot_outgoing_packet_sizes = setting;
  FLAG_plot_incoming_packet_count = setting;
  FLAG_plot_outgoing_packet_count = setting;
  FLAG_plot_audio_playout = setting;
  FLAG_plot_audio_level = setting;
  FLAG_plot_incoming_sequence_number_delta = setting;
  FLAG_plot_incoming_delay_delta = setting;
  FLAG_plot_incoming_delay = setting;
  FLAG_plot_incoming_loss_rate = setting;
  FLAG_plot_incoming_bitrate = setting;
  FLAG_plot_outgoing_bitrate = setting;
  FLAG_plot_incoming_stream_bitrate = setting;
  FLAG_plot_outgoing_stream_bitrate = setting;
  FLAG_plot_simulated_receiveside_bwe = setting;
  FLAG_plot_simulated_sendside_bwe = setting;
  FLAG_plot_network_delay_feedback = setting;
  FLAG_plot_fraction_loss_feedback = setting;
  FLAG_plot_timestamps = setting;
  FLAG_plot_audio_encoder_bitrate_bps = setting;
  FLAG_plot_audio_encoder_frame_length_ms = setting;
  FLAG_plot_audio_encoder_packet_loss = setting;
  FLAG_plot_audio_encoder_fec = setting;
  FLAG_plot_audio_encoder_dtx = setting;
  FLAG_plot_audio_encoder_num_channels = setting;
  FLAG_plot_audio_jitter_buffer = setting;
}
