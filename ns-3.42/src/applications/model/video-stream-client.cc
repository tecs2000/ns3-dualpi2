/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "video-stream-client.h"

#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/tcp-dctcp.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VideoStreamClientApplication");

NS_OBJECT_ENSURE_REGISTERED(VideoStreamClient);

TypeId
VideoStreamClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::VideoStreamClient")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<VideoStreamClient>()
                            .AddAttribute("RemoteAddress",
                                          "The destination address of the outbound packets",
                                          AddressValue(),
                                          MakeAddressAccessor(&VideoStreamClient::m_peerAddress),
                                          MakeAddressChecker())
                            .AddAttribute("RemotePort",
                                          "The destination port of the outbound packets",
                                          UintegerValue(5000),
                                          MakeUintegerAccessor(&VideoStreamClient::m_peerPort),
                                          MakeUintegerChecker<uint16_t>())
                            .AddAttribute("TCPType",
                                          "The TCP protocol to use",
                                          StringValue("Cubic"),
                                          MakeStringAccessor(&VideoStreamClient::m_protocol),
                                          MakeStringChecker());
    return tid;
}

VideoStreamClient::VideoStreamClient()
    : m_initialDelay(3),
      m_stopCounter(0),
      m_rebufferCounter(0),
      m_videoLevel(3),
      m_frameRate(25),
      m_frameSize(0),
      m_lastRecvFrame(1e6),
      m_lastBufferSize(0),
      m_currentBufferSize(0),
      m_bytesReceived(0)
{
    NS_LOG_FUNCTION(this);
}

VideoStreamClient::~VideoStreamClient()
{
    double bytesReceived_in_Mbits = (m_bytesReceived * 8) / 1024 / 1024;
    std::cout << "Application Goodput in Mb = " << bytesReceived_in_Mbits << "\n";

    NS_LOG_FUNCTION(this);
    m_socket = nullptr;
}

void
VideoStreamClient::SetRemote(Address ip, uint16_t port)
{
    NS_LOG_FUNCTION(this << ip << port);
    m_peerAddress = ip;
    m_peerPort = port;
}

void
VideoStreamClient::SetRemote(Address addr)
{
    NS_LOG_FUNCTION(this << addr);
    m_peerAddress = addr;
}

void
VideoStreamClient::DoDispose(void)
{
    NS_LOG_FUNCTION(this);
    Application::DoDispose();
}

void
VideoStreamClient::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);

        bool success = false;

        if (Ipv4Address::IsMatchingType(m_peerAddress))

            success = (m_socket->Bind() != -1) &&
                      (m_socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress),
                                                           m_peerPort)) != -1);

        else if (Ipv6Address::IsMatchingType(m_peerAddress) ||
                 Inet6SocketAddress::IsMatchingType(m_peerAddress))

            success = (m_socket->Bind6() != -1) && (m_socket->Connect(m_peerAddress) != -1);

        else if (InetSocketAddress::IsMatchingType(m_peerAddress))

            success = (m_socket->Bind() != -1) && (m_socket->Connect(m_peerAddress) != -1);

        else

            NS_ASSERT_MSG(false, "Incompatible address type: " << m_peerAddress);

        if (!success)
        {
            NS_FATAL_ERROR("Failed to bind or connect socket");
        }
    }

    NS_LOG_INFO(this << " Successfully connected to server");
    m_socket->SetRecvCallback(MakeCallback(&VideoStreamClient::HandleRead, this));
    m_sendEvent = Simulator::Schedule(MilliSeconds(1.0), &VideoStreamClient::Send, this);
    m_bufferEvent =
        Simulator::Schedule(Seconds(m_initialDelay), &VideoStreamClient::ReadFromBuffer, this);
}

void
VideoStreamClient::StopApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        m_socket = nullptr;
    }

    Simulator::Cancel(m_sendEvent);
    Simulator::Cancel(m_bufferEvent);
}

void
VideoStreamClient::Send(void)
{
    NS_LOG_FUNCTION(this << "m_protocol = " << m_protocol);
    NS_ASSERT(m_sendEvent.IsExpired());

    Ptr<Packet> packet = Create<Packet>(10);
    Ipv4Header ipv4Header;

    if(m_protocol == "TcpDctcp"){
        NS_LOG_INFO(this << " VideoStreamClient::Send: Setting ECN to ECT1");
        ipv4Header.SetEcn(Ipv4Header::ECN_ECT1);
        packet->AddHeader(ipv4Header);
    }
    else{
        NS_LOG_INFO(this << " VideoStreamClient::Send: Setting ECN to Not-ECT");
        ipv4Header.SetEcn(Ipv4Header::ECN_NotECT);
        packet->AddHeader(ipv4Header);
    }
    m_socket->Send(packet);

    if (Ipv4Address::IsMatchingType(m_peerAddress))
    {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent 10 bytes to "
                               << Ipv4Address::ConvertFrom(m_peerAddress) << " port "
                               << m_peerPort);
    }
    else if (Ipv6Address::IsMatchingType(m_peerAddress))
    {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent 10 bytes to "
                               << Ipv6Address::ConvertFrom(m_peerAddress) << " port "
                               << m_peerPort);
    }
    else if (InetSocketAddress::IsMatchingType(m_peerAddress))
    {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent 10 bytes to "
                               << InetSocketAddress::ConvertFrom(m_peerAddress).GetIpv4()
                               << " port "
                               << InetSocketAddress::ConvertFrom(m_peerAddress).GetPort());
    }
    else if (Inet6SocketAddress::IsMatchingType(m_peerAddress))
    {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent 10 bytes to "
                               << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetIpv6()
                               << " port "
                               << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetPort());
    }
}

void
VideoStreamClient::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        if (InetSocketAddress::IsMatchingType(from))
        {
            uint8_t recvData[packet->GetSize()];
            packet->CopyData(recvData, packet->GetSize());
            uint32_t frameNum;
            sscanf((char*)recvData, "%u", &frameNum);

            m_bytesReceived += packet->GetSize();

            if (frameNum == m_lastRecvFrame)
            {
                m_frameSize += packet->GetSize();
            }
            else
            {
                if (frameNum > 0)
                {
                    NS_LOG_INFO("At time "
                                << Simulator::Now().GetSeconds() << "s client received frame "
                                << frameNum - 1 << " and " << m_frameSize << " bytes from "
                                << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                                << InetSocketAddress::ConvertFrom(from).GetPort());
                }

                m_currentBufferSize++;
                m_lastRecvFrame = frameNum;
                m_frameSize = packet->GetSize();
            }

            // The rebuffering event has happend 3+ times, which suggest the client to lower the
            // video quality.
            if (m_rebufferCounter >= 3)
            {
                if (m_videoLevel > 1)
                {
                    NS_LOG_INFO("At time " << Simulator::Now().GetSeconds()
                                           << "s: Lower the video quality level!");
                    m_videoLevel--;
                    // reflect the change to the server
                    uint8_t dataBuffer[10];
                    sprintf((char*)dataBuffer, "%hu", m_videoLevel);
                    Ptr<Packet> levelPacket = Create<Packet>(dataBuffer, 10);
                    socket->Send(levelPacket, 0);
                    m_rebufferCounter = 0;
                }
            }

            // If the current buffer size supports 5+ seconds video, we can try to increase the
            // video quality level.
            if (m_currentBufferSize > 5 * m_frameRate)
            {
                if (m_videoLevel < MAX_VIDEO_LEVEL)
                {
                    m_videoLevel++;
                    // reflect the change to the server
                    uint8_t dataBuffer[10];
                    sprintf((char*)dataBuffer, "%hu", m_videoLevel);
                    Ptr<Packet> levelPacket = Create<Packet>(dataBuffer, 10);
                    socket->Send(levelPacket, 0);
                    m_currentBufferSize = m_frameRate;
                    NS_LOG_INFO("At time " << Simulator::Now().GetSeconds()
                                           << "s: Increase the video quality level to "
                                           << m_videoLevel);
                }
            }
        }
    }
}

void
VideoStreamClient::ReadFromBuffer(void)
{
    if (m_currentBufferSize < m_frameRate)
    {
        if (m_lastBufferSize == m_currentBufferSize)
        {
            m_stopCounter++;
            // If the counter reaches 3, which means the client has been waiting for 3 sec, and no
            // packets arrived. In this case, we think the video streaming has finished, and there
            // is no need to schedule the event.
            if (m_stopCounter < 3)
            {
                m_bufferEvent =
                    Simulator::Schedule(Seconds(1.0), &VideoStreamClient::ReadFromBuffer, this);
            }
        }
        else
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds()
                                   << " s: Not enough frames in the buffer, rebuffering!");
            m_stopCounter = 0; // reset the stopCounter
            m_rebufferCounter++;
            m_bufferEvent =
                Simulator::Schedule(Seconds(1.0), &VideoStreamClient::ReadFromBuffer, this);
        }

        m_lastBufferSize = m_currentBufferSize;
    }
    else
    {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds()
                               << " s: Play video frames from the buffer");

        m_stopCounter = 0;     // reset the stopCounter
        m_rebufferCounter = 0; // reset the rebufferCounter
        m_currentBufferSize -= m_frameRate;

        m_bufferEvent = Simulator::Schedule(Seconds(1.0), &VideoStreamClient::ReadFromBuffer, this);
        m_lastBufferSize = m_currentBufferSize;
    }
}

} // namespace ns3