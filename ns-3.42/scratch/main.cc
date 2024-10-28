#include "ns3/applications-module.h"
#include "ns3/config-store.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/lte-helper.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include <ns3/flow-monitor-helper.h>
#include <ns3/flow-monitor.h>
#include <ns3/point-to-point-helper.h>

#include <iostream>
#include <random>

#define CVM ConstantVelocityMobilityModel

using namespace ns3;

NodeContainer ues;
NodeContainer eNBs;
NodeContainer remoteHosts;
Ptr<Node> pgw;
Ptr<Node> sgw;
Ptr<Node> remoteHost;

// cmd line variables
int nUE=5;
int aqm = 0;
int baseDistance=2500;
int baseDelay = 50; // ms
int printNetStats = 0;

// enb related variables
int nENBs;
int maxDistance = baseDistance + 2500; // radius
double enbX = 2500.0;
double enbY = 2500.0;

int nRemoteHosts;
int duration = 20;

Ptr<UniformRandomVariable> uRand = CreateObject<UniformRandomVariable>();

Ptr<LteHelper> lte;
Ptr<PointToPointEpcHelper> core;

void setMobility();
void createRouteUEsRemoteHost();
void ScheduleCheckCourse(Ptr<MobilityModel> mobility);
void CheckCourse(std::string context, Ptr<MobilityModel> mobility);
void printStats(Ptr<FlowMonitor> monitor,
                FlowMonitorHelper& flowmon,
                Ipv4InterfaceContainer& uesIPs,
                Ipv4InterfaceContainer& internetIPs);

NS_LOG_COMPONENT_DEFINE("LtePDCPCqi");

int
main(int argc, char* argv[])
{
    LogComponentEnable("LtePdcp", LOG_LEVEL_FUNCTION);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);

    CommandLine cmd;
    cmd.AddValue("nUE", "n of user equipment", nUE);
    cmd.AddValue("stats", "print network stats", printNetStats);
    cmd.AddValue("base-dist", "the minimal distance an UE can be placed from the eNB", baseDistance);
    cmd.AddValue("base-delay", "the base delay between the core and the remote host", baseDelay);

    cmd.Parse(argc, argv);

    nENBs = 1;
    nRemoteHosts = 1;

    ues.Create(nUE);
    eNBs.Create(nENBs);
    remoteHosts.Create(nRemoteHosts);

    lte = CreateObject<LteHelper>(); // uses FriisPropagationLossModel as default
    core = CreateObject<PointToPointEpcHelper>();

    lte->SetEpcHelper(core);

    pgw = core->GetPgwNode();
    sgw = core->GetSgwNode();
    remoteHost = remoteHosts.Get(0);

    setMobility();

    NetDeviceContainer eNBsDevs = lte->InstallEnbDevice(eNBs);
    NetDeviceContainer UEsDevs = lte->InstallUeDevice(ues);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(baseDelay)));

    NetDeviceContainer internetDevices = p2p.Install(pgw, remoteHost);

    InternetStackHelper internet;
    internet.Install(remoteHosts);
    internet.Install(ues);

    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIPs = ipv4h.Assign(internetDevices);

    Ipv4InterfaceContainer uesIPs = core->AssignUeIpv4Address(UEsDevs);

    lte->Attach(UEsDevs, eNBsDevs.Get(0));

    createRouteUEsRemoteHost();

    UdpEchoServerHelper echoServer(9);

    ApplicationContainer serverApps = echoServer.Install(remoteHost);

    serverApps.Start(Seconds(4.0));
    serverApps.Stop(Seconds(20.0));

    Ipv4Address remoteHostAddr = internetIPs.GetAddress(1, 0);

    UdpEchoClientHelper echoClient(remoteHostAddr, 9);

    echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(ues.Get(0));

    clientApps.Start(Seconds(4.0));
    clientApps.Stop(Seconds(14.0));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(duration));
    Simulator::Run();

    if (printNetStats)
        printStats(monitor, flowmon, uesIPs, internetIPs);

    Simulator::Destroy();

    return 0;
}

void
printStats(Ptr<FlowMonitor> monitor,
           FlowMonitorHelper& flowmon,
           Ipv4InterfaceContainer& uesIPs,
           Ipv4InterfaceContainer& internetIPs)
{
    NS_LOG_INFO("PGW IPv4 Address: " << internetIPs.GetAddress(0));
    NS_LOG_INFO("RemoteHost IPv4 Address: " << internetIPs.GetAddress(1));
    NS_LOG_INFO("Default Gateway IPv4 Address: " << core->GetUeDefaultGatewayAddress());

    std::vector<Ipv4Address> ues_ips;
    for (int u = 0; u < nUE; ++u)
    {
        ues_ips.push_back(uesIPs.GetAddress(u));
        NS_LOG_INFO("UE " << u << " ipaddr: " << uesIPs.GetAddress(u));
    }

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        std::vector<Ipv4Address>::iterator it =
            std::find(ues_ips.begin(), ues_ips.end(), t.sourceAddress);

        // Plot only data related to the UE
        if (it != ues_ips.end())
        {
            std::cout << "Flow ID " << i->first << " (" << t.sourceAddress << " -> "
                      << t.destinationAddress << ")\n";

            double timeTaken =
                i->second.timeLastTxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds();
            double Throughput = i->second.txBytes * 8.0 / timeTaken / 1024 / 1024;

            std::cout << "  Throughput (Mbps): " << Throughput << "\n";

            std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
            std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
            std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
            std::cout << "  Rx Bytes: " << i->second.rxBytes << "\n";
            std::cout << "  Average Rx Packet Size: " << i->second.rxBytes / i->second.rxPackets
                      << "\n";
            std::cout << "  Average Tx Packet Size: " << i->second.txBytes / i->second.txPackets
                      << "\n";
            std::cout << "  Average Delay:  "
                      << i->second.delaySum.GetSeconds() / i->second.rxPackets << " s\n";
        }
    }
}

void
setMobility()
{
    uRand->SetAttribute("Min", DoubleValue(50.0));
    uRand->SetAttribute("Max", DoubleValue(100.0));

    MobilityHelper nodesMobility;
    MobilityHelper uesMobility;

    Ptr<ListPositionAllocator> PositionAlloc = CreateObject<ListPositionAllocator>();

    PositionAlloc->Add(Vector(4000.0, 1000.0, 0.0)); // Remote Host
    PositionAlloc->Add(Vector(enbX, enbY, 0.0));     // eNB
    PositionAlloc->Add(Vector(2500.0, 1000.0, 0.0)); // PGW
    PositionAlloc->Add(Vector(2500, 1500.0, 0.0));   // SGW
    nodesMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    nodesMobility.SetPositionAllocator(PositionAlloc);
    nodesMobility.Install(remoteHosts);
    nodesMobility.Install(eNBs);
    nodesMobility.Install(pgw);
    nodesMobility.Install(sgw);

    // UEs - Set position allocator for UEs relative to the eNB
    uesMobility.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                                     "X",
                                     StringValue("2500.0"),
                                     "Y",
                                     StringValue("2500.0"),
                                     "Rho",
                                     StringValue("ns3::UniformRandomVariable[Min=" + std::to_string(baseDistance) +"|Max="+ std::to_string(maxDistance) +"]"));
    uesMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    uesMobility.Install(ues);

    for (int i = 0; i < nUE; i++)
    {
        Ptr<CVM> mobility = ues.Get(i)->GetObject<CVM>();

        double newSpeed = uRand->GetValue();
        double newDirection = uRand->GetValue(0.0, 2 * M_PI);

        double x = newSpeed * cos(newDirection);
        double y = newSpeed * sin(newDirection);

        mobility->SetVelocity(Vector(x, y, 0.0));

        ScheduleCheckCourse(mobility);
    }
}

/**
 * Schedule next course check
 */
void
ScheduleCheckCourse(Ptr<MobilityModel> mobility)
{
    Simulator::Schedule(Seconds(1.0), &CheckCourse, "", mobility);
}

/**
 * Check if the UE is outside the cell range or closer to the eNB than 2500m.
 * If so, generate a new speed and direction to UE, and schedule a new check.
 */
void
CheckCourse(std::string context, Ptr<MobilityModel> mobility)
{
    Vector pos = mobility->GetPosition();
    Vector vel = mobility->GetVelocity();

    double ueX = pos.x;
    double ueY = pos.y;

    double distance = std::sqrt(std::pow(ueX - enbX, 2) + std::pow(ueY - enbY, 2));

    if (distance > maxDistance || distance < baseDistance)
    {
        // Reflect the velocity components to reverse direction
        vel.x = -vel.x;
        vel.y = -vel.y;

        NS_LOG_INFO("Adjusted UE direction to opposite");
    }

    ScheduleCheckCourse(mobility);
}

void
createRouteUEsRemoteHost()
{
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    for (uint32_t u = 0; u < ues.GetN(); ++u)
    {
        Ptr<Node> ueNode = ues.Get(u);
        Ptr<Ipv4StaticRouting> ueStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(core->GetUeDefaultGatewayAddress(), 1);
    }
}
