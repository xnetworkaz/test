/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_RTCSTATSCOLLECTOR_H_
#define PC_RTCSTATSCOLLECTOR_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "api/optional.h"
#include "api/stats/rtcstats_objects.h"
#include "api/stats/rtcstatscollectorcallback.h"
#include "api/stats/rtcstatsreport.h"
#include "call/call.h"
#include "media/base/mediachannel.h"
#include "pc/datachannel.h"
#include "pc/peerconnectioninternal.h"
#include "pc/trackmediainfomap.h"
#include "rtc_base/asyncinvoker.h"
#include "rtc_base/refcount.h"
#include "rtc_base/scoped_ref_ptr.h"
#include "rtc_base/sigslot.h"
#include "rtc_base/sslidentity.h"
#include "rtc_base/timeutils.h"

namespace webrtc {

// All public methods of the collector are to be called on the signaling thread.
// Stats are gathered on the signaling, worker and network threads
// asynchronously. The callback is invoked on the signaling thread. Resulting
// reports are cached for |cache_lifetime_| ms.
class RTCStatsCollector : public virtual rtc::RefCountInterface,
                          public sigslot::has_slots<> {
 public:
  static rtc::scoped_refptr<RTCStatsCollector> Create(
      PeerConnectionInternal* pc,
      int64_t cache_lifetime_us = 50 * rtc::kNumMicrosecsPerMillisec);

  // Gets a recent stats report. If there is a report cached that is still fresh
  // it is returned, otherwise new stats are gathered and returned. A report is
  // considered fresh for |cache_lifetime_| ms. const RTCStatsReports are safe
  // to use across multiple threads and may be destructed on any thread.
  void GetStatsReport(rtc::scoped_refptr<RTCStatsCollectorCallback> callback);
  // Clears the cache's reference to the most recent stats report. Subsequently
  // calling |GetStatsReport| guarantees fresh stats.
  void ClearCachedStatsReport();

  // If there is a |GetStatsReport| requests in-flight, waits until it has been
  // completed. Must be called on the signaling thread.
  void WaitForPendingRequest();

 protected:
  RTCStatsCollector(PeerConnectionInternal* pc, int64_t cache_lifetime_us);
  ~RTCStatsCollector();

  // Stats gathering on a particular thread. Calls |AddPartialResults| before
  // returning. Virtual for the sake of testing.
  virtual void ProducePartialResultsOnSignalingThread(int64_t timestamp_us);
  virtual void ProducePartialResultsOnNetworkThread(int64_t timestamp_us);

  // Can be called on any thread.
  void AddPartialResults(
      const rtc::scoped_refptr<RTCStatsReport>& partial_report);

 private:
  struct CertificateStatsPair {
    std::unique_ptr<rtc::SSLCertificateStats> local;
    std::unique_ptr<rtc::SSLCertificateStats> remote;
  };

  void AddPartialResults_s(rtc::scoped_refptr<RTCStatsReport> partial_report);
  void DeliverCachedReport();

  // Produces |RTCCertificateStats|.
  void ProduceCertificateStats_n(
      int64_t timestamp_us,
      const std::map<std::string, CertificateStatsPair>& transport_cert_stats,
      RTCStatsReport* report) const;
  // Produces |RTCCodecStats|.
  void ProduceCodecStats_n(
      int64_t timestamp_us, const TrackMediaInfoMap& track_media_info_map,
      RTCStatsReport* report) const;
  // Produces |RTCDataChannelStats|.
  void ProduceDataChannelStats_s(
      int64_t timestamp_us, RTCStatsReport* report) const;
  // Produces |RTCIceCandidatePairStats| and |RTCIceCandidateStats|.
  void ProduceIceCandidateAndPairStats_n(
      int64_t timestamp_us,
      const std::map<std::string, cricket::TransportStats>&
          transport_stats_by_name,
      const cricket::VideoMediaInfo* video_media_info,
      const Call::Stats& call_stats,
      RTCStatsReport* report) const;
  // Produces |RTCMediaStreamStats| and |RTCMediaStreamTrackStats|.
  void ProduceMediaStreamAndTrackStats_s(
      int64_t timestamp_us, RTCStatsReport* report) const;
  // Produces |RTCPeerConnectionStats|.
  void ProducePeerConnectionStats_s(
      int64_t timestamp_us, RTCStatsReport* report) const;
  // Produces |RTCInboundRTPStreamStats| and |RTCOutboundRTPStreamStats|.
  void ProduceRTPStreamStats_n(
      int64_t timestamp_us,
      const std::map<std::string, std::string>& transport_names_by_mid,
      const TrackMediaInfoMap& track_media_info_map,
      RTCStatsReport* report) const;
  // Produces |RTCTransportStats|.
  void ProduceTransportStats_n(
      int64_t timestamp_us,
      const std::map<std::string, cricket::TransportStats>&
          transport_stats_by_name,
      const std::map<std::string, CertificateStatsPair>& transport_cert_stats,
      RTCStatsReport* report) const;

  // Helper function to stats-producing functions.
  std::map<std::string, CertificateStatsPair>
  PrepareTransportCertificateStats_n(
      const std::map<std::string, cricket::TransportStats>&
          transport_stats_by_name) const;
  std::unique_ptr<TrackMediaInfoMap> PrepareTrackMediaInfoMap_s() const;
  std::map<MediaStreamTrackInterface*, std::string> PrepareTrackToID_s() const;

  // Slots for signals (sigslot) that are wired up to |pc_|.
  void OnDataChannelCreated(DataChannel* channel);
  // Slots for signals (sigslot) that are wired up to |channel|.
  void OnDataChannelOpened(DataChannel* channel);
  void OnDataChannelClosed(DataChannel* channel);

  PeerConnectionInternal* const pc_;
  rtc::Thread* const signaling_thread_;
  rtc::Thread* const worker_thread_;
  rtc::Thread* const network_thread_;
  rtc::AsyncInvoker invoker_;

  int num_pending_partial_reports_;
  int64_t partial_report_timestamp_us_;
  rtc::scoped_refptr<RTCStatsReport> partial_report_;
  std::vector<rtc::scoped_refptr<RTCStatsCollectorCallback>> callbacks_;

  // Set in |GetStatsReport|, read in |ProducePartialResultsOnNetworkThread| and
  // |ProducePartialResultsOnSignalingThread|, reset after work is complete. Not
  // passed as arguments to avoid copies. This is thread safe - when we
  // set/reset we know there are no pending stats requests in progress.
  std::map<std::string, std::string> transport_names_by_mid_;
  std::unique_ptr<TrackMediaInfoMap> track_media_info_map_;
  std::map<MediaStreamTrackInterface*, std::string> track_to_id_;

  rtc::Optional<std::string> voice_mid_;
  rtc::Optional<std::string> video_mid_;

  Call::Stats call_stats_;

  // A timestamp, in microseconds, that is based on a timer that is
  // monotonically increasing. That is, even if the system clock is modified the
  // difference between the timer and this timestamp is how fresh the cached
  // report is.
  int64_t cache_timestamp_us_;
  int64_t cache_lifetime_us_;
  rtc::scoped_refptr<const RTCStatsReport> cached_report_;

  // Data recorded and maintained by the stats collector during its lifetime.
  // Some stats are produced from this record instead of other components.
  struct InternalRecord {
    InternalRecord() : data_channels_opened(0),
                       data_channels_closed(0) {}

    // The opened count goes up when a channel is fully opened and the closed
    // count goes up if a previously opened channel has fully closed. The opened
    // count does not go down when a channel closes, meaning (opened - closed)
    // is the number of channels currently opened. A channel that is closed
    // before reaching the open state does not affect these counters.
    uint32_t data_channels_opened;
    uint32_t data_channels_closed;
    // Identifies by address channels that have been opened, which remain in the
    // set until they have been fully closed.
    std::set<uintptr_t> opened_data_channels;
  };
  InternalRecord internal_record_;
};

const char* CandidateTypeToRTCIceCandidateTypeForTesting(
    const std::string& type);
const char* DataStateToRTCDataChannelStateForTesting(
    DataChannelInterface::DataState state);

}  // namespace webrtc

#endif  // PC_RTCSTATSCOLLECTOR_H_
