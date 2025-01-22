#include "ns3/video-stream-server.h"

#include "ns3/address-utils.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/tcp-socket.h"
#include "ns3/uinteger.h"
#include "ns3/ipv4-header.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VideoStreamServerApplication");

NS_OBJECT_ENSURE_REGISTERED(VideoStreamServer);

TypeId
VideoStreamServer::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::VideoStreamServer")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<VideoStreamServer>()
                            .AddAttribute("Interval",
                                          "The time to wait between packets",
                                          TimeValue(Seconds(0.01)),
                                          MakeTimeAccessor(&VideoStreamServer::m_interval),
                                          MakeTimeChecker())
                            .AddAttribute("Port",
                                          "Port on which we listen for incoming packets.",
                                          UintegerValue(5000),
                                          MakeUintegerAccessor(&VideoStreamServer::m_port),
                                          MakeUintegerChecker<uint16_t>())
                            .AddAttribute("MaxPacketSize",
                                          "The maximum size of a packet",
                                          UintegerValue(1400),
                                          MakeUintegerAccessor(&VideoStreamServer::m_maxPacketSize),
                                          MakeUintegerChecker<uint16_t>())
                            .AddAttribute("FrameFile",
                                          "The file that contains the video frame sizes",
                                          StringValue(""),
                                          MakeStringAccessor(&VideoStreamServer::SetFrameFile,
                                                             &VideoStreamServer::GetFrameFile),
                                          MakeStringChecker())
                            .AddAttribute("VideoLength",
                                          "The length of the video in seconds",
                                          UintegerValue(100),
                                          MakeUintegerAccessor(&VideoStreamServer::m_videoLength),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

VideoStreamServer::VideoStreamServer()
{
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    m_frameRate = 25;
    m_frameSizeList = std::vector<uint32_t>();
}

VideoStreamServer::~VideoStreamServer()
{
    NS_LOG_FUNCTION(this);
    m_socket = 0;
}

void
VideoStreamServer::DoDispose(void)
{
    NS_LOG_FUNCTION(this);
    Application::DoDispose();
}

void
VideoStreamServer::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);

        if (m_socket->Bind(local) == -1)
            NS_FATAL_ERROR("Failed to bind socket");

        m_socket->Listen();
        m_socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                    MakeCallback(&VideoStreamServer::HandleAccept, this));
    }

    m_socket->SetRecvCallback(MakeCallback(&VideoStreamServer::HandleRead, this));
}

void
VideoStreamServer::StopApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        m_socket = 0;
    }

    for (auto iter = m_clients.begin(); iter != m_clients.end(); iter++)
    {
        Simulator::Cancel(iter->second->m_sendEvent);
    }
}

void
VideoStreamServer::HandleAccept(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_FUNCTION(this << socket << InetSocketAddress::ConvertFrom(from).GetIpv4()
                         << InetSocketAddress::ConvertFrom(from).GetPort());

    socket->SetRecvCallback(MakeCallback(&VideoStreamServer::HandleRead, this));

    Address localAddress;
    socket->GetSockName(localAddress);

    uint32_t ipAddr = InetSocketAddress::ConvertFrom(from).GetIpv4().Get();

    // The first time we receive a connection from the client
    if (m_clients.find(ipAddr) == m_clients.end())
    {
        ClientInfo* newClient = new ClientInfo();
        newClient->m_sent = 0;
        newClient->m_videoLevel = 3;
        newClient->m_address = from;
        newClient->m_socket = socket;

        m_clients[ipAddr] = newClient; // save the client in the clients map

        newClient->m_sendEvent =
            Simulator::Schedule(Seconds(0.0), &VideoStreamServer::Send, this, ipAddr);
    }
}

void
VideoStreamServer::SetFrameFile(std::string frameFile)
{
    NS_LOG_FUNCTION(this << frameFile);
    m_frameFile = frameFile;
    if (frameFile != "")
    {
        std::string line;
        std::ifstream fileStream(frameFile);
        while (std::getline(fileStream, line))
        {
            int result = std::stoi(line);
            m_frameSizeList.push_back(result);
        }
    }
    NS_LOG_INFO("Frame list size: " << m_frameSizeList.size());
}

std::string
VideoStreamServer::GetFrameFile(void) const
{
    NS_LOG_FUNCTION(this);
    return m_frameFile;
}

void
VideoStreamServer::SetMaxPacketSize(uint32_t maxPacketSize)
{
    m_maxPacketSize = maxPacketSize;
}

uint32_t
VideoStreamServer::GetMaxPacketSize(void) const
{
    return m_maxPacketSize;
}

void
VideoStreamServer::Send(uint32_t ipAddress)
{
    NS_LOG_FUNCTION(this);

    uint32_t frameSize, totalFrames;
    ClientInfo* clientInfo = m_clients.at(ipAddress);

    NS_ASSERT(clientInfo->m_sendEvent.IsExpired());

    if (m_frameSizeList.empty())
    {
        frameSize = m_frameSizes[clientInfo->m_videoLevel];
        totalFrames = m_videoLength * m_frameRate;
    }
    else
    {
        frameSize = m_frameSizeList[clientInfo->m_sent] * clientInfo->m_videoLevel;
        totalFrames = m_frameSizeList.size();
    }

    // the frame might require several packets to send
    for (uint32_t i = 0; i < frameSize / m_maxPacketSize; i++)
        SendPacket(clientInfo, m_maxPacketSize);

    uint32_t remainder = frameSize % m_maxPacketSize;
    if (remainder > 0)
        SendPacket(clientInfo, remainder);

    clientInfo->m_sent += 1;
    if (clientInfo->m_sent < totalFrames)
    {
        clientInfo->m_sendEvent =
            Simulator::Schedule(m_interval, &VideoStreamServer::Send, this, ipAddress);
    }
}

void
VideoStreamServer::SendPacket(ClientInfo* client, uint32_t packetSize)
{
    NS_LOG_FUNCTION(this << client << packetSize);

    uint8_t* dataBuffer = new uint8_t[packetSize];
    sprintf((char*)dataBuffer, "%u", client->m_sent);
    Ptr<Packet> p = Create<Packet>(dataBuffer, packetSize);
    Ipv4Header ipv4Header;

    if(client->l4s){
        NS_LOG_INFO(this << " VideoStreamServer::Send: Setting ECN to ECT1");
        ipv4Header.SetEcn(Ipv4Header::ECN_ECT1);
        p->AddHeader(ipv4Header);
    }
    else{
        NS_LOG_INFO(this << " VideoStreamServer::Send: Setting ECN to Not-ECT");
        ipv4Header.SetEcn(Ipv4Header::ECN_NotECT);
        p->AddHeader(ipv4Header);
    }

    p->PeekHeader(ipv4Header);
    NS_LOG_INFO("Packet sent with ECN = " << ipv4Header.GetEcn());

    int actual = client->m_socket->Send(p);
    delete[] dataBuffer;

    if (actual < 0)
    {
        // NS_LOG_ERROR("Error while sending "
        //              << packetSize << " bytes to "
        //              << InetSocketAddress::ConvertFrom(client->m_address).GetIpv4() << " port "
        //              << InetSocketAddress::ConvertFrom(client->m_address).GetPort());
        
        return;
    }

    NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s server sent frame "
                           << client->m_sent << " and " << actual << " bytes to "
                           << InetSocketAddress::ConvertFrom(client->m_address).GetIpv4()
                           << " port "
                           << InetSocketAddress::ConvertFrom(client->m_address).GetPort());
}

void
VideoStreamServer::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    uint32_t ipAddr;
    bool checkedL4s = false;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
            break;

        ipAddr = InetSocketAddress::ConvertFrom(from).GetIpv4().Get();

        if (m_clients.find(ipAddr) == m_clients.end())
        {
            NS_LOG_ERROR("Received packet from unknown client "
                         << InetSocketAddress::ConvertFrom(from).GetIpv4());
            continue;
        }
        
        if(!checkedL4s && NrRlcUm::isL4S(packet))
        {
            m_clients.at(ipAddr)->l4s = true;
            checkedL4s = true;
            NS_LOG_INFO("Received L4S packet");
        }
        else
        {
            NS_LOG_INFO("Received non-L4S packet");
        }

        NS_LOG_INFO("Received " << packet->GetSize() << " bytes from "
                                << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                                << InetSocketAddress::ConvertFrom(from).GetPort());
    }
}

} // namespace ns3