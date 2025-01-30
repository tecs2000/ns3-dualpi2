// Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
//
// SPDX-License-Identifier: GPL-2.0-only
//
// Author: Manuel Requena <manuel.requena@cttc.es>

#include "nr-rlc-um-dualpi2.h"

#include "nr-rlc-header.h"
#include "nr-rlc-sdu-status-tag.h"
#include "nr-rlc-tag.h"
#include "nr-pdcp-header.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-header.h"
#include "ns3/log.h"
#include "ns3/object.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrRlcUmDualpi2");

NS_OBJECT_ENSURE_REGISTERED(NrRlcUmDualpi2);

NrRlcUmDualpi2::NrRlcUmDualpi2()
    : m_maxAqmBufferSize(10 * 1024),
      m_aqmBufferSize(0),
      m_sequenceNumber(0),
      m_vrUr(0),
      m_vrUx(0),
      m_vrUh(0),
      m_windowSize(512),
      m_expectedSeqNumber(0)
{
    NS_LOG_FUNCTION(this);
    aqm = CreateObject<DualQCoupledPiSquareQueueDisc>();
    aqm->SetQueueLimit(10); // set to 10 in accordance to m_maxAqmBufferSize
    aqm->Initialize();
    m_reassemblingState = WAITING_S0_FULL;
}

NrRlcUmDualpi2::~NrRlcUmDualpi2()
{
    NS_LOG_FUNCTION(this);
    
    uint32_t aqmDrops = aqm->GetStats().forcedDrop + aqm->GetStats().unforcedClassicDrop;
    uint32_t aqmMarks = aqm->GetStats().unforcedClassicMark + aqm->GetStats().unforcedL4SMark;
    
    NS_LOG_INFO("RLC Dualpi2 AQM stats\n  Drops: " << aqmDrops << "\n  Marks: " << aqmMarks);
}

TypeId
NrRlcUmDualpi2::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NrRlcUmDualpi2")
            .SetParent<NrRlc>()
            .SetGroupName("Nr")
            .AddConstructor<NrRlcUmDualpi2>()
            .AddAttribute("MaxTxBufferSize",
                          "Maximum Size of the Transmission Buffer (in Bytes)",
                          UintegerValue(10 * 1024), // 10 pkts of 1024 bytes
                          MakeUintegerAccessor(&NrRlcUmDualpi2::m_maxAqmBufferSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ReorderingTimer",
                          "Value of the t-Reordering timer (See section 7.3 of 3GPP TS 36.322)",
                          TimeValue(MilliSeconds(100)),
                          MakeTimeAccessor(&NrRlcUmDualpi2::m_reorderingTimerValue),
                          MakeTimeChecker())
            .AddAttribute(
                "EnablePdcpDiscarding",
                "Whether to use the PDCP discarding, i.e., perform discarding at the moment "
                "of passing the PDCP SDU to RLC)",
                BooleanValue(true),
                MakeBooleanAccessor(&NrRlcUmDualpi2::m_enablePdcpDiscarding),
                MakeBooleanChecker())
            .AddAttribute("DiscardTimerMs",
                          "Discard timer in milliseconds to be used to discard packets. "
                          "If set to 0 then packet delay budget will be used as the discard "
                          "timer value, otherwise it will be used this value.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&NrRlcUmDualpi2::m_discardTimerMs),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

void
NrRlcUmDualpi2::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_reorderingTimer.Cancel();
    m_rbsTimer.Cancel();

    NrRlc::DoDispose();
}

/**
 * RLC SAP
 */
bool
NrRlcUmDualpi2::isL4S(Ptr<Packet> packet)
{
    NrPdcpHeader pdcpHeader;
    if (packet->PeekHeader(pdcpHeader))
    {
        if (pdcpHeader.GetEct() == 1)
            return true;
        
        return false;
    }
    std::cout << "NrPdcpHeader not found" << std::endl;
    return false;
}

void
NrRlcUmDualpi2::DoTransmitPdcpPdu(Ptr<Packet> p)
{
    NS_LOG_FUNCTION(this << m_rnti << (uint32_t)m_lcid << p->GetSize());

    int aqmBytes = aqm->GetQueueSize();
    if (aqmBytes + p->GetSize() <= m_maxAqmBufferSize)
    {
        if (m_enablePdcpDiscarding)
        {
            // discart the packet
            uint32_t headOfLineDelayInMs = 0;
            uint32_t discardTimerMs =
                (m_discardTimerMs > 0) ? m_discardTimerMs : m_packetDelayBudgetMs;

            if (aqmBytes > 0)
                headOfLineDelayInMs = (Simulator::Now() - aqm->GetQueueDelay()).GetMilliSeconds();

            NS_LOG_DEBUG("head of line delay in MS:" << headOfLineDelayInMs);
            if (headOfLineDelayInMs > discardTimerMs)
            {
                NS_LOG_INFO("Tx HOL is higher than this packet can allow. RLC SDU discarded");
                NS_LOG_DEBUG("headOfLineDelayInMs   = " << headOfLineDelayInMs);
                NS_LOG_DEBUG("m_packetDelayBudgetMs = " << m_packetDelayBudgetMs);
                NS_LOG_DEBUG("packet size           = " << p->GetSize());
                m_txDropTrace(p);
                return;
            }
            
            /** Store PDCP PDU */
            NS_LOG_INFO("Adding RLC SDU to aqm after adding NrRlcSduStatusTag: FULL_SDU");

            NrRlcSduStatusTag aqmTag;
            aqmTag.SetStatus(NrRlcSduStatusTag::FULL_SDU);
            p->AddPacketTag(aqmTag);

            // enqueue the packet to the AQM
            Ptr<QueueDiscItem> item;

            if (isL4S(p))
            {
                NS_LOG_INFO("RLC Dualpi2 received a L4S packet");
                item = Create<DualQueueL4SQueueDiscItem>(p, dest, 0);
            }

            else
            {
                NS_LOG_INFO("RLC Dualpi2 received a Classic packet");
                item = Create<DualQueueClassicQueueDiscItem>(p, dest, 0);
            }

            item->SetTimeStamp(Simulator::Now());
            aqm->Enqueue(item);

            NS_LOG_LOGIC("packets in the AQM buffer  = " << aqm->GetQueueSize());
            NS_LOG_LOGIC("AQM size in bytes          = " << aqm->GetQueueSizeBytes());
        }
    }
    else
    {
        // Discard full RLC SDU
        NS_LOG_INFO("AQM buffer is full. RLC SDU discarded");
        NS_LOG_LOGIC("MaxTxBufferSize  = " << m_maxAqmBufferSize);
        NS_LOG_LOGIC("aqmBufferSize    = " << aqm->GetQueueSizeBytes());
        NS_LOG_LOGIC("packet size      = " << p->GetSize());
        m_txDropTrace(p);
    }

    /** Report Buffer Status */
    DoReportBufferStatus();
    m_rbsTimer.Cancel();
}

/**
 * MAC SAP
 */

void
NrRlcUmDualpi2::DoNotifyTxOpportunity(NrMacSapUser::TxOpportunityParameters txOpParams)
{
    NS_LOG_FUNCTION(this << m_rnti << (uint32_t)m_lcid << txOpParams.bytes);
    NS_LOG_INFO("RLC Dualpi2 layer is preparing data for the following Tx opportunity of "
                << txOpParams.bytes << " bytes for RNTI=" << m_rnti << ", LCID=" << (uint32_t)m_lcid
                << ", CCID=" << (uint32_t)txOpParams.componentCarrierId << ", HARQ ID="
                << (uint32_t)txOpParams.harqId << ", MIMO Layer=" << (uint32_t)txOpParams.layer);

    if (txOpParams.bytes <= 2)
    {
        // Stingy MAC: Header fix part is 2 bytes, we need more bytes for the data
        NS_LOG_INFO("TX opportunity too small - Only " << txOpParams.bytes << " bytes");
        return;
    }

    bool l4s;
    Ptr<Packet> p = Create<Packet>();
    NrRlcHeader aqmRlcHeader;
    Time aqmFirstSegmentTime;
    Ptr<Packet> aqmFirstSegment;
    uint32_t aqmNextSegmentSize = txOpParams.bytes - 2;
    uint32_t aqmNextSegmentId = 1;
    uint32_t aqmDataFieldAddedSize = 0;
    std::vector<Ptr<Packet>> aqmDataField;

    if (aqm->GetQueueSize() == 0){
        NS_LOG_LOGIC("No data pending in the AQM, skipping...");
        return;
    }

    NS_LOG_LOGIC("SDUs in the AQM  = " << aqm->GetQueueSize());

    Ptr<QueueDiscItem> aqmItem = aqm->Dequeue();
    l4s = aqmItem->IsL4S();
    aqmFirstSegment = aqmItem->GetPacket();
    aqmFirstSegmentTime = aqmItem->GetTimeStamp();

    NS_LOG_LOGIC("First SDU buffer  = " << aqmFirstSegment);
    NS_LOG_LOGIC("First SDU size    = " << aqmFirstSegment->GetSize());
    NS_LOG_LOGIC("Next segment size = " << aqmNextSegmentSize);
    NS_LOG_LOGIC("Remove SDU from AQM");
    NS_LOG_LOGIC("AQM buffer size      = " << aqm->GetQueueSizeBytes());

    while (aqmFirstSegment && (aqmFirstSegment->GetSize() > 0) && (aqmNextSegmentSize > 0))
    {
        NS_LOG_LOGIC("WHILE ( aqmFirstSegment && aqmFirstSegment->GetSize > 0 && "
                     "aqmNextSegmentSize > 0 )");
        NS_LOG_LOGIC("    aqmFirstSegment size  = " << aqmFirstSegment->GetSize());
        NS_LOG_LOGIC("    aqmNextSegmentSize = " << aqmNextSegmentSize);
        if ((aqmFirstSegment->GetSize() > aqmNextSegmentSize) ||
            // Segment larger than 2047 octets can only be mapped to the end of the Data field
            (aqmFirstSegment->GetSize() > 2047))
        {
            // Take the minimum size, due to the 2047-bytes 3GPP exception
            // This exception is due to the length of the LI field (just 11 bits)
            uint32_t aqmCurrSegmentSize = std::min(aqmFirstSegment->GetSize(), aqmNextSegmentSize);

            NS_LOG_LOGIC("    IF ( aqmFirstSegment > aqmNextSegmentSize ||");
            NS_LOG_LOGIC("         aqmFirstSegment > 2047 )");

            // Segment aqmFirstSegment and
            // Give back the remaining segment to the transmission buffer
            Ptr<Packet> aqmNewSegment = aqmFirstSegment->CreateFragment(0, aqmCurrSegmentSize);
            NS_LOG_LOGIC("    aqmNewSegment size   = " << aqmNewSegment->GetSize());

            // Status tag of the new and remaining segments
            // Note: This is the only place where a PDU is segmented and
            // therefore its status can change
            NrRlcSduStatusTag aqmOldTag;
            NrRlcSduStatusTag aqmNewTag;
            aqmFirstSegment->RemovePacketTag(aqmOldTag);
            aqmNewSegment->RemovePacketTag(aqmNewTag);

            if (aqmOldTag.GetStatus() == NrRlcSduStatusTag::FULL_SDU)
            {
                aqmNewTag.SetStatus(NrRlcSduStatusTag::FIRST_SEGMENT);
                aqmOldTag.SetStatus(NrRlcSduStatusTag::LAST_SEGMENT);
            }
            else if (aqmOldTag.GetStatus() == NrRlcSduStatusTag::LAST_SEGMENT)
            {
                aqmNewTag.SetStatus(NrRlcSduStatusTag::MIDDLE_SEGMENT);
            }

            // Give back the remaining segment to the transmission buffer
            aqmFirstSegment->RemoveAtStart(aqmCurrSegmentSize);

            NS_LOG_LOGIC(
                "    firstSegment size (after RemoveAtStart) = " << aqmFirstSegment->GetSize());

            if (aqmFirstSegment->GetSize() > 0)
            {
                aqmFirstSegment->AddPacketTag(aqmOldTag);

                uint32_t itemSize = 0;
                Ptr<QueueDiscItem> item;

                if (l4s)
                    item = Create<DualQueueL4SQueueDiscItem>(aqmFirstSegment, dest, 0);
                else
                    item = Create<DualQueueClassicQueueDiscItem>(aqmFirstSegment, dest, 0);

                itemSize = item->GetSize();
                aqm->Requeue(item);

                NS_LOG_LOGIC("    AQM: Give back the remaining segment");
                NS_LOG_LOGIC("    AQM size = " << aqm->GetQueueSize());
                NS_LOG_LOGIC("    Front buffer size = " << itemSize);
                NS_LOG_LOGIC("    aqmBufferSize = " << aqm->GetQueueSizeBytes());
            }
            else
            {
                // Whole segment was taken, so adjust tag
                if (aqmNewTag.GetStatus() == NrRlcSduStatusTag::FIRST_SEGMENT)
                {
                    aqmNewTag.SetStatus(NrRlcSduStatusTag::FULL_SDU);
                }
                else if (aqmNewTag.GetStatus() == NrRlcSduStatusTag::MIDDLE_SEGMENT)
                {
                    aqmNewTag.SetStatus(NrRlcSduStatusTag::LAST_SEGMENT);
                }
            }

            // Segment is completely taken or
            // the remaining segment is given back to the transmission buffer
            aqmFirstSegment = nullptr;

            // Put status tag once it has been adjusted
            aqmNewSegment->AddPacketTag(aqmNewTag);

            // Add Segment to Data field
            aqmDataFieldAddedSize = aqmNewSegment->GetSize();
            aqmDataField.push_back(aqmNewSegment);
            aqmNewSegment = nullptr;

            // ExtensionBit (Next_Segment - 1) = 0
            aqmRlcHeader.PushExtensionBit(NrRlcHeader::DATA_FIELD_FOLLOWS);

            // no LengthIndicator for the last one
            aqmNextSegmentSize -= aqmDataFieldAddedSize;
            aqmNextSegmentId++;

            // nextSegmentSize MUST be zero (only if segment is smaller or equal to 2047)

            // (NO more segments) → exit
            // break;
        }
        else if ((aqmNextSegmentSize - aqmFirstSegment->GetSize() <= 2) || aqm->GetQueueSize() == 0)
        {
            NS_LOG_LOGIC("    IF aqmNextSegmentSize - aqmFirstSegment->GetSize () <= 2 || "
                         "aqm->GetQueueSize() == 0");
            // Add txBuffer.FirstBuffer to DataField
            aqmDataFieldAddedSize = aqmFirstSegment->GetSize();
            aqmDataField.push_back(aqmFirstSegment);
            aqmFirstSegment = nullptr;

            // ExtensionBit (Next_Segment - 1) = 0
            aqmRlcHeader.PushExtensionBit(NrRlcHeader::DATA_FIELD_FOLLOWS);

            // no LengthIndicator for the last one
            aqmNextSegmentSize -= aqmDataFieldAddedSize;
            aqmNextSegmentId++;

            NS_LOG_LOGIC("        SDUs in AQM buffer  = " << aqm->GetQueueSize());
            NS_LOG_LOGIC("        Next segment size   = " << aqmNextSegmentSize);

            // nextSegmentSize <= 2 (only if txBuffer is not empty)

            // (NO more segments) → exit
            // break;
        }
        else // (aqmFirstSegment->GetSize () < aqmNextSegmentSize) && (aqm->GetQueueSize() > 0)
        {
            NS_LOG_LOGIC("    IF aqmFirstSegment < NextSegmentSize && aqm->GetQueueSize() > 0");
            // Add txBuffer.FirstBuffer to DataField
            aqmDataFieldAddedSize = aqmFirstSegment->GetSize();
            aqmDataField.push_back(aqmFirstSegment);

            // ExtensionBit (Next_Segment - 1) = 1
            aqmRlcHeader.PushExtensionBit(NrRlcHeader::E_LI_FIELDS_FOLLOWS);

            // LengthIndicator (Next_Segment)  = txBuffer.FirstBuffer.length()
            aqmRlcHeader.PushLengthIndicator(aqmFirstSegment->GetSize());

            aqmNextSegmentSize -= ((aqmNextSegmentId % 2) ? (2) : (1)) + aqmDataFieldAddedSize;
            aqmNextSegmentId++;

            NS_LOG_LOGIC("        SDUs in AQM  = " << aqm->GetQueueSize());
            NS_LOG_LOGIC("        Next segment size = " << aqmNextSegmentSize);
            NS_LOG_LOGIC("        Remove SDU from AQM");

            // (more segments)
            Ptr<QueueDiscItem> aqmItem = aqm->Dequeue();
            aqmFirstSegment = aqmItem->GetPacket();
            aqmFirstSegmentTime = aqmItem->GetTimeStamp();

            NS_LOG_LOGIC("        aqmBufferSize = " << aqm->GetQueueSize());
        }
    }

    // Build RLC header
    aqmRlcHeader.SetSequenceNumber(m_sequenceNumber++);

    // Build RLC PDU with DataField and Header
    auto it = aqmDataField.begin();

    uint8_t aqmFramingInfo = 0;

    // FIRST SEGMENT
    NrRlcSduStatusTag aqmTag;
    NS_ASSERT_MSG((*it)->PeekPacketTag(aqmTag), "NrRlcSduStatusTag is missing");
    (*it)->PeekPacketTag(aqmTag);
    if ((aqmTag.GetStatus() == NrRlcSduStatusTag::FULL_SDU) ||
        (aqmTag.GetStatus() == NrRlcSduStatusTag::FIRST_SEGMENT))
    {
        aqmFramingInfo |= NrRlcHeader::FIRST_BYTE;
    }
    else
    {
        aqmFramingInfo |= NrRlcHeader::NO_FIRST_BYTE;
    }

    while (it < aqmDataField.end())
    {
        NS_LOG_LOGIC("Adding SDU/segment to packet, length = " << (*it)->GetSize());

        NS_ASSERT_MSG((*it)->PeekPacketTag(aqmTag), "NrRlcSduStatusTag is missing");
        (*it)->RemovePacketTag(aqmTag);
        if (p->GetSize() > 0)
        {
            p->AddAtEnd(*it);
        }
        else
        {
            p = (*it);
        }
        it++;
    }

    // LAST SEGMENT (Note: There could be only one and be the first one)
    it--;
    if ((aqmTag.GetStatus() == NrRlcSduStatusTag::FULL_SDU) ||
        (aqmTag.GetStatus() == NrRlcSduStatusTag::LAST_SEGMENT))
    {
        aqmFramingInfo |= NrRlcHeader::LAST_BYTE;
    }
    else
    {
        aqmFramingInfo |= NrRlcHeader::NO_LAST_BYTE;
    }

    aqmRlcHeader.SetFramingInfo(aqmFramingInfo);

    NS_LOG_LOGIC("RLC Dualpi2 header: " << aqmRlcHeader);
    p->AddHeader(aqmRlcHeader);

    // Sender timestamp
    NrRlcTag aqmRlcTag(Simulator::Now());
    p->AddByteTag(aqmRlcTag, 1, aqmRlcHeader.GetSerializedSize());
    m_txPdu(m_rnti, m_lcid, p->GetSize());

    // Send RLC PDU to MAC layer
    NrMacSapProvider::TransmitPduParameters params;
    params.pdu = p;
    params.rnti = m_rnti;
    params.lcid = m_lcid;
    params.layer = txOpParams.layer;
    params.harqProcessId = txOpParams.harqId;
    params.componentCarrierId = txOpParams.componentCarrierId;

    NS_LOG_INFO("Forward RLC Dualpi2 PDU to MAC Layer");
    m_macSapProvider->TransmitPdu(params);

    if (aqm->GetQueueSize() != 0)
    {
        m_rbsTimer.Cancel();
        m_rbsTimer = Simulator::Schedule(MilliSeconds(10), &NrRlcUmDualpi2::ExpireRbsTimer, this);
    }
}

void
NrRlcUmDualpi2::DoNotifyHarqDeliveryFailure()
{
    NS_LOG_FUNCTION(this);
}

void
NrRlcUmDualpi2::DoReceivePdu(NrMacSapUser::ReceivePduParameters rxPduParams)
{
    NS_LOG_FUNCTION(this << m_rnti << (uint32_t)m_lcid << rxPduParams.p->GetSize());

    // Receiver timestamp
    NrRlcTag rlcTag;
    Time delay;

    bool ret = rxPduParams.p->FindFirstMatchingByteTag(rlcTag);
    NS_ASSERT_MSG(ret, "NrRlcTag is missing");

    delay = Simulator::Now() - rlcTag.GetSenderTimestamp();
    m_rxPdu(m_rnti, m_lcid, rxPduParams.p->GetSize(), delay.GetNanoSeconds());

    // 5.1.2.2 Receive operations

    // Get RLC header parameters
    NrRlcHeader rlcHeader;
    rxPduParams.p->PeekHeader(rlcHeader);
    NS_LOG_LOGIC("RLC Dualpi2 header: " << rlcHeader);
    nr::SequenceNumber10 seqNumber = rlcHeader.GetSequenceNumber();

    // 5.1.2.2.1 General
    // The receiving UM RLC entity shall maintain a reordering window according to state variable
    // VR(UH) as follows:
    // - a SN falls within the reordering window if (VR(UH) - UM_Window_Size) <= SN < VR(UH);
    // - a SN falls outside of the reordering window otherwise.
    // When receiving an UMD PDU from lower layer, the receiving UM RLC entity shall:
    // - either discard the received UMD PDU or place it in the reception buffer (see sub
    // clause 5.1.2.2.2);
    // - if the received UMD PDU was placed in the reception buffer:
    // - update state variables, reassemble and deliver RLC SDUs to upper layer and start/stop
    // t-Reordering as needed (see sub clause 5.1.2.2.3); When t-Reordering expires, the receiving
    // UM RLC entity shall:
    // - update state variables, reassemble and deliver RLC SDUs to upper layer and start
    // t-Reordering as needed (see sub clause 5.1.2.2.4).

    // 5.1.2.2.2 Actions when an UMD PDU is received from lower layer
    // When an UMD PDU with SN = x is received from lower layer, the receiving UM RLC entity shall:
    // - if VR(UR) < x < VR(UH) and the UMD PDU with SN = x has been received before; or
    // - if (VR(UH) - UM_Window_Size) <= x < VR(UR):
    //    - discard the received UMD PDU;
    // - else:
    //    - place the received UMD PDU in the reception buffer.

    NS_LOG_LOGIC("VR(UR) = " << m_vrUr);
    NS_LOG_LOGIC("VR(UX) = " << m_vrUx);
    NS_LOG_LOGIC("VR(UH) = " << m_vrUh);
    NS_LOG_LOGIC("SN = " << seqNumber);

    m_vrUr.SetModulusBase(m_vrUh - m_windowSize);
    m_vrUh.SetModulusBase(m_vrUh - m_windowSize);
    seqNumber.SetModulusBase(m_vrUh - m_windowSize);

    if (((m_vrUr < seqNumber) && (seqNumber < m_vrUh) &&
         (m_rxBuffer.count(seqNumber.GetValue()) > 0)) ||
        (((m_vrUh - m_windowSize) <= seqNumber) && (seqNumber < m_vrUr)))
    {
        NS_LOG_LOGIC("PDU discarded");
        rxPduParams.p = nullptr;
        return;
    }
    else
    {
        NS_LOG_LOGIC("Place PDU in the reception buffer");
        m_rxBuffer[seqNumber.GetValue()] = rxPduParams.p;
    }

    // 5.1.2.2.3 Actions when an UMD PDU is placed in the reception buffer
    // When an UMD PDU with SN = x is placed in the reception buffer, the receiving UM RLC entity
    // shall:

    // - if x falls outside of the reordering window:
    //    - update VR(UH) to x + 1;
    //    - reassemble RLC SDUs from any UMD PDUs with SN that falls outside of the reordering
    //    window, remove
    //      RLC headers when doing so and deliver the reassembled RLC SDUs to upper layer in
    //      ascending order of the RLC SN if not delivered before;
    //    - if VR(UR) falls outside of the reordering window:
    //        - set VR(UR) to (VR(UH) - UM_Window_Size);

    if (!IsInsideReorderingWindow(seqNumber))
    {
        NS_LOG_LOGIC("SN is outside the reordering window");

        m_vrUh = seqNumber + 1;
        NS_LOG_LOGIC("New VR(UH) = " << m_vrUh);

        ReassembleOutsideWindow();

        if (!IsInsideReorderingWindow(m_vrUr))
        {
            m_vrUr = m_vrUh - m_windowSize;
            NS_LOG_LOGIC("VR(UR) is outside the reordering window");
            NS_LOG_LOGIC("New VR(UR) = " << m_vrUr);
        }
    }

    // - if the reception buffer contains an UMD PDU with SN = VR(UR):
    //    - update VR(UR) to the SN of the first UMD PDU with SN > current VR(UR) that has not been
    //    received;
    //    - reassemble RLC SDUs from any UMD PDUs with SN < updated VR(UR), remove RLC headers when
    //    doing
    //      so and deliver the reassembled RLC SDUs to upper layer in ascending order of the RLC SN
    //      if not delivered before;

    if (m_rxBuffer.count(m_vrUr.GetValue()) > 0)
    {
        NS_LOG_LOGIC("Reception buffer contains SN = " << m_vrUr);

        uint16_t newVrUr;
        nr::SequenceNumber10 oldVrUr = m_vrUr;

        auto it = m_rxBuffer.find(m_vrUr.GetValue());
        newVrUr = (it->first) + 1;
        while (m_rxBuffer.count(newVrUr) > 0)
        {
            newVrUr++;
        }
        m_vrUr = newVrUr;
        NS_LOG_LOGIC("New VR(UR) = " << m_vrUr);

        ReassembleSnInterval(oldVrUr, m_vrUr);
    }

    // m_vrUh can change previously, set new modulus base
    // for the t-Reordering timer-related comparisons
    m_vrUr.SetModulusBase(m_vrUh - m_windowSize);
    m_vrUx.SetModulusBase(m_vrUh - m_windowSize);
    m_vrUh.SetModulusBase(m_vrUh - m_windowSize);

    // - if t-Reordering is running:
    //    - if VR(UX) <= VR(UR); or
    //    - if VR(UX) falls outside of the reordering window and VR(UX) is not equal to VR(UH)::
    //        - stop and reset t-Reordering;
    if (m_reorderingTimer.IsPending())
    {
        NS_LOG_LOGIC("Reordering timer is running");

        if ((m_vrUx <= m_vrUr) || ((!IsInsideReorderingWindow(m_vrUx)) && (m_vrUx != m_vrUh)))
        {
            NS_LOG_LOGIC("Stop reordering timer");
            m_reorderingTimer.Cancel();
        }
    }

    // - if t-Reordering is not running (includes the case when t-Reordering is stopped due to
    // actions above):
    //    - if VR(UH) > VR(UR):
    //        - start t-Reordering;
    //        - set VR(UX) to VR(UH).
    if (!m_reorderingTimer.IsPending())
    {
        NS_LOG_LOGIC("Reordering timer is not running");

        if (m_vrUh > m_vrUr)
        {
            NS_LOG_LOGIC("VR(UH) > VR(UR)");
            NS_LOG_LOGIC("Start reordering timer");
            m_reorderingTimer =
                Simulator::Schedule(m_reorderingTimerValue, &NrRlcUmDualpi2::ExpireReorderingTimer, this);
            m_vrUx = m_vrUh;
            NS_LOG_LOGIC("New VR(UX) = " << m_vrUx);
        }
    }
}

bool
NrRlcUmDualpi2::IsInsideReorderingWindow(nr::SequenceNumber10 seqNumber)
{
    NS_LOG_FUNCTION(this << seqNumber);
    NS_LOG_LOGIC("Reordering Window: " << m_vrUh << " - " << m_windowSize << " <= " << seqNumber
                                       << " < " << m_vrUh);

    m_vrUh.SetModulusBase(m_vrUh - m_windowSize);
    seqNumber.SetModulusBase(m_vrUh - m_windowSize);

    if (((m_vrUh - m_windowSize) <= seqNumber) && (seqNumber < m_vrUh))
    {
        NS_LOG_LOGIC(seqNumber << " is INSIDE the reordering window");
        return true;
    }
    else
    {
        NS_LOG_LOGIC(seqNumber << " is OUTSIDE the reordering window");
        return false;
    }
}

void
NrRlcUmDualpi2::ReassembleAndDeliver(Ptr<Packet> packet)
{
    NrRlcHeader rlcHeader;
    packet->RemoveHeader(rlcHeader);
    uint8_t framingInfo = rlcHeader.GetFramingInfo();
    nr::SequenceNumber10 currSeqNumber = rlcHeader.GetSequenceNumber();
    bool expectedSnLost;

    if (currSeqNumber != m_expectedSeqNumber)
    {
        expectedSnLost = true;
        NS_LOG_LOGIC("There are losses. Expected SN = " << m_expectedSeqNumber
                                                        << ". Current SN = " << currSeqNumber);
        m_expectedSeqNumber = currSeqNumber + 1;
    }
    else
    {
        expectedSnLost = false;
        NS_LOG_LOGIC("No losses. Expected SN = " << m_expectedSeqNumber
                                                 << ". Current SN = " << currSeqNumber);
        m_expectedSeqNumber++;
    }

    // Build list of SDUs
    uint8_t extensionBit;
    uint16_t lengthIndicator;
    do
    {
        extensionBit = rlcHeader.PopExtensionBit();
        NS_LOG_LOGIC("E = " << (uint16_t)extensionBit);

        if (extensionBit == 0)
        {
            m_sdusBuffer.push_back(packet);
        }
        else // extensionBit == 1
        {
            lengthIndicator = rlcHeader.PopLengthIndicator();
            NS_LOG_LOGIC("LI = " << lengthIndicator);

            // Check if there is enough data in the packet
            if (lengthIndicator >= packet->GetSize())
            {
                NS_LOG_LOGIC("INTERNAL ERROR: Not enough data in the packet ("
                             << packet->GetSize() << "). Needed LI=" << lengthIndicator);
            }

            // Split packet in two fragments
            Ptr<Packet> data_field = packet->CreateFragment(0, lengthIndicator);
            packet->RemoveAtStart(lengthIndicator);

            m_sdusBuffer.push_back(data_field);
        }
    } while (extensionBit == 1);

    // Current reassembling state
    if (m_reassemblingState == WAITING_S0_FULL)
    {
        NS_LOG_LOGIC("Reassembling State = 'WAITING_S0_FULL'");
    }
    else if (m_reassemblingState == WAITING_SI_SF)
    {
        NS_LOG_LOGIC("Reassembling State = 'WAITING_SI_SF'");
    }
    else
    {
        NS_LOG_LOGIC("Reassembling State = Unknown state");
    }

    // Received framing Info
    NS_LOG_LOGIC("Framing Info = " << (uint16_t)framingInfo);

    // Reassemble the list of SDUs (when there is no losses)
    if (!expectedSnLost)
    {
        switch (m_reassemblingState)
        {
        case WAITING_S0_FULL:
            switch (framingInfo)
            {
            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Deliver one or multiple PDUs
                 */
                for (auto it = m_sdusBuffer.begin(); it != m_sdusBuffer.end(); it++)
                {
                    m_rlcSapUser->ReceivePdcpPdu(*it);
                }
                m_sdusBuffer.clear();
                break;

            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                m_reassemblingState = WAITING_SI_SF;

                /**
                 * Deliver full PDUs
                 */
                while (m_sdusBuffer.size() > 1)
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }

                /**
                 * Keep S0
                 */
                m_keepS0 = m_sdusBuffer.front();
                m_sdusBuffer.pop_front();
                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Discard SI or SN
                 */
                m_sdusBuffer.pop_front();

                /**
                 * Deliver zero, one or multiple PDUs
                 */
                while (!m_sdusBuffer.empty())
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }
                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                if (m_sdusBuffer.size() == 1)
                {
                    m_reassemblingState = WAITING_S0_FULL;
                }
                else
                {
                    m_reassemblingState = WAITING_SI_SF;
                }

                /**
                 * Discard SI or SN
                 */
                m_sdusBuffer.pop_front();

                if (!m_sdusBuffer.empty())
                {
                    /**
                     * Deliver zero, one or multiple PDUs
                     */
                    while (m_sdusBuffer.size() > 1)
                    {
                        m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                        m_sdusBuffer.pop_front();
                    }

                    /**
                     * Keep S0
                     */
                    m_keepS0 = m_sdusBuffer.front();
                    m_sdusBuffer.pop_front();
                }
                break;

            default:
                /**
                 * ERROR: Transition not possible
                 */
                NS_LOG_LOGIC(
                    "INTERNAL ERROR: Transition not possible. FI = " << (uint32_t)framingInfo);
                break;
            }
            break;

        case WAITING_SI_SF:
            switch (framingInfo)
            {
            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Deliver (Kept)S0 + SN
                 */
                m_keepS0->AddAtEnd(m_sdusBuffer.front());
                m_sdusBuffer.pop_front();
                m_rlcSapUser->ReceivePdcpPdu(m_keepS0);

                /**
                 * Deliver zero, one or multiple PDUs
                 */
                while (!m_sdusBuffer.empty())
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }
                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                m_reassemblingState = WAITING_SI_SF;

                /**
                 * Keep SI
                 */
                if (m_sdusBuffer.size() == 1)
                {
                    m_keepS0->AddAtEnd(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }
                else // m_sdusBuffer.size () > 1
                {
                    /**
                     * Deliver (Kept)S0 + SN
                     */
                    m_keepS0->AddAtEnd(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                    m_rlcSapUser->ReceivePdcpPdu(m_keepS0);

                    /**
                     * Deliver zero, one or multiple PDUs
                     */
                    while (m_sdusBuffer.size() > 1)
                    {
                        m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                        m_sdusBuffer.pop_front();
                    }

                    /**
                     * Keep S0
                     */
                    m_keepS0 = m_sdusBuffer.front();
                    m_sdusBuffer.pop_front();
                }
                break;

            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::LAST_BYTE):
            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
            default:
                /**
                 * ERROR: Transition not possible
                 */
                NS_LOG_LOGIC(
                    "INTERNAL ERROR: Transition not possible. FI = " << (uint32_t)framingInfo);
                break;
            }
            break;

        default:
            NS_LOG_LOGIC(
                "INTERNAL ERROR: Wrong reassembling state = " << (uint32_t)m_reassemblingState);
            break;
        }
    }
    else // Reassemble the list of SDUs (when there are losses, i.e. the received SN is not the
         // expected one)
    {
        switch (m_reassemblingState)
        {
        case WAITING_S0_FULL:
            switch (framingInfo)
            {
            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Deliver one or multiple PDUs
                 */
                for (auto it = m_sdusBuffer.begin(); it != m_sdusBuffer.end(); it++)
                {
                    m_rlcSapUser->ReceivePdcpPdu(*it);
                }
                m_sdusBuffer.clear();
                break;

            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                m_reassemblingState = WAITING_SI_SF;

                /**
                 * Deliver full PDUs
                 */
                while (m_sdusBuffer.size() > 1)
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }

                /**
                 * Keep S0
                 */
                m_keepS0 = m_sdusBuffer.front();
                m_sdusBuffer.pop_front();
                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Discard SN
                 */
                m_sdusBuffer.pop_front();

                /**
                 * Deliver zero, one or multiple PDUs
                 */
                while (!m_sdusBuffer.empty())
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }
                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                if (m_sdusBuffer.size() == 1)
                {
                    m_reassemblingState = WAITING_S0_FULL;
                }
                else
                {
                    m_reassemblingState = WAITING_SI_SF;
                }

                /**
                 * Discard SI or SN
                 */
                m_sdusBuffer.pop_front();

                if (!m_sdusBuffer.empty())
                {
                    /**
                     * Deliver zero, one or multiple PDUs
                     */
                    while (m_sdusBuffer.size() > 1)
                    {
                        m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                        m_sdusBuffer.pop_front();
                    }

                    /**
                     * Keep S0
                     */
                    m_keepS0 = m_sdusBuffer.front();
                    m_sdusBuffer.pop_front();
                }
                break;

            default:
                /**
                 * ERROR: Transition not possible
                 */
                NS_LOG_LOGIC(
                    "INTERNAL ERROR: Transition not possible. FI = " << (uint32_t)framingInfo);
                break;
            }
            break;

        case WAITING_SI_SF:
            switch (framingInfo)
            {
            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Discard S0
                 */
                m_keepS0 = nullptr;

                /**
                 * Deliver one or multiple PDUs
                 */
                while (!m_sdusBuffer.empty())
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }
                break;

            case (NrRlcHeader::FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                m_reassemblingState = WAITING_SI_SF;

                /**
                 * Discard S0
                 */
                m_keepS0 = nullptr;

                /**
                 * Deliver zero, one or multiple PDUs
                 */
                while (m_sdusBuffer.size() > 1)
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }

                /**
                 * Keep S0
                 */
                m_keepS0 = m_sdusBuffer.front();
                m_sdusBuffer.pop_front();

                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::LAST_BYTE):
                m_reassemblingState = WAITING_S0_FULL;

                /**
                 * Discard S0
                 */
                m_keepS0 = nullptr;

                /**
                 * Discard SI or SN
                 */
                m_sdusBuffer.pop_front();

                /**
                 * Deliver zero, one or multiple PDUs
                 */
                while (!m_sdusBuffer.empty())
                {
                    m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                    m_sdusBuffer.pop_front();
                }
                break;

            case (NrRlcHeader::NO_FIRST_BYTE | NrRlcHeader::NO_LAST_BYTE):
                if (m_sdusBuffer.size() == 1)
                {
                    m_reassemblingState = WAITING_S0_FULL;
                }
                else
                {
                    m_reassemblingState = WAITING_SI_SF;
                }

                /**
                 * Discard S0
                 */
                m_keepS0 = nullptr;

                /**
                 * Discard SI or SN
                 */
                m_sdusBuffer.pop_front();

                if (!m_sdusBuffer.empty())
                {
                    /**
                     * Deliver zero, one or multiple PDUs
                     */
                    while (m_sdusBuffer.size() > 1)
                    {
                        m_rlcSapUser->ReceivePdcpPdu(m_sdusBuffer.front());
                        m_sdusBuffer.pop_front();
                    }

                    /**
                     * Keep S0
                     */
                    m_keepS0 = m_sdusBuffer.front();
                    m_sdusBuffer.pop_front();
                }
                break;

            default:
                /**
                 * ERROR: Transition not possible
                 */
                NS_LOG_LOGIC(
                    "INTERNAL ERROR: Transition not possible. FI = " << (uint32_t)framingInfo);
                break;
            }
            break;

        default:
            NS_LOG_LOGIC(
                "INTERNAL ERROR: Wrong reassembling state = " << (uint32_t)m_reassemblingState);
            break;
        }
    }
}

void
NrRlcUmDualpi2::ReassembleOutsideWindow()
{
    NS_LOG_LOGIC("Reassemble Outside Window");

    auto it = m_rxBuffer.begin();

    while ((it != m_rxBuffer.end()) && !IsInsideReorderingWindow(nr::SequenceNumber10(it->first)))
    {
        NS_LOG_LOGIC("SN = " << it->first);

        // Reassemble RLC SDUs and deliver the PDCP PDU to upper layer
        ReassembleAndDeliver(it->second);

        auto it_tmp = it;
        ++it;
        m_rxBuffer.erase(it_tmp);
    }

    if (it != m_rxBuffer.end())
    {
        NS_LOG_LOGIC("(SN = " << it->first << ") is inside the reordering window");
    }
}

void
NrRlcUmDualpi2::ReassembleSnInterval(nr::SequenceNumber10 lowSeqNumber, nr::SequenceNumber10 highSeqNumber)
{
    NS_LOG_LOGIC("Reassemble SN between " << lowSeqNumber << " and " << highSeqNumber);

    nr::SequenceNumber10 reassembleSn = lowSeqNumber;
    NS_LOG_LOGIC("reassembleSN = " << reassembleSn);
    NS_LOG_LOGIC("highSeqNumber = " << highSeqNumber);
    while (reassembleSn < highSeqNumber)
    {
        NS_LOG_LOGIC("reassembleSn < highSeqNumber");
        auto it = m_rxBuffer.find(reassembleSn.GetValue());
        NS_LOG_LOGIC("it->first  = " << it->first);
        NS_LOG_LOGIC("it->second = " << it->second);
        if (it != m_rxBuffer.end())
        {
            NS_LOG_LOGIC("SN = " << it->first);

            // Reassemble RLC SDUs and deliver the PDCP PDU to upper layer
            ReassembleAndDeliver(it->second);

            m_rxBuffer.erase(it);
        }

        reassembleSn++;
    }
}

void
NrRlcUmDualpi2::DoReportBufferStatus()
{
    Time holDelay(0);
    uint32_t queueSize = 0;

    int aqmCurrSize = aqm->GetQueueSizeBytes();
    if (aqmCurrSize != 0)
    {
        holDelay = Simulator::Now() - aqm->GetQueueDelay();

        queueSize =
            aqmCurrSize + 2 * aqm->GetQueueSize(); // Data in the aqm + estimated headers size
    }

    NrMacSapProvider::ReportBufferStatusParameters r;
    r.rnti = m_rnti;
    r.lcid = m_lcid;
    r.txQueueSize = queueSize;
    r.txQueueHolDelay = holDelay.GetMilliSeconds();
    r.retxQueueSize = 0;
    r.retxQueueHolDelay = 0;
    r.statusPduSize = 0;

    NS_LOG_LOGIC("Send ReportBufferStatus = " << r.txQueueSize << ", " << r.txQueueHolDelay);
    m_macSapProvider->ReportBufferStatus(r);
}

void
NrRlcUmDualpi2::ExpireReorderingTimer()
{
    NS_LOG_FUNCTION(this << m_rnti << (uint32_t)m_lcid);
    NS_LOG_LOGIC("Reordering timer has expired");

    // 5.1.2.2.4 Actions when t-Reordering expires
    // When t-Reordering expires, the receiving UM RLC entity shall:
    // - update VR(UR) to the SN of the first UMD PDU with SN >= VR(UX) that has not been received;
    // - reassemble RLC SDUs from any UMD PDUs with SN < updated VR(UR), remove RLC headers when
    // doing so
    //   and deliver the reassembled RLC SDUs to upper layer in ascending order of the RLC SN if not
    //   delivered before;
    // - if VR(UH) > VR(UR):
    //    - start t-Reordering;
    //    - set VR(UX) to VR(UH).

    nr::SequenceNumber10 newVrUr = m_vrUx;

    while (m_rxBuffer.find(newVrUr.GetValue()) != m_rxBuffer.end())
    {
        newVrUr++;
    }
    nr::SequenceNumber10 oldVrUr = m_vrUr;
    m_vrUr = newVrUr;
    NS_LOG_LOGIC("New VR(UR) = " << m_vrUr);

    ReassembleSnInterval(oldVrUr, m_vrUr);

    if (m_vrUh > m_vrUr)
    {
        NS_LOG_LOGIC("Start reordering timer");
        m_reorderingTimer =
            Simulator::Schedule(m_reorderingTimerValue, &NrRlcUmDualpi2::ExpireReorderingTimer, this);
        m_vrUx = m_vrUh;
        NS_LOG_LOGIC("New VR(UX) = " << m_vrUx);
    }
}

void
NrRlcUmDualpi2::ExpireRbsTimer()
{
    NS_LOG_LOGIC("RBS Timer expires");

    if(aqm->GetQueueSize() != 0){
        DoReportBufferStatus();
        m_rbsTimer = Simulator::Schedule(MilliSeconds(10), &NrRlcUmDualpi2::ExpireRbsTimer, this);
    }
}

} // namespace ns3