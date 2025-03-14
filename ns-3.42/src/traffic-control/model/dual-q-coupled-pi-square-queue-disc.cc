/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
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
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

 #include "math.h"
 #include "ns3/log.h"
 #include "ns3/enum.h"
 #include "ns3/uinteger.h"
 #include "ns3/double.h"
 #include "ns3/simulator.h"
 #include "ns3/abort.h"
 #include "ns3/object-factory.h"
 #include "ns3/string.h"
 #include "dual-q-coupled-pi-square-queue-disc.h"
 #include "ns3/drop-tail-queue.h"
 #include "ns3/ipv4.h"
 #include "ns3/ipv4-l3-protocol.h"
 
 #define min (a,b)((a) < (b) ? (a) : (b))
 
 namespace ns3 {
 
 NS_LOG_COMPONENT_DEFINE ("DualQCoupledPiSquareQueueDisc");
 
 /**
  * L4S Queue Disc Item Implementations
  */
 DualQueueL4SQueueDiscItem::DualQueueL4SQueueDiscItem (Ptr<Packet> p, const Address & addr, uint16_t protocol)
   : QueueDiscItem (p, addr, protocol)
 {
 }
 
 DualQueueL4SQueueDiscItem::~DualQueueL4SQueueDiscItem ()
 {
 }
 
 void
 DualQueueL4SQueueDiscItem::AddHeader (void)
 {
 }
 
 bool
 DualQueueL4SQueueDiscItem::Mark (void)
 {
   Ptr<Packet> m_packet = this->GetPacket();
 
   Ipv4Header ipv4Header;
   if (m_packet->PeekHeader(ipv4Header)) {
     uint8_t ecn = ipv4Header.GetEcn();
 
     if(ecn == Ipv4Header::ECN_CE) {
       return true;
     }
     else {
       ipv4Header.SetEcn(Ipv4Header::ECN_CE);
 
       // Reinsert the modified header back into the packet
       m_packet->RemoveHeader(ipv4Header);
       m_packet->AddHeader(ipv4Header);
 
       return true;
     }
   }
   return false;
 }
 
 bool
 DualQueueL4SQueueDiscItem::IsL4S (void)
 {
   return true;
 }
 
 /**
  * Classic Queue Disc Item Implementations
  */
 DualQueueClassicQueueDiscItem::DualQueueClassicQueueDiscItem (Ptr<Packet> p, const Address & addr, uint16_t protocol)
   : QueueDiscItem (p, addr, protocol)
 {
 }
 
 DualQueueClassicQueueDiscItem::~DualQueueClassicQueueDiscItem ()
 {
 }
 
 void
 DualQueueClassicQueueDiscItem::AddHeader (void)
 {
 }
 
 bool 
 DualQueueClassicQueueDiscItem::Mark (void) {
   Ptr<Packet> m_packet = this->GetPacket();
 
   Ipv4Header ipv4Header;
   if (m_packet->PeekHeader(ipv4Header)) {
     
     // Check if ECN is enabled (NotECT packets cannot be marked)
     if (ipv4Header.GetEcn() == ns3::Ipv4Header::ECN_NotECT)
       return false;
 
     ipv4Header.SetEcn(Ipv4Header::ECN_CE);
 
     // Reinsert the modified header back into the packet
     m_packet->RemoveHeader(ipv4Header);
     m_packet->AddHeader(ipv4Header);
 
     return true; // Packet marked successfully
   }
 
   return false;
 }
 
 bool
 DualQueueClassicQueueDiscItem::IsL4S (void)
 {
   return false;
 }
 
 class DualQCoupledPiSquareTimestampTag : public Tag
 {
 public:
   DualQCoupledPiSquareTimestampTag ();
   /**
    * \brief Get the type ID.
    * \return the object TypeId
    */
   static TypeId GetTypeId (void);
   virtual TypeId GetInstanceTypeId (void) const;
 
   virtual uint32_t GetSerializedSize (void) const;
   virtual void Serialize (TagBuffer i) const;
   virtual void Deserialize (TagBuffer i);
   virtual void Print (std::ostream &os) const;
 
   /**
    * \brief Gets the Tag creation time
    *
    * \return the time object stored in the tag
    */
   Time GetTxTime (void) const;
 
 private:
   uint64_t m_creationTime; //!< Tag creation time
 };
 
 DualQCoupledPiSquareTimestampTag::DualQCoupledPiSquareTimestampTag ()
   : m_creationTime (Simulator::Now ().GetTimeStep ())
 {
 }
 
 TypeId
 DualQCoupledPiSquareTimestampTag::GetTypeId (void)
 {
   static TypeId tid = TypeId ("ns3::DualQCoupledPiSquareTimestampTag")
     .SetParent<Tag> ()
     .AddConstructor<DualQCoupledPiSquareTimestampTag> ()
     .AddAttribute ("CreationTime",
                    "The time at which the timestamp was created",
                    StringValue ("0.0s"),
                    MakeTimeAccessor (&DualQCoupledPiSquareTimestampTag::GetTxTime),
                    MakeTimeChecker ())
   ;
   return tid;
 }
 
 TypeId
 DualQCoupledPiSquareTimestampTag::GetInstanceTypeId (void) const
 {
   return GetTypeId ();
 }
 
 uint32_t
 DualQCoupledPiSquareTimestampTag::GetSerializedSize (void) const
 {
   return 8; // Why return 8???????
 }
 
 void
 DualQCoupledPiSquareTimestampTag::Serialize (TagBuffer i) const
 {
   i.WriteU64 (m_creationTime);
 }
 
 void
 DualQCoupledPiSquareTimestampTag::Deserialize (TagBuffer i)
 {
   m_creationTime = i.ReadU64 ();
 }
 
 void
 DualQCoupledPiSquareTimestampTag::Print (std::ostream &os) const
 {
   os << "CreationTime=" << m_creationTime;
 }
 
 Time
 DualQCoupledPiSquareTimestampTag::GetTxTime (void) const
 {
   return TimeStep (m_creationTime);
 }
 
 NS_OBJECT_ENSURE_REGISTERED (DualQCoupledPiSquareQueueDisc);
 
 TypeId DualQCoupledPiSquareQueueDisc::GetTypeId (void)
 {
   static TypeId tid = TypeId ("ns3::DualQCoupledPiSquareQueueDisc")
     .SetParent<QueueDisc> ()
     .SetGroupName ("TrafficControl")
     .AddConstructor<DualQCoupledPiSquareQueueDisc> ()
     .AddAttribute ("Mode",
                "Determines unit for QueueLimit",
                EnumValue (QUEUE_DISC_MODE_PACKETS),
                MakeEnumAccessor<QueueDiscMode> (&DualQCoupledPiSquareQueueDisc::SetMode),
                MakeEnumChecker (QUEUE_DISC_MODE_BYTES, "QUEUE_DISC_MODE_BYTES",
                                       QUEUE_DISC_MODE_PACKETS, "QUEUE_DISC_MODE_PACKETS"))
     .AddAttribute ("MeanPktSize",
                    "Average of packet size",
                    UintegerValue (1024),
                    MakeUintegerAccessor (&DualQCoupledPiSquareQueueDisc::m_meanPktSize),
                    MakeUintegerChecker<uint32_t> ())
     .AddAttribute ("A",
                    "Value of alpha",
                    DoubleValue (10),
                    MakeDoubleAccessor (&DualQCoupledPiSquareQueueDisc::m_alpha),
                    MakeDoubleChecker<double> ())
     .AddAttribute ("B",
                    "Value of beta",
                    DoubleValue (100),
                    MakeDoubleAccessor (&DualQCoupledPiSquareQueueDisc::m_beta),
                    MakeDoubleChecker<double> ())
     .AddAttribute ("Tupdate",
                    "Time period to calculate drop probability",
                    TimeValue (Seconds (0.016)),
                    MakeTimeAccessor (&DualQCoupledPiSquareQueueDisc::m_tUpdate),
                    MakeTimeChecker ())
     .AddAttribute ("Supdate",
                    "Start time of the update timer",
                    TimeValue (Seconds (0.0)),
                    MakeTimeAccessor (&DualQCoupledPiSquareQueueDisc::m_sUpdate),
                    MakeTimeChecker ())
     .AddAttribute ("QueueLimit",
                    "Queue limit in bytes/packets",
                    UintegerValue (25),
                    MakeUintegerAccessor (&DualQCoupledPiSquareQueueDisc::SetQueueLimit),
                    MakeUintegerChecker<uint32_t> ())
     .AddAttribute ("ClassicQueueDelayReference",
                    "Desired queue delay of Classic traffic",
                    TimeValue (Seconds (0.015)),
                    MakeTimeAccessor (&DualQCoupledPiSquareQueueDisc::m_classicQueueDelayRef),
                    MakeTimeChecker ())
     .AddAttribute ("L4SMarkThresold",
                    "L4S marking threshold in Time",
                    TimeValue (Seconds (0.001)),
                    MakeTimeAccessor (&DualQCoupledPiSquareQueueDisc::m_l4sThreshold),
                    MakeTimeChecker ())
     .AddAttribute ("K",
                    "Coupling factor",
                    UintegerValue (2),
                    MakeUintegerAccessor (&DualQCoupledPiSquareQueueDisc::m_k),
                    MakeUintegerChecker<uint32_t> ())
   ;
 
   return tid;
 }
 
 DualQCoupledPiSquareQueueDisc::DualQCoupledPiSquareQueueDisc ()
   : QueueDisc ()
 {
   NS_LOG_FUNCTION (this);
   m_uv = CreateObject<UniformRandomVariable> ();
   m_rtrsEvent = Simulator::Schedule (m_sUpdate, &DualQCoupledPiSquareQueueDisc::CalculateP, this);
   m_queueSizeBytes = 0;
 }
 
 DualQCoupledPiSquareQueueDisc::~DualQCoupledPiSquareQueueDisc ()
 {
   NS_LOG_FUNCTION (this);
 }
 
 void
 DualQCoupledPiSquareQueueDisc::DoDispose (void)
 {
   NS_LOG_FUNCTION (this);
   m_uv = 0;
   Simulator::Remove (m_rtrsEvent);
   QueueDisc::DoDispose ();
 }
 
 void
 DualQCoupledPiSquareQueueDisc::SetMode (QueueDiscMode mode)
 {
   NS_LOG_FUNCTION (this << mode);
   m_mode = mode;
 }
 
 int
 DualQCoupledPiSquareQueueDisc::GetQueueSizeBytes (void)
 {
   NS_LOG_FUNCTION (this);
   return m_queueSizeBytes;
 }
 
 DualQCoupledPiSquareQueueDisc::QueueDiscMode
 DualQCoupledPiSquareQueueDisc::GetMode (void)
 {
   NS_LOG_FUNCTION (this);
   return m_mode;
 }
 
 void
 DualQCoupledPiSquareQueueDisc::SetQueueLimit (uint32_t lim)
 {
   NS_LOG_FUNCTION (this << lim);
   m_queueLimit = lim;
 }
 
 uint32_t
 DualQCoupledPiSquareQueueDisc::GetQueueSize (void)
 {
   NS_LOG_FUNCTION (this);
   if (GetMode () == QUEUE_DISC_MODE_BYTES)
     {
       NS_ASSERT(GetInternalQueue (0)->GetNBytes () >= 0);
       NS_ASSERT(GetInternalQueue (1)->GetNBytes () >= 0);
       
       return (GetInternalQueue (0)->GetNBytes () + GetInternalQueue (1)->GetNBytes ());
     }
   else if (GetMode () == QUEUE_DISC_MODE_PACKETS)
     {
       NS_ASSERT(GetInternalQueue (0)->GetNPackets () >= 0);
       NS_ASSERT(GetInternalQueue (1)->GetNPackets () >= 0);
 
       return (GetInternalQueue (0)->GetNPackets () + GetInternalQueue (1)->GetNPackets ());
     }
   else
     {
       NS_ABORT_MSG ("Unknown Dual Queue PI Square mode.");
     }
 }
 
 DualQCoupledPiSquareQueueDisc::Stats
 DualQCoupledPiSquareQueueDisc::GetStats ()
 {
   NS_LOG_FUNCTION (this);
   return m_stats;
 }
 
 Time
 DualQCoupledPiSquareQueueDisc::GetQueueDelay (void)
 {
     NS_LOG_FUNCTION(this);
 
     Ptr<const QueueDiscItem> item1;
     Ptr<const QueueDiscItem> item2;
     Time classicQueueTime;
     Time l4sQueueTime;
     DualQCoupledPiSquareTimestampTag tag1;
     DualQCoupledPiSquareTimestampTag tag2;
 
     if ((item1 = GetInternalQueue(0)->Peek()))
     {
         item1->GetPacket()->PeekPacketTag(tag1);
         classicQueueTime = tag1.GetTxTime();
     }
     else
     {
         classicQueueTime = Time(Seconds(0));
     }
 
     if ((item2 = GetInternalQueue(1)->Peek()))
     {
         item2->GetPacket()->PeekPacketTag(tag2);
         l4sQueueTime = tag2.GetTxTime();
     }
     else
     {
         l4sQueueTime = Time(Seconds(0));
     }
 
     if (classicQueueTime >= l4sQueueTime)
         return classicQueueTime;
 
     else
         return l4sQueueTime;
 }
 
 double
 DualQCoupledPiSquareQueueDisc::GetDropProb (void)
 {
   NS_LOG_FUNCTION (this);
   return m_dropProb;
 }
 
 int64_t
 DualQCoupledPiSquareQueueDisc::AssignStreams (int64_t stream)
 {
   NS_LOG_FUNCTION (this << stream);
   m_uv->SetStream (stream);
   return 1;
 }
 
 bool
 DualQCoupledPiSquareQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
 {
   NS_LOG_FUNCTION (this << item);
   bool queueNumber;
 
   // attach arrival time to packet
   Ptr<Packet> p = item->GetPacket ();
   DualQCoupledPiSquareTimestampTag tag;
   p->AddPacketTag (tag);
 
   uint32_t nQueued = GetQueueSize ();
   if ((GetMode () == QUEUE_DISC_MODE_PACKETS && nQueued >= m_queueLimit)
       || (GetMode () == QUEUE_DISC_MODE_BYTES && nQueued + item->GetSize () > m_queueLimit))
     {
       // Drops due to queue limit
       DropBeforeEnqueue (item, "Drops due to queue limit");
       m_stats.forcedDrop++;
       return false;
     }
   else
     {
       if (item->IsL4S ())
         {
           NS_LOG_INFO("Enqueuing L4S packet");
           queueNumber = 1;
         }
       else
         {
           NS_LOG_INFO("Enqueuing Classic packet");
           queueNumber = 0;
         }
     }
 
   m_queueSizeBytes += item->GetSize ();
   bool retval = GetInternalQueue (queueNumber)->Enqueue (item);
   NS_LOG_INFO ("Number packets in queue-number " << (int) queueNumber << ": " << GetInternalQueue (queueNumber)->GetNPackets ());
   NS_LOG_INFO ("Number packets in queue-number " << (int) !queueNumber << ": " << GetInternalQueue (!queueNumber)->GetNPackets ());
   return retval;
 }
 
 void
 DualQCoupledPiSquareQueueDisc::InitializeParams (void)
 {
   m_tShift = 2 * m_classicQueueDelayRef;
   m_alphaU = m_alpha * m_tUpdate.GetSeconds ();
   m_betaU = m_beta * m_tUpdate.GetSeconds ();
   m_minL4SLength = 2 * m_meanPktSize;
   m_dropProb = 0.0;
   m_qDelayOld = Time (Seconds (0));
   m_stats.forcedDrop = 0;
   m_stats.unforcedClassicDrop = 0;
   m_stats.unforcedClassicMark = 0;
   m_stats.unforcedL4SMark = 0;
 }
 
 void DualQCoupledPiSquareQueueDisc::CalculateP ()
 {
   NS_LOG_FUNCTION (this);
 
   // Use queuing time of first-in Classic packet
   Ptr<const QueueDiscItem> item;
   Time qDelay;
   bool updateProb = true;
 
   if ((item = GetInternalQueue (0)->Peek ()))
     {
       DualQCoupledPiSquareTimestampTag tag;
       item->GetPacket ()->PeekPacketTag (tag);
       qDelay = Simulator::Now () - tag.GetTxTime ();
     }
   else
     {
       qDelay = Time (Seconds (0));
     }
 
   // If qdelay is zero and qlen is not, it means qlen is very small,
   // less than dequeue_rate, so we do not update probabilty in this round
   if (qDelay.IsZero() && GetQueueSize () > 0)
     {
       m_rtrsEvent = Simulator::Schedule (m_tUpdate, &DualQCoupledPiSquareQueueDisc::CalculateP, this);
       return;
     }
   double delta = m_alphaU * (qDelay.GetSeconds () - m_classicQueueDelayRef.GetSeconds ()) +
     m_betaU * (qDelay.GetSeconds () - m_qDelayOld.GetSeconds ());
 
   m_dropProb += delta;
 
   // Non-linear drop in probability: Reduce drop probability quickly if delay
   // is 0 for 2 consecutive Tupdate periods
   if ((qDelay.IsZero()) && (m_qDelayOld.IsZero()) && updateProb)
     {
       m_dropProb = (m_dropProb * 0.98);
     }
 
   m_dropProb = (m_dropProb > 0) ? m_dropProb : 0;
   m_dropProb = (m_dropProb < 1) ? m_dropProb : 1;
 
   m_l4sDropProb = m_dropProb * m_k;
   m_classicDropProb = m_dropProb * m_dropProb;
   m_qDelayOld = qDelay;
   m_rtrsEvent = Simulator::Schedule (m_tUpdate, &DualQCoupledPiSquareQueueDisc::CalculateP, this);
   
   NS_LOG_INFO(this << " Finished computing drop probility: " << m_classicDropProb);
 }
 
 Ptr<QueueDiscItem>
 DualQCoupledPiSquareQueueDisc::DoDequeue ()
 {
   NS_LOG_FUNCTION (this);
   Ptr<const QueueDiscItem> item1;
   Ptr<const QueueDiscItem> item2;
   Time classicQueueTime;
   Time l4sQueueTime;
   DualQCoupledPiSquareTimestampTag tag1;
   DualQCoupledPiSquareTimestampTag tag2;
 
   while (GetQueueSize () > 0)
     {
       if ((item1 = GetInternalQueue (0)->Peek ()))
         {
           item1->GetPacket ()->PeekPacketTag (tag1);
           classicQueueTime = tag1.GetTxTime ();
         }
       else
         {
           classicQueueTime = Time (Seconds (0));
         }
 
       if ((item2 = GetInternalQueue (1)->Peek ()))
         {
           item2->GetPacket ()->PeekPacketTag (tag2);
           l4sQueueTime = tag2.GetTxTime ();
         }
       else
         {
           l4sQueueTime = Time (Seconds (0));
         }
 
       if (l4sQueueTime.GetSeconds () + m_tShift.GetSeconds () >= classicQueueTime.GetSeconds () && GetInternalQueue (1)->Peek () )
         {
           Ptr<QueueDiscItem> item = GetInternalQueue (1)->Dequeue ();
           DualQCoupledPiSquareTimestampTag tag;
           item->GetPacket ()->PeekPacketTag (tag);
           bool minL4SQueueSizeFlag = false;
           if (GetMode () == QUEUE_DISC_MODE_BYTES && GetInternalQueue (1)->GetNBytes () > 2 * m_meanPktSize)
             {
               minL4SQueueSizeFlag = true;
             }
           else if (GetMode () == QUEUE_DISC_MODE_PACKETS && GetInternalQueue (1)->GetNPackets () > 2 )
             {
               minL4SQueueSizeFlag = true;
             }
 
           if ((Simulator::Now () - tag.GetTxTime () > m_l4sThreshold && minL4SQueueSizeFlag) || (m_l4sDropProb > m_uv->GetValue ()))
             {
               item->Mark ();
               m_stats.unforcedL4SMark++;
             }
 
           m_queueSizeBytes -= item->GetSize ();
           return item;
         }
 
       else
         {
           Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();
           m_queueSizeBytes -= item->GetSize ();
 
           if (m_classicDropProb / (m_k * 1.0) >  m_uv->GetValue ())
             {
               if (!item->Mark ())
                 {
                   if(GetQueueSize()){ // there is something else in the queue
                     Drop (item, "Drops due to drop probability");
                     m_stats.unforcedClassicDrop++;
                     continue;
                   }
                   else // it was the only packet in the queue, so send it anyway
                     return item;
                   
                 }
               else
                 {
                   m_stats.unforcedClassicMark++;
                   return item;
                 }
             }
           return item;
         }
     }
   NS_LOG_INFO("Queue empty");
   return nullptr;
 }
 
 Ptr<const QueueDiscItem>
 DualQCoupledPiSquareQueueDisc::DoPeek () const
 {
   NS_LOG_FUNCTION (this);
   Ptr<const QueueDiscItem> item;
 
   for (uint32_t i = 0; i < GetNInternalQueues (); i++)
     {
       if ((item = GetInternalQueue (i)->Peek ()))
         {
           NS_LOG_LOGIC ("Peeked from queue number " << i << ": " << item);
           NS_LOG_LOGIC ("Number packets queue number " << i << ": " << GetInternalQueue (i)->GetNPackets ());
           NS_LOG_LOGIC ("Number bytes queue number " << i << ": " << GetInternalQueue (i)->GetNBytes ());
           return item;
         }
     }
 
   NS_LOG_LOGIC ("Queue empty");
   return item;
 }
 
 bool
 DualQCoupledPiSquareQueueDisc::CheckConfig (void)
 {
   NS_LOG_FUNCTION (this);
   if (GetNQueueDiscClasses () > 0)
     {
       NS_LOG_ERROR ("DualQCoupledPiSquareQueueDisc cannot have classes");
       return false;
     }
 
   if (GetNPacketFilters () > 0)
     {
       NS_LOG_ERROR ("DualQCoupledPiSquareQueueDisc cannot have packet filters");
       return false;
     }
 
   if (GetNInternalQueues () == 0)
     {
       // Create 2 DropTail queues
       Ptr<InternalQueue> queue1 = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ();
       Ptr<InternalQueue> queue2 = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ();
       if (m_mode == QUEUE_DISC_MODE_PACKETS)
         {
 
           queue1->SetMaxSize(QueueSize(QueueSizeUnit::PACKETS, m_queueLimit));
           queue2->SetMaxSize(QueueSize(QueueSizeUnit::PACKETS, m_queueLimit));
         }
       else
         {
           queue1->SetMaxSize(QueueSize(QueueSizeUnit::BYTES, m_queueLimit));
           queue2->SetMaxSize(QueueSize(QueueSizeUnit::BYTES, m_queueLimit));
         }
       AddInternalQueue (queue1);
       AddInternalQueue (queue2);
     }
 
   if (GetNInternalQueues () != 2)
     {
       NS_LOG_ERROR ("DualQCoupledPiSquareQueueDisc needs 2 internal queue");
       return false;
     }
   if ((GetInternalQueue (0)->GetCurrentSize ().GetUnit() == QueueSizeUnit::PACKETS && m_mode == QUEUE_DISC_MODE_BYTES)
       || (GetInternalQueue (0)->GetCurrentSize ().GetUnit() == QueueSizeUnit::BYTES && m_mode == QUEUE_DISC_MODE_PACKETS))
     {
       NS_LOG_ERROR ("The mode provided for Classic traffic queue does not match the mode set on the DualQCoupledPiSquareQueueDisc");
       return false;
     }
 
   if ((GetInternalQueue (1)->GetCurrentSize ().GetUnit() == QueueSizeUnit::PACKETS && m_mode == QUEUE_DISC_MODE_BYTES)
       || (GetInternalQueue (1)->GetCurrentSize ().GetUnit() == QueueSizeUnit::BYTES && m_mode == QUEUE_DISC_MODE_PACKETS))
     {
       NS_LOG_ERROR ("The mode provided for L4S traffic queue does not match the mode set on the DualQCoupledPiSquareQueueDisc");
       return false;
     }
 
   if ((m_mode ==  QUEUE_DISC_MODE_PACKETS && GetInternalQueue (0)->GetMaxSize ().GetValue() < m_queueLimit)
       || (m_mode ==  QUEUE_DISC_MODE_BYTES && GetInternalQueue (0)->GetMaxSize ().GetValue() < m_queueLimit))
     {
       NS_LOG_ERROR ("The size of the internal Classic traffic queue is less than the queue disc limit");
       return false;
     }
 
   if ((m_mode ==  QUEUE_DISC_MODE_PACKETS && GetInternalQueue (1)->GetMaxSize ().GetValue() < m_queueLimit)
       || (m_mode ==  QUEUE_DISC_MODE_BYTES && GetInternalQueue (1)->GetMaxSize ().GetValue() < m_queueLimit))
     {
       NS_LOG_ERROR ("The size of the internal L4S traffic queue is less than the queue disc limit");
       return false;
     }
 
   return true;
 }
 
 } //namespace ns3
 