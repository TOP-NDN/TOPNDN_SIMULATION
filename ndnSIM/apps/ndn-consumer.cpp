/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "ndn-consumer.hpp"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"

#include "utils/ndn-ns3-packet-tag.hpp"
#include "model/ndn-app-face.hpp"
#include "utils/ndn-rtt-mean-deviation.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/ref.hpp>


NS_LOG_COMPONENT_DEFINE("ndn.Consumer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Consumer);

TypeId
Consumer::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::Consumer")
      .SetGroupName("Ndn")
      .SetParent<App>()
      .AddAttribute("StartSeq", "Initial sequence number", IntegerValue(0),
                    MakeIntegerAccessor(&Consumer::m_seq), MakeIntegerChecker<int32_t>())

      .AddAttribute("Prefix", "Name of the Interest", StringValue("/"),
                    MakeNameAccessor(&Consumer::m_interestName), MakeNameChecker())
      .AddAttribute("LifeTime", "LifeTime for interest packet", StringValue("2s"),
                    MakeTimeAccessor(&Consumer::m_interestLifeTime), MakeTimeChecker())

      .AddAttribute("RetxTimer",
                    "Timeout defining how frequent retransmission timeouts should be checked",
                    StringValue("50ms"),
                    MakeTimeAccessor(&Consumer::GetRetxTimer, &Consumer::SetRetxTimer),
                    MakeTimeChecker())

      .AddTraceSource("LastRetransmittedInterestDataDelay",
                      "Delay between last retransmitted Interest and received Data",
                      MakeTraceSourceAccessor(&Consumer::m_lastRetransmittedInterestDataDelay),
                      "ns3::ndn::Consumer::LastRetransmittedInterestDataDelayCallback")

      .AddTraceSource("FirstInterestDataDelay",
                      "Delay between first transmitted Interest and received Data",
                      MakeTraceSourceAccessor(&Consumer::m_firstInterestDataDelay),
                      "ns3::ndn::Consumer::FirstInterestDataDelayCallback");

  return tid;
}

Consumer::Consumer()
  : m_rand(CreateObject<UniformRandomVariable>())
  , m_seq(0)
  , m_seqMax(0)             // don't request anything
  , m_retxNum(3)      // We allow retransmition twice
{
  NS_LOG_FUNCTION_NOARGS();

  m_rtt = CreateObject<RttMeanDeviation>();
}

void
Consumer::SetRetxTimer(Time retxTimer)
{
  m_retxTimer = retxTimer;
  if (m_retxEvent.IsRunning())
  {
    // m_retxEvent.Cancel (); // cancel any scheduled cleanup events
    Simulator::Remove(m_retxEvent); // slower, but better for memory
  }

  // schedule even with new timeout
  m_retxEvent = Simulator::Schedule(m_retxTimer, &Consumer::CheckRetxTimeout, this);
}

Time
Consumer::GetRetxTimer() const
{
  return m_retxTimer;
}

// Find out which interest with sequence should be retransmitted
void
Consumer::CheckRetxTimeout()
{
  Time now = Simulator::Now();

  //Get the RTO value from RTTestimator
  Time rto = m_rtt->RetransmitTimeout();
  // NS_LOG_DEBUG ("Current RTO: " << rto.ToDouble (Time::S) << "s");

  while (!m_seqTimeouts.empty())
  {
    SeqTimeoutsContainer::index<i_timestamp>::type::iterator entry =
      m_seqTimeouts.get<i_timestamp>().begin();
    // timeout expired?
    if (entry->time + rto <= now)
    {
      uint32_t seqNo = entry->seq;
      m_seqTimeouts.get<i_timestamp>().erase(entry);
      OnTimeout(seqNo);
    }
    else
      break; // nothing else to do. All later packets need not be retransmitted
  }

  m_retxEvent = Simulator::Schedule(m_retxTimer, &Consumer::CheckRetxTimeout, this);
}

// Application Methods
void
Consumer::StartApplication() // Called at time specified by Start
{
  NS_LOG_FUNCTION_NOARGS();

  // do base stuff
  App::StartApplication();

  ScheduleNextPacket();
}

void
Consumer::StopApplication() // Called at time specified by Stop
{
  NS_LOG_FUNCTION_NOARGS();

  // cancel periodic packet generation
  Simulator::Cancel(m_sendEvent);

  // cleanup base stuff
  App::StopApplication();
}

void
Consumer::SendPacket()
{
  if (!m_active)
    return;

  NS_LOG_FUNCTION_NOARGS();

  //Find the max value of sequence number
  uint32_t seq = std::numeric_limits<uint32_t>::max(); // invalid

  //If there is something need to be retransmitted
  //Select the 1st one in this set and to send it
  while (m_retxSeqs.size())
  {
    seq = *m_retxSeqs.begin();
    m_retxSeqs.erase(m_retxSeqs.begin());
    break;
  }

  //2 Max Number of sequence numbers
  // 1 is physical max
  // 2 is application max
  if (seq == std::numeric_limits<uint32_t>::max())
  {
    if (m_seqMax != std::numeric_limits<uint32_t>::max())
    {
      if (m_seq >= m_seqMax)
      {
        return; // we are totally done
      }
    }
    seq = m_seq++;
  }

  //Setting a name for current interest
  shared_ptr<Name> nameWithSequence = make_shared<Name>(m_interestName);
  nameWithSequence->appendSequenceNumber(seq);
  //

  //Create an Interest packet
  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
  interest->setName(*nameWithSequence);
  time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
  interest->setInterestLifetime(interestLifeTime);

  // NS_LOG_INFO ("Requesting Interest: \n" << *interest);
  NS_LOG_INFO("> Interest for " << seq);

  //Save time and squence number
  //How to set RTO for an interest
 // WillSendOutInterest(seq, *nameWithSequence);
  WaitBeforeSendOutInterest(seq, *nameWithSequence);

  //------------------------------------------------------------------------
  //Debug -Yuwei
  cout<<"Send Interest="<<interest->getName()<<" Seq="<<seq<<endl;

  /*Test
  Name tmpName1("/S/NankaiDistrict/WeijingRoad/A/TrafficInformer/RoadCongestion");
  Name tmpName2("/S/NankaiDistrict/NanjingRoad/BinjiangStreet/A/TrafficInformer/RoadStatus");
  //interest->getName().CorrelativityWith(interest->getName());
  //interest->getName().CorrelativityWith(lastInterestName);
  //cout<<"CO = "<<tmpName1.correlativityWith(tmpName2)<<endl;
  cout<<tmpName1.getAppcorrelativityWith(tmpName2)<<endl;
  cout<<tmpName2.getAppcorrelativityWith(tmpName1)<<endl;

  cout<<tmpName1.getSpcorrelativityWith(tmpName2)<<endl;
  cout<<tmpName2.getSpcorrelativityWith(tmpName1)<<endl;
  */
  //cout<<"RTO by C0="<<m_rtt->CalRTObyCorrelativity(interest->getName()).ToDouble(Time::S)<<endl;

  m_transmittedInterests(interest, this, m_face);
  m_face->onReceiveInterest(*interest);

  //Do nothing?
  ScheduleNextPacket();
}

///////////////////////////////////////////////////
//          Process incoming packets             //
///////////////////////////////////////////////////

void
Consumer::OnData(shared_ptr<const Data> data)
{
  if (!m_active)
    return;

  App::OnData(data); // tracing inside

  NS_LOG_FUNCTION(this << data);

  // NS_LOG_INFO ("Received content object: " << boost::cref(*data));

  // This could be a problem......
  uint32_t seq = data->getName().at(-1).toSequenceNumber();
  NS_LOG_INFO("< DATA for " << seq);

  int hopCount = 0;
  auto ns3PacketTag = data->getTag<Ns3PacketTag>();
  if (ns3PacketTag != nullptr)
  { // e.g., packet came from local node's cache
    FwHopCountTag hopCountTag;
    if (ns3PacketTag->getPacket()->PeekPacketTag(hopCountTag)) {
      hopCount = hopCountTag.Get();
      NS_LOG_DEBUG("Hop count: " << hopCount);
    }
  }

  //--------------------------------------------------------------------------------------------------
  //Tracing
  SeqTimeoutsContainer::iterator entry = m_seqLastDelay.find(seq);
  if (entry != m_seqLastDelay.end())
  {
    m_lastRetransmittedInterestDataDelay(this, seq, Simulator::Now() - entry->time, hopCount);
  }
  entry = m_seqFullDelay.find(seq);
  if (entry != m_seqFullDelay.end())
  {
    m_firstInterestDataDelay(this, seq, Simulator::Now() - entry->time, m_seqRetxCounts[seq], hopCount);
  }
  //----------------------------------------------------------------------------------------------------

  m_seqRetxCounts.erase(seq);   //recording the transmission number of every seq
  m_seqTimeouts.erase(seq);    //record all the packets with send time for timeout
  m_retxSeqs.erase(seq);          //record the timeout seq , so consumer will send it later

  m_seqFullDelay.erase(seq);     //Tracing
  m_seqLastDelay.erase(seq);    //Tracing

  //m_rtt->AckSeq(SequenceNumber32(seq));
  shared_ptr<Name> dataName = make_shared<Name>(data->getName());

  /*---------------------------------------------------------------------------------
  //Yuwei
  cout<<"Get Data="<<data->getName()
		  <<" Seq="<<seq
		  <<" Time="<<Simulator::Now().ToDouble(Time::S)
		  <<endl;*/
  //---------------------------------------------------------------------------------
  m_rtt->AckSeq(*dataName, SequenceNumber32(seq));
}

void
Consumer::OnTimeout(uint32_t sequenceNumber)
{
  NS_LOG_FUNCTION(sequenceNumber);
  // std::cout << Simulator::Now () << ", TO: " << sequenceNumber << ", current RTO: " <<
  // m_rtt->RetransmitTimeout ().ToDouble (Time::S) << "s\n";

  m_rtt->IncreaseMultiplier(); // Double the next RTO

  //-----------------------------------------------------------------------------------
  //Yuwei

  if(m_seqRetxCounts[sequenceNumber] < GetRetxNumber())
  {
	  //cout<<"SEQ: "<<sequenceNumber<<" transmitted "<<m_seqRetxCounts[sequenceNumber]<<"times."<<endl;
	  m_rtt->SentSeq(SequenceNumber32(sequenceNumber),1); // make sure to disable RTT calculation for this sample
	  m_retxSeqs.insert(sequenceNumber);     //insert data into retransmitted table
	  ScheduleNextPacket();
  }
  else
  {
	  //cout<<"Discard! "<<sequenceNumber<<" = "<<m_seqRetxCounts[sequenceNumber]<<endl;
	  //discard this interest
	  m_seqRetxCounts.erase(sequenceNumber);   //recording the transmission number of every seq
	  m_seqFullDelay.erase(sequenceNumber);     //Tracing
	  m_seqLastDelay.erase(sequenceNumber);    //Tracing
	  m_seqTimeouts.erase(sequenceNumber);    //record all the packets with send time for timeout
	  m_rtt->DiscardInterestBySeq(SequenceNumber32(sequenceNumber));     //delete record in rtthistory
  }
}

void
Consumer::WillSendOutInterest(uint32_t sequenceNumber)
{
  NS_LOG_DEBUG("Trying to add " << sequenceNumber << " with " << Simulator::Now() << ". already "
                                << m_seqTimeouts.size() << " items");

  //Save the time of interest with sequence number
  m_seqTimeouts.insert(SeqTimeout(sequenceNumber, Simulator::Now()));
  m_seqFullDelay.insert(SeqTimeout(sequenceNumber, Simulator::Now()));

  m_seqLastDelay.erase(sequenceNumber);
  m_seqLastDelay.insert(SeqTimeout(sequenceNumber, Simulator::Now()));

  m_seqRetxCounts[sequenceNumber]++;

  m_rtt->SentSeq(SequenceNumber32(sequenceNumber), 1);
}

//=======================================================
//Yuwei
void
Consumer::WaitBeforeSendOutInterest(uint32_t sequenceNumber, Name name)
{
	  NS_LOG_DEBUG("Trying to add " << sequenceNumber << " with " << Simulator::Now() << ". already "
	                                << m_seqTimeouts.size() << " items");

	  //Save the time of interest with sequence number
	  m_seqTimeouts.insert(SeqTimeout(sequenceNumber, Simulator::Now()));
	  m_seqFullDelay.insert(SeqTimeout(sequenceNumber, Simulator::Now()));

	  m_seqLastDelay.erase(sequenceNumber);
	  m_seqLastDelay.insert(SeqTimeout(sequenceNumber, Simulator::Now()));

	  m_seqRetxCounts[sequenceNumber]++;

	  //m_rtt->SentSeq(SequenceNumber32(sequenceNumber), 1);
	  m_rtt->SetInterestInfo(name, SequenceNumber32(sequenceNumber), 1);
}
void
Consumer::SetPrefix(string pre)
{
	m_interestName.clear();
	cout<<m_interestName<<endl;
	m_interestName.set(pre);
	cout<<m_interestName<<endl;
}

//-----------------------------------------------------------------------------------------------
void
Consumer::SetRetxNumber(uint32_t num)
{
	m_retxNum = num;
}
uint32_t
Consumer::GetRetxNumber() const
{
	return m_retxNum;
}

//=======================================================
} // namespace ndn
} // namespace ns3
