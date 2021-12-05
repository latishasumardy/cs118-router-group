/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/***
 * Copyright (c) 2017 Alexander Afanasyev
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */


#include "simple-router.hpp"
#include "core/utils.hpp"

#include <fstream>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD
void
SimpleRouter::handlePacket(const Buffer& packet, const std::string& inIface)
{
  std::cerr << "Got packet of size " << packet.size() << " on interface " << inIface << std::endl;

  const Interface* iface = findIfaceByName(inIface);
  if (iface == nullptr) {
    std::cerr << "Received packet, but interface is unknown, ignoring" << std::endl;
    return;
  }

  std::cerr << getRoutingTable() << std::endl;

  // FILL THIS IN

  /*check ethernet header */

  ethernet_hdr* ethHdr = (ethernet_hdr*)packet.data();
  //check destination
  bool flag_match = true;
  static const uint8_t BroadcastEtherAddr[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t* my_addr = iface->addr.data();
  const uint8_t* dest_addr = ethHdr->ether_dhost;

  for(int i = 0; i < 6; i++) {
    if(dest_addr[i] != BroadcastEtherAddr[i] ) {
      flag_match = false;
      i = 6;
    }
  }

  if (flag_match == false) {
    flag_match = true;

    for(int i = 0; i < 6; i++) {
      if(dest_addr[i] != my_addr[i] ) {
        flag_match = false;
        i = 6;
      }
    }

    if(flag_match == false) {
      std::cerr << "packet not destined for this router " << std::endl;
      return;
    }

  }
  

  //check type
  if(ethHdr->ether_type != htons(ethertype_ip)) {
    std::cerr << "packet type is not IPv4" << std::endl;
    return;
  }

  /*check ip header*/
  ip_hdr* ipHdr = (ip_hdr*)(packet.data() + sizeof(ethernet_hdr));

  //min length check
  if(ipHdr->ip_len < 20) {
    std::cerr << "IP total length field is less than 20" << std::endl;
    return;
  } 

  //ttl field??

  //check checksum
  uint16_t initial_checksum = ipHdr->ip_sum;
  ipHdr->ip_sum = 0; 
  uint16_t generated_sum = cksum(ipHdr, sizeof(ip_hdr));

  if(initial_checksum != generated_sum){
    std::cerr << "Checksums do not match" << std::endl;
    return;
  }

  /*check if packet is for one of our interfaces*/
  const Interface* My_Interface = findIfaceByIp(ipHdr->ip_dst);
  if(My_Interface != nullptr) { //for us
    //check if icmp
    if(ipHdr->ip_p == ip_protocol_icmp) {
      icmp_hdr* icmpHdr = (icmp_hdr*)(packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr));

      //check if icmp checksum is correct
      uint16_t initial_icmp_checksum = icmpHdr->icmp_sum;
      icmpHdr->icmp_sum = 0;
      uint16_t generated_icmp_sum = cksum(icmpHdr, packet.size() - sizeof(ethernet_hdr) - sizeof(ip_hdr));

      if(initial_icmp_checksum != generated_icmp_sum) {
        std::cerr << "ICMP checksums do not match" << std::endl;
        return;
      }

      //check type of ICMP packet to make sure its an echo
      if(icmpHdr->icmp_type != 8) {
        std::cerr << "ICMP packet is not an echo" << std::endl;
        return;
      }

      /*Send out packet*/
      

    }
    else {
      std::cerr << "packet is not ICMP" << std::endl;
      return;
    }

  }
  else { //forward bc packet is not for us

  }


}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.
SimpleRouter::SimpleRouter()
  : m_arp(*this)
{
}

void
SimpleRouter::sendPacket(const Buffer& packet, const std::string& outIface)
{
  m_pox->begin_sendPacket(packet, outIface);
}

bool
SimpleRouter::loadRoutingTable(const std::string& rtConfig)
{
  return m_routingTable.load(rtConfig);
}

void
SimpleRouter::loadIfconfig(const std::string& ifconfig)
{
  std::ifstream iff(ifconfig.c_str());
  std::string line;
  while (std::getline(iff, line)) {
    std::istringstream ifLine(line);
    std::string iface, ip;
    ifLine >> iface >> ip;

    in_addr ip_addr;
    if (inet_aton(ip.c_str(), &ip_addr) == 0) {
      throw std::runtime_error("Invalid IP address `" + ip + "` for interface `" + iface + "`");
    }

    m_ifNameToIpMap[iface] = ip_addr.s_addr;
  }
}

void
SimpleRouter::printIfaces(std::ostream& os)
{
  if (m_ifaces.empty()) {
    os << " Interface list empty " << std::endl;
    return;
  }

  for (const auto& iface : m_ifaces) {
    os << iface << "\n";
  }
  os.flush();
}

const Interface*
SimpleRouter::findIfaceByIp(uint32_t ip) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [ip] (const Interface& iface) {
      return iface.ip == ip;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

const Interface*
SimpleRouter::findIfaceByMac(const Buffer& mac) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [mac] (const Interface& iface) {
      return iface.addr == mac;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

void
SimpleRouter::reset(const pox::Ifaces& ports)
{
  std::cerr << "Resetting SimpleRouter with " << ports.size() << " ports" << std::endl;

  m_arp.clear();
  m_ifaces.clear();

  for (const auto& iface : ports) {
    auto ip = m_ifNameToIpMap.find(iface.name);
    if (ip == m_ifNameToIpMap.end()) {
      std::cerr << "IP_CONFIG missing information about interface `" + iface.name + "`. Skipping it" << std::endl;
      continue;
    }

    m_ifaces.insert(Interface(iface.name, iface.mac, ip->second));
  }

  printIfaces(std::cerr);
}

const Interface*
SimpleRouter::findIfaceByName(const std::string& name) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [name] (const Interface& iface) {
      return iface.name == name;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}


} // namespace simple_router {
