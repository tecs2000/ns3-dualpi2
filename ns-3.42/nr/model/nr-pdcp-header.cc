// Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only
//
// Author: Manuel Requena <manuel.requena@cttc.es>

#include "nr-pdcp-header.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrPdcpHeader");

NS_OBJECT_ENSURE_REGISTERED(NrPdcpHeader);

NrPdcpHeader::NrPdcpHeader()
    : m_ect(0x00),
      m_sequenceNumber(0xfffa)
{
}

NrPdcpHeader::~NrPdcpHeader()
{
    m_ect = 0xff;
    m_sequenceNumber = 0xfffb;
}

void
NrPdcpHeader::SetEct(uint8_t ect)
{
    m_ect = ect & 0x01;
}

void
NrPdcpHeader::SetSequenceNumber(uint16_t sequenceNumber)
{
    m_sequenceNumber = sequenceNumber & 0x0FFF;
}

uint8_t
NrPdcpHeader::GetEct() const
{
    return m_ect;
}

uint16_t
NrPdcpHeader::GetSequenceNumber() const
{
    return m_sequenceNumber;
}

TypeId
NrPdcpHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NrPdcpHeader")
                            .SetParent<Header>()
                            .SetGroupName("Nr")
                            .AddConstructor<NrPdcpHeader>();
    return tid;
}

TypeId
NrPdcpHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
NrPdcpHeader::Print(std::ostream& os) const
{
    os << "ect=" << (uint16_t)m_ect;
    os << " SN=" << m_sequenceNumber;
}

uint32_t
NrPdcpHeader::GetSerializedSize() const
{
    return 2;
}

void
NrPdcpHeader::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;

    i.WriteU8((m_ect << 7) | (m_sequenceNumber & 0x0F00) >> 8);
    i.WriteU8(m_sequenceNumber & 0x00FF);
}

uint32_t
NrPdcpHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    uint8_t byte_1;
    uint8_t byte_2;

    byte_1 = i.ReadU8();
    byte_2 = i.ReadU8();
    m_ect = (byte_1 & 0x80) >> 7;
    
    m_sequenceNumber = ((byte_1 & 0x0F) << 8) | byte_2;

    return GetSerializedSize();
}

}; // namespace ns3