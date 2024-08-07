/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2019
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Stefano Avallone <stavallo@unina.it>
 *          Sébastien Deronne <sebastien.deronne@gmail.com>
 * Modified by: Davide Magrin <magrinda@uw.edu>
 */

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/log.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/mobility-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/qos-txop.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/application-container.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/on-off-helper.h"
#include "ns3/bulk-send-helper.h"
#include "ns3/bulk-send-application.h"
#include "ns3/ping-helper.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-psdu.h"
#include "ns3/ctrl-headers.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/he-configuration.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/trace-helper.h"
#include "ns3/wifi-mpdu.h"
// #include "ns3/regular-wifi-mac.h"
// #include "ns3/v4ping-helper.h"


#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <fstream>
#include <algorithm>
#include <type_traits>
#include "ns3/core-module.h"
#include <string>



using namespace ns3;
// using ::operator<<;


NS_LOG_COMPONENT_DEFINE ("WifiOfdmaExample");
bool graph_stats{false}; // If true we are simulating to gather data for plots
uint32_t mac_tx = 0;
uint32_t mac_rx = 0;
Time total_txDuration_basic_trigger = Seconds(0);
Time total_txDuration_bsrp_trigger = Seconds(0);
Time total_ul_psduDurationSum = Seconds(0);

// void
// CwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
// {
//     *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd
//                          << std::endl;
//     // std::cout << "Old Congestion window value changed from: "<< oldCwnd <<" to "<< newCwnd <<"\n";

// }
AsciiTraceHelper asciiTraceHelper;
Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("tracecwnd_new.cwnd");
      

void
CwndChange_new(uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd
                         << std::endl;
    // std::cout << "new Congestion window value changed from: "<< oldCwnd <<" to "<< newCwnd <<"\n";

}


// Trace Function for cwnd
void
TraceCwnd(Ptr<BulkSendApplication> sender, uint32_t node, uint32_t cwndWindow, Callback<void, uint32_t, uint32_t> CwndTrace)
{
    std::cout << "tracecwnd called"<<"\n";
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(node) +
                                      "/$ns3::TcpL4Protocol/SocketList/" +
                                      std::to_string(cwndWindow) + "/CongestionWindow",
                                  CwndTrace);

      // auto x = sender->GetNode()->GetId();
      sender->GetSocket()->TraceConnectWithoutContext("CongestionWindow",
                                             MakeBoundCallback(&CwndChange_new));

}


    

/**
 * \brief Example to test DL/UL OFDMA
 *
 * This example creates an 11ax BSS (the BSS under test) and a configurable
 * number of overlapping BSSes made of stations of a given technology (11n,
 * 11ac or 11ax).
 *
 * Usage: ./waf --run "wifi-ofdma [options]"
 *
 * The available options can be listed by typing:
 *
 * ./waf --run "wifi-ofdma --help"
 *
 * It is mandatory to pass a text file (via the trafficFile option) specifying
 * the traffic flows to generate. Empty lines or lines beginning with '#'
 * in such text file are ignored. The syntax of a line is:
 *
 * <START_STA_ID> [<END_STA_ID>] <AC> <L4PROTO> <DIR> <PKTSIZE> IDT|CBR <VALUE>
 *
 * where:
 *
 * - START_STA_ID is the ID of the non-AP STA which is the sender/receiver
 *   (depending on the traffic direction) of this flow. IDs start at 1.
 * - if END_STA_ID (>= START_STA_ID) is specified, flows will be generated
 *   for every station having ID included in START_STA_ID..END_STA_ID.
 * - AC is the Access Category ('BE', 'BK', 'VI' or 'VO').
 * - L4PROTO is the transport layer protocol ('UDP' or 'TCP').
 * - DIR can be 'DL' for a single downlink flow, 'UL' for a single uplink flow
 *   and 'DL+UL' for two symmetric flows (downlink and uplink).
 * - PKTSIZE is the packet size in bytes.
 * - if 'IDT' is specified, VALUE represents the inter-departure time (in
 *   milliseconds) between two consecutive packets. If 'CBR' is specified,
 *   VALUE represents the data rate (in Mbps).
 */
class WifiOfdmaExample
{
public:
  // Helper class to store min, max, avg and count statistics
  template <typename T>
  struct Stats
  {
    Stats (uint16_t val = 0) {}   // to allow init to 0 (in PrintStatsWithTotal)
    void AddSample (T value);
    void RemoveSample (T value);
    Stats<T>& operator+= (const Stats<T>& rhs);  // merge samples

    std::multiset<T> m_samples;
  };

  struct PacketsinFlow  // To keep track of packets in flow which dropped and which received
  { 

    std::multiset<std::pair<uint64_t /* UID */,bool>> m_samples;
  };


  struct Flow
  {
    enum
    {
      DOWNLINK,
      UPLINK
    } m_direction;
    enum
    {
      TCP,
      UDP
    } m_l4Proto;
    AcIndex m_ac;
    uint16_t m_stationId;     // starting at 1
    uint16_t m_dstPort;
    uint32_t m_payloadSize;   // bytes
    double m_dataRate;        // b/s
    /* Measured end-to-end metrics */
    Stats<double> m_latency;
    

    PacketsinFlow packetsinFlow, packetsinFlow_mac;
    Stats<std::pair<uint64_t /* UID */,double> > appLatencies;
     
    long double m_txBytes {0};
    uint64_t m_txPackets {0};
    uint64_t m_packetsRejectedBySocket{0};
    long double m_rxBytes {0};
    uint64_t m_rxPackets {0};
    long double m_prevRxBytes {0};
    // 
    uint64_t m_txPackets_mac{0};
    uint64_t m_rxPackets_mac{0};
    long double m_rxBytes_mac {0};
    long double m_txBytes_mac {0};
    /*
     * For UDP flows, map packet's UID to the time it was transmitted. For TCP flows,
     * map the total amount of bytes transmitted at the time a packet was sent to
     * the time the packet was transmitted.
     */
    std::unordered_map<uint64_t, Time> m_inFlightPackets;
  };

  /**
   * Create an example instance.
   */
  WifiOfdmaExample ();
  virtual ~WifiOfdmaExample ();
  /**
   * Parse the options provided through command line.
   */
  void Config (int argc, char *argv[]);
  /**
   * Read the specs of the traffic flows to generate.
   */
  void GenerateTrafficFlows (void);
  /**
   * Setup nodes, devices and internet stacks.
   */
  void Setup (void);
  /**
   * Run simulation.
   */
  std::string getULStatus(){
        return m_ulTraffic;
        }
  void Run (void);
  /**
   * Print Packet Latencies. #Newly Added by Prajwal
   */
  void PrintPacketLatencies (std::ostream &os);
    /**
   * Print results.
   */
  void PrintResults (std::ostream& os);
  /**
   * Make the current station associate with the AP.
   */
  void StartAssociation (void);
  /**
   * Make the AP establish a BA agreement with the current station.
   */
  void EstablishBaAgreement (Mac48Address bssid);
  /**
   * Start a client application.
   */
  void StartClient (OnOffHelper client, std::size_t i, Ptr<Node> node);
  /**
   * Start a client application.
   */
  void StartClient_tcp (BulkSendHelper client, std::size_t i, Ptr<Node> node);
  /**
   * Start an OBSS client application.
   */
  void StartObssClient (OnOffHelper client, uint16_t bss, Ptr<Node> node);
  /**
   * Delay the start of traffic generation.
   */
  void DelayStart (void);
  /**
   * Start generating traffic.
   */
  void StartTraffic (void);
  /**
   * Start collecting statistics.
   */
  void StartStatistics (void);
  /**
   * Stop collecting statistics.
   */
  void StopStatistics (void);
  /**
   * Report that an MPDU was not correctly received.
   */
  void NotifyTxFailed (Ptr<const WifiMpdu> mpdu);
  /**
   * Report that the lifetime of an MSDU expired.
   */
  void NotifyMsduExpired (Ptr<const WifiMpdu> mpdu);
  /**
   * Report that an MSDU was dropped before enqueue into EDCA queue.
   */
  void NotifyMsduRejected (Ptr<const WifiMpdu> mpdu);
  /**
   * Report that a packet was transmitted by the i-th application.
   */
  void NotifyAppTx (std::size_t i, Ptr<const Packet> packet);
  /**
   * Report that a packet was received by the i-th application.
   */
  void NotifyAppRx (std::size_t i, Ptr<const Packet> packet, const Address &address);
  /**
   * Report that a packet was enqueued in an EDCA queue.
   */
  void NotifyEdcaEnqueue (Ptr<const WifiMpdu> mpdu);
  /**
   * Report that the MAC layer is forwarding up a received packet.
   */
  void NotifyMacForwardUp (Ptr<const Packet> p);
  /**
   * Report that an MSDU was dequeued from the EDCA queue.
   */
  void NotifyMsduDequeuedFromEdcaQueue (Time maxDelay, Ptr<const WifiMpdu> item);
  /**
   * Report that PSDUs were forwarded down to the PHY.
   */
  void NotifyPsduForwardedDown (WifiConstPsduMap psduMap, WifiTxVector txVector, double txPowerW);

  void NotifyPpduForwardedDown (Ptr<WifiPpdu> ppdu, WifiTxVector txVector, double txPowerW);
  
  /**
   * Report the TXOP duration when a TXOP ends.
   */
  void NotifyTxopDuration (uint32_t nodeId, AcIndex ac, Time startTime, Time duration);
  /**
   * Report the sojourn time of a packet dequeued from a qdisc
   */
  void NotifySojournTime (std::size_t nodeId, AcIndex ac, Time sojournTime);
  /**
   * Report that a packet has been dropped after dequeue
   */
  void NotifyDropAfterDequeue (std::size_t nodeId, AcIndex ac, Ptr<const QueueDiscItem> item, const char* reason);
  /**
   * Store the per-flow amount of bytes received so far
   */
  void StoreCumulativeRxBytes (void);
  /**
   * Parse context strings of the form "/NodeList/x/DeviceList/y/" to extract the NodeId
   */
  uint32_t ContextToNodeId (const std::string &context);

  void NotifyStateChange (std::string context, Time oldt, Time duration, WifiPhyState newState);

  void NotifyCwChange (uint32_t oldCw, uint32_t newCw);
  void NotifyBackoffChange (uint32_t backoff);

  static const std::map<AcIndex, std::string> m_aciToString;
  static uint16_t m_nEcdfSamples;       // by default, minimum, median and maximum

private:
  template <typename T>
  void ReadParamList (std::string s, std::vector<T>& vec);

  template <typename T, typename FUNC>
  void PrintStatsWithTotal (std::vector<std::map<AcIndex, T>> v, FUNC select,
                            std::ostream& os, std::string s, std::string s0 = "");
  template <typename T, typename FUNC>
  void PrintStats (std::vector<std::map<AcIndex, T>> v, FUNC select,
                   std::ostream& os, std::string s, std::string s0 = "");

  /* Parameters */
  // double m_simulationTime{20}; // seconds
//   bool graph_stats{true}; // If true we are simulating to gather data for plots
  double extrastoptime{100}; // Extra time simulator runs and then stops
  double m_simulationTime{10}; // seconds
  uint32_t m_startInterval{10};
  int m_na{5};
  std::string m_dlTraffic{"mu"};
  std::string m_ulTraffic{"mu"};
  double m_ulFlowDataRate{1}; // If -1, full queues
  double m_dlFlowDataRate{1}; // If -1, full queues
  // duration of the interval in which apps can start sending packets (ms)
  uint16_t m_nHeStations{4};
  uint16_t m_nVhtStations{0};
  double m_legacyFraction{0};
  uint16_t m_nStations; // number of non-AP stations in the BSS under test
  uint16_t m_nObss{0}; // number of overlapping BSSs
  uint16_t m_nStationsPerObss{0}; // number of non-AP stations per each OBSS
  WifiStandard m_obssStandard;
  double m_radius{0}; // meters
  bool m_enableDlOfdma{true};
  bool m_forceDlOfdma{false};
  //enable ofdma
  bool m_enableUlOfdma{true};
  bool startThroughputcalc{false};
  bool m_enableTxopSharing{false};
  bool m_enableBsrp{true};  
   double prevTime=0,currTime;
 
   uint64_t recvPackets=0;
  //  std::multimap< ns3::Time,uint64_t> tcptime;
   
  bool m_useCentral26TonesRus {false};
  uint32_t m_ulPsduSize{2000}; // bytes
  uint16_t m_channelWidth{40}; // channel bandwidth (MHz)
  uint8_t m_channelNumber{36};
  WifiPhyBand m_band {WIFI_PHY_BAND_UNSPECIFIED};
  uint16_t m_guardInterval{800}; // GI in nanoseconds
  bool m_enableObssPd{false};  // Changed this
  double m_obssPdThresholdBss{-99.0};
  double m_obssPdThresholdMinBss{-82.0};
  double m_obssPdThresholdMaxBss{-62.0};
  bool m_enableThresholdPreambleDetection{false};
  uint32_t m_obssDlPayload{1400}; // bytes
  uint32_t m_obssUlPayload{1400}; // bytes
  double m_obssDlAggregateRate{50.0}; // Mbps
  double m_obssUlAggregateRate{50.0}; // Mbps
  double m_powerSta{17.0}; // dBm
  double m_powerAp{17.0}; // dBm
  double m_psdLimitation {10.0};        // dBm/MHz
  double m_ccaThresholdSta{-62}; // dBm
  double m_ccaThresholdAp{-62}; // dBm
  double m_txGain{0.0}; // dBi
  double m_rxGain{0.0}; // dBi
  double m_rxSensitivity{-91.0};
  uint16_t m_maxNRus{18}; // max number of RUs per MU PPDU
  std::string m_heRate{"5"}; // "ideal" or MCS value
  uint16_t m_beMaxAmsduSize{0}; // maximum A-MSDU size for BE
  uint16_t m_bkMaxAmsduSize{0}; // maximum A-MSDU size for BK
  uint16_t m_viMaxAmsduSize{0}; // maximum A-MSDU size for VI
  uint16_t m_voMaxAmsduSize{0}; // maximum A-MSDU size for VO
  uint32_t m_beMaxAmpduSize{8388607u}; // maximum A-MPDU size for BE
  uint32_t m_bkMaxAmpduSize{8388607u}; // maximum A-MPDU size for BK
  uint32_t m_viMaxAmpduSize{8388607u}; // maximum A-MPDU size for VI
  uint32_t m_voMaxAmpduSize{8388607u}; // maximum A-MPDU size for VO
  uint32_t m_obssMaxAmpduSize{65535}; // maximum A-MPDU size for overlapping BSSes
  double m_beTxopLimit{4800}; // microseconds
  double m_bkTxopLimit{4800}; // microseconds
  double m_viTxopLimit{4800}; // microseconds
  double m_voTxopLimit{4800}; // microseconds
  double m_obssTxopLimit{2528}; // microseconds
  uint32_t m_macQueueSize{50}; // packets
  uint32_t m_beMsduLifetime{20000}; // milliseconds
  uint32_t m_bkMsduLifetime{500}; // milliseconds
  uint32_t m_viMsduLifetime{500}; // milliseconds
  uint32_t m_voMsduLifetime{100}; // milliseconds
  bool m_useExplicitBar{false}; // use explicit BAR after missed Block Ack
  // uint16_t m_muBeAifsn{3};
  // uint16_t m_muBkAifsn{0};
  // uint16_t m_muViAifsn{0};
  // uint16_t m_muVoAifsn{0};
  uint16_t m_BeCwMin{15};
  // uint16_t m_muBeCwMin{15};
  // uint16_t m_muBkCwMin{15};
  // uint16_t m_muViCwMin{15};
  // uint16_t m_muVoCwMin{15};
  // uint16_t m_muBeCwMax{8191};
  // uint16_t m_muBkCwMax{8191};
  // uint16_t m_muViCwMax{8191};
  // uint16_t m_muVoCwMax{8191};
  // uint32_t m_beMuEdcaTimer{0}; // microseconds
  // uint32_t m_bkMuEdcaTimer{0}; // microseconds
  // uint32_t m_viMuEdcaTimer{0}; // microseconds
  // uint32_t m_voMuEdcaTimer{0}; // microseconds
  bool m_enableRts{false};
  // std::string m_dlAckSeqType{"AGGR-MU-BAR"};
  std::string m_dlAckSeqType{"ACK-SU-FORMAT"};
  uint16_t m_baBufferSize{256};
  std::string m_queueDisc{"default"};
  bool m_enablePcap{true};
  double m_warmup{0.5}; // duration of the warmup period (seconds)
  uint32_t m_tcpSegmentSize{1500}; // TCP maximum segment size (0 = use default)
  uint32_t m_tcpInitialCwnd{0}; // TCP initial congestion window size (segments, 0 = use default)
  uint32_t m_tcpMinRto{500}; // TCP minimum retransmit timeout (milliseconds, 0 = use default)
  std::string m_trafficFile; // name of file describing traffic flows to generate
  bool m_verbose{false};
  uint16_t m_nIntervals{20}; // number of intervals in which the simulation time is divided
  uint16_t m_elapsedIntervals{0};
  // std::string m_scheduler = "rr";
  std::string m_dlscheduler = "rr"; 
  std::string m_ulscheduler = "rr";
  // std::string m_ulscheduler = "rr";
 
  // rr - full bw usage(no bw waste)
  // bellalta - maximizes STA(there can be bw waste) equal split

  int m_frameSize{1500}; // Size of APP layer packets in bytes
  std::string m_ssidPrefix{"network-"};
  std::vector<double> m_apDistances, m_apThetas;
  NodeContainer m_apNodes;
  NodeContainer m_staNodes;
  std::vector<NodeContainer> m_obssStaNodes;
  NetDeviceContainer m_apDevices;
  NetDeviceContainer m_staDevices;
  std::vector<NetDeviceContainer> m_obssStaDevices;
  Ipv4InterfaceContainer m_apInterfaces;
  Ipv4InterfaceContainer m_staInterfaces;
  std::vector<Ipv4InterfaceContainer> m_obssStaInterfaces;
  uint16_t m_currentSta {0};         // index of the current station
  ApplicationContainer m_sinkApps;
  std::vector<ApplicationContainer> m_obssSinkApps;
  std::vector<Ptr<Application>> m_clientApps;
  std::vector<double> m_clientApps_onTime_Info;
  std::vector<ApplicationContainer> m_obssClientApps;
  std::vector<Flow> m_flows;
  std::vector<double> Throughputs;

  std::vector<uint64_t> m_obssDlRxStart, m_obssDlRxStop;
  std::vector<uint64_t> m_obssUlRxStart, m_obssUlRxStop;
  Stats<double> m_ulMuCompleteness;
  Stats<double> m_dlMuCompleteness; // ratio of actual amount of bytes to max amount of bytes
                                    // (given its duration) carried in a DL MU PPDU
  Stats<double> m_dlMuPpduDuration; // TX duration (ms) of DL MU PPDUs
  Time dlMuPpduDuration_all{0};
  uint64_t m_nBasicTriggerFramesSent {0};
  uint64_t m_nFailedBasicTriggerFrames {0};  // no station responded (with a QoS Data or BAR frame)
  uint64_t m_nBsrpTriggerFramesSent {0};
  uint64_t m_nFailedBsrpTriggerFrames {0};  // no station responded with a QoS Null frame
  Stats<double> m_heTbCompleteness; // ratio of sum of durations of A-MPDUs carrying QoS Data/BAR frames in
                                    // an HE TB PPDU to sum of durations granted by Basic Trigger Frame
  Time m_tfUlLength {Seconds (0)};  // TX duration coded in UL Length subfield of Trigger Frame
  Stats<double> m_heTbPpduDuration; // TX duration (ms) coded in UL Length subfield of Basic TFs
  Time m_overallTimeGrantedByTf {Seconds (0)}; // m_tfUlLength times the number of addressed stations
  Time m_durationOfResponsesToLastBasicTf {Seconds (0)}; // sum of the durations of the HE TB PPDUs (excluding
                                                         // QoS Null frames) in response to the last Basic TF
  uint64_t m_countOfNullResponsesToLastTf {0}; // count of QoS Null frames sent in response to the last TF
  TriggerFrameType m_lastTfType;   // type of the last Trigger Frame sent (Basic or Bsrp)

  // TriggerFrameType m_lastTfType_ppdu;

  Time m_durationOfResponsesToLastBasicTf_all{0};
  int prev_tx{0};
  int prev_rx{0}; 

  // Metrics that can be measured (sender side) for each (AP,STA) pair (DL) or
  // (STA,AP) pair (UL) and for each Access Category
  struct PairwisePerAcStats
  {
    uint64_t expired {0};
    uint64_t rejected {0};          // dropped before enqueue into EDCA queue
    uint64_t failed {0};
    Stats<double> l2Latency;
    Time lastTxTime {Seconds (0)};
    Stats<double> pairwiseHol;      // pairwise Head-of-Line delay *DL only*
    Stats<uint32_t> ampduSize;      // size (bytes) of MPDUs sent to/received from each STAs
    Stats<double> ampduRatio;       // ratio of the duration of the A-MPDU sent to (for DL) or received
                                    // from (for UL) a STA to the duration of the DL MU PPDU or HE TB PPDU
  };
  std::vector<std::map<AcIndex, PairwisePerAcStats>> m_dlPerStaAcStats;   // A vector element per station (DL)
  std::vector<std::map<AcIndex, PairwisePerAcStats>> m_ulPerStaAcStats;   // A vector element per station (UL)

  // Metrics that can be measured (sender side) for each Access Category
  // and are independent of the receiver
  struct PerAcStats
  {
    Stats<double> txopDuration;
    Stats<double> queueDiscSojournTime;
    uint32_t droppedByQueueDisc {0};
    Time lastTxTime {Seconds (0)};
    Stats<double> aggregateHoL;     // aggregate Head-of-Line delay
  };
  std::vector<std::map<AcIndex, PerAcStats>> m_perAcStats;  // first vector element for the AP, then one per STA

  struct InFlightPacketInfo
  {
    Mac48Address m_srcAddress;
    Mac48Address m_dstAddress;
    AcIndex m_ac;
    Ptr<const Packet> m_ptrToPacket;
    Time m_edcaEnqueueTime {Seconds (0)};  // time the packet was enqueued into an EDCA queue
  };
  std::unordered_map<uint64_t /* UID */, std::list<InFlightPacketInfo>> m_inFlightPacketMap;
  
  std::vector<uint64_t> m_nSolicitingBasicTriggerFrames;

  // Function object to compute the hash of a MAC address
  struct MacAddressHash
  {
    std::size_t operator()(const Mac48Address& address) const;
  };

  std::unordered_map <Mac48Address, uint32_t, MacAddressHash> m_staMacAddressToNodeId;

  /**
   * Return the ID of the node containing the device having the given address.
   */
  uint32_t MacAddressToNodeId (Mac48Address address);
  /**
   * Return an iterator to the PairwisePerAcStats object corresponding to the given MPDU header.
   */
  std::pair<std::map<AcIndex, PairwisePerAcStats>::iterator, bool>
    GetPairwisePerAcStats (const WifiMacHeader& hdr, AcIndex = AC_UNDEF);
  /**
   * Return the OBSS id and the index of station having the given id.
   */
  std::pair<uint16_t, std::size_t> StationIdToBssIndexPair (std::size_t id);
};

const std::map<AcIndex, std::string> WifiOfdmaExample::m_aciToString = { {AC_BE, "BE"}, {AC_BK, "BK"},
                                                                         {AC_VI, "VI"}, {AC_VO, "VO"}, };

uint16_t WifiOfdmaExample::m_nEcdfSamples = 3;       // by default, minimum, median and maximum

template <typename T>
void
WifiOfdmaExample::Stats<T>::AddSample (T value)
{
  m_samples.insert (value);
}

template <typename T>
void
WifiOfdmaExample::Stats<T>::RemoveSample (T value)
{
  auto it = m_samples.find (value);
  if (it != m_samples.end ())
    {
      m_samples.erase (it);
    }
}

template <typename T>
WifiOfdmaExample::Stats<T>&
WifiOfdmaExample::Stats<T>::operator+= (const Stats<T>& rhs)
{
  this->m_samples.insert (rhs.m_samples.begin (), rhs.m_samples.end ());
  return *this;
}

template <typename T>
std::ostream& operator<< (std::ostream& os, const WifiOfdmaExample::Stats<T> &stats)
{
//   os << std::fixed << std::setprecision (3)
//      << "(" << stats.m_min << ", " << stats.m_avg << ", " << stats.m_max << ", " << stats.m_count << ") ";
  
  os << std::fixed << std::setprecision (3) << "(";
  uint16_t m = WifiOfdmaExample::m_nEcdfSamples - 1;   // number of sub-intervals of [0,1]
  std::size_t pos = 0, count = stats.m_samples.size ();
  double sum = 0;

  if (count > 0)
    {
      for (auto it = stats.m_samples.begin (); it != stats.m_samples.end(); ++it)
        {
          sum += *it;
        }
    }

  if (count > 0)
    {
      auto it = stats.m_samples.begin ();
      for (uint8_t i = 0; i <= m; i++)
        {
          std::advance (it, pos);
          os << *it << (i < m ? ", " : "");
          pos = (i + 1) * (count - 1) / m - i * (count - 1) / m;
        }
    }
  os << ")[" << count << "]"
     << "<" << sum / double (count) << ">";

  os << "sum: "<< sum;
  return os;

}

std::ostream &
operator<< (std::ostream &os, const WifiOfdmaExample::Flow &flow)
{
  os << "{staId=" << flow.m_stationId
     << (flow.m_direction == WifiOfdmaExample::Flow::DOWNLINK ? " DL " : " UL ")
     << "AC=" << WifiOfdmaExample::m_aciToString.at (flow.m_ac)
     << (flow.m_l4Proto == WifiOfdmaExample::Flow::TCP ? " TCP " : " UDP ") << flow.m_payloadSize
     << "B " << std::defaultfloat << flow.m_dataRate << "bps}";
  return os;
}

std::size_t
WifiOfdmaExample::MacAddressHash::operator() (const Mac48Address &address) const
{
  uint8_t buffer[6];
  address.CopyTo (buffer);
  std::string s (buffer, buffer + 6);
  return std::hash<std::string>{}(s);
}

std::pair<uint16_t, std::size_t>
WifiOfdmaExample::StationIdToBssIndexPair (std::size_t id)
{
  NS_ABORT_MSG_IF (id < m_nStations, "Station is part of the BSS under test");
  return std::make_pair ((id - m_nStations) / m_nStationsPerObss + 1,
                         (id - m_nStations) % m_nStationsPerObss);
}

template <typename T>
void
WifiOfdmaExample::ReadParamList (std::string s, std::vector<T> &vec)
{
  std::size_t start = 0, end;
  do
    {
      end = s.find_first_of (",", start);
      std::stringstream ss (s.substr (start, end));
      T tmp;
      NS_ABORT_MSG_IF (!(ss >> tmp), "Unexpected value " << ss.str ());
      vec.push_back (tmp);
      if (end != std::string::npos)
        {
          start = end + 1;
        }
    }
  while (end != std::string::npos);
}

WifiOfdmaExample::WifiOfdmaExample ()
{
}

WifiOfdmaExample::~WifiOfdmaExample ()
{
  m_dlPerStaAcStats.clear ();
  m_ulPerStaAcStats.clear ();
  m_perAcStats.clear ();
  m_inFlightPacketMap.clear ();
}

void
WifiOfdmaExample::Config (int argc, char *argv[])
{
  NS_LOG_FUNCTION (this);
  std::string simScheduler ("map");
  std::string obssStandard = "11ax";
  std::string psdLimitRegulator = "FCC";
  std::string apDistance, apTheta;
    // std::string apDistance = "5", apTheta="0";

  CommandLine cmd;
  cmd.AddValue ("verbose", "Whether to print additional output", m_verbose);
  cmd.AddValue ("frameSize", "Size of APP frames", m_frameSize);
  cmd.AddValue ("nStations", "Number of non-AP stations in the BSS under test", m_nStations);
  cmd.AddValue ("legacyFraction", "Fraction of stations using 802.11ac", m_legacyFraction);
  cmd.AddValue ("channelWidth", "Channel bandwidth (20, 40, 80, 160)", m_channelWidth);
  cmd.AddValue ("dl", "Whether the AP should generate traffic (toward all STAs)", m_dlTraffic);
  cmd.AddValue ("ul", "Whether all STAs should generate traffic (toward the AP)", m_ulTraffic);
  cmd.AddValue ("mcs", "'ideal' or the constant MCS value to transmit HE PPDUs", m_heRate);
  cmd.AddValue ("Na", "Maximum allowed number of aggregated MPDUs", m_na);
  cmd.AddValue ("cwMin", "Value of the QosTxop CWmin parameter", m_BeCwMin);
  // cmd.AddValue ("muCwMin", "Value of the QosTxop CWmin parameter", m_muBeCwMin);
  // cmd.AddValue ("muEdcaTimer", "Duration of the MU EDCA Parameter Set", m_beMuEdcaTimer);
  cmd.AddValue ("dlFlowDataRate", "Data rate of each flow", m_dlFlowDataRate);
  cmd.AddValue ("ulFlowDataRate", "Data rate of each flow", m_ulFlowDataRate);
  cmd.AddValue ("ackSeqType", "ACK Sequence to employ for DL tx", m_dlAckSeqType);
  cmd.AddValue ("maxTxopDuration", "TXOP duration for BE in microseconds", m_beTxopLimit);
  cmd.AddValue ("simulationTime", "Time to simulate", m_simulationTime);
  cmd.AddValue ("m_enableBsrp", "BSRP on or off", m_enableBsrp);
  cmd.Parse (argc, argv);

  std::cout << "DL Scheduler " << m_dlscheduler << '\n';
  std::cout << "UL Scheduler " << m_ulscheduler << '\n';

  // if (m_muBeCwMin == 0)
  //   {
  //     m_muBeAifsn = 0;
  //     m_muBeCwMin = 32767;
  //     m_muBeCwMax = 32767;
  //   }
  // else
  //   {
  //     m_muBeCwMax = (64 * (m_muBeCwMin + 1)) - 1;
  //   }

  m_nVhtStations = (int) (m_nStations * m_legacyFraction);
  m_nHeStations = (int) (m_nStations - m_nVhtStations);

  if (m_dlTraffic != "mu")
    {
      m_dlAckSeqType = "NO-OFDMA";
    }
  m_enableDlOfdma = (m_dlAckSeqType != "NO-OFDMA");

  m_enableUlOfdma = m_ulTraffic == "mu";


  // m_enableBsrp = m_ulTraffic == "mu";

  m_forceDlOfdma = m_dlTraffic == "mu";

  if (m_dlTraffic == "mu")
    {
      m_maxNRus = std::max (1, (int) m_nHeStations);
    }
  else
    {
      m_maxNRus = 1;
    }
  // m_ulPsduSize = 200 + (m_frameSize + 72) * m_na;
  m_ulPsduSize = 2500;
  m_beMaxAmpduSize = 200 + (m_frameSize + 72) * m_na;

  // if (m_beMuEdcaTimer > 0)
  //   {
  //     m_bkMuEdcaTimer = m_beMuEdcaTimer;
  //     m_viMuEdcaTimer = m_beMuEdcaTimer;
  //     m_voMuEdcaTimer = m_beMuEdcaTimer;
  //   }

  ObjectFactory factory;

  if (simScheduler == "map")
    {
      factory.SetTypeId ("ns3::MapScheduler");
    }
  else if (simScheduler == "list")
    {
      factory.SetTypeId ("ns3::ListScheduler");
    }
  else if (simScheduler == "heap")
    {
      factory.SetTypeId ("ns3::HeapScheduler");
    }
  else if (simScheduler == "cal")
    {
      factory.SetTypeId ("ns3::CalendarScheduler");
    }
  else
    {
      NS_ABORT_MSG ("Unknown simulator scheduler: " << simScheduler);
    }
  Simulator::SetScheduler (factory);

  NS_ABORT_MSG_IF (m_nVhtStations > 0 && m_heRate != "ideal",
                   "Cannot use constant rate manager when both VHT and HE stations are present");

  if (m_maxNRus == 0)
    {
      // we do not want to use OFDMA
      m_dlAckSeqType = "NO-OFDMA";
    }

  m_enableDlOfdma = (m_dlAckSeqType != "NO-OFDMA");
  // m_enableDlOfdma = false;

  switch (m_channelWidth)
    {
    case 20:
      m_channelNumber = 36;
      break;
    case 40:  
      m_channelNumber = 38;
      break;
    case 80:
      m_channelNumber = 42;
      break;
    case 160:
      m_channelNumber = 50;
      break;
    default:
      NS_ABORT_MSG ("Invalid channel bandwidth (must be 20, 40, 80 or 160)");
    }

  if (obssStandard == "11ax")
    {
      m_obssStandard = WIFI_STANDARD_80211ax;
    }
  else if (obssStandard == "11ac")
    {
      m_obssStandard = WIFI_STANDARD_80211ac;
    }
  else if (obssStandard == "11n")
    {
      m_obssStandard = WIFI_STANDARD_80211n;
    }
  else
    {
      NS_ABORT_MSG ("Invalid standard for OBSS (choose among 11ax, 11ac, 11n)");
    }

  if (m_obssStandard != WIFI_STANDARD_80211ax)
    {
      m_enableObssPd = false;
    }

  if (psdLimitRegulator == "FCC")
    {
      m_psdLimitation = 11.0; // 11 dBm/MHz, FCC limit for 5150 to 5350 MHz range for STAs
    }
  else if (psdLimitRegulator == "ETSI")
    {
      m_psdLimitation = 10.0; // 10 dBm/MHz, ETSI limit for 5150 to 5350 MHz range
    }
  else
    {
      NS_ABORT_MSG ("Invalid regulation body for PSD limitation (choose among FCC, ETSI)");
    }

  m_nStations = m_nHeStations + m_nVhtStations;

  GenerateTrafficFlows ();
  NS_ABORT_MSG_IF (m_flows.empty (), "No traffic flow specified!");

  if (!apDistance.empty ())
    {
      ReadParamList (apDistance, m_apDistances);
    }
  NS_ABORT_MSG_IF (m_apDistances.size () != m_nObss,
                   "The size of the AP distance vector ("
                       << m_apDistances.size ()
                       << ") does not match the number of overlapping BSSes (" << m_nObss << ")");

  if (!apTheta.empty ())
    {
      ReadParamList (apTheta, m_apThetas);
    }
  NS_ABORT_MSG_IF (m_apThetas.size () != m_nObss,
                   "The size of the AP theta vector ("
                       << m_apThetas.size () << ") does not match the number of overlapping BSSes ("
                       << m_nObss << ")");

//   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<'\n';
  std::cout << "Channel bw = " << m_channelWidth << " MHz" << std::endl
            << (m_heRate == "ideal" ? "Ideal rate manager" : "HE MCS = " + m_heRate) << std::endl
            
            << "Number of stations = " << m_nStations << std::endl

            

            << "EDCA queue max size = " << m_macQueueSize << " MSDUs" << std::endl
            << "MSDU lifetime = " << m_beMsduLifetime << " ms [BE]  " << m_bkMsduLifetime
            << " ms [BK]  " << m_viMsduLifetime << " ms [VI]  " << m_voMsduLifetime << " ms [VO]  "
            << std::endl
            << "BA buffer size = " << m_baBufferSize << std::endl;
  if (m_enableDlOfdma)
    {
      std::cout << "Ack sequence = " << m_dlAckSeqType << std::endl;
    }
  else
    {
      std::cout << "No OFDMA" << std::endl;
    }
  std::cout << std::endl;
}

void
WifiOfdmaExample::GenerateTrafficFlows ()
{
  uint16_t dstPort = 7000;
  uint16_t endStaId = m_nStations;


  //original code/////
  bool haptic = false; // 5 device haptic

  bool haptic_new = false; // 15 device haptic
 
  bool haptic_10 = false; // 8 device haptic

  if((haptic == false) && (haptic_new == false) && (haptic_10 == false)){

  for (uint16_t staId = 1; staId <= endStaId; staId++)
    {
      if (m_dlTraffic != "None")
        {
          Flow flow;
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = m_frameSize;
          flow.m_stationId = staId;
          flow.m_dataRate = 1 * m_dlFlowDataRate * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          m_flows.push_back (flow);

          // flow.m_ac = AC_VO;
          // flow.m_l4Proto = Flow::UDP;
          // flow.m_payloadSize = m_frameSize;
          // flow.m_stationId = staId;
          // flow.m_dataRate = 1 * m_dlFlowDataRate * 1e6;
          // flow.m_direction = Flow::DOWNLINK;
          // flow.m_dstPort = dstPort++;
          // NS_LOG_DEBUG ("Adding flow " << flow);
          // m_flows.push_back (flow);

        }
      if (m_ulTraffic != "None")
        {
          Flow flow; 
          // if(staId == 2){
          // flow.m_ac = AC_VI;
          // flow.m_l4Proto = Flow::UDP;
          // flow.m_payloadSize = 1000;
          // flow.m_stationId = staId;
          // flow.m_dataRate = 1 * 5 * 1e6;
          // flow.m_direction = Flow::UPLINK;
          // flow.m_dstPort = dstPort++;
          // NS_LOG_DEBUG ("Adding flow " << flow);
          // m_flows.push_back (flow);
          // }
          
          // if(staId ==1 ) flow.m_ac = AC_BE;
          // else if(staId == 2) flow.m_ac = AC_BE;
          // else if(staId == 3) flow.m_ac = AC_BE;
          // flow.m_l4Proto = Flow::UDP;
          // flow.m_payloadSize = m_frameSize;
          // flow.m_stationId = staId;
          // flow.m_dataRate = 1 * m_ulFlowDataRate * 1e6;
          // flow.m_direction = Flow::UPLINK;
          // flow.m_dstPort = dstPort++;
          // NS_LOG_DEBUG ("Adding flow " << flow);
          // m_flows.push_back (flow);

          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = m_frameSize;
          flow.m_stationId = staId;
          flow.m_dataRate = 1 * m_ulFlowDataRate * 1e6;
          // if(staId == 2){
          //   flow.m_dataRate = 1 * (m_ulFlowDataRate - 1) * 1e6;  
          // }
          // if(staId == 3){
          //   flow.m_dataRate = 1 * (m_ulFlowDataRate + 1.5) * 1e6;  
          // }
          
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          m_flows.push_back (flow);

          // if(staId == 1){
          // flow.m_ac = AC_VO;
          // flow.m_l4Proto = Flow::UDP;
          // flow.m_payloadSize = 1;
          // flow.m_stationId = staId;
          // flow.m_dataRate = 1 * (m_ulFlowDataRate/2) * 1e3;
          // flow.m_direction = Flow::UPLINK;
          // flow.m_dstPort = dstPort++;
          // NS_LOG_DEBUG ("Adding flow " << flow);
          // m_flows.push_back (flow);
          // }
          
        }
    }
}

  ///Haptic-model////////////////
  else if(haptic_new){

    for(uint16_t staId = 1; staId <= endStaId; staId++){
    Flow flow;
      if (m_dlTraffic != "None")
        {
          
          if(staId == 1 || staId == 2 || staId == 3 || staId == 4){ // Video call DL                                                      
          flow.m_ac = AC_VI;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 3 * 1 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 5 || staId == 6 || staId == 7 || staId == 8){ // Audio call DL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 160;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  64 * 1e3;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 9 || staId == 10 || staId == 11 || staId == 12){ // Video streaming DL
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 1 * 8 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 13 || staId == 14){ // file download DL
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::TCP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 8 * 1 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 15){ // haptic control DL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 64;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  512*1e3; 
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          

          }
        
      if (m_ulTraffic != "None")
        {
          // Ptr<UniformRandomVariable> rate_random;
          // rate_random = CreateObjectWithAttributes<UniformRandomVariable> ("Min", DoubleValue (0.0), "Max",
          //
          if(staId == 1 || staId == 2 || staId == 3 || staId == 4){ // video call UL                                                          
          flow.m_ac = AC_VI;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 3 * 1 * 1e6;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }else if(staId == 5 || staId == 6 || staId == 7 || staId == 8){ // audio call UL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize =  160;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  64 * 1e3;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }else if(staId == 15){ // Haptic UL(Audio, video and pos)
          flow.m_ac = AC_VO; //haptic flow 
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 64;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 64*8*1e3;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);

          /////////////////

          flow.m_ac = AC_VI; // video 
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;//assuming 30fps
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 4 * 1 * 1e6;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);

          // //////////////////

          }
          
        }
    }


  }
  else if(haptic_10)
  {
  
  for (uint16_t staId = 1; staId <= endStaId; staId++)
    {
      Flow flow;
      if (m_dlTraffic != "None")
        {
          
          if(staId == 1 || staId == 2){ // Video call DL                                                      
          flow.m_ac = AC_VI;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 3 * 1 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 3 || staId == 4){ // Audio call DL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 160;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  64 * 1e3;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 5 || staId == 6){ // Video streaming DL
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 1 * 8 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 7){ // file download DL
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::TCP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 8 * 1 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 8){ // haptic control DL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 64;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  512*1e3; 
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          
          }
        
      if (m_ulTraffic != "None")
        {
          // Ptr<UniformRandomVariable> rate_random;
          // rate_random = CreateObjectWithAttributes<UniformRandomVariable> ("Min", DoubleValue (0.0), "Max",
          //
          if(staId == 1 || staId == 2){ // video call UL                                                          
          flow.m_ac = AC_VI;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 3 * 1 * 1e6;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }else if(staId == 3 || staId == 4){ // audio call UL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize =  160;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  64 * 1e3;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }else if(staId == 8){ // Haptic UL(Audio, video and pos)
          flow.m_ac = AC_VO; //haptic flow 
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 64;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 64*8*1e3;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);

          /////////////////

          flow.m_ac = AC_VI; // video 
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;//assuming 30fps
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 4 * 1 * 1e6;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);

          // //////////////////

          // flow.m_ac = AC_VO; //audio
          // flow.m_l4Proto = Flow::UDP;
          // flow.m_payloadSize = 160;
          // // flow.m_payloadSize = 2500;
          // flow.m_stationId = staId;
          // flow.m_dataRate = 64 * 1e3;
          // flow.m_direction = Flow::UPLINK;
          // flow.m_dstPort = dstPort++;
          // NS_LOG_DEBUG ("Adding flow " << flow);
          // //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          // m_flows.push_back (flow);
          }
        }
          
        }
    }else if(haptic){
      {
  
  for (uint16_t staId = 1; staId <= endStaId; staId++)
    {
      Flow flow;
      if (m_dlTraffic != "None")
        {
          
          if(staId == 1){ // Video call DL                                                      
          flow.m_ac = AC_VI;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 3 * 1 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 2){ // Audio call DL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 160;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  64 * 1e3;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 3){ // Video streaming DL
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 1 * 8 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 4){ // file download DL
          flow.m_ac = AC_BE;
          flow.m_l4Proto = Flow::TCP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 8 * 1 * 1e6;
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          else if(staId == 5){ // haptic control DL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 64;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  512*1e3; 
          flow.m_direction = Flow::DOWNLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }
          
          }
        
      if (m_ulTraffic != "None")
        {
          // Ptr<UniformRandomVariable> rate_random;
          // rate_random = CreateObjectWithAttributes<UniformRandomVariable> ("Min", DoubleValue (0.0), "Max",
          //
          if(staId == 1){ // video call UL                                                          
          flow.m_ac = AC_VI;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 3 * 1 * 1e6;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }else if(staId == 2){ // audio call UL
          flow.m_ac = AC_VO;
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize =  160;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate =  64 * 1e3;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);
          }else if(staId == 5){ // Haptic UL(Audio, video and pos)
          flow.m_ac = AC_VO; //haptic flow 
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 64;
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 64*8*1e3;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);

          /////////////////

          flow.m_ac = AC_VI; // video 
          flow.m_l4Proto = Flow::UDP;
          flow.m_payloadSize = 1500;//assuming 30fps
          // flow.m_payloadSize = 2500;
          flow.m_stationId = staId;
          flow.m_dataRate = 4 * 1 * 1e6;
          flow.m_direction = Flow::UPLINK;
          flow.m_dstPort = dstPort++;
          NS_LOG_DEBUG ("Adding flow " << flow);
          //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          m_flows.push_back (flow);

          // //////////////////

          // flow.m_ac = AC_VO; //audio
          // flow.m_l4Proto = Flow::UDP;
          // flow.m_payloadSize = 160;
          // // flow.m_payloadSize = 2500;
          // flow.m_stationId = staId;
          // flow.m_dataRate = 64 * 1e3;
          // flow.m_direction = Flow::UPLINK;
          // flow.m_dstPort = dstPort++;
          // NS_LOG_DEBUG ("Adding flow " << flow);
          // //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Adding flow " << flow << '\n';
          // m_flows.push_back (flow);
          }
        }
          
        }
    }
    }
    }
    


void
WifiOfdmaExample::NotifyCwChange (uint32_t oldCw, uint32_t newCw)
{
  NS_LOG_FUNCTION ("Call to NotifyCwChange" << unsigned (oldCw) << unsigned (newCw));

  std::string filename = "CwLog.txt";
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::app);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }

  uint32_t c = Simulator::GetContext ();

  outFile << Simulator::Now ().GetSeconds () << " " << unsigned (c) << " " << unsigned (oldCw)
          << " " << unsigned (newCw) << std::endl;
}

void
WifiOfdmaExample::NotifyBackoffChange (uint32_t backoff)
{
  NS_LOG_FUNCTION ("Call to NotifyBackoffChange" << unsigned(backoff));

  std::string filename = "BackoffLog.txt";
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::app);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }

  uint32_t c = Simulator::GetContext();

  outFile << Simulator::Now ().GetSeconds () << " " << unsigned (c) << " " << unsigned (backoff)
          << std::endl;
}

void
WifiOfdmaExample::NotifyStateChange (std::string context, Time oldt, Time duration,
                                     WifiPhyState newState)
{
  NS_LOG_FUNCTION ("Call to NotifyStateChange" << context << oldt << duration << newState);

  std::string filename = "WifiPhyStateLog.txt";
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::app);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }

  uint32_t c = ContextToNodeId (context);

  outFile << Simulator::Now ().GetSeconds () << " " << c << " " << oldt.GetMicroSeconds () << " "
          << duration.GetMicroSeconds () << " " << newState << std::endl;
}

void
WifiOfdmaExample::Setup (void)
{
  NS_LOG_FUNCTION (this);
//   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Setup function called "<< '\n';

  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold",
                      m_enableRts ? StringValue ("0") : StringValue ("999999"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::MaxSsrc", UintegerValue (999999));
  Config::SetDefault ("ns3::WifiRemoteStationManager::MaxSlrc", UintegerValue (999999));
  Config::SetDefault ("ns3::HeConfiguration::GuardInterval", TimeValue (NanoSeconds (m_guardInterval)));
  Config::SetDefault ("ns3::WifiPhy::PowerDensityLimit", DoubleValue (m_psdLimitation));
  Config::SetDefault ("ns3::ArpCache::AliveTimeout", TimeValue (Seconds (3600 * 24))); // ARP cache entries expire after one day
  Config::SetDefault ("ns3::WifiMacQueue::MaxSize", QueueSizeValue (QueueSize (PACKETS, m_macQueueSize)));
  // Config::SetDefault ("ns3::HeConfiguration::MpduBufferSize", UintegerValue (m_baBufferSize));
  Config::SetDefault ("ns3::QosTxop::UseExplicitBarAfterMissedBlockAck", BooleanValue (m_useExplicitBar));
  // Config::SetDefault ("ns3::HeConfiguration::MuBeAifsn", UintegerValue (m_muBeAifsn));
  // Config::SetDefault ("ns3::HeConfiguration::MuBkAifsn", UintegerValue (m_muBkAifsn));
  // Config::SetDefault ("ns3::HeConfiguration::MuViAifsn", UintegerValue (m_muViAifsn));
  // Config::SetDefault ("ns3::HeConfiguration::MuVoAifsn", UintegerValue (m_muVoAifsn));
  // Config::SetDefault ("ns3::HeConfiguration::MuBeCwMin", UintegerValue (m_muBeCwMin));
  // Config::SetDefault ("ns3::HeConfiguration::MuBkCwMin", UintegerValue (m_muBkCwMin));
  // Config::SetDefault ("ns3::HeConfiguration::MuViCwMin", UintegerValue (m_muViCwMin));
  // Config::SetDefault ("ns3::HeConfiguration::MuVoCwMin", UintegerValue (m_muVoCwMin));
  // Config::SetDefault ("ns3::HeConfiguration::MuBeCwMax", UintegerValue (m_muBeCwMax));
  // Config::SetDefault ("ns3::HeConfiguration::MuBkCwMax", UintegerValue (m_muBkCwMax));
  // Config::SetDefault ("ns3::HeConfiguration::MuViCwMax", UintegerValue (m_muViCwMax));
  // Config::SetDefault ("ns3::HeConfiguration::MuVoCwMax", UintegerValue (m_muVoCwMax));
  // Config::SetDefault ("ns3::HeConfiguration::BeMuEdcaTimer", TimeValue (MicroSeconds (m_beMuEdcaTimer)));
  // Config::SetDefault ("ns3::HeConfiguration::BkMuEdcaTimer", TimeValue (MicroSeconds (m_bkMuEdcaTimer)));
  // Config::SetDefault ("ns3::HeConfiguration::ViMuEdcaTimer", TimeValue (MicroSeconds (m_viMuEdcaTimer)));
  // Config::SetDefault ("ns3::HeConfiguration::VoMuEdcaTimer", TimeValue (MicroSeconds (m_voMuEdcaTimer)));

  // Try to minimize the chances of collision among flows
  Config::SetDefault ("ns3::FqCoDelQueueDisc::Perturbation", UintegerValue (9973));
  Config::SetDefault ("ns3::FqCoDelQueueDisc::EnableSetAssociativeHash", BooleanValue (true));

  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
  // Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpLinuxReno"));
  // Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpCubic"));

 

  if (m_tcpSegmentSize != 0)
    {
      Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (m_tcpSegmentSize));
    }
  if (m_tcpInitialCwnd != 0)
    {
      Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (m_tcpInitialCwnd));
      // Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    }
  if (m_tcpMinRto != 0)
    {
      Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (m_tcpMinRto)));
    }

  Config::SetDefault("ns3::TcpL4Protocol::RecoveryType", TypeIdValue(TypeId::LookupByName("ns3::TcpPrrRecovery")));
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    // Set default initial congestion window as 10 segments
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    // Set default delayed ack count to a specified value
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));

    // Set default segment size of TCP packet to a specified value
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(524));

    // Enable/Disable SACK in TCP
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));

  Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
  Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel> ();
  spectrumChannel->AddPropagationLossModel (lossModel);
  Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel> ();
  spectrumChannel->SetPropagationDelayModel (delayModel);

  for (uint16_t bss = 0; bss < m_nObss + 1; bss++)
    {
      NodeContainer apNode, heStaNodes, vhtStaNodes, obssStaNodes;

      apNode.Create (1);
      m_apNodes.Add (apNode); // AP of the BSS under test is Node 0

      if (bss == 0)
        {
          // BSS under test
          heStaNodes.Create (m_nHeStations);
          m_staNodes.Add (heStaNodes);
          vhtStaNodes.Create (m_nVhtStations);
          m_staNodes.Add (vhtStaNodes);
        }
      else
        {
          // Overlapping BSS
          obssStaNodes.Create (m_nStationsPerObss);
          m_obssStaNodes.push_back (obssStaNodes);
        }

      SpectrumWifiPhyHelper phy;
      phy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
      phy.SetErrorRateModel ("ns3::NistErrorRateModel");
      phy.SetChannel (spectrumChannel);
      std::string channelStr("{0, " + std::to_string(m_channelWidth) + ", BAND_6GHZ, 0}");
      // std::string channelStr("{0, " + std::to_string(m_channelWidth) + ", BAND_5GHZ, 0}");
      // channelStr += "BAND_6GHZ, 0}";

      // phy.set("channelWidth", )
      
      phy.Set("ChannelSettings", StringValue(channelStr));
      if (m_enableThresholdPreambleDetection)
        {
          phy.SetPreambleDetectionModel ("ns3::ThresholdPreambleDetectionModel");
        }
      phy.Set ("TxGain", DoubleValue (m_txGain));
      phy.Set ("RxGain", DoubleValue (m_rxGain));
      phy.Set ("RxSensitivity", DoubleValue (m_rxSensitivity));

      WifiHelper wifi;
      // wifi.EnableLogContention(); 
      // To enable contention log
      if (bss == 0 && m_verbose)
        {
          wifi.EnableLogComponents ();
        }
      if (bss > 0 || m_heRate == "ideal")
        {
          wifi.SetRemoteStationManager ("ns3::IdealWifiManager");
        }
      else
        {
          NS_ASSERT (m_nVhtStations == 0);
          uint8_t mcs = std::stoul (m_heRate);
          std::string ctrlRate =
              mcs == 0 ? "OfdmRate6Mbps" : (mcs < 3 ? "OfdmRate12Mbps" : "OfdmRate24Mbps");
          wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode",
                                        StringValue ("HeMcs" + m_heRate), "ControlMode",
                                        StringValue (ctrlRate));
        }

      if (m_dlAckSeqType == "ACK-SU-FORMAT")
        {
          Config::SetDefault ("ns3::WifiDefaultAckManager::DlMuAckSequenceType",
                              EnumValue (WifiAcknowledgment::DL_MU_BAR_BA_SEQUENCE));
        }
      else if (m_dlAckSeqType == "MU-BAR")
        {
          Config::SetDefault ("ns3::WifiDefaultAckManager::DlMuAckSequenceType",
                              EnumValue (WifiAcknowledgment::DL_MU_TF_MU_BAR));
        }
      else if (m_dlAckSeqType == "AGGR-MU-BAR")
        {
          Config::SetDefault ("ns3::WifiDefaultAckManager::DlMuAckSequenceType",
                              EnumValue (WifiAcknowledgment::DL_MU_AGGREGATE_TF));
        }
      else if (m_dlAckSeqType != "NO-OFDMA")
        {
          NS_ABORT_MSG ("Invalid DL ack sequence type (must be NO-OFDMA, ACK-SU-FORMAT, MU-BAR or AGGR-MU-BAR)");
        }

      // Select the appropriate OfdmaManager based on the command line argument
      WifiMacHelper mac;
      // New configuration
        if (bss == 0 && m_enableDlOfdma)    // OFDMA only allowed for BSS under test
        {
          std::string m_dlschedulerLogic, m_ulschedulerLogic;
          if (m_dlscheduler == "rr")
            {
              m_dlschedulerLogic = "Standard";
            }
          else if (m_dlscheduler == "bellalta")
            {
              m_dlschedulerLogic = "Bellalta";
            }
          if (m_ulscheduler == "rr")
            {
              m_ulschedulerLogic = "Standard";
            }
          else if (m_ulscheduler == "bellalta")
            {
              m_ulschedulerLogic = "Bellalta";
            }
          std::string bw = std::to_string(m_channelWidth);

          mac.SetMultiUserScheduler ("ns3::RrMultiUserScheduler",
                                     "NStations", UintegerValue (m_maxNRus),
                                     "ForceDlOfdma", BooleanValue (m_forceDlOfdma),
                                     "EnableUlOfdma", BooleanValue (m_enableUlOfdma),
                                     "EnableTxopSharing", BooleanValue (m_enableTxopSharing),
                                     "EnableBsrp", BooleanValue (m_enableBsrp),
                                     "UlPsduSize", UintegerValue (m_ulPsduSize),
                                     "UseCentral26TonesRus", BooleanValue (m_useCentral26TonesRus),
                                     "DLSchedulerLogic", StringValue (m_dlschedulerLogic),
                                     "ULSchedulerLogic", StringValue (m_ulschedulerLogic),
                                     "Width", StringValue(bw)
                                    //  "MCS_mode", StringValue ("HeMcs" + m_heRate)
                                    );                           
        }
      // if (m_scheduler == "rr" && bss == 0 && m_enableDlOfdma)
      //   {
      //     wifi.SetOfdmaManager (
      //         "ns3::RrOfdmaManager", "NStations", UintegerValue (m_maxNRus), "ForceDlOfdma",
      //         BooleanValue (m_forceDlOfdma), "EnableUlOfdma", BooleanValue (m_enableUlOfdma),
      //         "EnableTxopSharing", BooleanValue (m_enableTxopSharing), "EnableBsrp",
      //         BooleanValue (m_enableBsrp), "UlPsduSize", UintegerValue (m_ulPsduSize),
      //         "SchedulerLogic", StringValue("Standard"));
      //   }
      // else if (m_scheduler == "bellalta" && bss == 0 && m_enableDlOfdma)
      //   {
      //     wifi.SetOfdmaManager (
      //         "ns3::RrOfdmaManager", "NStations", UintegerValue (m_maxNRus), "ForceDlOfdma",
      //         BooleanValue (m_forceDlOfdma), "EnableUlOfdma", BooleanValue (m_enableUlOfdma),
      //         "EnableTxopSharing", BooleanValue (m_enableTxopSharing), "EnableBsrp",
      //         BooleanValue (m_enableBsrp), "UlPsduSize", UintegerValue (m_ulPsduSize),
      //         "SchedulerLogic", StringValue("Bellalta"));
      //   }
      // else if (bss == 0 && m_enableDlOfdma)
      //   {
      //     NS_ABORT_MSG ("Unrecognized scheduler: " << m_scheduler);
      //   }

      if (m_enableObssPd)
        {
          wifi.SetObssPdAlgorithm ("ns3::ConstantObssPdAlgorithm", "ObssPdLevelMin",
                                   DoubleValue (m_obssPdThresholdMinBss), "ObssPdLevelMax",
                                   DoubleValue (m_obssPdThresholdMaxBss), "ObssPdLevel",
                                   DoubleValue (m_obssPdThresholdBss));
        }

      mac.SetType ("ns3::StaWifiMac",
                  "Ssid", SsidValue (Ssid ("non-existing-ssid")));  // prevent stations from automatically associating
      phy.Set ("TxPowerStart", DoubleValue (m_powerSta));
      phy.Set ("TxPowerEnd", DoubleValue (m_powerSta));
      phy.Set ("CcaEdThreshold", DoubleValue (m_ccaThresholdSta));

      NetDeviceContainer staDevices, apDevice;

      if (bss == 0)
        {
          wifi.SetStandard (WIFI_STANDARD_80211ax);
          staDevices.Add (wifi.Install (phy, mac, heStaNodes));

          wifi.SetStandard (WIFI_STANDARD_80211ac);
          staDevices.Add (wifi.Install (phy, mac, vhtStaNodes));

          m_staDevices.Add (staDevices);
        }
      else
        {
          wifi.SetStandard (m_obssStandard);
          staDevices = wifi.Install (phy, mac, obssStaNodes);
          m_obssStaDevices.push_back (staDevices);
        }

      wifi.SetStandard (bss == 0 ? WIFI_STANDARD_80211ax : m_obssStandard);
      mac.SetType ("ns3::ApWifiMac",
                  "Ssid", SsidValue (Ssid (m_ssidPrefix + std::to_string (bss))));
      phy.Set ("TxPowerStart", DoubleValue (m_powerAp));
      phy.Set ("TxPowerEnd", DoubleValue (m_powerAp));
      phy.Set ("CcaEdThreshold", DoubleValue (m_ccaThresholdAp));

      apDevice = wifi.Install (phy, mac, apNode);
      m_apDevices.Add (apDevice);

      // The below statements may be simplified in a future HeConfigurationHelper
      if ((m_enableObssPd))
        {
          Ptr<HeConfiguration> heConfiguration =
              DynamicCast<WifiNetDevice> (apDevice.Get (0))->GetHeConfiguration ();
          heConfiguration->SetAttribute ("BssColor", UintegerValue (bss + 1));
        }
//  PcapHelper pcapHelper;
  // pcapHelper.
      if (m_enablePcap)
        {
          // pcapHelper.EnablePcapAll ("STA_mac_pcap", staDevices);
          // pcapHelper.EnablePcapAll ("AP_mac_pcap", apDevice);
          //  pcapHelper.EnablePcap("abcd");
      
          // phy.EnablePcap ("STA_phy_pcap", staDevices);
          phy.EnablePcap ("1bsrp_nodl_AP_phy_pcap", apDevice);
        }
    }

  int64_t streamNumber = 100;

  // Assign fixed streams to random variables in use
  WifiHelper wifi;
  streamNumber += wifi.AssignStreams (m_apDevices, streamNumber);
  streamNumber += wifi.AssignStreams (m_staDevices, streamNumber);
  for (const auto& devices : m_obssStaDevices)
    {
      streamNumber += wifi.AssignStreams (devices, streamNumber);
    }

  // Configure max A-MSDU size and max A-MPDU size on the AP of the BSS under test
  Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
  dev->GetMac ()->SetAttribute ("BE_MaxAmsduSize", UintegerValue (m_beMaxAmsduSize));
  dev->GetMac ()->SetAttribute ("BK_MaxAmsduSize", UintegerValue (m_bkMaxAmsduSize));
  dev->GetMac ()->SetAttribute ("VI_MaxAmsduSize", UintegerValue (m_viMaxAmsduSize));
  dev->GetMac ()->SetAttribute ("VO_MaxAmsduSize", UintegerValue (m_voMaxAmsduSize));
  dev->GetMac ()->SetAttribute ("BE_MaxAmpduSize", UintegerValue (m_beMaxAmpduSize));
  dev->GetMac ()->SetAttribute ("BK_MaxAmpduSize", UintegerValue (m_bkMaxAmpduSize));
  dev->GetMac ()->SetAttribute ("VI_MaxAmpduSize", UintegerValue (m_viMaxAmpduSize));
  dev->GetMac ()->SetAttribute ("VO_MaxAmpduSize", UintegerValue (m_voMaxAmpduSize));
  // XXX Set large beacon generation interval at the AP
  dev->GetMac ()->SetAttribute ("BeaconInterval", TimeValue (NanoSeconds (102400000 * 20)));
  m_band = dev->GetPhy ()->GetPhyBand ();
  std::cout << "band: "<<m_band << "\n";
  // Configure TXOP Limit and MSDU lifetime on the AP
  PointerValue ptr;
  dev->GetMac ()->GetAttribute ("BE_Txop", ptr);
  ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_beTxopLimit));
  ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
  // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
  // ptr.Get<QosTxop> ()->SetMinCw (m_BeCwMin);
  dev->GetMac ()->GetAttribute ("BK_Txop", ptr);
  ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_bkTxopLimit));
  ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_bkMsduLifetime));
  // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_bkMsduLifetime));
  // ptr.Get<QosTxop> ()->SetMinCw (50);
  dev->GetMac ()->GetAttribute ("VI_Txop", ptr);
  ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_viTxopLimit));
  ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_viMsduLifetime));
  // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_bkMsduLifetime));
  // ptr.Get<QosTxop> ()->SetMinCw (5);
  dev->GetMac ()->GetAttribute ("VO_Txop", ptr);
  ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_voTxopLimit));
  ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_voMsduLifetime));
  // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_bkMsduLifetime));

      // Configure max A-MSDU size, max A-MPDU size, TXOP Limit and MSDU lifetime on the stations
      // of the BSS under test
      for (uint32_t i = 0; i < m_staNodes.GetN (); i++)
  {
    dev = DynamicCast<WifiNetDevice> (m_staDevices.Get (i));
    dev->GetMac ()->SetAttribute ("BE_MaxAmsduSize", UintegerValue (m_beMaxAmsduSize));
    dev->GetMac ()->SetAttribute ("BK_MaxAmsduSize", UintegerValue (m_bkMaxAmsduSize));
    dev->GetMac ()->SetAttribute ("VI_MaxAmsduSize", UintegerValue (m_viMaxAmsduSize));
    dev->GetMac ()->SetAttribute ("VO_MaxAmsduSize", UintegerValue (m_voMaxAmsduSize));
    dev->GetMac ()->SetAttribute ("BE_MaxAmpduSize", UintegerValue (m_beMaxAmpduSize));
    dev->GetMac ()->SetAttribute ("BK_MaxAmpduSize", UintegerValue (m_bkMaxAmpduSize));
    dev->GetMac ()->SetAttribute ("VI_MaxAmpduSize", UintegerValue (m_viMaxAmpduSize));
    dev->GetMac ()->SetAttribute ("VO_MaxAmpduSize", UintegerValue (m_voMaxAmpduSize));
    dev->GetMac ()->GetAttribute ("BE_Txop", ptr);
    ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_beTxopLimit));
    ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
    // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
    ptr.Get<QosTxop> ()->SetMinCw (m_BeCwMin);
    dev->GetMac ()->GetAttribute ("BK_Txop", ptr);
    ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_bkTxopLimit));
    ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_bkMsduLifetime));
    // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
    dev->GetMac ()->GetAttribute ("VI_Txop", ptr);
    ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_viTxopLimit));
    ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_viMsduLifetime));
    // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
    dev->GetMac ()->GetAttribute ("VO_Txop", ptr);
    ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_voTxopLimit));
    ptr.Get<QosTxop> ()->GetWifiMacQueue ()->SetMaxDelay (MilliSeconds (m_voMsduLifetime));
    // ptr.Get<QosTxop> ()->GetBaManager ()->GetRetransmitQueue ()->SetMaxDelay (MilliSeconds (m_beMsduLifetime));
    }

  // Configure max A-MPDU size and TXOP Limit for APs and stations of the overlapping BSSes
  for (uint16_t bss = 0; bss < m_nObss; bss++)
    {
      dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (bss + 1));
      dev->GetMac ()->SetAttribute ("BE_MaxAmpduSize", UintegerValue (m_obssMaxAmpduSize));
      dev->GetMac ()->GetAttribute ("BE_Txop", ptr);
      ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_obssTxopLimit));

      for (std::size_t i = 0; i < m_nStationsPerObss; i++)
        {
          dev = DynamicCast<WifiNetDevice> (m_obssStaDevices.at (bss).Get (i));
          dev->GetMac ()->SetAttribute ("BE_MaxAmpduSize", UintegerValue (m_obssMaxAmpduSize));
          dev->GetMac ()->GetAttribute ("BE_Txop", ptr);
          ptr.Get<QosTxop> ()->SetTxopLimit (MicroSeconds (m_obssTxopLimit));
        }
    }

  // Setting mobility model
//   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Setting mobility model"<< '\n';
  for (uint16_t bss = 0; bss < m_nObss + 1; bss++)
    {
      MobilityHelper mobility;
      mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

      Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
      Vector apPos (0.0, 0.0, 0.0); // initialized to the position of the AP of the BSS under test

      if (bss > 0)
        {
          // position of the AP of an overlapping BSS
          apPos = Vector (
              m_apDistances.at (bss - 1) * std::cos (m_apThetas.at (bss - 1) * M_PI / 180),
              m_apDistances.at (bss - 1) * std::sin (m_apThetas.at (bss - 1) * M_PI / 180), 0.0);
        }
      positionAlloc->Add (apPos);
      mobility.SetPositionAllocator (positionAlloc);
      mobility.Install (m_apNodes.Get (bss));

      Ptr<UniformDiscPositionAllocator> staPositionAlloc =
        CreateObjectWithAttributes<UniformDiscPositionAllocator> ("rho", DoubleValue (m_radius),
                                                                  "X", DoubleValue (apPos.x),
                                                                  "Y", DoubleValue (apPos.y));
      streamNumber += staPositionAlloc->AssignStreams (streamNumber);
      mobility.SetPositionAllocator (staPositionAlloc);

      if (bss == 0)
        {
          mobility.Install (m_staNodes);
        }
      else
        {
          mobility.Install (m_obssStaNodes.at (bss - 1));
        }
    }

  /* Internet stack */
  InternetStackHelper stack;
  stack.Install (m_apNodes);
  stack.Install (m_staNodes);
  for (auto &nodes : m_obssStaNodes)
    {
      stack.Install (nodes);
    }

  Ipv4AddressHelper address;
  address.SetBase ("10.1.0.0", "255.255.0.0");
  m_staInterfaces = address.Assign (m_staDevices);
  m_apInterfaces = address.Assign (m_apDevices.Get (0));

  for (uint16_t bss = 0; bss < m_nObss; bss++)
    {
      address.SetBase (std::string ("10." + std::to_string (bss + 2) + ".0.0").c_str (),
                       "255.255.0.0");
      m_apInterfaces.Add (address.Assign (m_apDevices.Get (bss + 1)));
      m_obssStaInterfaces.push_back (address.Assign (m_obssStaDevices.at (bss)));
    }

  /* Traffic Control layer */
  TrafficControlHelper tch;
  if (m_queueDisc.compare ("default") != 0)
    {
      // Uninstall the root queue disc on all the devices
      tch.Uninstall (m_apDevices);
      tch.Uninstall (m_staDevices);
      for (auto &devices : m_obssStaDevices)
        {
          tch.Uninstall (devices);
        }
    }

  m_clientApps.resize (m_flows.size ());
  m_clientApps_onTime_Info.resize(m_flows.size()); //??

  m_obssClientApps.resize (m_nObss);

  /* Install applications (receiver side) */
  for (auto &flow : m_flows)
    {
      std::string socketType =
          (flow.m_l4Proto == Flow::TCP ? "ns3::TcpSocketFactory" : "ns3::UdpSocketFactory");
      PacketSinkHelper packetSinkHelper (
          socketType, InetSocketAddress (Ipv4Address::GetAny (), flow.m_dstPort));
      if (flow.m_direction == Flow::DOWNLINK)
        {
          m_sinkApps.Add (packetSinkHelper.Install (m_staNodes.Get (flow.m_stationId - 1)));
        }
      else
        {
          m_sinkApps.Add (packetSinkHelper.Install (m_apNodes.Get (0)));
        }
    }

  m_sinkApps.Stop (
      Seconds (m_warmup + m_simulationTime + extrastoptime)); // let the servers be active for a long time

  m_obssSinkApps.resize (m_nObss);

  if (m_obssDlAggregateRate > 0)
    {
      // install receivers on stations of overlapping BSSes
      PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory",
                                         InetSocketAddress (Ipv4Address::GetAny (), 7000));
      for (uint16_t bss = 0; bss < m_nObss; bss++)
        {
          m_obssSinkApps.at (bss).Add (packetSinkHelper.Install (m_obssStaNodes.at (bss)));
        }
    }

  if (m_obssUlAggregateRate > 0)
    {
      // install receivers on the APs of overlapping BSSes
      PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory",
                                         InetSocketAddress (Ipv4Address::GetAny (), 7000));
      for (uint16_t bss = 0; bss < m_nObss; bss++)
        {
          m_obssSinkApps.at (bss).Add (packetSinkHelper.Install (m_apNodes.Get (bss + 1)));
        }
    }

  for (auto &obssSinkApps : m_obssSinkApps)
    {
      obssSinkApps.Stop (
          Seconds (m_warmup + m_simulationTime + extrastoptime)); // let the servers be active for a long time
    }

  /* Init statistics */
  m_obssDlRxStart.assign (m_nObss, 0.0);
  m_obssUlRxStart.assign (m_nObss, 0.0);
  m_obssDlRxStop.assign (m_nObss, 0.0);
  m_obssUlRxStop.assign (m_nObss, 0.0);

  // per-AC and pairwise per-AC statistics
  m_perAcStats.resize (m_nStations + 1);
  m_dlPerStaAcStats.resize (m_nStations);
  m_ulPerStaAcStats.resize (m_nStations);
  m_nSolicitingBasicTriggerFrames.assign (m_nStations, 0);

  for (auto &flow : m_flows)
    {
      if (flow.m_direction == Flow::DOWNLINK || flow.m_l4Proto == Flow::TCP)
        {
          if (flow.m_direction == Flow::DOWNLINK)
            {
              m_dlPerStaAcStats[flow.m_stationId - 1][flow.m_ac] = PairwisePerAcStats ();
              m_perAcStats[0][flow.m_ac] = PerAcStats ();
            }
          else
            {
              // TCP uplink flow -> BE downlink flow comprising TCP acks
              m_dlPerStaAcStats[flow.m_stationId - 1][AC_BE] = PairwisePerAcStats ();
              m_perAcStats[0][AC_BE] = PerAcStats ();
            }
        }
      if (flow.m_direction == Flow::UPLINK || flow.m_l4Proto == Flow::TCP)
        {
          if (flow.m_direction == Flow::UPLINK)
            {
              m_ulPerStaAcStats[flow.m_stationId - 1][flow.m_ac] = PairwisePerAcStats ();
              m_perAcStats[flow.m_stationId][flow.m_ac] = PerAcStats ();
            }
          else
            {
              // TCP downlink flow -> BE uplink flow comprising TCP acks
              m_ulPerStaAcStats[flow.m_stationId - 1][AC_BE] = PairwisePerAcStats ();
              m_perAcStats[flow.m_stationId][AC_BE] = PerAcStats ();
            }
        }
    }

  Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                                 MakeCallback (&WifiOfdmaExample::EstablishBaAgreement, this));

  // populate m_staMacAddressToNodeId map
  for (auto it = m_staDevices.Begin (); it != m_staDevices.End (); it++)
    {
      m_staMacAddressToNodeId[Mac48Address::ConvertFrom ((*it)->GetAddress ())] =
          (*it)->GetNode ()->GetId ();
    }
}

void
WifiOfdmaExample::Run (void)
{
  NS_LOG_FUNCTION (this);
//   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Calling Run function"<< '\n';

  // Start the setup phase by having the first station associate with the AP
  Simulator::ScheduleNow (&WifiOfdmaExample::StartAssociation, this);
  
  Simulator::Run ();
}

template <typename T, typename FUNC>
void
WifiOfdmaExample::PrintStatsWithTotal (std::vector<std::map<AcIndex, T>> v, FUNC select,
                                       std::ostream &os, std::string s, std::string s0)
{
  std::size_t i;
  auto ac = m_aciToString.begin ();
  decltype (select (std::declval<T> ())) sum{0}, totalSum{0}, ap{0};

  auto print = [&] (const std::map<AcIndex, T> &x, std::string str = "") {
    auto it = x.find (ac->first);
    if (it != x.end ())
      {
        os << (str.empty () ? s + std::to_string (i) : s0) << ": " << select (it->second) << " ";
        sum += select (it->second);
      }
    i++;
  };

  os << "------------------------------" << std::endl;
  for (; ac != m_aciToString.end (); ac++)
    {
      // check if at least one station exchanged traffic of this AC
      if (std::find_if (v.begin (), v.end (), [&ac] (const std::map<AcIndex, T> &map) {
            return map.find (ac->first) != map.end ();
          }) == v.end ())
        {
          continue;
        }

      os << "[" << ac->second << "] ";
      auto it = v.begin ();
      if (!s0.empty ())
        {
          i = 0;
          print (*it, "AP");
          it++;
          ap = sum;
          sum = 0;
        }
      i = 1;
      std::for_each (it, v.end (), print);
      os << std::endl;
      if (!s0.empty ())
        {
          os << "[" << ac->second << "] TotalDL: " << ap << std::endl
             << "[" << ac->second << "] TotalUL: " << sum << std::endl;
        }
      else
        {
          os << "[" << ac->second << "] Total: " << sum << std::endl;
        }
      totalSum += sum;
      totalSum += ap;
      sum = 0;
      ap = 0;
    }
  os << "TOTAL: " << totalSum << std::endl << std::endl;
}

template <typename T, typename FUNC>
void
WifiOfdmaExample::PrintStats (std::vector<std::map<AcIndex, T>> v, FUNC select, std::ostream &os,
                              std::string s, std::string s0)
{
  std::size_t i;
  auto ac = m_aciToString.begin ();

  auto print = [&] (const std::map<AcIndex, T> &x, std::string str = "") {
    auto it = x.find (ac->first);
    if (it != x.end ())
      {
        os << (str.empty () ? s + std::to_string (i) : s0) << ": " << select (it->second) << " ";
      }
    i++;
  };

  os << "------------------------------" << std::endl;
  for (; ac != m_aciToString.end (); ac++)
    {
      // check if at least one station exchanged traffic of this AC
      if (std::find_if (v.begin (), v.end (), [&ac] (const std::map<AcIndex, T> &map) {
            return map.find (ac->first) != map.end ();
          }) == v.end ())
        {
          continue;
        }

      os << "[" << ac->second << "] ";
      auto it = v.begin ();
      if (!s0.empty ())
        {
          i = 0;
          print (*it, "AP");
          it++;
        }
      i = 1;
      std::for_each (it, v.end (), print);
      os << std::endl;
    }
  os << std::endl;
}

void
WifiOfdmaExample:: PrintPacketLatencies (std::ostream &os){
   NS_LOG_FUNCTION (this);
//    std::cout << "At time "<<Simulator::Now().GetMicroSeconds () << '\n';
//   os << "PER-FLOW per packet statistics" << std::endl << "************************" << std::endl;
//   for (std::size_t i = 0; i < m_flows.size (); i++)
    // {
    //    os << "FLOW " << m_flows[i] << std::endl<< std::fixed << std::setprecision (3);
        //  std::pair<uint64_t /* UID */,double> pktdata;
    //   for(std::pair<uint64_t /* UID */,double> pkt:m_flows[i].appLatencies.m_samples){

        //   os<<"Packet ID: "<< pkt.first<<" App layer Latency: "<<pkt.second << std::endl;
    //   }
        //  << static_cast<double> (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket -
        //                          m_flows[i].m_rxPackets) /
        //         (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket)
        //  << std::endl
        //  << "Dropped packets (app layer): " << m_flows[i].m_packetsRejectedBySocket << std::endl
        //  << "Latency: " << m_flows[i].m_latency << std::endl
        //  << std::endl;
    // }
// os<<"-------------------------------Dropped Packets-----------------------------"<<std::endl ;
uint32_t drop_count_total_UL = 0;
uint32_t drop_count_total_DL = 0;
uint32_t tx_total = 0;
uint32_t rx_total = 0;

uint32_t drop_count_total_mac = 0;
uint32_t drop_count_total_app = 0;

uint64_t rx_bytes_app_dl = 0;
uint64_t rx_bytes_app_ul = 0;

uint64_t tx_bytes_app_dl = 0;
uint64_t tx_bytes_app_ul = 0;

uint64_t tx_bytes_mac = 0;
uint64_t rx_bytes_mac = 0;


    for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      tx_total += m_flows[i].m_txPackets;
      rx_total += m_flows[i].m_rxPackets;
      if(i % 2 == 0){
        drop_count_total_DL += (m_flows[i].m_txPackets-m_flows[i].m_rxPackets);
        rx_bytes_app_dl += m_flows[i].m_rxBytes;
        tx_bytes_app_dl += m_flows[i].m_txBytes;

      }else{
        drop_count_total_UL += (m_flows[i].m_txPackets-m_flows[i].m_rxPackets);
        rx_bytes_app_ul += m_flows[i].m_rxBytes;
        tx_bytes_app_ul += m_flows[i].m_txBytes;
      }
       os << "FLOW " << m_flows[i] << " Total dropped packets : "<< (m_flows[i].m_txPackets-m_flows[i].m_rxPackets) <<std::endl;
        //  std::pair<uint64_t /* UID */,double> pktdata;
         
    //   for(auto pkt:m_flows[i].packetsinFlow.m_samples){
        //   if(!pkt.second)
        //   {os << "FLOW " << m_flows[i] <<"   Dropped Packet ID: "<< pkt.first<<std::endl;}
    //   }
        //  << static_cast<double> (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket -
        //                          m_flows[i].m_rxPackets) /
        //         (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket)
        //  << std::endl
        //  << "Dropped packets (app layer): " << m_flows[i].m_packetsRejectedBySocket << std::endl
        //  << "Latency: " << m_flows[i].m_latency << std::endl
        //  << std::endl;

      // if(i % 2 == 0){
          

      //   }else{

      //   for(auto pkt:m_flows[0].packetsinFlow_mac.m_samples){
      //     if(!pkt.second)
      //     {drop_count_total_UL_mac++;}
      //     }
      // }

    }
    // for(auto pkt:m_flows[0].packetsinFlow_mac.m_samples){
    //       if(!pkt.second)
    //       {drop_count_total_mac++;}
          
    //     }
    
    rx_bytes_mac = m_flows[0].m_rxBytes_mac;
    tx_bytes_mac = m_flows[0].m_txBytes_mac;

    drop_count_total_app = drop_count_total_DL + drop_count_total_UL;
    drop_count_total_mac = m_flows[0].m_txPackets_mac - m_flows[0].m_rxPackets_mac;
          
    os << "Total dropped Packets at App(DL): " << drop_count_total_DL << '\n';
    os << "Total dropped Packets at App(UL): " << drop_count_total_UL << '\n';

    os << "Total dropped Packets at App(DL + UL): " << drop_count_total_app << '\n'; 
    os << "Total dropped Packets at MAC(DL + UL): " << drop_count_total_mac << '\n';

    os << "Total Transmitted Packets at MAC(DL + UL): " << m_flows[0].m_txPackets_mac << '\n';
    os << "Total Received Packets at MAC(DL + UL): " << m_flows[0].m_rxPackets_mac << '\n';

    os << "Total Transmitted Packets at App(DL + UL): " << tx_total << '\n';
    os << "Total Received Packets at App(DL + UL): " << rx_total << '\n';

    os << "Total Received bytes at App(DL): " << rx_bytes_app_dl << '\n';
    os << "Total Received bytes at App(UL): " << rx_bytes_app_ul << '\n';

    os << "Total Transmitted bytes at App(DL): " << tx_bytes_app_dl << '\n';
    os << "Total Transmitted bytes at App(UL): " << tx_bytes_app_ul << '\n';

    os << "Total Received bytes at MAC(DL + UL): " << rx_bytes_mac << '\n';
    os << "Total Transmitted bytes at MAC(DL + UL): " << tx_bytes_mac << '\n';


    // os << "Total dropped bytes at App(DL): " << tx_bytes_app_dl - rx_bytes_app_dl << '\n';
    // os << "Total dropped bytes at App(UL): " << tx_bytes_app_ul - rx_bytes_app_ul << '\n';

    // os << "Total dropped bytes at App(DL + UL): " << (tx_bytes_app_dl - rx_bytes_app_dl) + (tx_bytes_app_ul - rx_bytes_app_ul)<< '\n';
    os << "Total dropped bytes at MAC(DL + UL): " << tx_bytes_mac - rx_bytes_mac << '\n';
  for(auto thru:Throughputs){
os << "Throughput per 1000 packets " << thru << '\n';

    
  }

  // for (auto temp:tcptime){

  //   std::cout<< "Enqueue time : "<<temp.first<<" Packet count: "<< tcptime.count(temp.first)<<"\n";
  // }

  // for(auto it = tcptime.begin();)

  os << " Last Packet Received at (ms) " << prev_rx << '\n';
     
}

void
WifiOfdmaExample::PrintResults (std::ostream &os)
{

    if(graph_stats){
    double aggr_thr_dl = 0;
    double aggr_thr_ul = 0;
    double aggr_lat_dl = 0;
    double aggr_lat_ul = 0;
    uint32_t aggr_dl_pkt = 0;
    uint32_t aggr_ul_pkt = 0;
    uint16_t ul_flows = 0;
    uint16_t dl_flows = 0;
        
    for (std::size_t i = 0; i < m_flows.size (); i++)
    {
        double avglat = 0;
        size_t count = 0;
        double stdeviation = 0;
        count = m_flows[i].m_latency.m_samples.size();
        for (auto it = m_flows[i].m_latency.m_samples.begin (); it != m_flows[i].m_latency.m_samples.end(); ++it)
        {
          avglat += *it;
        }
        double latmean = avglat/double(count);
        for (auto it = m_flows[i].m_latency.m_samples.begin (); it != m_flows[i].m_latency.m_samples.end(); ++it)
        {
          stdeviation += ((*it-latmean)*(*it-latmean));
        }
        stdeviation = sqrt(stdeviation/double(count));
        

      os << "FLOW " << m_flows[i] << std::endl;
      if(m_flows[i].m_direction == Flow::DOWNLINK){

        aggr_thr_dl += (m_flows[i].m_rxPackets*m_flows[i].m_payloadSize * 8.) / (m_simulationTime * 1e6);
        if(m_flows[i].m_l4Proto == Flow::UDP){
          dl_flows++;
          aggr_lat_dl += avglat/double(count);
          }

        // aggr_dl_pkt += (m_flows[i].m_rxBytes)/(1610);
        aggr_dl_pkt += m_flows[i].m_rxPackets;
      }else{
        aggr_thr_ul += (m_flows[i].m_rxPackets*m_flows[i].m_payloadSize * 8.) / (m_simulationTime * 1e6);
        if(m_flows[i].m_l4Proto == Flow::UDP){
          ul_flows++;
          aggr_lat_ul += avglat/double(count);
        }
        // aggr_ul_pkt += (m_flows[i].m_rxBytes)/(1610);
        aggr_ul_pkt += m_flows[i].m_rxPackets;
      }
      uint64_t rcv;
      if(m_flows[i].m_l4Proto == Flow::TCP){
        // rcv = DynamicCast<PacketSink> (m_sinkApps.Get (i))->GetTotalRx ();
        // rcv = m_flows[i].m_rxBytes;
        rcv = m_flows[i].m_rxPackets*m_flows[i].m_payloadSize;
      }
      else{
        rcv = m_flows[i].m_rxPackets*m_flows[i].m_payloadSize;
      }
      
      os << std::fixed << std::setprecision (3)
         << "Throughput: " << (rcv * 8.) / (m_simulationTime * 1e6)
         << " Latency: " << avglat/double(count) << " Count :" << count
         << " Latency std: " << stdeviation << "\n";
        
        //  << "Latency: " << m_flows[i].m_latency << std::endl
        //  << std::endl;
         
    }

    os << std::fixed << std::setprecision (3)
         << "Aggregate DL Throughput: " << aggr_thr_dl << std::endl
         << "Aggregate UL Throughput: " << aggr_thr_ul << std::endl
         << "Average DL Latency: " << aggr_lat_dl/dl_flows << std::endl
         << "Average UL Latency: " << aggr_lat_ul/ul_flows << std::endl
         << "Total received DL Packets: " << aggr_dl_pkt << std::endl
         << "Total received UL Packets: " << aggr_ul_pkt << std::endl;

    }
    else{

  NS_LOG_FUNCTION (this);
////////////////////////////////////////
    double aggr_thr_dl = 0;
    double aggr_thr_ul = 0;
    double aggr_lat_dl = 0;
    double aggr_lat_ul = 0;
    uint32_t aggr_dl_pkt = 0;
    uint32_t aggr_ul_pkt = 0;
    int ul_size = 0;
    int dl_size = 0;
    double stdeviationul_aggr = 0;
    double stdeviationdl_aggr = 0;
        
        for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      if(m_flows[i].m_direction == Flow::DOWNLINK && m_flows[i].m_l4Proto == Flow::UDP) dl_size++;
      if(m_flows[i].m_direction == Flow::UPLINK && m_flows[i].m_l4Proto == Flow::UDP) ul_size++;

        
        double avglat = 0;
        double stdeviation = 0;
        double count = m_flows[i].m_latency.m_samples.size();
        for (auto it = m_flows[i].m_latency.m_samples.begin (); it != m_flows[i].m_latency.m_samples.end(); ++it)
        {
          avglat += *it;
        }
        double latmean = avglat/double(count);
        for (auto it = m_flows[i].m_latency.m_samples.begin (); it != m_flows[i].m_latency.m_samples.end(); ++it)
        {
          stdeviation += ((*it-latmean)*(*it-latmean));
        }
        stdeviation = sqrt(stdeviation/double(count));
      

      os << "FLOW " << m_flows[i] << std::endl;
      if(m_flows[i].m_direction == Flow::DOWNLINK){
        aggr_thr_dl += (m_flows[i].m_rxPackets*m_flows[i].m_payloadSize * 8.) / (m_simulationTime * 1e6);
        aggr_lat_dl += avglat/double(count);
        // aggr_dl_pkt += (m_flows[i].m_rxBytes)/(1610);
        aggr_dl_pkt += m_flows[i].m_rxPackets;
      }else{
        aggr_thr_ul += (m_flows[i].m_rxPackets*m_flows[i].m_payloadSize * 8.) / (m_simulationTime * 1e6);
        aggr_lat_ul += avglat/double(count);
        // aggr_ul_pkt += (m_flows[i].m_rxBytes)/(1610);
        aggr_ul_pkt += m_flows[i].m_rxPackets;
      }
      
      
      os << std::fixed << std::setprecision (3)
         << "Throughput: " << (m_flows[i].m_rxPackets*m_flows[i].m_payloadSize * 8.) / (m_simulationTime * 1e6)
         << " Latency: " << avglat/double(count) << " Count :" << count 
         << " Latency std: " << stdeviation << "\n";
        
        //  << "Latency: " << m_flows[i].m_latency << std::endl
        //  << std::endl;
        if(m_flows[i].m_direction == Flow::DOWNLINK){
          stdeviationdl_aggr += stdeviation;
        } else{
          stdeviationul_aggr += stdeviation;
        }        
    }

    os << std::fixed << std::setprecision (3)
         << "Aggregate DL Throughput: " << aggr_thr_dl << std::endl
         << "Aggregate UL Throughput: " << aggr_thr_ul << std::endl
         << "Average DL Latency: " << aggr_lat_dl/dl_size << std::endl
         << "Average DL Latency std: " << stdeviationdl_aggr/dl_size << std::endl
         << "Average UL Latency: " << aggr_lat_ul/ul_size << std::endl
         << "Average UL Latency std: " << stdeviationul_aggr/ul_size << std::endl
         << "Total received DL Packets: " << aggr_dl_pkt << std::endl
         << "Total received UL Packets: " << aggr_ul_pkt << std::endl;

// ///////////////////////////////////////



//   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () << '\n';
  os << "PER-FLOW statistics" << std::endl << "************************" << std::endl;
  for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      os << "FLOW " << m_flows[i] << std::endl
         << std::fixed << std::setprecision (3)
         << "Throughput: " << (m_flows[i].m_rxPackets*m_flows[i].m_payloadSize * 8.) / (m_simulationTime * 1e6) << std::endl
         << "Expected data rate: " << m_flows[i].m_dataRate / 1e6 << std::endl
         << "Actual data rate: " << (m_flows[i].m_txBytes * 8.) / (m_simulationTime * 1e6)
         << std::endl
         << "Packet loss rate: "
         << static_cast<double> (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket -
                                 m_flows[i].m_rxPackets) /
                (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket)
         << std::endl
         << "Dropped packets (app layer): " << m_flows[i].m_packetsRejectedBySocket << std::endl
         << "Latency: " << m_flows[i].m_latency << std::endl
         << "Difference of tx and rx bytes: "<< m_flows[i].m_txBytes - m_flows[i].m_rxBytes << std::endl
         << std::endl;         
    }

  for (uint16_t bss = 0; bss < m_nObss; bss++)
    {
      os << "OBSS " << bss + 1 << std::endl;
      if (m_obssDlAggregateRate > 0)
        {
          os << "DL Throughput: "
             << ((m_obssDlRxStop[bss] - m_obssDlRxStart[bss]) * 8.) / (m_simulationTime * 1e6)
             << std::endl;
        }
      if (m_obssUlAggregateRate > 0)
        {
          os << "UL Throughput: "
             << ((m_obssUlRxStop[bss] - m_obssUlRxStart[bss]) * 8.) / (m_simulationTime * 1e6)
             << std::endl;
        }
      os << std::endl;
    }

  /* Per-AC statistics from per-flow statistics */
  std::vector<std::map<AcIndex, double>> dlTput (m_nStations), ulTput (m_nStations);
  std::vector<std::map<AcIndex, Stats<double>>> dlLatency (m_nStations), ulLatency (m_nStations);
  std::vector<std::map<AcIndex, uint64_t>> dlSentPkts (m_nStations), ulSentPkts (m_nStations),
      dlRecvPkts (m_nStations), ulRecvPkts (m_nStations);

  for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      std::vector<std::map<AcIndex, double>>::iterator tputVecIt;
      std::vector<std::map<AcIndex, Stats<double>>>::iterator latencyVecIt;
      std::vector<std::map<AcIndex, uint64_t>>::iterator sentVecIt, recvVecIt;

      if (m_flows[i].m_direction == Flow::DOWNLINK)
        {
          tputVecIt = std::next (dlTput.begin (), m_flows[i].m_stationId - 1);
          latencyVecIt = std::next (dlLatency.begin (), m_flows[i].m_stationId - 1);
          sentVecIt = std::next (dlSentPkts.begin (), m_flows[i].m_stationId - 1);
          recvVecIt = std::next (dlRecvPkts.begin (), m_flows[i].m_stationId - 1);
        }
      else
        {
          tputVecIt = std::next (ulTput.begin (), m_flows[i].m_stationId - 1);
          latencyVecIt = std::next (ulLatency.begin (), m_flows[i].m_stationId - 1);
          sentVecIt = std::next (ulSentPkts.begin (), m_flows[i].m_stationId - 1);
          recvVecIt = std::next (ulRecvPkts.begin (), m_flows[i].m_stationId - 1);
        }

      auto mapIt = tputVecIt->insert ({m_flows[i].m_ac, 0.0}).first;
      mapIt->second += (m_flows[i].m_rxBytes * 8.) / (m_simulationTime * 1e6);

      auto mapIt2 = latencyVecIt->insert ({m_flows[i].m_ac, 0}).first;
      mapIt2->second += m_flows[i].m_latency;

      auto mapIt3 = sentVecIt->insert ({m_flows[i].m_ac, 0.0}).first;
      mapIt3->second += (m_flows[i].m_txPackets + m_flows[i].m_packetsRejectedBySocket);

      mapIt3 = recvVecIt->insert ({m_flows[i].m_ac, 0.0}).first;
      mapIt3->second += m_flows[i].m_rxPackets;
    }

  os << "PAIRWISE PER-AC statistics" << std::endl << "************************" << std::endl;

  os << "Throughput (Mbps) [DL]" << std::endl;
  PrintStatsWithTotal (dlTput, [](const double& t) { return t; }, os, "To_STA_");
  os << "Throughput (Mbps) [UL]" << std::endl;
  PrintStatsWithTotal (ulTput, [](const double& t) { return t; }, os, "From_STA_");

  os << "E2e latency (ms) [DL]" << std::endl;
  PrintStatsWithTotal (dlLatency, [](const Stats<double>& t) { return t; }, os, "To_STA_");
  os << "E2e latency (ms) [UL]" << std::endl;
  PrintStatsWithTotal (ulLatency, [](const Stats<double>& t) { return t; }, os, "From_STA_");

  os << "Transmitted packets [DL]" << std::endl;
  PrintStatsWithTotal (dlSentPkts, [](const uint64_t& t) { return t; }, os, "To_STA_");
  os << "Transmitted packets [UL]" << std::endl;
  PrintStatsWithTotal (ulSentPkts, [](const uint64_t& t) { return t; }, os, "From_STA_");

  os << "Received packets [DL]" << std::endl;
  PrintStatsWithTotal (dlRecvPkts, [](const uint64_t& t) { return t; }, os, "To_STA_");
  os << "Received packets [UL]" << std::endl;
  PrintStatsWithTotal (ulRecvPkts, [](const uint64_t& t) { return t; }, os, "From_STA_");

  /* EXPIRED MSDUs */
  os << "MSDUs expired at the AP [DL]" << std::endl;
  PrintStatsWithTotal (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.expired; },
                       os, "To_STA_");
  os << "MSDUs expired at the STAs [UL]" << std::endl;
  PrintStatsWithTotal (m_ulPerStaAcStats, [](const PairwisePerAcStats& s) { return s.expired; },
                       os, "From_STA_");
  
  /* REJECTED MSDUs */
  os << "MSDUs rejected at the AP [DL]" << std::endl;
  PrintStatsWithTotal (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.rejected; },
                       os, "To_STA_");
  os << "MSDUs rejected at the STAs [UL]" << std::endl;
  PrintStatsWithTotal (m_ulPerStaAcStats, [](const PairwisePerAcStats& s) { return s.rejected; },
                       os, "From_STA_");

  /* FAILED MPDUs */
  os << "TX failures at the AP [DL]" << std::endl;
  PrintStatsWithTotal (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.failed; },
                       os, "To_STA_");
  os << "TX failures at the STAs [UL]" << std::endl;
  PrintStatsWithTotal (m_ulPerStaAcStats, [](const PairwisePerAcStats& s) { return s.failed; },
                       os, "From_STA_");

  /* A-MPDU SIZE */
  os << "A-MPDU size (min/avg/max/count) [DL]" << std::endl;
  PrintStats (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.ampduSize; },
              os, "To_STA_");
  os << "A-MPDU size (min/avg/max/count) [UL]" << std::endl;
  PrintStats (m_ulPerStaAcStats, [](const PairwisePerAcStats& s) { return s.ampduSize; },
              os, "From_STA_");

  /* A-MPDU DURATION TO DL/UL MU PPDU DURATION RATIO */
  os << "A-MPDU duration to DL MU PPDU duration ratio (min/avg/max/count) [DL]" << std::endl;
  PrintStats (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.ampduRatio; },
              os, "To_STA_");
  os << "A-MPDU duration to Basic TF's UL Length ratio (min/avg/max/count) [UL]" << std::endl;
  PrintStats (m_ulPerStaAcStats, [](const PairwisePerAcStats& s) { return s.ampduRatio; },
              os, "From_STA_");

  /* L2 LATENCY */
  os << "L2 latency (percentiles[count]) [DL]" << std::endl;
  PrintStatsWithTotal (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.l2Latency; },
                       os, "To_STA_");
  os << "L2 latency (percentiles[count]) [UL]" << std::endl;
  PrintStatsWithTotal (m_ulPerStaAcStats, [](const PairwisePerAcStats& s) { return s.l2Latency; },
                       os, "From_STA_");

  /* HEAD-OF-LINE DELAY */
  os << "Pairwise Head-of-Line delay (percentiles[count]) at the AP [DL]" << std::endl;
  PrintStatsWithTotal (m_dlPerStaAcStats, [](const PairwisePerAcStats& s) { return s.pairwiseHol; },
                       os, "To_STA_");

  os << "PER-AC statistics" << std::endl << "************************" << std::endl;

  os << "Aggregate Head-of-Line delay (percentiles[count])" << std::endl;
  PrintStats (m_perAcStats, [](const PerAcStats& s) { return s.aggregateHoL; },
              os, "STA_", "AP");

  /* QUEUE DISC SOJOURN TIME AND PACKET DROPPED  */
  os << "Sojourn time in the queue disc associated with each AC" << std::endl;
  PrintStatsWithTotal (m_perAcStats, [](const PerAcStats& s) { return s.queueDiscSojournTime; },
                       os, "STA_", "AP");

  os << "Packets dropped by the queue disc associated with each AC" << std::endl;
  PrintStatsWithTotal (m_perAcStats, [](const PerAcStats& s) { return s.droppedByQueueDisc; },
                       os, "STA_", "AP");

  /* TXOP DURATION */
  os << "TXOP duration (percentiles[count])" << std::endl;
  PrintStats (m_perAcStats, [](const PerAcStats& s) { return s.txopDuration; },
              os, "STA_", "AP");

  os << "PER-STA statistics" << std::endl << "************************" << std::endl;

  os << "DL MU PPDU completeness (percentiles[count])" << std::endl
     << "------------------------------" << std::endl
     << m_dlMuCompleteness << std::endl
     << std::endl;

  os << "UL MU PPDU completeness (percentiles[count])" << std::endl
     << "------------------------------" << std::endl
     << m_ulMuCompleteness << std::endl
     << std::endl;

  os << "HE TB PPDU completeness (percentiles[count])" << std::endl
     << "------------------------------" << std::endl
     << m_heTbCompleteness << std::endl
     << std::endl;

  os << "DL MU PPDU duration (percentiles[count])" << std::endl
     << "------------------------------" << std::endl
     << m_dlMuPpduDuration << std::endl
     << std::endl;

  os << "HE TB PPDU duration (percentiles[count])" << std::endl
     << "------------------------------" << std::endl
     << m_heTbPpduDuration << std::endl
     << std::endl;

  os << "Basic Trigger Frames (failed/sent)" << std::endl
     << "------------------------------" << std::endl
     << "(" << m_nFailedBasicTriggerFrames << ", " << m_nBasicTriggerFramesSent << ")" << std::endl
     << std::endl;

  os << "BSRP Trigger Frames (failed/sent)" << std::endl
     << "------------------------------" << std::endl
     << "(" << m_nFailedBsrpTriggerFrames << ", " << m_nBsrpTriggerFramesSent << ")" << std::endl
     << std::endl;

  os << "Unresponded Basic TFs ratio" << std::endl << "------------------------------" << std::endl;
  for (uint16_t i = 0; i < m_nStations; i++)
    {
      uint64_t heTbPPduCount = 0; // for i-th STA
      for (auto &acStats : m_ulPerStaAcStats.at (i))
        {
          heTbPPduCount += acStats.second.ampduRatio.m_samples.size ();
        }
      os << "STA_" << i + 1 << ": "
         << static_cast<double> (m_nSolicitingBasicTriggerFrames.at (i) - heTbPPduCount) /
                m_nSolicitingBasicTriggerFrames.at (i)
         << " ";
    }
  os << std::endl << std::endl;
    }
}

void
WifiOfdmaExample::StartAssociation (void)
{
//   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Calling Start Association function"<< '\n';
  NS_LOG_FUNCTION (this << m_currentSta);
  NS_ASSERT (m_currentSta < m_nStations + m_nObss * m_nStationsPerObss);

  Ptr<WifiNetDevice> dev;
  uint16_t bss = 0;

  if (m_currentSta < m_nStations)
    {
      dev = DynamicCast<WifiNetDevice> (m_staDevices.Get (m_currentSta));
    }
  else
    {
      auto pair = StationIdToBssIndexPair (m_currentSta);
      bss = pair.first;
      dev = DynamicCast<WifiNetDevice> (m_obssStaDevices.at (bss - 1).Get (pair.second));
    }

  // NS_ASSERT (dev != 0);
  // this will lead the station to associate with the AP
  dev->GetMac ()->SetSsid (Ssid (m_ssidPrefix + std::to_string (bss)));
}

void
WifiOfdmaExample::EstablishBaAgreement (Mac48Address bssid)
{
  NS_LOG_FUNCTION (this << bssid << m_currentSta);

//   std::cout << "At time "<<Simulator::Now() <<" Calling EstablishBaAgreement function"<< '\n';

  // Now that the current station is associated with the AP, let's trigger the creation
  // of an entry in the ARP cache (of both the AP and the STA) and the establishment of
  // Block Ack agreements between the AP and the STA (and viceversa) for the relevant
  // TIDs. This is done by having the AP send 4 ICMP Echo Requests to the STA
  uint16_t pingInterval = 50; // milliseconds


//   std::cout << "At time "<<Simulator::Now().GetSeconds() << " Sending 4 ICMP echo requests to establish Block Ack Agreement" << '\n';

  std::map<AcIndex, uint8_t> acTos = {{AC_BE, 0x00 /* CS0 */},
                                      {AC_BK, 0x28 /* AF11 */},
                                      {AC_VI, 0xb8 /* EF */},
                                      {AC_VO, 0xc0 /* CS7 */}};
  Ipv4Address staAddress;
  Ipv4Address apAddress;
  Ptr<Node> apNode;
  uint16_t bss = 0;

  if (m_currentSta < m_nStations)
    {
      staAddress = m_staInterfaces.GetAddress (m_currentSta);
      apNode = m_apNodes.Get (0);
    }
  else
    {
      auto pair = StationIdToBssIndexPair (m_currentSta);
      bss = pair.first;
      staAddress = m_obssStaInterfaces.at (bss - 1).GetAddress (pair.second);
      apNode = m_apNodes.Get (bss);
    }

  apAddress = m_apInterfaces.GetAddress(0);

  PingHelper ping;
  std::cout<<"Adding ping\n";
  ping.SetAttribute("Destination",AddressValue(staAddress));
   
  ping.SetAttribute("InterfaceAddress",AddressValue(apAddress));
std::cout<<"Added ping addresses\n";
  // , apAddress);
  void (*addPingApp)(PingHelper, Ptr<Node>);  // function pointer
  addPingApp = [](PingHelper helper, Ptr<Node> node)
               {
                 ApplicationContainer apps = helper.Install (node);
                 apps.Stop (MilliSeconds (10)); // send just one Echo Request
               };

  if (m_verbose)
    {
      ping.SetAttribute ("Verbose", BooleanValue (true));
    }
  for (auto &ac : acTos)
    {
      // ping.SetAttribute ("Tos", UintegerValue (ac.second));
      Simulator::Schedule (MilliSeconds (pingInterval * static_cast<uint16_t> (ac.first)),
                           addPingApp, ping, apNode);
    //   std::cout<<"Call scheduled at  "<< MilliSeconds (pingInterval * static_cast<uint16_t> (ac.first))<< " "<<"\n";
    }

  // set the duration of the "On" interval to zero for all client applications so that,
  // once started, they will stay quiet (i.e., no packet is transmitted) and wake at the
  // end of every "Off" interval to check if the "On" period has become non-zero.
  // The start of all the client applications is aligned to an integer multiple of
  // pingInterval (also referred to as a "slot"),

  // convert pingInterval to time units
  int64_t slotUnits = pingInterval * std::pow (10, (Time::GetResolution () - Time::MS) * 3);
  // the start time is 4 pingIntervals from now and aligned to the next slot boundary
  int64_t startTime =
      ((Simulator::Now ().GetTimeStep () + 4 * slotUnits) / slotUnits + 1) * slotUnits;//changed to 2 from 1
  Time startDelay = TimeStep (startTime) - Simulator::Now ();

  std::cout<<"1 slot time : "<<slotUnits<< " Start delay : "<<startDelay<<"\n";

  NS_ASSERT (startDelay.IsStrictlyPositive ());

  std::size_t i = 0;
  for (auto &flow : m_flows)
    {
      // Install a client application for each flow involving the current station.
      // In case of TCP traffic, this will trigger the establishment of a TCP connection.
      std::string socketType =
              (flow.m_l4Proto == Flow::TCP ? "ns3::TcpSocketFactory" : "ns3::UdpSocketFactory");
          
      if (flow.m_stationId == m_currentSta + 1)
        {
        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () << " Installing client application for flow " << i << '\n'; 
          
          if(flow.m_l4Proto == Flow::UDP){
          OnOffHelper client (socketType, Ipv4Address::GetAny ());

// ////////////////////////////////
          // double mean = 2;
          // double variance = 0.1;
          // Ptr<NormalRandomVariable> x = CreateObject<NormalRandomVariable> ();
          // x->SetAttribute ("Mean", DoubleValue (mean));
          // x->SetAttribute ("Variance", DoubleValue (variance));
          // double value = x->GetValue ();

//////////////////////////////////
          
          client.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
          client.SetAttribute ("OffTime",
                               StringValue ("ns3::ConstantRandomVariable[Constant=" +
                                            std::to_string (pingInterval / 1000.) + "]"));
          client.SetAttribute ("DataRate", DataRateValue (DataRate (flow.m_dataRate)));
          client.SetAttribute ("PacketSize", UintegerValue (flow.m_payloadSize));
          Ipv4Address destAddress;
          Ptr<Node> srcNode;
          if (flow.m_direction == Flow::DOWNLINK)
            {
              destAddress = m_staInterfaces.GetAddress (m_currentSta);
              srcNode = m_apNodes.Get (0);
            }
          else
            {
              destAddress = m_apInterfaces.GetAddress (0);
              srcNode = m_staNodes.Get (m_currentSta);
            }
          InetSocketAddress dest (destAddress, flow.m_dstPort);
          dest.SetTos (acTos.at (flow.m_ac));
          client.SetAttribute ("Remote", AddressValue (dest));
          std::cout << "startdelay: "<<startDelay <<"\n";

///////////////////////extra code to change start delay for all flows
//           int64_t startTime =
//           ((Simulator::Now ().GetTimeStep () + 4 * slotUnits) / slotUnits + (i + 1)) * slotUnits;//changed to 2 from 1
//           Time startDelay = TimeStep (startTime) - Simulator::Now ();
// /////////////////////

        //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () <<" Scheduled the start of client application for station " << flow.m_stationId << 
        //   " with "<<(flow.m_direction==Flow::DOWNLINK?"Downlink":"Uplink") <<" flow from source node at memory location "<< srcNode <<'\n';
          Simulator::Schedule (startDelay, &WifiOfdmaExample::StartClient, this, client, i,
                               srcNode);
          }
          else{ //for TCP

          // OnOffHelper client
          //  (socketType, Ipv4Address::GetAny ());
          // uint32_t maxBytes = 500*8*1e6; // 500 MB file
          uint32_t maxBytes = 0;
          BulkSendHelper client ("ns3::TcpSocketFactory", Ipv4Address::GetAny ());

          client.SetAttribute("MaxBytes", UintegerValue(maxBytes));
          client.SetAttribute("SendSize", UintegerValue(flow.m_payloadSize)); 
          // Assuming flow.m_payloadSize is the payload size of each packet
          
          // client.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
          // client.SetAttribute ("OffTime",
          //                      StringValue ("ns3::ConstantRandomVariable[Constant=" +
          //                                   std::to_string (pingInterval / 1000.) + "]"));
          // client.SetAttribute ("DataRate", DataRateValue (DataRate (flow.m_dataRate)));
          // client.SetAttribute ("PacketSize", UintegerValue (flow.m_payloadSize));
          Ipv4Address destAddress;
          Ptr<Node> srcNode;


          if (flow.m_direction == Flow::DOWNLINK)
            {
              destAddress = m_staInterfaces.GetAddress (m_currentSta);
              srcNode = m_apNodes.Get (0);
            }
          else
            {
              destAddress = m_apInterfaces.GetAddress (0);
              srcNode = m_staNodes.Get (m_currentSta);
            }
          InetSocketAddress dest (destAddress, flow.m_dstPort);
          dest.SetTos (acTos.at (flow.m_ac));
          client.SetAttribute ("Remote", AddressValue (dest));
          // std::cout << "startdelay: "<<startDelay <<"\n";

          Simulator::Schedule (startDelay, &WifiOfdmaExample::StartClient_tcp, this, client, i,
                               srcNode);


          }
        }
      i++;
    }
std::cout<<"Added all flows\n";
  // Install client applications in the OBSSes
  if (m_currentSta >= m_nStations && m_obssDlAggregateRate > 0)
    {
      OnOffHelper client ("ns3::UdpSocketFactory", Ipv4Address::GetAny ());
      client.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      client.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant="
                                                   + std::to_string (pingInterval / 1000.) + "]"));
      client.SetAttribute ("DataRate", DataRateValue (DataRate (m_obssDlAggregateRate * 1e6 / m_nStationsPerObss)));
      client.SetAttribute ("PacketSize", UintegerValue (m_obssDlPayload));
      client.SetAttribute ("Remote", AddressValue (InetSocketAddress (staAddress, 7000)));

      Simulator::Schedule (startDelay, &WifiOfdmaExample::StartObssClient, this, client, bss,
                           apNode);
    }

  if (m_currentSta >= m_nStations && m_obssUlAggregateRate > 0)
    {
      OnOffHelper client ("ns3::UdpSocketFactory", Ipv4Address::GetAny ());
      client.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      client.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant="
                                                   + std::to_string (pingInterval / 1000.) + "]"));
      client.SetAttribute ("DataRate", DataRateValue (DataRate (m_obssUlAggregateRate * 1e6 / m_nStationsPerObss)));
      client.SetAttribute ("PacketSize", UintegerValue (m_obssUlPayload));
      client.SetAttribute ("Remote", AddressValue (InetSocketAddress (m_apInterfaces.GetAddress (bss),
                                                                      7000)));

      auto pair = StationIdToBssIndexPair (m_currentSta);

      Simulator::Schedule (startDelay, &WifiOfdmaExample::StartObssClient, this, client, bss,
                           m_obssStaNodes.at (bss - 1).Get (pair.second));
    }

  // continue with the next station, if any is remaining
  if (++m_currentSta < m_nStations + m_nObss * m_nStationsPerObss)
    {
      Simulator::Schedule (startDelay + MilliSeconds (pingInterval),
                           &WifiOfdmaExample::StartAssociation, this);
    }
  else
    {
      // call DelayStart (which changes the duration of the last "Off" period) 1 ms
      // before a slot boundary
    //   std::cout<<"Finished association and BA Agreement establishment between all stations and AP\n";
      Simulator::Schedule (startDelay + MilliSeconds (pingInterval - 1),
                           &WifiOfdmaExample::DelayStart, this);
    }
}

std::set<Ptr<Node>> nodeset;

void
WifiOfdmaExample::StartClient (OnOffHelper client, std::size_t i, Ptr<Node> node)
{
  NS_LOG_FUNCTION (this << node->GetId ());
  std::cout << "At time "<<Simulator::Now() <<" Calling StartClient function with node: "<< node <<'\n';

  m_clientApps[i] = client.Install (node).Get (0);
  m_clientApps[i]->SetStopTime (
      Seconds (m_warmup + m_simulationTime + extrastoptime)); // let clients be active for a long time
}

void
WifiOfdmaExample::StartClient_tcp (BulkSendHelper client, std::size_t i, Ptr<Node> node)
{
  NS_LOG_FUNCTION (this << node->GetId ());
  std::cout << "At time "<<Simulator::Now() <<" Calling StartClient_tcp function with node: "<< node <<'\n';

  m_clientApps[i] = client.Install (node).Get (0);
  m_clientApps[i]->SetStopTime (
      Seconds (m_warmup + m_simulationTime + extrastoptime)); // let clients be active for a long time

  Ptr<BulkSendApplication> sender = DynamicCast<BulkSendApplication> (m_clientApps[i]);
      
  
  // Simulator::Schedule(Seconds(0.1), &TraceCwnd, MakeCallback(&CwndChange_new));
  // auto x = MakeCallback(&CwndChange_new)
  Simulator::Schedule(Seconds(0.001), &TraceCwnd, sender, node->GetId(), 0, MakeCallback(&CwndChange_new));
}

void
WifiOfdmaExample::StartObssClient (OnOffHelper client, uint16_t bss, Ptr<Node> node)
{
  NS_LOG_FUNCTION (this << node->GetId ());

  Ptr<Application> app = client.Install (node).Get (0);
  app->SetStopTime (
      Seconds (m_warmup + m_simulationTime + extrastoptime)); // let clients be active for a long time
  m_obssClientApps.at (bss - 1).Add (app);
}

void
WifiOfdmaExample::DelayStart (void)
{
  NS_LOG_FUNCTION (this);

  // OnOffApplication clients send the first packet an inter-departure time after the
  // beginning of the first "On" period. Compute the maximum IDT among all the flows
  double maxIdt = 0.; // seconds
  for (auto &flow : m_flows)
    {
      maxIdt = std::max (maxIdt, (flow.m_payloadSize * 8 / flow.m_dataRate));
    }
  if (m_nStationsPerObss > 0 && m_obssDlAggregateRate > 0)
    {
      maxIdt = std::max (
          maxIdt, (m_obssDlPayload * 8 / (m_obssDlAggregateRate * 1e6 / m_nStationsPerObss)));
    }
  if (m_nStationsPerObss > 0 && m_obssUlAggregateRate > 0)
    {
      maxIdt = std::max (
          maxIdt, (m_obssUlPayload * 8 / (m_obssUlAggregateRate * 1e6 / m_nStationsPerObss)));
    }

  // for each client application 'c', the last "Off" period lasts maxIdt + startDelay(c) - IDT(c)
  // so that every client application transmits the first packet after maxIdt + startDelay(c)
  Ptr<UniformRandomVariable> startDelayRV;
  startDelayRV = CreateObjectWithAttributes<UniformRandomVariable> ("Min", DoubleValue (0.0), "Max",
                                                                    DoubleValue (m_startInterval));
  auto delayrv = startDelayRV->GetValue ();
  UintegerValue pktSize;
  DataRateValue cbrRate;
  bool found;
  double offTime;

  for (auto &clientApp : m_clientApps)
    { 
      found = clientApp->GetAttributeFailSafe ("PacketSize", pktSize);
      if(!found)found = clientApp->GetAttributeFailSafe ("SendSize", pktSize);
      // NS_ABORT_IF (!found);
      found = clientApp->GetAttributeFailSafe ("DataRate", cbrRate);
      if(!found)cbrRate = DataRateValue (DataRate (10*1e6));

      // NS_ABORT_IF (!found);
      // if(cbrRate.Get ().GetBitRate () == 0){
      //   // std::cout << "inf"<<"\n";
      //   offTime = 0.000230168;
      // }else{
        // std::cout << "notinf"<<"\n";
      // std::cout << "startdelayrv: "<< startDelayRV->GetValue () <<'\n';

      offTime = maxIdt + (delayrv / 1000.) - (pktSize.Get () * 8. / (cbrRate.Get ().GetBitRate ())) + 50e-9;
      // }
      // we add 50 nanoseconds to offTime to prevent it from being zero, which
      // happens if m_startInterval is zero and all the flows have the same IDT.
      // In such a case, at the next slot boundary both the "On" and the "Off"
      // periods would be zero and this would cause an infinite loop

      // std::cout << "OFFTIME: "<<offTime<<'\n';
      // if(m_dlFlowDataRate == 0){
      //   offTime = 0.000230168;
      // }
     std::cout << "OFFTIME: "<<offTime<<"Packet size "<<cbrRate.Get ().GetBitRate ()<<'\n';
      NS_ASSERT (offTime > 0);
      // clientApp->

      // std::cout << "Packet size: " << pktSize.Get() << "\n";

      std::stringstream ss;
      ss << "ns3::ConstantRandomVariable[Constant=" << std::fixed << std::setprecision (10)
         << offTime << "]";
      // std::cout <<"ksjhsabjn"<<"\n";

      // found = clientApp->GetAttributeFailSafe ("OffTime", StringValue((ss.str())));
  
     if(found) clientApp->SetAttribute ("OffTime", StringValue (ss.str ()));

    }

  for (auto &obssClientApp : m_obssClientApps)
    {
      for (auto appIt = obssClientApp.Begin (); appIt != obssClientApp.End (); appIt++)
        {
          found = (*appIt)->GetAttributeFailSafe ("PacketSize", pktSize);
          // NS_ABORT_IF (!found);
          found = (*appIt)->GetAttributeFailSafe ("DataRate", cbrRate);
          // NS_ABORT_IF (!found);
          offTime = maxIdt + (startDelayRV->GetValue () / 1000.) -
                    (pktSize.Get () * 8. / cbrRate.Get ().GetBitRate ()) + 50e-9;
          NS_ASSERT (offTime > 0);

          std::stringstream ss;
          ss << "ns3::ConstantRandomVariable[Constant=" << std::fixed << std::setprecision (10)
             << offTime << "]";
          (*appIt)->SetAttribute ("OffTime", StringValue (ss.str ()));
        }
    }

  // StartTraffic sets the "Off" time to zero and the "On" time to the simulation
  // duration. Therefore, it must be called after the next slot boundary (when
  // the last "Off" period must be scheduled) and before the last "Off" period
  // expires. The next slot boundary is in one millisecond and the minimum
  // duration of the last "Off" period is 50 nanoseconds.
  Simulator::Schedule (MilliSeconds (1) + NanoSeconds (30), &WifiOfdmaExample::StartTraffic, this);
}

void
WifiOfdmaExample::StartTraffic (void)
{
  NS_LOG_FUNCTION (this);

  ///////////////////////////Our change///////////////////////
//  double totalTime = m_warmup + m_simulationTime;
//   std::string onTime = std::to_string (totalTime);
//    std::string offTime = std::to_string (0);
  
// //  int slots = 10;
// //  double slotTime = totalTime/double(slots);

 

// //   std::cout << "At time "<<Simulator::Now().GetMicroSeconds () << "StartTraffic is called." << '\n';

//   for (long unsigned int i=0;i< (m_clientApps.size());i++)

//     { 

//       auto clientApp = m_clientApps[i];
// // Ptr<UniformRandomVariable> on = CreateObject<UniformRandomVariable> ();
// //  on->SetAttribute("Min",DoubleValue(0));
// //  on->SetAttribute("Max",DoubleValue(slotTime));
// //   double onFor = on->GetValue();
// //   double offFor = slotTime - onFor;
  
// //   std::string onTime = std::to_string (onFor);
// //   std::string offTime = std::to_string (offFor);

// //    m_clientApps_onTime_Info[i] = onFor;
//       // clientApp->SetAttribute ("OnTime", StringValue ("ns3::UniformRandomVariable[Min=1.0|Max="
//       //                                                 + onTime + "]"));
//       // clientApp->SetAttribute ("OffTime", StringValue ("ns3::UniformRandomVariable[Min=1.0|Max="
//       //                                                 + onTime + "]"));                                                

//       clientApp->SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant="
//                                                       + onTime + "]"));
//       clientApp->SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant="+ offTime
//       +"]"));
//      // int sta = m_flows[i].m_stationId ;
//    //   std::cout<<"Station :"<<sta<<" Dst port"<< m_flows[i].m_dstPort<<
//       //  std::cout<< m_flows[i]<<"\n  ON time:"<<onFor<<" OFF time:"
//       // << offFor<<" "<<slots   <<" slots \n";
        
//     }

//////////////////////our change////////////////////////////////

  std::string onTime = std::to_string (m_warmup + m_simulationTime);
  // int h = 0;

  for (auto &clientApp : m_clientApps)
    {


      ////////////////////////////
      // double scale = 2.0;
      // double shape = 3.0;
      // Ptr<WeibullRandomVariable> x = CreateObject<WeibullRandomVariable> ();
      // x->SetAttribute ("Scale", DoubleValue (scale));
      // x->SetAttribute ("Shape", DoubleValue (shape));
      // double value = x->GetValue ();
      // std::cout << "weibull: "<<value << '\n';

      
/////////////////////////////
  
      
      
      
      // clientApp->SetAttribute("OffTime", StringValue("ns3::LogNormalRandomVariable[Mu=0.04|Sigma=0.003]"));
      // clientApp->SetAttribute("OnTime", StringValue("ns3::LogNormalRandomVariable[Mu=0.4, Sigma = 0.03]"));

      // clientApp->SetAttribute("MaxBytes", UintegerValue(30000));
      UintegerValue pktSize;
  
 bool found = clientApp->GetAttributeFailSafe ("PacketSize", pktSize);
     if(found){ 
      // clientApp->SetAttribute ("OffTime", StringValue ("ns3::UniformRandomVariable[Min=0.1|Max=0.2]"));
     clientApp->SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      

      // clientApp->SetAttribute ("OnTime", StringValue ("ns3::UniformRandomVariable[Min=0.5|Max=1]"));
      clientApp->SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant="
                                                      + onTime + "]"));

      // clientApp->SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant = 1]"));}
      
     
      // clientApp->SetAttribute("OnTime", StringValue("ns3::WeibullRandomVariable[Shape=3.0|Scale=2.0]"));
     
     }
      // clientApp->SetAttribute("OnTime", StringValue("ns3::NormalRandomVariable[Mean=2.0|Variance=0.1]"));
      // std::cout << "nodenode: " << clientApp->GetNode() << '\n';

    // h++;
    }
   
  for (auto &obssClientApp : m_obssClientApps)
    {
      for (auto appIt = obssClientApp.Begin (); appIt != obssClientApp.End (); appIt++)
        {
//           Ptr<UniformRandomVariable> on = CreateObject<UniformRandomVariable> ();
//  on->SetAttribute("Min",DoubleValue(0));
//  on->SetAttribute("Max",DoubleValue(slotTime));
//   double onFor = on->GetValue();
//   double offFor = slotTime - onFor;
  
//   std::string onTime = std::to_string (onFor);
//   std::string offTime = std::to_string (offFor);

          (*appIt)->SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant="
                                                         + onTime + "]"));
          (*appIt)->SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
        }
    }

  Simulator::Schedule (Seconds (m_warmup), &WifiOfdmaExample::StartStatistics, this);
}

void
WifiOfdmaExample::StartStatistics (void)
{
  NS_LOG_FUNCTION (this);

  std::cout << "Starting statistics at " << Simulator::Now ().GetMicroSeconds() << std::endl;
  /* Connect traces on all the stations (including the AP) and for all the ACs */
  NetDeviceContainer devices = m_staDevices;
  devices.Add (m_apDevices.Get (0));

  if (m_verbose)
    {
      Config::Connect ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/State/State",
                       MakeCallback (&WifiOfdmaExample::NotifyStateChange, this));
    }

  for (auto devIt = devices.Begin (); devIt != devices.End (); devIt++)
    {
      Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (*devIt);
      Ptr<QosTxop> qosTxop;

      for (auto &ac : m_aciToString)
        {
          // get the EDCA on the station for the given AC
          PointerValue ptr;
          dev->GetMac ()->GetAttribute (std::string (ac.second + "_Txop"), ptr);
          qosTxop = ptr.Get<QosTxop> ();
          // trace expired MSDUs
          qosTxop->GetWifiMacQueue ()->TraceConnectWithoutContext ("Expired",
            MakeCallback (&WifiOfdmaExample::NotifyMsduExpired, this));
            // std::cout<<"Done1\n";
          // qosTxop->GetBaManager ()->GetRetransmitQueue ()->TraceConnectWithoutContext ("Expired",
          //   MakeCallback (&WifiOfdmaExample::NotifyMsduExpired, this));
          // trace rejected MSDUs
          qosTxop->GetWifiMacQueue ()->TraceConnectWithoutContext ("DropBeforeEnqueue",
            MakeCallback (&WifiOfdmaExample::NotifyMsduRejected, this));
                      // std::cout<<"Done2\n";

          // trace packets enqueued in the EDCA queue
          qosTxop->GetWifiMacQueue ()->TraceConnectWithoutContext ("Enqueue",
            MakeCallback (&WifiOfdmaExample::NotifyEdcaEnqueue, this));
            //  std::cout<<"Done3\n";

          // trace MSDUs dequeued from the EDCA queue
          qosTxop->GetWifiMacQueue ()->TraceConnectWithoutContext ("Dequeue", 
            MakeCallback (&WifiOfdmaExample::NotifyMsduDequeuedFromEdcaQueue, this)
            .Bind (qosTxop->GetWifiMacQueue ()->GetMaxDelay ()));
            //  std::cout<<"Done4\n";

          // trace TXOP duration
          // qosTxop->TraceConnectWithoutContext ("TxopTrace",
          //   MakeCallback (&WifiOfdmaExample::NotifyTxopDuration, this)
          //   .Bind (MacAddressToNodeId (Mac48Address::ConvertFrom (dev->GetAddress ())), ac.first));
            //  std::cout<<"Done5\n";
        }
std::cout<<"Done\n";
      // trace TX failures
    //  DynamicCast<RegularWifiMac> (dev->GetMac ())->TraceConnectWithoutContext ("NAckedMpdu", MakeCallback (&WifiOfdmaExample::NotifyTxFailed, this));
      // trace PSDUs forwarded down to the PHY
      dev->GetPhy ()->TraceConnectWithoutContext ("PhyTxPsduBegin", MakeCallback (&WifiOfdmaExample::NotifyPsduForwardedDown, this));
      // trace packets forwarded up by the MAC layer
      dev->GetMac ()->TraceConnectWithoutContext ("MacRx", MakeCallback (&WifiOfdmaExample::NotifyMacForwardUp, this));

      dev->GetPhy ()->TraceConnectWithoutContext ("PhyTxPpduBegin", MakeCallback (&WifiOfdmaExample::NotifyPpduForwardedDown, this));
    }

  for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      if(m_flows[i].m_l4Proto == Flow::UDP){
      Ptr<OnOffApplication> sender = DynamicCast<OnOffApplication> (m_clientApps[i]);
      NS_ABORT_MSG_IF (sender == nullptr, "Not an OnOffApplication?");
      sender->TraceConnectWithoutContext ("Tx", MakeCallback (&WifiOfdmaExample::NotifyAppTx, this)
                                                .Bind (i));
      // m_flows[i].m_packetsRejectedBySocket = sender->GetTotalPacketsFailed ();

      Ptr<PacketSink> sink = DynamicCast<PacketSink> (m_sinkApps.Get (i));
      NS_ABORT_MSG_IF (sink == nullptr, "Not a PacketSink?");
      sink->TraceConnectWithoutContext ("Rx", MakeCallback (&WifiOfdmaExample::NotifyAppRx, this)
                                              .Bind (i));
      m_flows[i].m_prevRxBytes = sink->GetTotalRx ();
      }else{ // for bulksendtcp
      
      Ptr<BulkSendApplication> sender = DynamicCast<BulkSendApplication> (m_clientApps[i]);
      NS_ABORT_MSG_IF (sender == nullptr, "Not an BulkSendApplication?");
      sender->TraceConnectWithoutContext ("Tx", MakeCallback (&WifiOfdmaExample::NotifyAppTx, this)
                                                .Bind (i));
      // m_flows[i].m_packetsRejectedBySocket = sender->GetTotalPacketsFailed ();

      Ptr<PacketSink> sink = DynamicCast<PacketSink> (m_sinkApps.Get (i));
      NS_ABORT_MSG_IF (sink == nullptr, "Not a PacketSink?");
      sink->TraceConnectWithoutContext ("Rx", MakeCallback (&WifiOfdmaExample::NotifyAppRx, this)
                                              .Bind (i));
      m_flows[i].m_prevRxBytes = sink->GetTotalRx ();

      ///////////////////////////

      // Ptr<Socket> socket_cwnd = sender->GetSocket();
      // AsciiTraceHelper asciiTraceHelper;
      
      // Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("seventh" + std::to_string(i) + ".cwnd");
      // socket_cwnd->TraceConnectWithoutContext("CongestionWindow",
      //                                        MakeBoundCallback(&CwndChange, stream));

      }
    }

  for (std::size_t i = 0; i <= m_nStations; i++)
    {
      Ptr<Node> node = (i == 0 ? m_apNodes.Get (0) : m_staNodes.Get (i - 1));
      Ptr<NetDevice> device = (i == 0 ? m_apDevices.Get (0) : m_staDevices.Get (i - 1));
      Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer> ();
      NS_ABORT_MSG_IF (tc == nullptr, "No TrafficControlLayer object aggregated to node");

      Ptr<QueueDisc> qdisc = tc->GetRootQueueDiscOnDevice (device);
      if (qdisc != nullptr && qdisc->GetNQueueDiscClasses () == 4)
        {
          Ptr<QueueDisc> child;
          for (auto &acStats : m_perAcStats.at (i))
            {
              child = qdisc->GetQueueDiscClass (static_cast<std::size_t> (acStats.first))
                          ->GetQueueDisc ();
              acStats.second.droppedByQueueDisc = child->GetStats ().nTotalDroppedPackets;
              child->TraceConnectWithoutContext ("SojournTime",
                                                 MakeCallback (&WifiOfdmaExample::NotifySojournTime, this)
                                                 .Bind (i, acStats.first));
              child->TraceConnectWithoutContext ("DropAfterDequeue",
                                                 MakeCallback (&WifiOfdmaExample::NotifyDropAfterDequeue, this)
                                                 .Bind (i, acStats.first));
            }
        }
    }

  if (m_obssDlAggregateRate > 0)
    {
      for (uint16_t bss = 0; bss < m_nObss; bss++)
        {
          for (std::size_t i = 0; i < m_nStationsPerObss; i++)
            {
              m_obssDlRxStart.at (bss) +=
                  DynamicCast<PacketSink> (m_obssSinkApps.at (bss).Get (i))->GetTotalRx ();
            }
        }
    }

  if (m_obssUlAggregateRate > 0)
    {
      for (uint16_t bss = 0; bss < m_nObss; bss++)
        {
          uint32_t last = m_obssSinkApps.at (bss).GetN () - 1;
          m_obssUlRxStart.at (bss) =
              DynamicCast<PacketSink> (m_obssSinkApps.at (bss).Get (last))->GetTotalRx ();
        }
    }

  Simulator::Schedule (Seconds (m_simulationTime / m_nIntervals),
                       &WifiOfdmaExample::StoreCumulativeRxBytes, this);
  Simulator::Schedule (Seconds (m_simulationTime), &WifiOfdmaExample::StopStatistics, this);
}

void
WifiOfdmaExample::StoreCumulativeRxBytes (void)
{
#if 0
  Ptr<WifiMacQueue> queue = DynamicCast<RegularWifiMac> (DynamicCast<WifiNetDevice> (m_apDevices.Get (0))->GetMac ())->GetQosTxop (AC_BE)->GetWifiMacQueue ();
  for (auto it = queue->QueuedPacketsBegin (); it != queue->QueuedPacketsEnd (); it++)
    {
      std::cout << "STA " << it->first.first << " : " << it->second << " MSDUs" << std::endl;
    }
#endif
  std::map<AcIndex, double> dlTput, ulTput;
  std::map<AcIndex, double>::iterator mapIt;

  for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      Ptr<PacketSink> sink = DynamicCast<PacketSink> (m_sinkApps.Get (i));
      NS_ABORT_MSG_IF (sink == nullptr, "Not a PacketSink?");

      if (m_flows[i].m_direction == Flow::DOWNLINK)
        {
          mapIt = dlTput.insert ({m_flows[i].m_ac, 0.0}).first;
        }
      else
        {
          mapIt = ulTput.insert ({m_flows[i].m_ac, 0.0}).first;
        }
      mapIt->second += (sink->GetTotalRx () - m_flows[i].m_prevRxBytes) * 8. /
                       (m_simulationTime / m_nIntervals * 1e6);
      m_flows[i].m_prevRxBytes = sink->GetTotalRx ();
    }
    
    if(graph_stats == false){   
  std::cout << "Per-AC throughput in the last time interval (time: " << Simulator::Now ().GetMicroSeconds () << ")"
            << std::endl << "DOWNLINK" << std::endl;
  for (auto& acMap : dlTput)
    {
      std::cout << "[" << m_aciToString.at (acMap.first) << "] " << acMap.second << std::endl;
    }
  std::cout << "UPLINK" << std::endl;
  for (auto& acMap : ulTput)
    {
      std::cout << "[" << m_aciToString.at (acMap.first) << "] " << acMap.second << std::endl;
    }
  std::cout << std::endl;
    }

  if (++m_elapsedIntervals < m_nIntervals)
    {
      Simulator::Schedule (Seconds (m_simulationTime / m_nIntervals),
                           &WifiOfdmaExample::StoreCumulativeRxBytes, this);
    }
}

void
WifiOfdmaExample::StopStatistics (void)
{
  NS_LOG_FUNCTION (this);

  std::cout << "Stopping statistics at " << Simulator::Now ().GetMicroSeconds () << std::endl;

  if (m_verbose)
    {
      Config::Disconnect ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/State/State",
                          MakeCallback (&WifiOfdmaExample::NotifyStateChange, this));
    }

  /* Disconnect traces on all the stations (including the AP) and for all the ACs */
  NetDeviceContainer devices = m_staDevices;
  devices.Add (m_apDevices.Get (0));

  for (auto devIt = devices.Begin (); devIt != devices.End (); devIt++)
    {
      Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (*devIt);
      Ptr<QosTxop> qosTxop;

      for (auto &ac : m_aciToString)
        {
          // get the EDCA on the station for the given AC
          PointerValue ptr;
          dev->GetMac ()->GetAttribute (std::string (ac.second + "_Txop"), ptr);
          qosTxop = ptr.Get<QosTxop> ();
          // stop tracing expired MSDUs
          qosTxop->GetWifiMacQueue ()->TraceDisconnectWithoutContext ("Expired",
            MakeCallback (&WifiOfdmaExample::NotifyMsduExpired, this));
          // qosTxop->GetBaManager ()->GetRetransmitQueue ()->TraceDisconnectWithoutContext ("Expired",
          //   MakeCallback (&WifiOfdmaExample::NotifyMsduExpired, this));
          // stop tracing rejected MSDUs
          qosTxop->GetWifiMacQueue ()->TraceDisconnectWithoutContext ("DropBeforeEnqueue",
            MakeCallback (&WifiOfdmaExample::NotifyMsduRejected, this));
          // stop tracing packets enqueued in the EDCA queue
          qosTxop->GetWifiMacQueue ()->TraceDisconnectWithoutContext ("Enqueue",
            MakeCallback (&WifiOfdmaExample::NotifyEdcaEnqueue, this));
          // stop tracing MSDUs dequeued from the EDCA queue
          qosTxop->GetWifiMacQueue ()->TraceDisconnectWithoutContext ("Dequeue", 
            MakeCallback (&WifiOfdmaExample::NotifyMsduDequeuedFromEdcaQueue, this)
            .Bind (qosTxop->GetWifiMacQueue ()->GetMaxDelay ()));
          // stop tracing TXOP duration
          // qosTxop->TraceDisconnectWithoutContext ("TxopTrace",
          //   MakeCallback (&WifiOfdmaExample::NotifyTxopDuration, this)
          //   .Bind (MacAddressToNodeId (Mac48Address::ConvertFrom (dev->GetAddress ())), ac.first));
        }

      // stop tracing TX failures
      //DynamicCast<RegularWifiMac> (dev->GetMac ())->TraceDisconnectWithoutContext ("NAckedMpdu", MakeCallback (&WifiOfdmaExample::NotifyTxFailed, this));
      // stop tracing PSDUs forwarded down to the PHY
      dev->GetPhy ()->TraceDisconnectWithoutContext ("PhyTxPsduBegin", MakeCallback (&WifiOfdmaExample::NotifyPsduForwardedDown, this));
      // stop tracing packets forwarded up by thNotifyTxopDuratione MAC layer
      dev->GetMac ()->TraceDisconnectWithoutContext ("MacRx", MakeCallback (&WifiOfdmaExample::NotifyMacForwardUp, this));

      dev->GetPhy ()->TraceDisconnectWithoutContext ("PhyTxPpduBegin", MakeCallback (&WifiOfdmaExample::NotifyPpduForwardedDown, this));
      
    }

  for (std::size_t i = 0; i < m_flows.size (); i++)
    {
      if(m_flows[i].m_l4Proto == Flow::UDP){
      Ptr<OnOffApplication> sender = DynamicCast<OnOffApplication> (m_clientApps[i]);
      NS_ABORT_MSG_IF (sender == nullptr, "Not an OnOffApplication?");
      sender->TraceDisconnectWithoutContext ("Tx", MakeCallback (&WifiOfdmaExample::NotifyAppTx, this)
                                                   .Bind (i));
      // m_flows[i].m_packetsRejectedBySocket = sender->GetTotalPacketsFailed ()
      //                                        - m_flows[i].m_packetsRejectedBySocket;

      Ptr<PacketSink> sink = DynamicCast<PacketSink> (m_sinkApps.Get (i));
      NS_ABORT_MSG_IF (sink == nullptr, "Not a PacketSink?");
      sink->TraceDisconnectWithoutContext ("Rx", MakeCallback (&WifiOfdmaExample::NotifyAppRx, this)
                                                 .Bind (i));
      }else{
      Ptr<BulkSendApplication> sender = DynamicCast<BulkSendApplication> (m_clientApps[i]);
      NS_ABORT_MSG_IF (sender == nullptr, "Not an BulkSendApplication?");
      sender->TraceDisconnectWithoutContext ("Tx", MakeCallback (&WifiOfdmaExample::NotifyAppTx, this)
                                                   .Bind (i));
      // m_flows[i].m_packetsRejectedBySocket = sender->GetTotalPacketsFailed ()
      //                                        - m_flows[i].m_packetsRejectedBySocket;

      Ptr<PacketSink> sink = DynamicCast<PacketSink> (m_sinkApps.Get (i));
      NS_ABORT_MSG_IF (sink == nullptr, "Not a PacketSink?");
      sink->TraceDisconnectWithoutContext ("Rx", MakeCallback (&WifiOfdmaExample::NotifyAppRx, this)
                                                 .Bind (i));

      }
    }

  for (std::size_t i = 0; i <= m_nStations; i++)
    {
      Ptr<Node> node = (i == 0 ? m_apNodes.Get (0) : m_staNodes.Get (i - 1));
      Ptr<NetDevice> device = (i == 0 ? m_apDevices.Get (0) : m_staDevices.Get (i - 1));
      Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer> ();
      NS_ABORT_MSG_IF (tc == nullptr, "No TrafficControlLayer object aggregated to node");

      Ptr<QueueDisc> qdisc = tc->GetRootQueueDiscOnDevice (device);
      if (qdisc != nullptr && qdisc->GetNQueueDiscClasses () == 4)
        {
          Ptr<QueueDisc> child;
          for (auto &acStats : m_perAcStats.at (i))
            {
              child = qdisc->GetQueueDiscClass (static_cast<std::size_t> (acStats.first))
                          ->GetQueueDisc ();
              acStats.second.droppedByQueueDisc =
                  child->GetStats ().nTotalDroppedPackets - acStats.second.droppedByQueueDisc;
            }
        }
    }

  if (m_obssDlAggregateRate > 0)
    {
      for (uint16_t bss = 0; bss < m_nObss; bss++)
        {
          for (std::size_t i = 0; i < m_nStationsPerObss; i++)
            {
              m_obssDlRxStop.at (bss) +=
                  DynamicCast<PacketSink> (m_obssSinkApps.at (bss).Get (i))->GetTotalRx ();
            }
        }
    }

  if (m_obssUlAggregateRate > 0)
    {
      for (uint16_t bss = 0; bss < m_nObss; bss++)
        {
          uint32_t last = m_obssSinkApps.at (bss).GetN () - 1;
          m_obssUlRxStop.at (bss) =
              DynamicCast<PacketSink> (m_obssSinkApps.at (bss).Get (last))->GetTotalRx ();
        }
    }

  // (Brutally) stop client applications
  for (auto &clientApp : m_clientApps)
    {
      clientApp->Dispose ();
    }
  for (auto &obssClientApps : m_obssClientApps)
    {
      for (auto appIt = obssClientApps.Begin (); appIt != obssClientApps.End (); appIt++)
        {
          (*appIt)->Dispose ();
        }
    }

  // we are done
  Simulator::Stop ();
}

uint32_t
WifiOfdmaExample::MacAddressToNodeId (Mac48Address address)
{
  if (m_apDevices.Get (0)->GetAddress () == address)
    {
      return m_apNodes.Get (0)->GetId ();
    }

  auto it = m_staMacAddressToNodeId.find (address);
  if (it != m_staMacAddressToNodeId.end ())
    {
      return it->second;
    }

  NS_ABORT_MSG ("Found no node having MAC address " << address);
}

std::pair<std::map<AcIndex, WifiOfdmaExample::PairwisePerAcStats>::iterator, bool>
WifiOfdmaExample::GetPairwisePerAcStats (const WifiMacHeader &hdr, AcIndex ac)
{
  std::map<AcIndex, PairwisePerAcStats>::iterator mapIt;

  if (hdr.IsQosData () && !hdr.GetAddr1 ().IsGroup ())
    {
      ac = QosUtilsMapTidToAc (hdr.GetQosTid ());
    }
  else if (!hdr.IsBlockAckReq () || ac == AC_UNDEF)
    {
      // we need to count BARs transmnitted in HE TB PPDUs
      return std::make_pair (mapIt, false);
    }

  uint32_t srcNodeId = MacAddressToNodeId (hdr.GetAddr2 ());
  uint32_t dstNodeId = MacAddressToNodeId (hdr.GetAddr1 ());
  std::vector<std::map<AcIndex, PairwisePerAcStats>>::iterator vecIt;

  if (srcNodeId == 0)
    {
      // downlink
      vecIt = std::next (m_dlPerStaAcStats.begin (), dstNodeId - 1);
    }
  else
    {
      // uplink
      vecIt = std::next (m_ulPerStaAcStats.begin (), srcNodeId - 1);
    }

  mapIt = vecIt->find (ac);

  return std::make_pair (mapIt, mapIt != vecIt->end ());
}

void
WifiOfdmaExample::NotifyTxFailed (Ptr<const WifiMpdu> mpdu)
{
  if (mpdu->GetHeader ().GetAddr1 ().IsGroup ())
    {
      return;
    }

  auto itPair = GetPairwisePerAcStats (mpdu->GetHeader ());

  if (itPair.second)
    {
      itPair.first->second.failed++;
    }
}

void
WifiOfdmaExample::NotifyMsduExpired (Ptr<const WifiMpdu> item)
{
  if (item->GetHeader ().GetAddr1 ().IsGroup ())
    {
      return;
    }

  auto itPair = GetPairwisePerAcStats (item->GetHeader ());

  if (itPair.second)
    {
      itPair.first->second.expired++;
    }
}

void
WifiOfdmaExample::NotifyMsduRejected (Ptr<const WifiMpdu> item)
{
  if (item->GetHeader ().GetAddr1 ().IsGroup ())
    {
      return;
    }

  auto itPair = GetPairwisePerAcStats (item->GetHeader ());

  if (itPair.second)
    {
      itPair.first->second.rejected++;
    }
}

void
WifiOfdmaExample::NotifyMsduDequeuedFromEdcaQueue (Time maxDelay, Ptr<const WifiMpdu> item)
{
  if (!item->GetHeader ().IsQosData () || item->GetHeader ().GetAddr1 ().IsGroup () ||
      Simulator::Now () > item->GetTimestamp() + maxDelay)
    {
      // the frame is not a unicast QoS data frame or the MSDU lifetime is higher than the
      // max queue delay, hence the MSDU has been discarded. Do nothing in this case.
      return;
    }

  uint32_t srcNodeId = MacAddressToNodeId (item->GetHeader ().GetAddr2 ());

  // update the pairwise HoL delay if this packet is being sent by the AP
  if (srcNodeId == 0)
    {
      auto itPair = GetPairwisePerAcStats (item->GetHeader ());

      if (itPair.second)
        {
          // a new HoL sample is stored if this is not the first MSDU being dequeued
          // and if this MSDU is not dequeued to be aggregated to a previously dequeued
          // MSDU (which would give a null sample)
          if (itPair.first->second.lastTxTime.IsStrictlyPositive () &&
              Simulator::Now () > itPair.first->second.lastTxTime)
            {
              double newHolSample =
                  (Simulator::Now () - Max (itPair.first->second.lastTxTime, item->GetTimestamp ()))
                      .ToDouble (Time::MS);
              itPair.first->second.pairwiseHol.AddSample (newHolSample);
            }
          itPair.first->second.lastTxTime = Simulator::Now ();
        }
    }

  // update the aggregate HoL delay
  auto mapIt =
      m_perAcStats.at (srcNodeId).find (QosUtilsMapTidToAc (item->GetHeader ().GetQosTid ()));

  if (mapIt == m_perAcStats.at (srcNodeId).end ())
    {
      return;
    }

  // a new HoL sample is stored if this is not the first MSDU being dequeued
  // and if this MSDU is not dequeued to be aggregated to a previously dequeued
  // MSDU (which would give a null sample)
  if (mapIt->second.lastTxTime.IsStrictlyPositive () &&
      Simulator::Now () > mapIt->second.lastTxTime)
    {
      double newHolSample =
          (Simulator::Now () - Max (mapIt->second.lastTxTime, item->GetTimestamp ()))
              .ToDouble (Time::MS);

      mapIt->second.aggregateHoL.AddSample (newHolSample);
    }
  mapIt->second.lastTxTime = Simulator::Now ();
}

void 
WifiOfdmaExample::NotifyPpduForwardedDown (Ptr<WifiPpdu> ppdu, WifiTxVector txVector, double txPowerW){

  /////////////////////////////////Old code/////////////////////////////////////////////
  
  // std::cout << "PPDU forwarded down tx duration: "<<ppdu->GetTxDuration()<<"\n";

  // ns3::Time ppduduration = ppdu->GetTxDuration();
  // ns3::Time psduduration = Seconds(0);
  
  // //////////////////////////////////////////////////////////
  // WifiConstPsduMap psduMap = ppdu->GetPsduMap();

  // Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
  // Mac48Address apAddress = dev->GetMac ()->GetAddress ();

  // if (psduMap.size () == 1 && psduMap.begin ()->second->GetAddr1 () == apAddress)
  //   {
  //     // Uplink frame
  //     const WifiMacHeader &hdr = psduMap.begin ()->second->GetHeader (0);

  //     ns3::Time ul_psduDurationSum = Seconds(0);

  //     if (txVector.GetPreambleType () == WIFI_PREAMBLE_HE_TB)
  //       {
  //         // HE TB PPDU
  //         if (hdr.HasData () || hdr.IsBlockAckReq ())
  //           {
  //             for (auto &staIdPsdu : psduMap)
  //               {
  //               if (staIdPsdu.second->GetSize () > 0)
  //                 {
  //                   Time txDuration = WifiPhy::CalculateTxDuration (staIdPsdu.second->GetSize (),
  //                                                                 txVector, m_band,
  //                                                                 staIdPsdu.first);
  //                   ul_psduDurationSum += txDuration;
  //                   }
  //           }

            
  //           std::cout << "ul_psduDurationSum: "<< ul_psduDurationSum<<"\n";
          
              
  //             Time txDuration = WifiPhy::CalculateTxDuration (psduMap.begin ()->second->GetSize (),
  //                                                             txVector, m_band,
  //                                                             psduMap.begin ()->first);

  //             AcIndex ac = AC_UNDEF;
  //             if (hdr.IsBlockAckReq ())
  //               {
  //                 CtrlBAckRequestHeader baReqHdr;
  //                 psduMap.begin ()->second->GetPayload (0)->PeekHeader (baReqHdr);
  //                 ac = QosUtilsMapTidToAc (baReqHdr.GetTidInfo ());
  //               }

  //             auto itPair = GetPairwisePerAcStats (hdr, ac);
  //             if (itPair.second)
  //               {
  //                 // itPair.first->second.ampduRatio.AddSample (currRatio);
  //               }
  //           }
  //         else if (hdr.GetType () == WIFI_MAC_QOSDATA_NULL)
  //           {
  //             // m_countOfNullResponsesToLastTf++;
  //           }
  //       psduduration = ul_psduDurationSum;
  //       }
  //       std::cout << "UL PDSU duration: "<< psduduration << "\n";
  //       std::cout << "UL PPDU duration: "<< ppduduration << "\n";
  //   }

  // // Downlink frame
  // else if (psduMap.begin ()->second->GetHeader (0).IsQosData ())
  //   {
  
  //     Time dlMuPpduDuration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
      
  //     // DL MU PPDU -- DL is data
  //     if (txVector.GetPreambleType () == WIFI_PREAMBLE_HE_MU)
  //       {
  //         Time dlMuPpduDuration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
  //         Time psduDurationSum = Seconds (0);

  //         for (auto &staIdPsdu : psduMap)
  //           {
              
  //             if (staIdPsdu.second->GetSize () > 0)
  //               {
  //                 std::cout<<" DL psdu size: "<<staIdPsdu.second->GetSize ()<<" station  :"<<staIdPsdu.first <<"\n";
  //                 Time txDuration = WifiPhy::CalculateTxDuration (staIdPsdu.second->GetSize (),
  //                                                                 txVector, m_band,
  //                                                                 staIdPsdu.first);
  //                 psduDurationSum += txDuration;
  //               }

  //           }

  //         // std::cout << "psdu duration sum: "<< psduDurationSum << "\n";

  //         // std::cout << "dlMuPpduDuration: " << dlMuPpduDuration <<"\n";

  //         psduduration = psduDurationSum;
  //         }
  //         std::cout << "DL PDSU duration: "<< psduduration << "\n";
  //         std::cout << "DL PPDU duration: "<< ppduduration << "\n";
  //   }
  //    //if DL is trigger
  // // else if (psduMap.size () == 1 && psduMap.begin ()->second->GetHeader (0).IsTrigger ())
  // else if (psduMap.begin ()->second->GetHeader (0).IsTrigger ())
  //   {
  //     std::cout << "DL is trigger"<<"\n";
  //     CtrlTriggerHeader trigger;
  //     psduMap.begin()->second->GetPayload(0)->PeekHeader(trigger);
  //     // auto hdrtid = psduMap.begin ()->second->GetHeader (0).GetQosTid();
    
  //     auto x= (psduMap.begin ()->second->GetTids().begin());
  //     auto accesscategory = QosUtilsMapTidToAc (*x);


  //     WifiTxVector heTbTxVector = trigger.GetHeTbTxVector (trigger.begin ()->GetAid12 ());
      
  //     if (trigger.IsBasic ())
  //       {
  //         m_lastTfType_ppdu = TriggerFrameType::BASIC_TRIGGER;
          
  //         dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
  //         Ptr<ApWifiMac> mac = DynamicCast<ApWifiMac> (dev->GetMac ());

  //         Time basic_trigger_duration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
                  
          
  //         for (auto &userInfo : trigger)
  //           {    
  //             Mac48Address address = mac->GetStaList (accesscategory).at (userInfo.GetAid12 ());
  //             std::size_t index = MacAddressToNodeId (address) - 1;
  //           }

  //           psduduration = basic_trigger_duration;
  //           std::cout << "BASIC PDSU duration: "<< psduduration << "\n";
  //           std::cout << "BASIC PPDU duration: "<< ppduduration << "\n";
  //         }
  //     else if (trigger.IsBsrp ())
  //       {
  //         Time bsrp_trigger_duration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
  //         m_lastTfType_ppdu = TriggerFrameType::BSRP_TRIGGER;
  //         psduduration = bsrp_trigger_duration;
  //         std::cout << "BSRP PDSU duration: "<< psduduration << "\n";
  //         std::cout << "BSRP PPDU duration: "<< ppduduration << "\n";
  //         }
  //   }

  //   std::cout << "PSDU to PPDU ratio: "<< psduduration.GetDouble()/ppduduration.GetDouble() << "\n";

////////////////////////PSDU PPDU combined code/////////////////////////////////////////////
  Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
  Mac48Address apAddress = dev->GetMac ()->GetAddress ();
  WifiConstPsduMap psduMap = ppdu->GetPsduMap();
  ns3::Time psduduration = Seconds(0);
  ns3::Time ppduduration = ppdu->GetTxDuration();


  // std::cout << "PSDU map size: "<< psduMap.size() <<"\n";

  if (psduMap.size () == 1 && psduMap.begin ()->second->GetAddr1 () == apAddress)
    {
      // Uplink frame
      const WifiMacHeader &hdr = psduMap.begin ()->second->GetHeader (0);

      if (txVector.GetPreambleType () == WIFI_PREAMBLE_HE_TB || txVector.GetPreambleType () == WIFI_PREAMBLE_HE_SU)
        {
          // HE TB PPDU
          if (hdr.HasData () || hdr.IsBlockAckReq ())
            {
              // std::cout << "txvector size in UL MU: "<< txVector.GetLength()<<"\n";

              Time ul_psduDurationSum = Seconds(0);
              for (auto &staIdPsdu : psduMap)
                {
                if (staIdPsdu.second->GetSize () > 0)
                  {
                    // std::cout << "Packet size of psdu for loop UL: "<< staIdPsdu.second->GetPacket()->GetSize() <<" for station: "<< staIdPsdu.first << "\n";
                  
                    // std::cout << "Packet of psdu for loop UL: "<< staIdPsdu.second->GetPacket() <<" for station: "<< staIdPsdu.first << "\n";
                  
                    // std::cout << "psdu duration for loop UL: "<< staIdPsdu.second->GetDuration() <<" for station: "<< staIdPsdu.first << "\n";
                    // std::cout << "psdu size for UL station :"<<staIdPsdu.first <<" is "<<staIdPsdu.second->GetSize ()<<"\n";
                    // int n = staIdPsdu.second->GetNMpdus();
                   
                    // int payloadsz=0;
                    // for(int i=0;i<n;i++){
                    //     payloadsz+=staIdPsdu.second->GetPayload(i)->GetSize();
                    // } 
                    
                    //  std::cout << "Payload size of psdu for loop UL: "<< payloadsz <<" for station: "<< staIdPsdu.first << "\n";
                  
                    Time txDuration = WifiPhy::CalculateTxDuration (staIdPsdu.second->GetSize (),
                                                                  txVector, m_band,
                                                                  staIdPsdu.first);
                    ul_psduDurationSum += txDuration;
                    // total_ul_psduDurationSum += txDuration;
                    // std::cout << "txduration in for loop: "<<txDuration <<"\n";
                  }
            }

            // std::cout << "HE TB PSDU sum of UL: "<< ul_psduDurationSum <<"\n";
            total_ul_psduDurationSum += ul_psduDurationSum;
            // std::cout << "total_ul_psduDurationSum: "<< total_ul_psduDurationSum<<"\n";
          
              Time txDuration = WifiPhy::CalculateTxDuration (psduMap.begin ()->second->GetSize (),
                                                              txVector, m_band,
                                                              psduMap.begin ()->first);
              m_durationOfResponsesToLastBasicTf += txDuration;
              // std::cout << "txduration outside for loop: "<<txDuration<<"\n";
              m_durationOfResponsesToLastBasicTf_all += m_durationOfResponsesToLastBasicTf;
              
              // double currRatio = txDuration.GetSeconds () / m_tfUlLength.GetSeconds ();

              // std::cout << "m_durationOfResponsesToLastBasicTf: " << m_durationOfResponsesToLastBasicTf<<"\n";
              // std::cout << "m_durationOfResponsesToLastBasicTf_all" <<m_durationOfResponsesToLastBasicTf_all<<"\n";

              psduduration = ul_psduDurationSum;

              AcIndex ac = AC_UNDEF;
              if (hdr.IsBlockAckReq ())
                {
                  CtrlBAckRequestHeader baReqHdr;
                  psduMap.begin ()->second->GetPayload (0)->PeekHeader (baReqHdr);
                  ac = QosUtilsMapTidToAc (baReqHdr.GetTidInfo ());
                }

              auto itPair = GetPairwisePerAcStats (hdr, ac);
              if (itPair.second)
                {
                  // itPair.first->second.ampduRatio.AddSample (currRatio);
                }
                m_ulMuCompleteness.AddSample(ul_psduDurationSum.GetSeconds () /
                                        ppduduration.GetSeconds ());

            }
          else if (hdr.GetType () == WIFI_MAC_QOSDATA_NULL)
            {
              m_countOfNullResponsesToLastTf++;
            }
            
        }

      if (hdr.HasData ())
        {
          auto itPair = GetPairwisePerAcStats (hdr);
          if (itPair.second)
            {
              itPair.first->second.ampduSize.AddSample (psduMap.begin ()->second->GetSize ());
            }
        }

        

        // std::cout << "UL PSDU duration: "<< psduduration << "\n";
        // std::cout << "UL PPDU duration: "<< ppduduration << "\n";

        // std::cout << "UL psduppdu ratio: "<<psduduration.GetDouble()/ppduduration.GetDouble()<<"\n";
        
    }

  // Downlink frame
  else if (psduMap.begin ()->second->GetHeader (0).IsQosData ())
    {
  
      for (auto &psdu : psduMap)
        {
          auto itPair = GetPairwisePerAcStats (psdu.second->GetHeader (0));
          if (itPair.second)
            {
              itPair.first->second.ampduSize.AddSample (psdu.second->GetSize ());
            }
        }

      Time dlMuPpduDuration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
      m_dlMuPpduDuration.AddSample (dlMuPpduDuration.ToDouble (Time::MS));

      // DL MU PPDU -- DL is data
      if (txVector.GetPreambleType () == WIFI_PREAMBLE_HE_MU || txVector.GetPreambleType() == WIFI_PREAMBLE_HE_SU)
        {
          Time dlMuPpduDuration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
          m_dlMuPpduDuration.AddSample (dlMuPpduDuration.ToDouble (Time::MS));
          Time psduDurationMax = Seconds (0);
          Time psduDurationSum = Seconds (0);

          for (auto &staIdPsdu : psduMap)
            {
              // double currRatio = 0.0;

              if (staIdPsdu.second->GetSize () > 0)
                {
                  // std::cout << "Packet size of psdu for loop DL: "<< staIdPsdu.second->GetPacket()->GetSize() <<" for station: "<< staIdPsdu.first << "\n";
                  
                  
                  // std::cout << "Packet of psdu for loop DL: "<< staIdPsdu.second->GetPacket() <<" for station: "<< staIdPsdu.first << "\n";
                  
                  // std::cout << "psdu duration for loop DL: "<< staIdPsdu.second->GetDuration() <<" for station: "<< staIdPsdu.first << "\n";
                    
                  // std::cout << "psdu size for DL station :"<<staIdPsdu.first <<" is "<<staIdPsdu.second->GetSize ()<<"\n";
                    
                  
                  Time txDuration = WifiPhy::CalculateTxDuration (staIdPsdu.second->GetSize (),
                                                                  txVector, m_band,
                                                                  staIdPsdu.first);
                  psduDurationMax = std::max(psduduration, txDuration);
                  psduDurationSum += txDuration;
                  // currRatio = txDuration.GetSeconds () / dlMuPpduDuration.GetSeconds ();
                }

              auto itPair = GetPairwisePerAcStats (staIdPsdu.second->GetHeader (0));
              if (itPair.second)
                {
                  // itPair.first->second.ampduRatio.AddSample (currRatio);
                }
                // std::cout << "currRatio: "<< currRatio<<"\n";
            }
          psduduration = psduDurationMax;
          // std::cout << "psdu duration max: "<< psduDurationMax << "\n";

          // std::cout << "dlMuPpduDuration: " << dlMuPpduDuration <<"\n";
          dlMuPpduDuration_all += dlMuPpduDuration;
          // std::cout << "dlMuPpduDuration_all: " << dlMuPpduDuration_all <<"\n";
          m_dlMuCompleteness.AddSample (psduDurationSum.GetSeconds () /
                                        (dlMuPpduDuration.GetSeconds () * psduMap.size ()));

          // std::cout << "DL PSDU duration: "<< psduduration << "\n";
          // std::cout << "DL PPDU duration: "<< ppduduration << "\n";

          // std::cout << "DL psduppdu ratio: "<<psduduration.GetDouble()/ppduduration.GetDouble()<<"\n";
        
        }
    } //if DL is trigger
  // else if (psduMap.size () == 1 && psduMap.begin ()->second->GetHeader (0).IsTrigger ())
  else if (psduMap.begin ()->second->GetHeader (0).IsTrigger ())
    {
      // std::cout << "DL is trigger"<<"\n";
      CtrlTriggerHeader trigger;
      psduMap.begin()->second->GetPayload(0)->PeekHeader(trigger);
      // auto hdrtid = psduMap.begin ()->second->GetHeader (0).GetQosTid();
      
      auto accesscategory = QosUtilsMapTidToAc (*(psduMap.begin ()->second->GetTids().begin()));

// std::cout<<" PSDU map tid: "<<unsigned(accesscategory)<<" \n";
     // std::cout<<"Header tid : "<< int(hdrtid)<<" PSDU map tid: "<<unsigned(accesscategory)<<" \n";
      // if (m_tfUlLength.IsStrictlyPositive ())
      //   {
      //     // This is not the first Trigger Frame being sent
      //     if (m_lastTfType == BASIC_TRIGGER)
      //       {
      //         if (m_durationOfResponsesToLastBasicTf.IsZero ())
      //           {
      //             // no station responded to the previous TF
      //             m_nFailedBasicTriggerFrames++;
      //           }
      //         else
      //           {
      //             // double currRatio = m_durationOfResponsesToLastBasicTf.GetSeconds () /
      //             //                    m_overallTimeGrantedByTf.GetSeconds ();
      //             // m_heTbCompleteness.AddSample (currRatio);
      //           }
      //       }
      //     else if (m_lastTfType == BSRP_TRIGGER)
      //       {
      //         if (m_countOfNullResponsesToLastTf == 0)
      //           {
      //             // no station responded to the previous TF
      //             m_nFailedBsrpTriggerFrames++;
      //           }
      //       }
      //   }

      // reset counters
      m_countOfNullResponsesToLastTf = 0;
      m_durationOfResponsesToLastBasicTf = Seconds (0);

      WifiTxVector heTbTxVector = trigger.GetHeTbTxVector (trigger.begin ()->GetAid12 ());
      // m_tfUlLength = WifiPhy::ConvertLSigLengthToHeTbPpduDuration (trigger.GetUlLength (), heTbTxVector, m_band);
      // m_overallTimeGrantedByTf = m_tfUlLength * trigger.GetNUserInfoFields ();

      if (trigger.IsBasic ())
        {
          m_lastTfType = TriggerFrameType::BASIC_TRIGGER;
          m_nBasicTriggerFramesSent++;
          // m_heTbPpduDuration.AddSample (m_tfUlLength.ToDouble (Time::MS));

          dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
          Ptr<ApWifiMac> mac = DynamicCast<ApWifiMac> (dev->GetMac ());

          Time txDuration_basic_trigger = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
                  
          total_txDuration_basic_trigger += txDuration_basic_trigger;

          for (auto &userInfo : trigger)
            {
              
              Mac48Address address = mac->GetStaList (accesscategory).at (userInfo.GetAid12 ());
              std::size_t index = MacAddressToNodeId (address) - 1;
              NS_ASSERT (index < m_nSolicitingBasicTriggerFrames.size ());
              m_nSolicitingBasicTriggerFrames[index]++;
            }
            psduduration = txDuration_basic_trigger;

            // std::cout << "BASIC PSDU duration: "<< psduduration << "\n";
            // std::cout << "BASIC PPDU duration: "<< ppduduration << "\n";

            // std::cout << "BASIC psduppdu ratio: "<<psduduration.GetDouble()/ppduduration.GetDouble()<<"\n";      

            // std::cout << "tx_duration_basic: "<< total_txDuration_basic_trigger << "\n";
        }
      else if (trigger.IsBsrp ())
        {
          
          Time txDuration_bsrp_trigger = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
                  
          total_txDuration_bsrp_trigger += txDuration_bsrp_trigger;

          m_lastTfType = TriggerFrameType::BSRP_TRIGGER;
          m_nBsrpTriggerFramesSent++;
          psduduration = txDuration_bsrp_trigger;

          // std::cout << "BSRP PSDU duration: "<< psduduration << "\n";
          // std::cout << "BSRP PPDU duration: "<< ppduduration << "\n";

          // std::cout << "BSRP psduppdu ratio: "<<psduduration.GetDouble()/ppduduration.GetDouble()<<"\n";
        
          // std::cout << "tx_duration_bsrp: "<< total_txDuration_basic_trigger << "\n";
        }
    }


}
  

void
WifiOfdmaExample::NotifyPsduForwardedDown (WifiConstPsduMap psduMap, WifiTxVector txVector, double txPowerW)
{

  std::cout << "NotifyPsduForwardedDown called"<<"\n";

  
//   Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
//   Mac48Address apAddress = dev->GetMac ()->GetAddress ();

//   std::cout << "PSDU map size: "<< psduMap.size() <<"\n";

//   if (psduMap.size () == 1 && psduMap.begin ()->second->GetAddr1 () == apAddress)
//     {
//       // Uplink frame
//       const WifiMacHeader &hdr = psduMap.begin ()->second->GetHeader (0);

//       if (txVector.GetPreambleType () == WIFI_PREAMBLE_HE_TB)
//         {
//           // HE TB PPDU
//           if (hdr.HasData () || hdr.IsBlockAckReq ())
//             {
//               std::cout << "txvector size in UL MU: "<< txVector.GetLength()<<"\n";

//               Time ul_psduDurationSum = Seconds(0);
//               for (auto &staIdPsdu : psduMap)
//                 {
//                 if (staIdPsdu.second->GetSize () > 0)
//                   {
//                     Time txDuration = WifiPhy::CalculateTxDuration (staIdPsdu.second->GetSize (),
//                                                                   txVector, m_band,
//                                                                   staIdPsdu.first);
//                     // ul_psduDurationSum += txDuration;
//                     total_ul_psduDurationSum += txDuration;
//                     std::cout << "txduration in for loop: "<<txDuration <<"\n";
//                   }
//             }

//             // std::cout << "HE TB PSDU sum of UL: "<< ul_psduDurationSum <<"\n";
//             // total_ul_psduDurationSum += ul_psduDurationSum;
//             std::cout << "total_ul_psduDurationSum: "<< total_ul_psduDurationSum<<"\n";
          
              
//               Time txDuration = WifiPhy::CalculateTxDuration (psduMap.begin ()->second->GetSize (),
//                                                               txVector, m_band,
//                                                               psduMap.begin ()->first);
//               m_durationOfResponsesToLastBasicTf += txDuration;
//               std::cout << "txduration outside for loop: "<<txDuration<<"\n";
//               m_durationOfResponsesToLastBasicTf_all += m_durationOfResponsesToLastBasicTf;
              
//               // double currRatio = txDuration.GetSeconds () / m_tfUlLength.GetSeconds ();

//               std::cout << "m_durationOfResponsesToLastBasicTf: " << m_durationOfResponsesToLastBasicTf<<"\n";
//               std::cout << "m_durationOfResponsesToLastBasicTf_all" <<m_durationOfResponsesToLastBasicTf_all<<"\n";

//               AcIndex ac = AC_UNDEF;
//               if (hdr.IsBlockAckReq ())
//                 {
//                   CtrlBAckRequestHeader baReqHdr;
//                   psduMap.begin ()->second->GetPayload (0)->PeekHeader (baReqHdr);
//                   ac = QosUtilsMapTidToAc (baReqHdr.GetTidInfo ());
//                 }

//               auto itPair = GetPairwisePerAcStats (hdr, ac);
//               if (itPair.second)
//                 {
//                   // itPair.first->second.ampduRatio.AddSample (currRatio);
//                 }
//             }
//           else if (hdr.GetType () == WIFI_MAC_QOSDATA_NULL)
//             {
//               m_countOfNullResponsesToLastTf++;
//             }
//         }

//       if (hdr.HasData ())
//         {
//           auto itPair = GetPairwisePerAcStats (hdr);
//           if (itPair.second)
//             {
//               itPair.first->second.ampduSize.AddSample (psduMap.begin ()->second->GetSize ());
//             }
//         }
        
//     }

//   // Downlink frame
//   else if (psduMap.begin ()->second->GetHeader (0).IsQosData ())
//     {
  
//       for (auto &psdu : psduMap)
//         {
//           auto itPair = GetPairwisePerAcStats (psdu.second->GetHeader (0));
//           if (itPair.second)
//             {
//               itPair.first->second.ampduSize.AddSample (psdu.second->GetSize ());
//             }
//         }

//       Time dlMuPpduDuration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
//       m_dlMuPpduDuration.AddSample (dlMuPpduDuration.ToDouble (Time::MS));

//       // DL MU PPDU -- DL is data
//       if (txVector.GetPreambleType () == WIFI_PREAMBLE_HE_MU)
//         {
//           Time dlMuPpduDuration = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
//           m_dlMuPpduDuration.AddSample (dlMuPpduDuration.ToDouble (Time::MS));
//           Time psduDurationSum = Seconds (0);

//           for (auto &staIdPsdu : psduMap)
//             {
//               double currRatio = 0.0;

//               if (staIdPsdu.second->GetSize () > 0)
//                 {
//                   Time txDuration = WifiPhy::CalculateTxDuration (staIdPsdu.second->GetSize (),
//                                                                   txVector, m_band,
//                                                                   staIdPsdu.first);
//                   psduDurationSum += txDuration;
//                   currRatio = txDuration.GetSeconds () / dlMuPpduDuration.GetSeconds ();
//                 }

//               auto itPair = GetPairwisePerAcStats (staIdPsdu.second->GetHeader (0));
//               if (itPair.second)
//                 {
//                   itPair.first->second.ampduRatio.AddSample (currRatio);
//                 }
//                 std::cout << "currRatio: "<< currRatio<<"\n";
//             }

//           std::cout << "psdu duration sum: "<< psduDurationSum << "\n";

//           std::cout << "dlMuPpduDuration: " << dlMuPpduDuration <<"\n";
//           dlMuPpduDuration_all += dlMuPpduDuration;
//           std::cout << "dlMuPpduDuration_all: " << dlMuPpduDuration_all <<"\n";
//           m_dlMuCompleteness.AddSample (psduDurationSum.GetSeconds () /
//                                         (dlMuPpduDuration.GetSeconds () * psduMap.size ()));
//         }
//     } //if DL is trigger
//   // else if (psduMap.size () == 1 && psduMap.begin ()->second->GetHeader (0).IsTrigger ())
//   else if (psduMap.begin ()->second->GetHeader (0).IsTrigger ())
//     {
//       std::cout << "DL is trigger"<<"\n";
//       CtrlTriggerHeader trigger;
//       psduMap.begin()->second->GetPayload(0)->PeekHeader(trigger);
//       // auto hdrtid = psduMap.begin ()->second->GetHeader (0).GetQosTid();
    
//       auto x= (psduMap.begin ()->second->GetTids().begin());
//       auto accesscategory = QosUtilsMapTidToAc (*x);

// // std::cout<<" PSDU map tid: "<<unsigned(accesscategory)<<" \n";
//      // std::cout<<"Header tid : "<< int(hdrtid)<<" PSDU map tid: "<<unsigned(accesscategory)<<" \n";
//       // if (m_tfUlLength.IsStrictlyPositive ())
//       //   {
//       //     // This is not the first Trigger Frame being sent
//       //     if (m_lastTfType == BASIC_TRIGGER)
//       //       {
//       //         if (m_durationOfResponsesToLastBasicTf.IsZero ())
//       //           {
//       //             // no station responded to the previous TF
//       //             m_nFailedBasicTriggerFrames++;
//       //           }
//       //         else
//       //           {
//       //             // double currRatio = m_durationOfResponsesToLastBasicTf.GetSeconds () /
//       //             //                    m_overallTimeGrantedByTf.GetSeconds ();
//       //             // m_heTbCompleteness.AddSample (currRatio);
//       //           }
//       //       }
//       //     else if (m_lastTfType == BSRP_TRIGGER)
//       //       {
//       //         if (m_countOfNullResponsesToLastTf == 0)
//       //           {
//       //             // no station responded to the previous TF
//       //             m_nFailedBsrpTriggerFrames++;
//       //           }
//       //       }
//       //   }

//       // reset counters
//       m_countOfNullResponsesToLastTf = 0;
//       m_durationOfResponsesToLastBasicTf = Seconds (0);

//       WifiTxVector heTbTxVector = trigger.GetHeTbTxVector (trigger.begin ()->GetAid12 ());
//       // m_tfUlLength = WifiPhy::ConvertLSigLengthToHeTbPpduDuration (trigger.GetUlLength (), heTbTxVector, m_band);
//       // m_overallTimeGrantedByTf = m_tfUlLength * trigger.GetNUserInfoFields ();

//       if (trigger.IsBasic ())
//         {
//           m_lastTfType = TriggerFrameType::BASIC_TRIGGER;
//           m_nBasicTriggerFramesSent++;
//           // m_heTbPpduDuration.AddSample (m_tfUlLength.ToDouble (Time::MS));

//           dev = DynamicCast<WifiNetDevice> (m_apDevices.Get (0));
//           Ptr<ApWifiMac> mac = DynamicCast<ApWifiMac> (dev->GetMac ());

//           Time txDuration_basic_trigger = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
                  
//           total_txDuration_basic_trigger += txDuration_basic_trigger;

//           for (auto &userInfo : trigger)
//             {
              
//               Mac48Address address = mac->GetStaList (accesscategory).at (userInfo.GetAid12 ());
//               std::size_t index = MacAddressToNodeId (address) - 1;
//               NS_ASSERT (index < m_nSolicitingBasicTriggerFrames.size ());
//               m_nSolicitingBasicTriggerFrames[index]++;
//             }

//             std::cout << "tx_duration_basic: "<< total_txDuration_basic_trigger << "\n";
//         }
//       else if (trigger.IsBsrp ())
//         {
          
//           Time txDuration_bsrp_trigger = WifiPhy::CalculateTxDuration (psduMap, txVector, m_band);
                  
//           total_txDuration_bsrp_trigger += txDuration_bsrp_trigger;

//           m_lastTfType = TriggerFrameType::BSRP_TRIGGER;
//           m_nBsrpTriggerFramesSent++;
//           std::cout << "tx_duration_bsrp: "<< total_txDuration_basic_trigger << "\n";
//         }
//     }
}

void
WifiOfdmaExample::NotifyTxopDuration (uint32_t nodeId, AcIndex ac, Time startTime, Time duration)
{
  auto mapIt = m_perAcStats.at (nodeId).find (ac);

  if (mapIt != m_perAcStats.at (nodeId).end ())
    {
      mapIt->second.txopDuration.AddSample (duration.ToDouble (Time::MS));
    }
}

void
WifiOfdmaExample::NotifySojournTime (std::size_t nodeId, AcIndex ac, Time sojournTime)
{
  auto mapIt = m_perAcStats.at (nodeId).find (ac);

  if (mapIt != m_perAcStats.at (nodeId).end ())
    {
      mapIt->second.queueDiscSojournTime.AddSample (sojournTime.ToDouble (Time::MS));
    }
}

void
WifiOfdmaExample::NotifyDropAfterDequeue (std::size_t nodeId, AcIndex ac,
                                          Ptr<const QueueDiscItem> item, const char *reason)
{
  auto mapIt = m_perAcStats.at (nodeId).find (ac);

  if (mapIt != m_perAcStats.at (nodeId).end ())
    {
      double sample = (Simulator::Now () - item->GetTimeStamp ()).ToDouble (Time::MS);
      mapIt->second.queueDiscSojournTime.RemoveSample (sample);
    }
}

void
WifiOfdmaExample::NotifyAppTx (std::size_t i, Ptr<const Packet> packet)
{
  if(!startThroughputcalc){startThroughputcalc=true;
  currTime = Simulator::Now().GetMilliSeconds();
  }

  prev_tx = Simulator::Now().GetMicroSeconds();
  m_flows[i].m_txBytes += packet->GetSize ();
  // m_flows[i].m_txPackets++;
  // std::cout << "Packet size at APP: " << packet->GetSize () << "\n";
  

  bool inserted;

  if (m_flows[i].m_l4Proto == Flow::UDP)
    {
      inserted =
          m_flows[i].m_inFlightPackets.insert ({packet->GetUid (), Simulator::Now ()}).second;
      NS_ABORT_MSG_IF (!inserted, "Duplicate UID " << packet->GetUid () << " with UDP?");
    }
  else
    {
      inserted =
          m_flows[i].m_inFlightPackets.insert ({m_flows[i].m_txBytes, Simulator::Now ()}).second;
      NS_ABORT_MSG_IF (!inserted,
                       "Duplicate total bytes sent " << m_flows[i].m_txBytes << " with TCP?");
    }
  // if(inserted){
    m_flows[i].packetsinFlow.m_samples.insert({packet->GetUid(),false});
    m_flows[i].m_txPackets++;

  // }
}

void
WifiOfdmaExample::NotifyAppRx (std::size_t i, Ptr<const Packet> packet, const Address &address)
{
  prev_rx = Simulator::Now().GetMicroSeconds();
  // std::cout <<"Delay of packet from App to App layer " << prev_rx - prev_tx << '\n';
  // m_flows[i].m_rxPackets++; // incrementing received pkts count if it is found in inflightpackets
  
  if(m_flows[i].m_direction == Flow::UPLINK && (++recvPackets)%1000==0){
    double thru = 0;
    prevTime=currTime;
    currTime= Simulator::Now().GetMicroSeconds();
    double timepassed = currTime-prevTime;
     
      thru= (packet->GetSize ()*8.0*1000)/(1.0*timepassed);
     
    std::cout<<"Printing throughput"<<thru<<" "<<recvPackets<<" \n";
    Throughputs.push_back(thru);

  }

  if (m_flows[i].m_l4Proto == Flow::UDP)
    {
      // there must be a unique packet with the same UID as the received packet
      auto it = m_flows[i].m_inFlightPackets.find (packet->GetUid ());
      if (it == m_flows[i].m_inFlightPackets.end ())
        {
          NS_LOG_WARN ("Packet with UID " << packet->GetUid () << " not found");
          return;
        }
      
      m_flows[i].m_rxPackets++; // incrementing received pkts count if it is found in inflightpackets
      m_flows[i].m_rxBytes += packet->GetSize ();
  


      m_flows[i].m_latency.AddSample ((Simulator::Now () - it->second).ToDouble (Time::MS));
      m_flows[i].appLatencies.AddSample({packet->GetUid(),(Simulator::Now () - it->second).ToDouble (Time::MS)});
      m_flows[i].m_inFlightPackets.erase (it);

      //////////////////////////////////////////////////////
      auto it1 = m_flows[i].packetsinFlow.m_samples.find({packet->GetUid (),false});
       if (it1 != m_flows[i].packetsinFlow.m_samples.end ()){
           m_flows[i].packetsinFlow.m_samples.erase({packet->GetUid (),false});
              m_flows[i].packetsinFlow.m_samples.insert({packet->GetUid (),true});
            
       }
    }
  else
    {

      uint64_t totalBytesReceived = DynamicCast<PacketSink> (m_sinkApps.Get (i))->GetTotalRx ();
      for (auto it = m_flows[i].m_inFlightPackets.begin (); it != m_flows[i].m_inFlightPackets.end (); )
        {
          if (it->first <= totalBytesReceived)
            {
              m_flows[i].m_latency.AddSample ((Simulator::Now () - it->second).ToDouble (Time::MS));

              m_flows[i].appLatencies.AddSample({packet->GetUid(),(Simulator::Now () - it->second).ToDouble (Time::MS)});
              it = m_flows[i].m_inFlightPackets.erase (it);
            //   auto it1 = m_flows[i].packetsinFlow.m_samples.find({packet->GetUid (),false});

            //  if (it1 != m_flows[i].packetsinFlow.m_samples.end ()){
            //     m_flows[i].packetsinFlow.m_samples.erase({packet->GetUid (),false});
            //     m_flows[i].packetsinFlow.m_samples.insert({packet->GetUid (),true});

            //  }
             m_flows[i].m_rxPackets++; // incrementing received pkts count if it is found in inflightpackets
             m_flows[i].m_rxBytes += packet->GetSize ();


             
            }
          else
            {
              it++;
            }
        }

 
   
    }
}

void
WifiOfdmaExample::NotifyEdcaEnqueue (Ptr<const WifiMpdu> mpdu)
{
  mac_tx++;
  // std::cout << "NotifyEdcaEnqueue :" << mac_tx <<'\n';
  
  m_flows[0].packetsinFlow_mac.m_samples.insert({mpdu->GetPacket()->GetUid(),false});
 
  if (!mpdu->GetHeader ().IsQosData ())
    {
      return;
    }

  
  // init a map entry if the packet's UID is not present
  auto temp = 
 
      m_inFlightPacketMap.insert ({mpdu->GetPacket ()->GetUid (), std::list<InFlightPacketInfo> ()})
          ;

  auto mapIt=temp.first;

  // if(temp.second){
      // uint32_t xsize = item->GetPacket()->GetSize();
      // std::cout << "Packet size at MAC :" << xsize << "\n";
  //         m_flows[0].m_txBytes_mac += item->GetPacket()->GetSize();
  //         m_flows[0].m_txPackets_mac++;
  //     // if(xsize == 1536){
  //     //     m_flows[0].m_txBytes_mac += item->GetPacket()->GetSize();
  //     //     m_flows[0].m_txPackets_mac++;
  //     // }
  // }

  m_flows[0].m_txBytes_mac += mpdu->GetPacket()->GetSize();

  // std::cout << "mpdu size" << mpdu->GetSize() <<"\n";
  // std::cout << "packet size" << mpdu->GetPacket()->GetSize()<<"\n";
  m_flows[0].m_txPackets_mac++;


  InFlightPacketInfo info;
  info.m_srcAddress = mpdu->GetHeader ().GetAddr2 ();
  info.m_dstAddress = mpdu->GetHeader ().GetAddr1 ();
  info.m_ac = QosUtilsMapTidToAc (mpdu->GetHeader ().GetQosTid ());
  info.m_ptrToPacket = mpdu->GetPacket ();
  info.m_edcaEnqueueTime = Simulator::Now ();
  //add suitable condition
  // tcptime.insert({info.m_edcaEnqueueTime,mpdu->GetPacket()->GetUid()});
  mapIt->second.insert (mapIt->second.end (), info);
}

void
WifiOfdmaExample::NotifyMacForwardUp (Ptr<const Packet> p)
{
  mac_rx++;
  // std::cout << "NotifyMacForwardUp" << mac_rx << '\n';
  
    auto it1 = m_flows[0].packetsinFlow_mac.m_samples.find({p->GetUid (),false});
     if (it1 != m_flows[0].packetsinFlow_mac.m_samples.end ()){
         m_flows[0].packetsinFlow_mac.m_samples.erase({p->GetUid (),false});
            m_flows[0].packetsinFlow_mac.m_samples.insert({p->GetUid (),true});
     } 

  auto mapIt = m_inFlightPacketMap.find (p->GetUid ());
  if (mapIt == m_inFlightPacketMap.end ())
    {
      NS_LOG_WARN ("No packet with UID " << p->GetUid () << " is currently in flight");
      // std::cout << "No packet with UID " << p->GetUid () << " is currently in flight" << '\n';
      return;
    }

  auto listIt = std::find_if (mapIt->second.begin (), mapIt->second.end (),
                              [&p](const InFlightPacketInfo& info)
                                { return info.m_ptrToPacket == p; });

  if (listIt == mapIt->second.end ())
    {
      NS_LOG_WARN ("Forwarding up a packet that has not been enqueued?");
      // std::cout << "Forwarding up a packet that has not been enqueued?" << '\n';
      return;
    }

  if (listIt->m_dstAddress.IsGroup ())
    {
      return;
    }

//  if(p->GetSize() == 1536){
//     m_flows[0].m_rxPackets_mac++;
//     m_flows[0].m_rxBytes_mac += p->GetSize();
//  }

  m_flows[0].m_rxPackets_mac++;
  m_flows[0].m_rxBytes_mac += p->GetSize();
  std::vector<std::map<AcIndex, PairwisePerAcStats>>::iterator vecIt;

  if (MacAddressToNodeId (listIt->m_srcAddress) == 0)
    {
      // downlink
      vecIt = std::next (m_dlPerStaAcStats.begin (), MacAddressToNodeId (listIt->m_dstAddress) - 1);
    }
  else
    {
      // uplink
      vecIt = std::next (m_ulPerStaAcStats.begin (), MacAddressToNodeId (listIt->m_srcAddress) - 1);
    }

  // NS_ABORT_MSG_IF (vecIt->find (listIt->m_ac) == vecIt->end (),
  //                  "AC " << listIt->m_ac << " not found");

  NS_ABORT_MSG_IF (listIt->m_edcaEnqueueTime.IsZero (), "Unknown EDCA enqueue time for the packet");

  if(vecIt->find (listIt->m_ac) == vecIt->end ()){
    mapIt->second.erase (listIt);
    return;
  }else{
  vecIt->at (listIt->m_ac)
      .l2Latency.AddSample ((Simulator::Now () - listIt->m_edcaEnqueueTime).ToDouble (Time::MS));
  }
  mapIt->second.erase (listIt);
}

uint32_t
WifiOfdmaExample::ContextToNodeId (const std::string &context)
{
  std::string sub = context.substr (10);
  uint32_t pos = sub.find ("/Device");
  uint32_t nodeId = atoi (sub.substr (0, pos).c_str ());
  return nodeId;
}

int
main (int argc, char *argv[])
{
  LogComponentEnable("WifiOfdmaExample", LOG_LEVEL_INFO);
 
  WifiOfdmaExample example;
  example.Config (argc, argv);
  example.Setup ();
  
  std::cout << "Uplink Status(mu or su) " << example.getULStatus()<< "\n";
  std::cout << "Inside updated folder\n";
  example.Run ();
    
  example.PrintResults (std::cout);
  example.PrintPacketLatencies (std::cout);
  
      
  Simulator::Destroy ();

  return 0;
}
