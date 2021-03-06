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

#ifndef NDN_CONSUMER_RANDOM_CBR_H
#define NDN_CONSUMER_RANDOM_CBR_H

#include "ns3/ndnSIM/model/ndn-common.hpp"

#include "ndn-consumer.hpp"

namespace ns3 {
namespace ndn {

//============================================================================
// Random Cbr Name Tree
class NsNode{
public:
	NsNode(string str);
	~NsNode();
	void CreateChilds(unsigned short num);
	string GetString(void);
	NsNode* GetChildByIndex(unsigned short i);
	unsigned short GetChildNum(void);
	void Print(void);
private:
	string m_element;
	list<NsNode*> p_childs;
};

class NsTree{
public:
	NsTree(string r);
	~NsTree();
	void InitBuild(unsigned short levels, unsigned short maxChilds);
	void Build(NsNode* r,unsigned short levels, unsigned short maxChilds);
	string GetName(NsNode* node, unsigned int levels);
	string GetRandomName(void);
	int GetLevels();
	void Print(void);
	//---------------------------------------------------------------------------
	void BuildScene(string sc);
private:
		NsNode* root;
		int levels;
};
//============================================================================
/**
 * @ingroup ndn-apps
 * @brief Ndn application for sending out Interest packets at a "constant" rate (Poisson process)
 * and with different names
 */
class ConsumerRandomCbr : public Consumer {
public:
  static TypeId
  GetTypeId();

  /**
   * \brief Default constructor
   * Sets up randomizer function and packet sequence number
   */
  ConsumerRandomCbr();
  virtual ~ConsumerRandomCbr();

protected:
  /**
   * \brief Constructs the Interest packet and sends it using a callback to the underlying NDN
   * protocol
   */
  virtual void
  ScheduleNextPacket();

  /**
   * @brief Set type of frequency randomization
   * @param value Either 'none', 'uniform', or 'exponential'
   */
  void
  SetRandomize(const std::string& value);

  /**
   * @brief Get type of frequency randomization
   * @returns either 'none', 'uniform', or 'exponential'
   */
  std::string
  GetRandomize() const;

protected:
  double m_frequency; // Frequency of interest packets (in hertz)
  bool m_firstTime;
  Ptr<RandomVariableStream> m_random;
  std::string m_randomType;
  //==========================================
  NsTree aNameTree;
  NsTree sNameTree;

};

} // namespace ndn
} // namespace ns3

#endif
