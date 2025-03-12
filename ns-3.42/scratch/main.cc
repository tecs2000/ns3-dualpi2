#include "ns3/applications-module.h"
#include "ns3/buildings-module.h"
#include "ns3/config-store-module.h"
#include "ns3/netanim-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/nr-module.h"
#include "ns3/eps-bearer.h"
#include "ns3/point-to-point-module.h"

/** -------------- Topology --------------
 *                                      -- cubic remoteHost 
 * ue -   |--- gnB ---|--- pgw ---| --- |
 *                                      -- dctcp remoteHost
 */

#define VelocityModel ConstantVelocityMobilityModel

using namespace ns3;

NodeContainer uesContainer;
NodeContainer gnbContainer;
NodeContainer remoteHosts;

Ptr<Node> pgw;
Ptr<Node> sgw;
Ptr<Node> remoteHost1;
Ptr<Node> remoteHost2;

int numberUes;
int numberGnbs;
int numberRemoteHosts;

int duration = 10;
double enbX = 225.0;
double enbY = 225.0;
double radius = 600;

Time simTime = Seconds(10);

Ptr<UniformRandomVariable> uRand = CreateObject<UniformRandomVariable>();

Ptr<NrHelper> nrHelper;
Ptr<NrPointToPointEpcHelper> core;

void NotifyCqiReport(std::string context, uint16_t cellId, uint16_t rnti, uint8_t cqi);
void SetMobility();
void CheckCourse(std::string context, Ptr<MobilityModel> mob);
void BuildApps(Ipv4InterfaceContainer& ueIps, uint32_t numUes);

NS_LOG_COMPONENT_DEFINE("Temp");

int
main(int argc, char* argv[])
{

    // LogComponentEnable("NrRlcUmDualpi2", LOG_LEVEL_INFO);
    LogComponentEnable("NrRlcUm", LOG_LEVEL_INFO);
    // LogComponentEnable("DualQCoupledPiSquareQueueDisc", LOG_LEVEL_INFO);
    // LogComponentEnable("DualQCoupledPiSquareQueueDisc", LOG_LEVEL_FUNCTION);
    // LogComponentEnable("QueueDisc", LOG_LEVEL_INFO);
    // LogComponentEnable("QueueDisc", LOG_LEVEL_FUNCTION);
    // LogComponentEnable("TcpDctcp", LOG_LEVEL_ALL);
    // LogComponentEnable("TcpCubic", LOG_LEVEL_ALL);

    numberGnbs = 1;
    numberUes = 10;
    numberRemoteHosts = 2;

    NS_LOG_INFO("Creating " << numberGnbs << " gNBs" <<
                " and " << numberUes << " UEs" << 
                " and " << numberRemoteHosts << " remote hosts");
        
    remoteHosts.Create(numberRemoteHosts);
    uesContainer.Create(numberUes);
    gnbContainer.Create(numberGnbs);

    for (int i = 0; i < numberUes; i++)
        NS_LOG_DEBUG("UE " << i << " -> " << uesContainer.Get(i)->GetId());

    for (int i = 0; i < numberGnbs; i++)
        NS_LOG_DEBUG("gNB " << i << " -> " << gnbContainer.Get(i)->GetId());

    for (int i = 0; i < numberRemoteHosts; i++)
        NS_LOG_DEBUG("remoteHost " << i << " -> " << remoteHosts.Get(i)->GetId());

    uint16_t numerology = 0;
    double centralFrequency = 4e9;
    double bandwidth = 10e6;
    double total = 10;
    int64_t randomStream = 1;

    // Where we will store the output files.
    std::string simTag = "default-" + std::to_string(numberUes);
    std::string outputDir = "./";

    nrHelper = CreateObject<NrHelper>();
    core = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(core);

    // Selecting MAC scheduler (implicit default has a bug!)
    nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerTdmaRR"));
    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));

    pgw = core->GetPgwNode();
    sgw = core->GetSgwNode();
    remoteHost1 = remoteHosts.Get(0);
    remoteHost2 = remoteHosts.Get(1);

    SetMobility();

    BandwidthPartInfoPtrVector allBwps;
    CcBwpCreator ccBwpCreator;
    const uint8_t numCcPerBand = 1; 

    auto bandMask = NrHelper::INIT_PROPAGATION | NrHelper::INIT_CHANNEL;

    // Create the configuration for the CcBwpHelper. SimpleOperationBandConf creates
    // a single BWP per CC
    CcBwpCreator::SimpleOperationBandConf bandConf (centralFrequency,
                                                    bandwidth,
                                                    numCcPerBand,
                                                    BandwidthPartInfo::UMa);

    bandConf.m_numBwp = 1;

    // By using the configuration created, it is time to make the operation band
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);

    /*
     * The configured spectrum division is:
     * ------------Band1--------------|
     * ------------CC1----------------|
     * ------------BWP1---------------|
     */

    nrHelper->InitializeOperationBand(&band, bandMask);
    allBwps = CcBwpCreator::GetAllBwps({band});

    Packet::EnableChecking();
    Packet::EnablePrinting();

    /*
     * We have configured the attributes we needed. Now, install and get the 
     * pointers to the NetDevices, which contains all the NR stack:
     */

    NetDeviceContainer gnbNetDev =
        nrHelper->InstallGnbDevice(gnbContainer, allBwps);
    NetDeviceContainer ueNetDev = 
        nrHelper->InstallUeDevice(uesContainer, allBwps);

    randomStream += nrHelper->AssignStreams(gnbNetDev, randomStream);
    randomStream += nrHelper->AssignStreams(ueNetDev, randomStream);

    // Get the first netdevice (enbNetDev.Get (0)) and the first bandwidth part (0)
    // and set the attribute.
    nrHelper->GetGnbPhy(gnbNetDev.Get(0), 0)
        ->SetAttribute("Numerology", UintegerValue(numerology));
    nrHelper->GetGnbPhy(gnbNetDev.Get(0), 0)
        ->SetAttribute("TxPower", DoubleValue(total));

    // When all the configuration is done, explicitly call UpdateConfig ()
    for (auto it = gnbNetDev.Begin(); it != gnbNetDev.End(); ++it)
        DynamicCast<NrGnbNetDevice>(*it)->UpdateConfig();

    for (auto it = ueNetDev.Begin(); it != ueNetDev.End(); ++it)
        DynamicCast<NrUeNetDevice>(*it)->UpdateConfig();

    InternetStackHelper internet;
    internet.Install(remoteHosts);

    // connect the remoteHosts to pgw. Setup routing too
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.005)));

    NetDeviceContainer internetDevices1 = p2ph.Install(pgw, remoteHost1);
    NetDeviceContainer internetDevices2 = p2ph.Install(pgw, remoteHost2);

    Ipv4AddressHelper ipv4h;
    Ipv4StaticRoutingHelper ipv4RoutingHelper;

    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces1 = ipv4h.Assign(internetDevices1);

    ipv4h.SetBase("2.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces2 = ipv4h.Assign(internetDevices2);

    Ptr<Ipv4StaticRouting> remoteHostStaticRouting1 =
        ipv4RoutingHelper.GetStaticRouting(remoteHost1->GetObject<Ipv4>());
    remoteHostStaticRouting1->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    Ptr<Ipv4StaticRouting> remoteHostStaticRouting2 = 
        ipv4RoutingHelper.GetStaticRouting(remoteHost2->GetObject<Ipv4>());
    remoteHostStaticRouting2->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    internet.Install(uesContainer);

    Ipv4InterfaceContainer ueIpIface = core->AssignUeIpv4Address(ueNetDev);

    // Set the default gateway for the UEs
    for (uint32_t j = 0; j < uesContainer.GetN(); ++j)
    {
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(
            uesContainer.Get(j)->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(core->GetUeDefaultGatewayAddress(), 1);
    }

    // attach UEs to the closest gnb
    nrHelper->AttachToClosestGnb(ueNetDev, gnbNetDev);

    // //pcap files and debug for nodeList
    // internet.EnablePcapIpv4("debugUe", uesContainer);

    // ---------------------------- Application ----------------------------

    BuildApps(ueIpIface, numberUes);

    // ---------------------------- Tracing ----------------------------

    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::NrGnbNetDevice/BandwidthPartMap/*/NrGnbPhy/ReportCqiValues",
    //                 MakeCallback(&NotifyCqiReport));

    // ---------------------------- Flow Monitor ----------------------------

    FlowMonitorHelper flowmonHelper;
    NodeContainer endpointNodes;
    endpointNodes.Add(remoteHost1);
    endpointNodes.Add(remoteHost2);
    endpointNodes.Add(uesContainer);

    Ptr<ns3::FlowMonitor> monitor = flowmonHelper.Install(endpointNodes);
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.001));
    monitor->SetAttribute("JitterBinWidth", DoubleValue(0.001));
    monitor->SetAttribute("PacketSizeBinWidth", DoubleValue(20));

    Simulator::Stop(simTime);
    Simulator::Run();

    // Print per-flow statistics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    std::ofstream outFile;
    std::string filename = outputDir + "/" + simTag;
    outFile.open(filename.c_str(), std::ofstream::out | std::ofstream::trunc);

    if (!outFile.is_open())
    {
        std::cerr << "Can't open file " << filename << std::endl;
        return 1;
    }

    outFile.setf(std::ios_base::fixed);

    int j = 0;
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        std::string histOutPath = "histogram-flow-" + std::to_string(j) + ".xml"; 
        std::ofstream histOutFile(histOutPath);

        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        
        outFile << "Flow " << i->first << " (" << t.sourceAddress << ":" << t.sourcePort << " -> "
                << t.destinationAddress << ":" << t.destinationPort << ") - " << "\n";
        outFile << "  Tx Packets: " << i->second.txPackets << "\n";
        outFile << "  Rx Packets: " << i->second.rxPackets << "\n";
        outFile << "  Throughput: " << i->second.rxBytes * 8.0 / simTime.GetSeconds() / 1024 / 1024
                << " Mbps\n";
        outFile << "  Average Delay: " 
                << (i->second.rxPackets > 0 ? (i->second.delaySum.GetSeconds() / i->second.rxPackets) : 0)
                << " s\n";

        i->second.delayHistogram.SerializeToXmlStream(histOutFile, 2, "HistogramDelay");
        
        ++j;
    }

    outFile.close();

    Simulator::Destroy();

    return 0;
}

void
NotifyCqiReport(std::string context, uint16_t cellId, uint16_t rnti, uint8_t cqi)
{
    NS_LOG_INFO(context << " - CQI report from UE " << rnti << " in cell " << cellId << ": " << +cqi);
}

void
SetMobility()
{
    MobilityHelper uesMobility;
    MobilityHelper nodesMobility;

    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    positionAlloc->Add(Vector(enbX, enbY, 0.0)); // gNB
    positionAlloc->Add(Vector(enbX, enbY - 30.0, 0.0)); //pgw
    positionAlloc->Add(Vector(enbX, enbY - 10.0, 0.0)); //sgw
    positionAlloc->Add(Vector(enbX - 75.0, enbY - 50.0, 0.0)); //remoteHost

    nodesMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    nodesMobility.SetPositionAllocator(positionAlloc);
    nodesMobility.Install(gnbContainer);
    nodesMobility.Install(pgw);
    nodesMobility.Install(sgw);
    nodesMobility.Install(remoteHosts);
    
    uesMobility.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                                    "X", DoubleValue(enbX),
                                    "Y", DoubleValue(enbY),
                                    "Rho", StringValue("ns3::UniformRandomVariable[Min=150]|[Max=600]"));

    uesMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                "Bounds", RectangleValue(Rectangle(0, 750, 0, 750)));
    uesMobility.Install(uesContainer);

}

void
CheckCourse(std::string context, Ptr<MobilityModel> mob)
{
    Vector pos = mob->GetPosition();
    // NS_LOG_DEBUG("UE position: " << pos);
    Vector vel = mob->GetVelocity();

    double ueX = pos.x;
    double ueY = pos.y;

    double distance = sqrt(pow(ueX - enbX, 2) + pow(ueY - enbY, 2));

    if (distance > radius)
    {
        vel.x = -vel.x;
        vel.y = -vel.y;

        NS_LOG_INFO("UE out of course. Changing direction.");
    }

    Simulator::Schedule(Seconds(1.0), &CheckCourse, "", mob);
}

void BuildApps(Ipv4InterfaceContainer& ueIps, uint32_t numUes)
{
    uint16_t dlPortClassic = 1234;
    uint16_t dlPortL4s = 1235;

    // Assign TCP types to remote hosts
    Config::Set("/NodeList/0/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpCubic::GetTypeId())); // remote host 0
    Config::Set("/NodeList/1/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpDctcp::GetTypeId())); // remote host 1

    for (uint32_t i = 0; i < numUes; ++i)
    {
        uint32_t ueGlobalIndex = 2 + i;  // UEs start at node 2 in global list

        // Assign Cubic to UE
        Config::Set("/NodeList/" + std::to_string(ueGlobalIndex) + "/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpCubic::GetTypeId()));

        Address sinkLocalAddressClassic(InetSocketAddress(Ipv4Address::GetAny(), dlPortClassic + i));
        PacketSinkHelper dlSinkClassic("ns3::TcpSocketFactory", sinkLocalAddressClassic);
        ApplicationContainer sinkAppClassic = dlSinkClassic.Install(uesContainer.Get(i));  // Fix: Correct UE indexing
        sinkAppClassic.Start(Seconds(1.0));
        sinkAppClassic.Stop(simTime + Seconds(1.0));

        BulkSendHelper classicClient("ns3::TcpSocketFactory", InetSocketAddress(ueIps.GetAddress(i), dlPortClassic + i));
        classicClient.SetAttribute("MaxBytes", UintegerValue(0));
        classicClient.SetAttribute("Remote", AddressValue(InetSocketAddress(ueIps.GetAddress(i), dlPortClassic + i)));

        ApplicationContainer clientAppsClassic = classicClient.Install(remoteHosts.Get(0));  // Fix: Use remoteHosts.Get(0)
        clientAppsClassic.Start(Seconds(2.0));
        clientAppsClassic.Stop(simTime);

        // Assign DCTCP to UE
        Config::Set("/NodeList/" + std::to_string(ueGlobalIndex) + "/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpDctcp::GetTypeId()));

        Address sinkLocalAddressL4s(InetSocketAddress(Ipv4Address::GetAny(), dlPortL4s + i));
        PacketSinkHelper dlSinkL4s("ns3::TcpSocketFactory", sinkLocalAddressL4s);
        ApplicationContainer sinkAppL4s = dlSinkL4s.Install(uesContainer.Get(i));  // Fix: Correct UE indexing
        sinkAppL4s.Start(Seconds(1.0));
        sinkAppL4s.Stop(simTime + Seconds(1.0));

        BulkSendHelper l4sClient("ns3::TcpSocketFactory", InetSocketAddress(ueIps.GetAddress(i), dlPortL4s + i));
        l4sClient.SetAttribute("MaxBytes", UintegerValue(0));
        l4sClient.SetAttribute("Remote", AddressValue(InetSocketAddress(ueIps.GetAddress(i), dlPortL4s + i)));

        ApplicationContainer clientAppsL4s = l4sClient.Install(remoteHosts.Get(1));  // Fix: Use remoteHosts.Get(1)
        clientAppsL4s.Start(Seconds(2.0));
        clientAppsL4s.Stop(simTime);
    }
}
