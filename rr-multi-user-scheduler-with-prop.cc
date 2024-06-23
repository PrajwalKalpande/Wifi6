/*
 * Copyright (c) 2020 Universita' degli Studi di Napoli Federico II
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
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "rr-multi-user-scheduler.h"
#include <unordered_map>
#include "he-configuration.h"
#include "he-frame-exchange-manager.h"
#include "he-phy.h"

#include "ns3/log.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-protection.h"
#include "ns3/wifi-psdu.h"
#include "ns3/string.h"
#include "multi-user-scheduler.h"


#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <fstream>
#include <algorithm>
#include <type_traits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RrMultiUserScheduler");

NS_OBJECT_ENSURE_REGISTERED(RrMultiUserScheduler);


ns3::Time total_bsrp_time = TimeStep(0);
bool last_tx_bsrp = false;
ns3::Time bsrp_start = TimeStep(0);

ns3::Time total_basic_time = TimeStep(0);
bool last_tx_basic = false;
ns3::Time basic_start = TimeStep(0);

bool bsrp_limit = false;
int count = 10;

bool prop_scheduler = true;

Time ul_time = Seconds(0);
Time payload_ul_time = Seconds(0),total_ul_time = Seconds(0);

TypeId
RrMultiUserScheduler::GetTypeId()
{
    
    static TypeId tid =
        TypeId("ns3::RrMultiUserScheduler")
            .SetParent<MultiUserScheduler>()
            .SetGroupName("Wifi")
            .AddConstructor<RrMultiUserScheduler>()
            .AddAttribute("NStations",
                          "The maximum number of stations that can be granted an RU in a DL MU "
                          "OFDMA transmission",
                          UintegerValue(4),
                          MakeUintegerAccessor(&RrMultiUserScheduler::m_nStations),
                          MakeUintegerChecker<uint8_t>(1, 74))
            .AddAttribute("EnableTxopSharing",
                          "If enabled, allow A-MPDUs of different TIDs in a DL MU PPDU.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableTxopSharing),
                          MakeBooleanChecker())
            .AddAttribute("ForceDlOfdma",
                          "If enabled, return DL_MU_TX even if no DL MU PPDU could be built.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_forceDlOfdma),
                          MakeBooleanChecker())
            .AddAttribute("EnableUlOfdma",
                          "If enabled, return UL_MU_TX if DL_MU_TX was returned the previous time.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableUlOfdma),
                          MakeBooleanChecker())
            .AddAttribute("EnableBsrp",
                          "If enabled, send a BSRP Trigger Frame before an UL MU transmission.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableBsrp),
                          MakeBooleanChecker())
            .AddAttribute("DLSchedulerLogic",
                          "Standard or Bellalta",
                          StringValue("Standard"),
                          MakeStringAccessor (&RrMultiUserScheduler::m_dlschedulerLogic),
                          MakeStringChecker ())
            .AddAttribute("ULSchedulerLogic",
                          "Standard or Bellalta",
                          StringValue ("Standard"),
                          MakeStringAccessor (&RrMultiUserScheduler::m_ulschedulerLogic),
                          MakeStringChecker ())
            .AddAttribute("Width",
                          "20, 40, 80 or 160",
                          StringValue ("20"),
                          MakeStringAccessor (&RrMultiUserScheduler::m_channelWidth),
                          MakeStringChecker ())
            .AddAttribute(
                "UlPsduSize",
                "The default size in bytes of the solicited PSDU (to be sent in a TB PPDU)",
                UintegerValue(500),
                MakeUintegerAccessor(&RrMultiUserScheduler::m_ulPsduSize),
                MakeUintegerChecker<uint32_t>())
            .AddAttribute("UseCentral26TonesRus",
                          "If enabled, central 26-tone RUs are allocated, too, when the "
                          "selected RU type is at least 52 tones.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_useCentral26TonesRus),
                          MakeBooleanChecker())
            .AddAttribute(
                "MaxCredits",
                "Maximum amount of credits a station can have. When transmitting a DL MU PPDU, "
                "the amount of credits received by each station equals the TX duration (in "
                "microseconds) divided by the total number of stations. Stations that are the "
                "recipient of the DL MU PPDU have to pay a number of credits equal to the TX "
                "duration (in microseconds) times the allocated bandwidth share",
                TimeValue(Seconds(1)),
                MakeTimeAccessor(&RrMultiUserScheduler::m_maxCredits),
                MakeTimeChecker());
    return tid;
}

 

std::vector<HeRu::RuSpec>
RrMultiUserScheduler::prop_scheduler_fun(std::list<std::pair<std::list<MasterInfo>::iterator, 
Ptr<WifiMpdu>>> m_candidates, uint16_t ch_width, bool ul){
    std::vector<HeRu::RuSpec> allocation;
    std::vector<int> ru_array;
    //////////////////////////////////////////
    std::vector<int> queue_array;
        double queue_sum = 0;
        if(ul){
            std::cout << "UL Queue size: ";    
        }else{
            std::cout << "DL Queue size: ";
        }
        for(auto it: m_candidates){
            int sz = 0;
            if(ul) sz = m_apMac->GetMaxBufferStatus(it.first->address);
            else{
                sz = (dlqueueinfo.find(*(it.first)))->second;
            }
            std::cout << "station: "<< it.first->address << " " << sz << " ";
            queue_array.push_back(sz);
            queue_sum += sz;
        }std::cout << "\n";

    if(ch_width == 20){ // 20MHz
        int total_width = 9;

        std::cout << "queue normal: ";
        for (int i = 0; i < int(queue_array.size()); i++)
        {
            queue_array[i] = int((queue_array[i]/queue_sum)*total_width);
            std::cout << queue_array[i] << " ";   
        }std::cout << "\n";

        int ru_106 = 0;
        int ru_52 = 0;
        int ru_26 = 0;

        for (int i = 0; i < int(queue_array.size()); i++)
        {
            if(ru_array.size() >= 9){
                break;
            }
            if(queue_array[i] == 1){
                if(ru_26 >= 9){
                    continue;
                }
                ru_array.push_back(26);
                total_width -=1;
                ru_26++;
            }else if(queue_array[i] >= 2 && queue_array[i] < 4){
                if(ru_52 >= 4){
                    continue;
                }
                ru_array.push_back(52);
                total_width-=2;
                ru_52++;
            }else if(queue_array[i] >= 4 && queue_array[i] < 9){
                if(ru_106 >= 2){
                    continue;
                }
                ru_array.push_back(106);
                total_width-=4;
                ru_106++;
            }else if(queue_array[i] == 9){
                ru_array.push_back(242);
                total_width-=9;
                break;
            }
                        
        }
        for (int i = 0; i < int(ru_array.size()); i++)
            {
                if(ru_array[i] == 106){
                    if(total_width >= 5){
                        ru_array[i] = 242;
                        ru_106--;
                        break;
                    }

                }else if(ru_array[i] == 52){
                    if(total_width >= 7){
                        ru_array[i] = 242;
                        ru_52--;
                        break;
                    }
                    else if(total_width >= 2){
                        if(ru_106 >= 2) continue;
                        ru_array[i] = 106;
                        ru_106++;
                        ru_52--;
                        total_width-=2;
                    }

                }else if(ru_array[i] == 26){
                    if(total_width >= 8){
                        ru_array[i] = 242;
                        ru_26--;
                        break;
                    }
                    else if(total_width >= 3){
                        if(ru_106 >= 2) continue;
                        ru_array[i] = 106;
                        ru_106++;
                        ru_26--;
                        total_width-=3;
                    }
                    else if(total_width >= 1){
                        if(ru_52 >= 4) continue;
                        ru_array[i] = 52;
                        ru_52++;
                        ru_26--;
                        total_width-=1;
                    }

                }
            }
        std::cout << "RU array after first correction and before allocation: ";
        for (int i = 0; i < int(ru_array.size()); i++)
        {
            std::cout << ru_array[i] << " ";
        }std::cout << "\n";
        
        
        int start_index = 1;    
        for (int i = 0; i < int(ru_array.size()); i++)
        {
            if(start_index > 9){
                if(ru_array[i] != 26){
                    break;
                }
            }
            if(ru_array[i] == 242){
                if(start_index == 5) start_index++;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_242_TONE, start_index, true);
                allocation.push_back(*(ruSet.begin()));
                break;
            }else if(ru_array[i] == 106){
                if(start_index + 4 > 10) break;
                if(start_index == 5) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=4) final_index = 1;
                if(start_index>=6 && start_index <=9) final_index = 2;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_106_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=4;
            }else if(ru_array[i] == 52){
                if(start_index + 2 > 10) break;
                if(start_index == 5) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=2) final_index = 1;
                if(start_index>=3 && start_index <=4) final_index = 2;
                if(start_index>=6 && start_index <=7) final_index = 3;
                if(start_index>=8 && start_index <=9) final_index = 4;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_52_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=2;
            }else{
                std::cout << "knadsmcjwx" << "\n";
                if(i == (int(ru_array.size()) -1)){
                    std::cout << "YESSS" << "\n";
                    auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_26_TONE, 5, true);
                    allocation.push_back(*(ruSet.begin()));
                }else{
                if(start_index + 1 > 10) break;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_26_TONE, start_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=1;
                }
            }
        }
        
    ///////////////////////////////////////   
    }else if(ch_width == 40){ //40MHz
        int total_width = 18;
        
        std::cout << "Queue array: ";
        for (int i = 0; i < int(queue_array.size()); i++)
        {
            queue_array[i] = int((queue_array[i]/queue_sum)*total_width);
            std::cout << queue_array[i] << " ";
        }std::cout << "\n";

        int ru_242 = 0;
        int ru_106 = 0;
        int ru_52 = 0;
        int ru_26 = 0;

        for (int i = 0; i < int(queue_array.size()); i++)
        {
            if(ru_array.size() >= 18){
                break;
            }
            
            if(queue_array[i] == 1){
                if(ru_26 >= 18){
                    continue;
                }
                ru_array.push_back(26);
                total_width-=1;
                ru_26++;
            }else if(queue_array[i] >= 2 && queue_array[i] < 4){
                if(ru_52 >= 8){
                    continue;
                }
                ru_array.push_back(52);
                total_width-=2;
                ru_52++;
            }else if(queue_array[i] >= 4 && queue_array[i] < 9){
                if(ru_106 >= 4){
                    continue;
                }
                ru_array.push_back(106);
                total_width-=4;
                ru_106++;
            }else if(queue_array[i] >= 9 && queue_array[i] < 18){
                if(ru_242 >= 2){
                    continue;
                }
                ru_array.push_back(242);
                total_width-=9;
                ru_242++;
            }else if(queue_array[i] == 18){
                ru_array.push_back(484);
                total_width-=18;
                break;
            }  
        }
        std::cout << "RU array start: ";
        for(auto it: ru_array){
            std::cout << it << " ";
        }
        std::cout << "\n";

        for (int i = 0; i < int(ru_array.size()); i++)
            {
                if(ru_array[i] == 242){
                    if(total_width >= 9){
                        ru_array[i] = 484;
                        ru_242--;
                        break;
                    }
                }
                else if(ru_array[i] == 106){
                    if(total_width >= 14){
                        ru_array[i] = 484;
                        ru_106--;
                        break;
                    }
                    else if(total_width >= 5){
                        if(ru_242 >= 2) continue;
                        ru_array[i] = 242;
                        ru_242++;
                        ru_106--;
                        total_width-=5;
                    }

                }else if(ru_array[i] == 52){
                    if(total_width >= 16){
                        ru_array[i] = 484;
                        ru_52--;
                        break;
                    }
                    else if(total_width >= 7){
                        if(ru_242 >= 2) continue;
                        ru_array[i] = 242;
                        ru_242++;
                        ru_52--;
                        total_width-=7;
                    }
                    else if(total_width >= 2){
                        if(ru_106 >= 4) continue;
                        ru_array[i] = 106;
                        ru_106++;
                        ru_52--;
                        total_width-=2;
                    }

                }else if(ru_array[i] == 26){
                    if(total_width >= 17){
                        ru_array[i] = 484;
                        ru_26--;
                        break;
                    }
                    else if(total_width >= 8){
                        if(ru_242 >= 2) continue;
                        ru_array[i] = 242;
                        ru_242++;
                        ru_26--;
                        total_width-=8;
                    }
                    else if(total_width >= 3){
                        if(ru_106 >= 4) continue;
                        ru_array[i] = 106;
                        ru_106++;
                        ru_26--;
                        total_width-=3;
                    }
                    else if(total_width >= 1){
                        if(ru_52 >= 8) continue;
                        ru_array[i] = 52;
                        ru_52++;
                        ru_26--;
                        total_width-=1;
                    }

                }
            }
        int start_index = 1; 
        bool index_14 = false;
        bool index_5 = false;   
        for (int i = 0; i < int(ru_array.size()); i++)
        {
            if(start_index > 18){
                if(ru_array[i] != 26){
                    break;
                }
                
            }
            if(ru_array[i] == 484){
                if(start_index == 5 || start_index == 14) start_index++;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_484_TONE, start_index, true);
                allocation.push_back(*(ruSet.begin()));
                break;
            }
            else if(ru_array[i] == 242){
                if(start_index + 9 > 19) break;
                if(start_index == 5 || start_index == 14) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=9) final_index = 1;
                if(start_index>=10 && start_index <=18) final_index = 2;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_242_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=9; 
            }else if(ru_array[i] == 106){
                if(start_index + 4 > 19) break;
                if(start_index == 5 || start_index == 14) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=4) final_index = 1;
                if(start_index>=6 && start_index <=9) final_index = 2;
                if(start_index>=10 && start_index <=13) final_index = 3;
                if(start_index>=15 && start_index <=18) final_index = 4;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_106_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=4;
            }else if(ru_array[i] == 52){
                if(start_index + 2 > 19) break;
                if(start_index == 5 || start_index == 14) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=2) final_index = 1;
                if(start_index>=3 && start_index <=4) final_index = 2;
                if(start_index>=6 && start_index <=7) final_index = 3;
                if(start_index>=8 && start_index <=9) final_index = 4;
                if(start_index>=10 && start_index <=11) final_index = 5;
                if(start_index>=12 && start_index <=13) final_index = 6;
                if(start_index>=15 && start_index <=16) final_index = 7;
                if(start_index>=17 && start_index <=18) final_index = 8;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_52_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=2;
            }else if(ru_array[i] == 26){
                if(i == (int(ru_array.size()) -1) || i == (int(ru_array.size()) -2)){
                    std::cout << "YESSS" << "\n";
                    int index_26tone = 5;
                    if(index_5 == false || index_14 == false){
                    if(index_5 == true){
                        index_26tone = 14;
                        index_14 = true;
                    }else{
                        index_5 = true;
                    }
                    auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_26_TONE, index_26tone, true);
                    allocation.push_back(*(ruSet.begin()));
                    }
                    else{
                        break;
                    }
                }else{
                if(start_index + 1 > 19) break;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_26_TONE, start_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=1;
                }
            }
        }
    ///////////////////////////////////
    }else if(ch_width == 80){ //80MHz
        int total_width = 37;
        
        for (int i = 0; i < int(queue_array.size()); i++)
        {
            queue_array[i] = int((queue_array[i]/queue_sum)*total_width);
        }
        
        int ru_484 = 0;
        int ru_242 = 0;
        int ru_106 = 0;
        int ru_52 = 0;
        int ru_26 = 0;

        for (int i = 0; i < int(queue_array.size()); i++)
        {
            if(ru_array.size() >= 37){
                break;
            }
            
            if(queue_array[i] == 1){
                if(ru_26 >= 37){
                    continue;
                }
                ru_array.push_back(26);
                total_width-=1;
                ru_26++;
            }else if(queue_array[i] >= 2 && queue_array[i] < 4){
                if(ru_52 >= 16){
                    continue;
                }
                ru_array.push_back(52);
                total_width-=2;
                ru_52++;
            }else if(queue_array[i] >= 4 && queue_array[i] < 9){
                if(ru_106 >= 8){
                    continue;
                }
                ru_array.push_back(106);
                total_width-=4;
                ru_106++;
            }else if(queue_array[i] >= 9 && queue_array[i] < 18){
                if(ru_242 >= 4){
                    continue;
                }
                ru_array.push_back(242);
                total_width-=9;
                ru_242++;
            }else if(queue_array[i] >= 18 && queue_array[i] < 37){
                if(ru_484 >= 2){
                    continue;
                }
                ru_array.push_back(484);
                total_width-=18;
                ru_484++;
            }else if(queue_array[i] == 37){
                ru_array.push_back(996);
                total_width-=37;
                break;
            }
        }
        for (int i = 0; i < int(ru_array.size()); i++)
            {
                if(ru_array[i] == 484){
                    if(total_width >= 19){
                        ru_array[i] = 996;
                        ru_484--;
                        break;
                    }
                }
                else if(ru_array[i] == 242){
                    if(total_width >= 28){
                        ru_array[i] = 996;
                        ru_242--;
                        break;
                    }
                    else if(total_width >= 9){
                        if(ru_484 >= 2) continue;
                        ru_array[i] = 484;
                        ru_484++;
                        ru_242--;
                        total_width-=9;
                    }
                }
                else if(ru_array[i] == 106){
                    if(total_width >= 33){
                        ru_array[i] = 996;
                        ru_106--;
                        break;
                    }
                    else if(total_width >= 14){
                        if(ru_242 >= 2) continue;
                        ru_array[i] = 484;
                        ru_484++;
                        ru_106--;
                        total_width-=14;
                    }
                    else if(total_width >= 5){
                        if(ru_242 >= 4) continue;
                        ru_array[i] = 242;
                        ru_242++;
                        ru_106--;
                        total_width-=5;
                    }

                }else if(ru_array[i] == 52){
                    if(total_width >= 35){
                        ru_array[i] = 996;
                        ru_52--;
                        break;
                    }
                    else if(total_width >= 16){
                        if(ru_484 >= 2) continue;
                        ru_array[i] = 484;
                        ru_484++;
                        ru_52--;
                        total_width-=16;
                    }
                    else if(total_width >= 7){
                        if(ru_242 >= 4) continue;
                        ru_array[i] = 242;
                        ru_242++;
                        ru_52--;
                        total_width-=7;
                    }
                    else if(total_width >= 2){
                        if(ru_106 >= 8) continue;
                        ru_array[i] = 106;
                        ru_106++;
                        ru_52--;
                        total_width-=2;
                    }

                }else if(ru_array[i] == 26){
                    if(total_width >= 36){
                        ru_array[i] = 996;
                        ru_26--;
                        break;
                    }
                    else if(total_width >= 17){
                        if(ru_484 >= 2) continue;
                        ru_array[i] = 484;
                        ru_484++;
                        ru_26--;
                        total_width -= 17;
                    }
                    else if(total_width >= 8){
                        if(ru_242 >= 4) continue;
                        ru_array[i] = 242;
                        ru_242++;
                        ru_26--;
                        total_width-=8;
                    }
                    else if(total_width >= 3){
                        if(ru_106 >= 8) continue;
                        ru_array[i] = 106;
                        ru_106++;
                        ru_26--;
                        total_width-=3;
                    }
                    else if(total_width >= 1){
                        if(ru_52 >= 16) continue;
                        ru_array[i] = 52;
                        ru_52++;
                        ru_26--;
                        total_width-=1;
                    }

                }
            }
        int start_index = 1;    
        for (int i = 0; i < int(ru_array.size()); i++)
        {
            if(start_index > 37){
                break;
            }
            if(ru_array[i] == 996){
                if(start_index == 5 || start_index == 14 || start_index == 19 || start_index == 24 || start_index == 33) start_index++;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_996_TONE, start_index, true);
                allocation.push_back(*(ruSet.begin()));
                break;
            }
            else if(ru_array[i] == 484){
                if(start_index + 18 > 38) break;
                if(start_index == 5 || start_index == 14 || start_index == 19 || start_index == 24 || start_index == 33) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=18) final_index = 1;
                if(start_index>=20 && start_index <=37) final_index = 2;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_484_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=18;
                }
            else if(ru_array[i] == 242){
                if(start_index + 9 > 38) break;
                if(start_index == 5 || start_index == 14 || start_index == 19 || start_index == 24 || start_index == 33) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=9) final_index = 1;
                if(start_index>=10 && start_index <=18) final_index = 2;
                if(start_index>=20 && start_index <=28) final_index = 3;
                if(start_index>=29 && start_index <=37) final_index = 4;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_242_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=9; 
            }else if(ru_array[i] == 106){
                if(start_index + 4 > 38) break;
                if(start_index == 5 || start_index == 14 || start_index == 19 || start_index == 24 || start_index == 33) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=4) final_index = 1;
                if(start_index>=6 && start_index <=9) final_index = 2;
                if(start_index>=10 && start_index <=13) final_index = 3;
                if(start_index>=15 && start_index <=18) final_index = 4;
                if(start_index>=20 && start_index <=23) final_index = 5;
                if(start_index>=25 && start_index <=28) final_index = 6;
                if(start_index>=29 && start_index <=32) final_index = 7;
                if(start_index>=34 && start_index <=37) final_index = 8;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_106_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=4;
            }else if(ru_array[i] == 52){
                if(start_index + 2 > 38) break;
                if(start_index == 5 || start_index == 14 || start_index == 19 || start_index == 24 || start_index == 33) start_index++;
                int final_index = start_index;
                if(start_index>=1 && start_index <=2) final_index = 1;
                if(start_index>=3 && start_index <=4) final_index = 2;
                if(start_index>=6 && start_index <=7) final_index = 3;
                if(start_index>=8 && start_index <=9) final_index = 4;
                if(start_index>=10 && start_index <=11) final_index = 5;
                if(start_index>=12 && start_index <=13) final_index = 6;
                if(start_index>=15 && start_index <=16) final_index = 7;
                if(start_index>=17 && start_index <=18) final_index = 8;
                if(start_index>=20 && start_index <=21) final_index = 9;
                if(start_index>=22 && start_index <=23) final_index = 10;
                if(start_index>=25 && start_index <=26) final_index = 11;
                if(start_index>=27 && start_index <=28) final_index = 12;
                if(start_index>=29 && start_index <=30) final_index = 13;
                if(start_index>=31 && start_index <=32) final_index = 14;
                if(start_index>=34 && start_index <=35) final_index = 15;
                if(start_index>=36 && start_index <=37) final_index = 16;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_52_TONE, final_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=2;
            }else if(ru_array[i] == 26){
                if(start_index + 1 > 38) break;
                auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), HeRu::RU_26_TONE, start_index, true);
                allocation.push_back(*(ruSet.begin()));
                start_index+=1;
            }
        }
    }
    std::cout << "RU array: ";
    for(auto it: ru_array){
        std::cout << it << " ";
    }
    std::cout << "\n";

    std::cout << "RU allocation: ";
    for(auto it: allocation){
        std::cout << it << " "; 
    }
    std::cout << "\n";
    
    return allocation;
}

RrMultiUserScheduler::RrMultiUserScheduler()
{
    NS_LOG_FUNCTION(this);
}

RrMultiUserScheduler::~RrMultiUserScheduler()
{
    NS_LOG_FUNCTION_NOARGS();
}

void
RrMultiUserScheduler::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_apMac);
    m_apMac->TraceConnectWithoutContext(
        "AssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationAssociated, this));
    m_apMac->TraceConnectWithoutContext(
        "DeAssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationDeassociated, this));
    for (const auto& ac : wifiAcList)
    {
        m_staListDl.insert({ac.first, {}});
    }
    MultiUserScheduler::DoInitialize();
}

void
RrMultiUserScheduler::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_staListDl.clear();
    m_staListUl.clear();
    m_candidates.clear();
    m_txParams.Clear();
    m_apMac->TraceDisconnectWithoutContext(
        "AssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationAssociated, this));
    m_apMac->TraceDisconnectWithoutContext(
        "DeAssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationDeassociated, this));
    MultiUserScheduler::DoDispose();
}


MultiUserScheduler::TxFormat
RrMultiUserScheduler::SelectTxFormat()
{
    // std::cout << "At time "<< Simulator::Now()<<" SelectTxFormat called"<<"\n";
    if(last_tx_bsrp){
        total_bsrp_time += (Simulator::Now() - bsrp_start);
        // std::cout << "duration bsrp: "<<(Simulator::Now() - bsrp_start)<<"\n";
        // std::cout << "Total BSRP time: "<< total_bsrp_time <<"\n";
    }
    last_tx_bsrp = false;

    if(last_tx_basic){
        total_basic_time += (Simulator::Now() - basic_start);
        // std::cout << "duration basic" <<(Simulator::Now() - bsrp_start)<<"\n";
        // std::cout << "Total BASIC time: "<< total_basic_time <<"\n";
    }
    last_tx_basic = false;

    
    NS_LOG_FUNCTION(this);

    Ptr<const WifiMpdu> mpdu = m_edca->PeekNextMpdu(m_linkId);
    // std::cout << "linkId: "<< unsigned(m_linkId) <<"\n";

    // if(!mpdu) std::cout << "mpdu zero in selecttx"<<"\n";

    if (mpdu && !m_apMac->GetHeSupported(mpdu->GetHeader().GetAddr1()))
    {
        return SU_TX;
    }

    if (m_enableUlOfdma && m_enableBsrp && (GetLastTxFormat(m_linkId) == DL_MU_TX || !mpdu) && 
    (m_trigger.GetType() != TriggerFrameType::BSRP_TRIGGER))
    {
        TxFormat txFormat = TrySendingBsrpTf();

        if (txFormat != DL_MU_TX)
        {

            if(bsrp_limit){
                count--;
                if(count <= 0){
                    count = 10;
                    return txFormat;
                }

            }else{
                last_tx_bsrp = true;
                bsrp_start = Simulator::Now();
                return txFormat;
            }
        }
    }
    else if (m_enableUlOfdma && ((GetLastTxFormat(m_linkId) == DL_MU_TX) ||
                                 (m_trigger.GetType() == TriggerFrameType::BSRP_TRIGGER) || !mpdu))
    {
        TxFormat txFormat = TrySendingBasicTf();

        if (txFormat != DL_MU_TX)
        {
            last_tx_basic = true;
            basic_start = Simulator::Now();
            
            return txFormat;
        }
    }

    return TrySendingDlMuPpdu();
}

template <class Func>
WifiTxVector
RrMultiUserScheduler::GetTxVectorForUlMu(Func canBeSolicited, bool isbasictf)
{

    if(isbasictf){// Basic TF
    // std::cout << "At time "<< Simulator::Now()<<" GetTxVectorForUlMu called"<<"\n";
    NS_LOG_FUNCTION(this);

    bool scheduler_x = true; // rr
    if(m_ulschedulerLogic == "Bellalta") scheduler_x = false; //bellalta
    else scheduler_x = true;


    // determine RUs to allocate to stations
    // std::cout << "m_staListUL size: "<<m_staListUl.size() <<"\n";
    auto count = std::min<std::size_t>(m_nStations, m_staListUl.size());
    std::size_t nCentral26TonesRus;
    std::size_t limit = 9;

    if(m_apMac->GetWifiPhy()->GetChannelWidth() == 20){
        limit = 9;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 40){
        limit = 18;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 80){
        limit = 37;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 160){
        limit = 74;
    }

    count = std::min(count, limit);
    std::cout << "width: "<<m_allowedWidth <<"\n";
    std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
    HeRu::GetEqualSizedRusForStations(m_apMac->GetWifiPhy()->GetChannelWidth(), count, nCentral26TonesRus, scheduler_x);
    NS_ASSERT(count >= 1);

    if (!m_useCentral26TonesRus)
    {
        nCentral26TonesRus = 0;
    }

    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration);

    WifiTxVector txVector;
    txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
    txVector.SetChannelWidth(m_apMac->GetWifiPhy()->GetChannelWidth());
    txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());
    txVector.SetBssColor(heConfiguration->GetBssColor());

    if(m_enableBsrp){
    m_staListUl.sort([this](const MasterInfo& a, const MasterInfo& b) { 

        return (m_apMac->GetMaxBufferStatus((a.address)) > m_apMac->GetMaxBufferStatus((b.address))); });
    }

    // iterate over the associated stations until an enough number of stations is identified
    auto staIt = m_staListUl.begin();
    m_candidates.clear();

    //ulCandidates.clear();
// txVector.GetHeMuUserInfoMap().size() <
//                std::min<std::size_t>(m_nStations, count + nCentral26TonesRus)
    while (staIt != m_staListUl.end())
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");
        std::cout<<" Address : "<<staIt->address<<" has buffer "<<unsigned(m_apMac->GetMaxBufferStatus(staIt->address))<<"\n";
        
        if(m_enableBsrp){
            auto x = m_apMac->GetMaxBufferStatus(staIt->address);
            if(x == 0) { staIt++; continue;}
        }

        if (!canBeSolicited(*staIt))
        {
            std::cout << "skipping station: "<<staIt->address << "in UL"<<"\n";
            NS_LOG_DEBUG("Skipping station based on provided function object");
            staIt++;
            continue;
        }

        if (txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_TB &&
            !m_apMac->GetEhtSupported(staIt->address))
        {
            NS_LOG_DEBUG(
                "Skipping non-EHT STA because this Trigger Frame is only soliciting EHT STAs");
            staIt++;
            continue;
        }

        uint8_t tid = 0;
        while (tid < 8)
        {
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for UL MU require block ack
            if (m_apMac->GetBaAgreementEstablishedAsRecipient(staIt->address, tid))
            {
                break;
            }
            ++tid;
        }
        if (tid == 8)
        {
            NS_LOG_DEBUG("No Block Ack agreement established with " << staIt->address);
            staIt++;
            continue;
        }

        // if the first candidate STA is an EHT STA, we switch to soliciting EHT TB PPDUs
        if (txVector.GetHeMuUserInfoMap().empty())
        {
            if (m_apMac->GetEhtSupported() && m_apMac->GetEhtSupported(staIt->address))
            {
                txVector.SetPreambleType(WIFI_PREAMBLE_EHT_TB);
                txVector.SetEhtPpduType(0);
            }
            // TODO otherwise, make sure the TX width does not exceed 160 MHz
        }

        // prepare the MAC header of a frame that would be sent to the candidate station,
        // just for the purpose of retrieving the TXVECTOR used to transmit to that station
        WifiMacHeader hdr(WIFI_MAC_QOSDATA);
        hdr.SetAddr1(GetWifiRemoteStationManager(m_linkId)
                         ->GetAffiliatedStaAddress(staIt->address)
                         .value_or(staIt->address));
        hdr.SetAddr2(m_apMac->GetFrameExchangeManager(m_linkId)->GetAddress());
        WifiTxVector suTxVector =
            GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(hdr, m_apMac->GetWifiPhy()->GetChannelWidth());
        txVector.SetHeMuUserInfo(staIt->aid,
                                 {HeRu::RuSpec(), // assigned later by FinalizeTxVector
                                  suTxVector.GetMode().GetMcsValue(),
                                  suTxVector.GetNss()});


        m_candidates.emplace_back(staIt, nullptr);
        // AcIndex ac = QosUtilsMapTidToAc(tid);
            
        // mpdu = m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIt->address);
        // m_candidates.emplace_back(staIt, mpdu);

        // move to the next station in the list
        staIt++;
    }

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station");
        // std::cout << "No suitable station"<<"\n";
        return txVector;
    }
    // std::cout << "finaltx called for UL"<<"\n";
    FinalizeTxVector(txVector, m_ulschedulerLogic, true, true);
    return txVector;

//////////////////////////////////////////////////////////////////
    }else{//BSRP TF
        std::cout << "At time "<< Simulator::Now()<<" GetTxVectorForUlMu called"<<"\n";
    NS_LOG_FUNCTION(this);

    bool scheduler_x = false; // always keep equal split so all stations respond
    
    // determine RUs to allocate to stations
    std::cout << "m_staListUL size: "<<m_staListUl.size() <<"\n";
    auto count = std::min<std::size_t>(m_nStations, m_staListUl.size());
    std::size_t nCentral26TonesRus;
    std::size_t limit = 9;
    if(m_apMac->GetWifiPhy()->GetChannelWidth() == 20){
        limit = 9;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 40){
        limit = 18;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 80){
        limit = 37;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 160){
        limit = 74;
    }
    std::cout << "count1: "<< count<<"\n";
    count = std::min(limit, count);
    std::cout << "width: "<<m_allowedWidth <<"\n";
    std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
    HeRu::GetEqualSizedRusForStations(m_apMac->GetWifiPhy()->GetChannelWidth(), count, nCentral26TonesRus, scheduler_x);
    std::cout << "count2: "<< count<<"\n";
    NS_ASSERT(count >= 1);

    if (!m_useCentral26TonesRus)
    {
        nCentral26TonesRus = 0;
    }

    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration);

    WifiTxVector txVector;
    txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
    txVector.SetChannelWidth(m_apMac->GetWifiPhy()->GetChannelWidth());
    txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());
    txVector.SetBssColor(heConfiguration->GetBssColor());

    // iterate over the associated stations until an enough number of stations is identified
    auto staIt = m_staListUl.begin();
    m_candidates.clear();

    //ulCandidates.clear();
// txVector.GetHeMuUserInfoMap().size() <
//                std::min<std::size_t>(m_nStations, count + nCentral26TonesRus)
    while (staIt != m_staListUl.end())
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");
        // std::cout<<" Address : "<<staIt->address<<" has buffer "<<unsigned(m_apMac->GetMaxBufferStatus(staIt->address))<<"\n";
        if (!canBeSolicited(*staIt))
        {
            // std::cout << "skipping station: "<<staIt->address << "in UL"<<"\n";
            NS_LOG_DEBUG("Skipping station based on provided function object");
            staIt++;
            continue;
        }

        if (txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_TB &&
            !m_apMac->GetEhtSupported(staIt->address))
        {
            NS_LOG_DEBUG(
                "Skipping non-EHT STA because this Trigger Frame is only soliciting EHT STAs");
            staIt++;
            continue;
        }

        uint8_t tid = 0;
        while (tid < 8)
        {
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for UL MU require block ack
            if (m_apMac->GetBaAgreementEstablishedAsRecipient(staIt->address, tid))
            {
                break;
            }
            ++tid;
        }
        if (tid == 8)
        {
            NS_LOG_DEBUG("No Block Ack agreement established with " << staIt->address);
            staIt++;
            continue;
        }

        // if the first candidate STA is an EHT STA, we switch to soliciting EHT TB PPDUs
        if (txVector.GetHeMuUserInfoMap().empty())
        {
            if (m_apMac->GetEhtSupported() && m_apMac->GetEhtSupported(staIt->address))
            {
                txVector.SetPreambleType(WIFI_PREAMBLE_EHT_TB);
                txVector.SetEhtPpduType(0);
            }
            // TODO otherwise, make sure the TX width does not exceed 160 MHz
        }

        // prepare the MAC header of a frame that would be sent to the candidate station,
        // just for the purpose of retrieving the TXVECTOR used to transmit to that station
        WifiMacHeader hdr(WIFI_MAC_QOSDATA);
        hdr.SetAddr1(GetWifiRemoteStationManager(m_linkId)
                         ->GetAffiliatedStaAddress(staIt->address)
                         .value_or(staIt->address));
        hdr.SetAddr2(m_apMac->GetFrameExchangeManager(m_linkId)->GetAddress());
        WifiTxVector suTxVector =
            GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(hdr, m_apMac->GetWifiPhy()->GetChannelWidth());
        txVector.SetHeMuUserInfo(staIt->aid,
                                 {HeRu::RuSpec(), // assigned later by FinalizeTxVector
                                  suTxVector.GetMode().GetMcsValue(),
                                  suTxVector.GetNss()});


         m_candidates.emplace_back(staIt, nullptr);

        // move to the next station in the list
        staIt++;
    }

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station");
        // std::cout << "No suitable station"<<"\n";
        return txVector;
    }
    // std::cout << "finaltx called for UL"<<"\n";
    FinalizeTxVector(txVector, m_ulschedulerLogic, true, false);
    return txVector;

    }
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingBsrpTf()
{
    // std::cout << "At time "<< Simulator::Now()<<" TrySendingBsrpTf called"<<"\n";
    NS_LOG_FUNCTION(this);
    
    if (m_staListUl.empty())
    {
        NS_LOG_DEBUG("No HE stations associated: return SU_TX");
        return TxFormat::SU_TX;
    }

    // only consider stations that have setup the current link
    // std::cout << "GetTxVectorForUlMu called from TrySendingBsrpTf"<<"\n";
    WifiTxVector txVector = GetTxVectorForUlMu([this](const MasterInfo& info) {
        const auto& staList = m_apMac->GetStaList(m_linkId);
        // std::cout << "bsrp mlinkID: "<< unsigned(m_linkId)<<"\n";
        // std::cout << "bsrp size of stalist: "<<"\n";
        return staList.find(info.aid) != staList.cend();
    }, false);

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station found");
        return TxFormat::DL_MU_TX;
    }

    m_trigger = CtrlTriggerHeader(TriggerFrameType::BSRP_TRIGGER, txVector);



    txVector.SetGuardInterval(m_trigger.GetGuardInterval());

    auto item = GetTriggerFrame(m_trigger, m_linkId);
    m_triggerMacHdr = item->GetHeader();

    m_txParams.Clear();
    // set the TXVECTOR used to send the Trigger Frame
    m_txParams.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(m_triggerMacHdr.GetAddr1(),
                                                                       m_apMac->GetWifiPhy()->GetChannelWidth());

    if (!GetHeFem(m_linkId)->TryAddMpdu(item, m_txParams, m_availableTime))
    {
        // sending the BSRP Trigger Frame is not possible, hence return NO_TX. In
        // this way, no transmission will occur now and the next time we will
        // try again sending a BSRP Trigger Frame.
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for BSRP TF exchange");
        return NO_TX;
    }

    // Compute the time taken by each station to transmit 8 QoS Null frames
    Time qosNullTxDuration = Seconds(0);
    for (const auto& userInfo : m_trigger)
    {
          
        
        Time duration = WifiPhy::CalculateTxDuration(GetMaxSizeOfQosNullAmpdu(m_trigger),
                                                     txVector,
                                                     m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                                     userInfo.GetAid12());
        qosNullTxDuration = Max(qosNullTxDuration, duration);
    }

    if (m_availableTime != Time::Min())
    {
        // TryAddMpdu only considers the time to transmit the Trigger Frame
        NS_ASSERT(m_txParams.m_protection &&
                  m_txParams.m_protection->protectionTime != Time::Min());
        NS_ASSERT(m_txParams.m_acknowledgment &&
                  m_txParams.m_acknowledgment->acknowledgmentTime.IsZero());
        NS_ASSERT(m_txParams.m_txDuration != Time::Min());

        if (m_txParams.m_protection->protectionTime + m_txParams.m_txDuration // BSRP TF tx time
                + m_apMac->GetWifiPhy(m_linkId)->GetSifs() + qosNullTxDuration >
            m_availableTime)
        {
            NS_LOG_DEBUG("Remaining TXOP duration is not enough for BSRP TF exchange");
            return NO_TX;
        }
    }

    uint16_t ulLength;
    std::tie(ulLength, qosNullTxDuration) = HePhy::ConvertHeTbPpduDurationToLSigLength(
        qosNullTxDuration,
        m_trigger.GetHeTbTxVector(m_trigger.begin()->GetAid12()),
        m_apMac->GetWifiPhy(m_linkId)->GetPhyBand());
    NS_LOG_DEBUG("Duration of QoS Null frames: " << qosNullTxDuration.As(Time::MS));
    m_trigger.SetUlLength(ulLength);

    return UL_MU_TX;
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingBasicTf()
{
    // std::cout << "At time "<< Simulator::Now()<<" TrySendingBasicTf called"<<"\n";
    // basic_time = Simulator::Now();
    // std::cout << "Duration of BSRP exchange: "<<basic_time - bsrp_time<<"\n";
    // if(basic_time - bsrp_time > TimeStep(0)){
    //     total_bsrp_time += basic_time - bsrp_time;
    //     // std::cout << "Total BSRP time: "<< total_bsrp_time << "\n";
    // }
    NS_LOG_FUNCTION(this);

    if (m_staListUl.empty())
    {
        NS_LOG_DEBUG("No HE stations associated: return SU_TX");
        return TxFormat::SU_TX;
    }

    // check if an UL OFDMA transmission is possible after a DL OFDMA transmission
    NS_ABORT_MSG_IF(m_ulPsduSize == 0, "The UlPsduSize attribute must be set to a non-null value");

    // only consider stations that have setup the current link and do not have
    // reported a null queue size
    // std::cout << "GetTxVectorForUlMu called from TrySendingBasicTf"<<"\n";
    WifiTxVector txVector = GetTxVectorForUlMu([this](const MasterInfo& info) {
        const auto& staList = m_apMac->GetStaList(m_linkId);
        // std::cout << "basic mlinkID: "<< unsigned(m_linkId)<<"\n";
        // std::cout << "basic size of stalist: "<<staList.size()<<"\n";
        // std::cout << "max buffer status of: "<<info.address <<" is "<<unsigned(m_apMac->GetMaxBufferStatus(info.address))<<"\n";
        if(m_enableBsrp) return staList.find(info.aid) != staList.cend() && m_apMac->GetMaxBufferStatus(info.address) > 0;
        return staList.find(info.aid) != staList.cend();
    }, true); // when BSRP is off all stations will go through as they have 255 queue

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station found");
        return TxFormat::DL_MU_TX;
    }

    uint32_t maxBufferSize = 0;

    for (const auto& candidate : txVector.GetHeMuUserInfoMap())
    {
        auto address = m_apMac->GetMldOrLinkAddressByAid(candidate.first);
        
        NS_ASSERT_MSG(address, "AID " << candidate.first << " not found");
        uint8_t queueSize = m_apMac->GetMaxBufferStatus(*address);

     
        // std::cout << "Buffer status of station " << *address << " is " << +queueSize <<"\n";

        if (queueSize == 255)
        {
            NS_LOG_DEBUG("Buffer status of station " << *address << " is unknown");
            maxBufferSize = std::max(maxBufferSize, m_ulPsduSize);
        }
        else if (queueSize == 254)
        {
            NS_LOG_DEBUG("Buffer status of station " << *address << " is not limited");
            maxBufferSize = 0xffffffff;

        }
        else
        {
            NS_LOG_DEBUG("Buffer status of station " << *address << " is " << +queueSize);
            ulCandidates.emplace(queueSize, candidate);
       
            maxBufferSize = std::max(maxBufferSize, static_cast<uint32_t>(queueSize * 256));
        }
    }

    if (maxBufferSize == 0)
    {
        // std::cout << "maxbuffersize is zero"<<"\n";
        return DL_MU_TX;
    }

    m_trigger = CtrlTriggerHeader(TriggerFrameType::BASIC_TRIGGER, txVector);
    txVector.SetGuardInterval(m_trigger.GetGuardInterval());

    auto item = GetTriggerFrame(m_trigger, m_linkId);
    m_triggerMacHdr = item->GetHeader();

    // compute the maximum amount of time that can be granted to stations.
    // This value is limited by the max PPDU duration
    Time maxDuration = GetPpduMaxTime(txVector.GetPreambleType());

    m_txParams.Clear();
    // set the TXVECTOR used to send the Trigger Frame
    m_txParams.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(m_triggerMacHdr.GetAddr1(),
                                                                       m_apMac->GetWifiPhy()->GetChannelWidth());

    if (!GetHeFem(m_linkId)->TryAddMpdu(item, m_txParams, m_availableTime))
    {
        // an UL OFDMA transmission is not possible, hence return NO_TX. In
        // this way, no transmission will occur now and the next time we will
        // try again performing an UL OFDMA transmission.
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for UL MU exchange");
        return NO_TX;
    }

    if (m_availableTime != Time::Min())
    {
        // TryAddMpdu only considers the time to transmit the Trigger Frame
        NS_ASSERT(m_txParams.m_protection &&
                  m_txParams.m_protection->protectionTime != Time::Min());
        NS_ASSERT(m_txParams.m_acknowledgment &&
                  m_txParams.m_acknowledgment->acknowledgmentTime != Time::Min());
        NS_ASSERT(m_txParams.m_txDuration != Time::Min());

        maxDuration = Min(maxDuration,
                          m_availableTime - m_txParams.m_protection->protectionTime -
                              m_txParams.m_txDuration - m_apMac->GetWifiPhy(m_linkId)->GetSifs() -
                              m_txParams.m_acknowledgment->acknowledgmentTime);
        if (maxDuration.IsNegative())
        {
            NS_LOG_DEBUG("Remaining TXOP duration is not enough for UL MU exchange");
            return NO_TX;
        }
    }

    // Compute the time taken by each station to transmit a frame of maxBufferSize size
    Time bufferTxTime = Seconds(0);
    Time duration = Seconds(0), headerduration = Seconds(0),payloadduration=Seconds(0);
    int cnt=0;
    for (const auto& userInfo : m_trigger)
    {
        auto address = m_apMac->GetMldOrLinkAddressByAid(userInfo.GetAid12());
        uint8_t queueSize = m_apMac->GetMaxBufferStatus(*address);
        cnt+=1;
       // std::cout<< userInfo->
        if(prop_scheduler){
        uint32_t buffer_queue = 0;
         auto addr = stationMap[userInfo.GetAid12()];
        auto sz  = pktsizes[addr];
        std::cout<< "Mapping "<< addr<<" "<<userInfo.GetAid12()<<" \n";
        std:: cout<< " Packet size data : "<< addr<<"  length : "<< sz<<"\n";
// for(auto x:pktsizes){
//     std:: cout<<" Hi "<<x.first<<" "<<x.second<<"\n";
// }


        if(queueSize == 255){
            buffer_queue = m_ulPsduSize;
        }else{
            // sz=0;
            buffer_queue = queueSize*(sz ? sz : 256);
        }
          

        duration = WifiPhy::CalculateTxDuration(buffer_queue,
                                                     txVector,
                                                     m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                                     userInfo.GetAid12());


        
        std::cout<<"Prop Buffer duration : "<<duration<< "  Queue size  "<<buffer_queue<< "\n";


        }else{
        std::cout<<"Buffer duration :"<<duration<<"\n";
      

        duration = WifiPhy::CalculateTxDuration(maxBufferSize,
                                                     txVector,
                                                     m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                                     userInfo.GetAid12()); 
        // Time maxbufduration = WifiPhy::CalculateTxDuration(maxBufferSize,
        //                                              txVector,
        //                                              m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
        //                                              userInfo.GetAid12());
        // std::cout << "maxbuffduration: "<< maxbufduration << "\n"; 
       
        std::cout<<"Buffer duration :"<<duration<<"\n";
        }
        total_ul_time+=duration;
        payload_ul_time+=duration;
        std::cout << "For station with AID: " << userInfo.GetAid12() <<" needs duration: "<< duration << "\n";
        bufferTxTime = Max(bufferTxTime, duration);
        std::cout << "buffertxtime: "<< bufferTxTime << "\n";
    }
    
    
       
        
    if (bufferTxTime < maxDuration)
    {
        // the maximum buffer size can be transmitted within the allowed time
        maxDuration = bufferTxTime;
    }
    else
    {
        // maxDuration may be a too short time. If it does not allow any station to
        // transmit at least m_ulPsduSize bytes, give up the UL MU transmission for now
        total_ul_time-=duration;
         payload_ul_time-=duration;
        Time minDuration = Seconds(0);
        for (const auto& userInfo : m_trigger)
        {
            Time duration =
                WifiPhy::CalculateTxDuration(m_ulPsduSize,
                                             txVector,
                                             m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                             userInfo.GetAid12());
            minDuration = (minDuration.IsZero() ? duration : Min(minDuration, duration));
            
        }

        


        if (maxDuration < minDuration)
        {
            // maxDuration is a too short time, hence return NO_TX. In this way,
            // no transmission will occur now and the next time we will try again
            // performing an UL OFDMA transmission.
            NS_LOG_DEBUG("Available time " << maxDuration.As(Time::MS) << " is too short");
            return NO_TX;
        }
        maxDuration = minDuration;
        total_ul_time+=minDuration;
         payload_ul_time+=duration;

    }

    headerduration =  cnt* WifiPhy::CalculatePhyPreambleAndHeaderDuration(txVector);
        
    payload_ul_time-=headerduration;

    
    ul_time = ul_time + maxDuration;

    std::cout << "UL time till now: "<< ul_time << "\n";
    std::cout << "total payload UL time till now: "<< payload_ul_time << "\n";
    std::cout << "total UL time till now: "<< total_ul_time << "\n";
    // maxDuration is the time to grant to the stations. Finalize the Trigger Frame
    uint16_t ulLength;
    std::tie(ulLength, maxDuration) =
        HePhy::ConvertHeTbPpduDurationToLSigLength(maxDuration,
                                                   txVector,
                                                   m_apMac->GetWifiPhy(m_linkId)->GetPhyBand());
    NS_LOG_DEBUG("TB PPDU duration: " << maxDuration.As(Time::MS));
    m_trigger.SetUlLength(ulLength);
    std::cout << "UlLength: "<< ulLength << "\n";
    // set Preferred AC to the AC that gained channel access
    std::cout << "ap_edca_AC: "<<unsigned(m_edca->GetAccessCategory())<<"\n";
    // AcIndex dummy = AC_BE;
    for (auto& userInfo : m_trigger)
    {
        userInfo.SetBasicTriggerDepUserInfo(0, 0, m_edca->GetAccessCategory());
        // userInfo.SetBasicTriggerDepUserInfo(0, 0, dummy);
    }

    UpdateCredits(m_staListUl, maxDuration, txVector);

    return UL_MU_TX;
}

void
RrMultiUserScheduler::NotifyStationAssociated(uint16_t aid, Mac48Address address)
{
    std::cout << "At time "<< Simulator::Now()<<" NotifyStationAssociated called"<<"\n";
   
    NS_LOG_FUNCTION(this << aid << address);

    if (!m_apMac->GetHeSupported(address))
    {
        return;
    }

    auto mldOrLinkAddress = m_apMac->GetMldOrLinkAddressByAid(aid);
    NS_ASSERT_MSG(mldOrLinkAddress, "AID " << aid << " not found");

    for (auto& staList : m_staListDl)
    {
        // if this is not the first STA of a non-AP MLD to be notified, an entry
        // for this non-AP MLD already exists
        const auto staIt = std::find_if(staList.second.cbegin(),
                                        staList.second.cend(),
                                        [aid](auto&& info) { return info.aid == aid; });
        if (staIt == staList.second.cend())
        {
            staList.second.push_back(MasterInfo{aid, *mldOrLinkAddress, 0.0});
            stationMap[aid] =address;
        }

        std::cout<<"Station address and aid "<<  stationMap[aid]  <<" "<<aid<<" \n";

    }

    const auto staIt = std::find_if(m_staListUl.cbegin(), m_staListUl.cend(), [aid](auto&& info) {
        return info.aid == aid;
    });
    if (staIt == m_staListUl.cend())
    {
        m_staListUl.push_back(MasterInfo{aid, *mldOrLinkAddress, 0.0});
    }

    
}

void
RrMultiUserScheduler::NotifyStationDeassociated(uint16_t aid, Mac48Address address)
{
    std::cout << "At time "<< Simulator::Now()<<" NotifyStationDeassociated called"<<"\n";
   
    NS_LOG_FUNCTION(this << aid << address);

    if (!m_apMac->GetHeSupported(address))
    {
        return;
    }

    auto mldOrLinkAddress = m_apMac->GetMldOrLinkAddressByAid(aid);
    NS_ASSERT_MSG(mldOrLinkAddress, "AID " << aid << " not found");

    if (m_apMac->IsAssociated(*mldOrLinkAddress))
    {
        // Another STA of the non-AP MLD is still associated
        return;
    }

    for (auto& staList : m_staListDl)
    {
        staList.second.remove_if([&aid](const MasterInfo& info) { return info.aid == aid; });
    }
    m_staListUl.remove_if([&aid](const MasterInfo& info) { return info.aid == aid; });
    stationMap.clear();
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingDlMuPpdu()
{
    // std::cout << "At time "<< Simulator::Now()<<" TrySendingDlMuPpdu called"<<"\n";
   
    NS_LOG_FUNCTION(this);

    AcIndex primaryAc = m_edca->GetAccessCategory();

    if (m_staListDl[primaryAc].empty())
    {
        NS_LOG_DEBUG("No HE stations associated: return SU_TX");
        return TxFormat::SU_TX;
    }

    bool scheduler_x = true; // rr
    if(m_dlschedulerLogic == "Bellalta") scheduler_x = false;
    else scheduler_x = true;


    std::size_t count =
        std::min(static_cast<std::size_t>(m_nStations), m_staListDl[primaryAc].size());
    // std::cout << "dlprimaryac: "<<unsigned(primaryAc) <<"\n";
    // std::cout << "initial count: "<<count <<"\n";
    std::size_t nCentral26TonesRus=0;
    uint8_t currTid = wifiAcList.at(primaryAc).GetHighTid();

    Ptr<WifiMpdu> mpdu = m_edca->PeekNextMpdu(m_linkId);

    if (mpdu && mpdu->GetHeader().IsQosData())
    {
        currTid = mpdu->GetHeader().GetQosTid();
    }

    // determine the list of TIDs to check
    std::vector<uint8_t> tids;

    if (m_enableTxopSharing)
    {
        for (auto acIt = wifiAcList.find(primaryAc); acIt != wifiAcList.end(); acIt++)
        {
            uint8_t firstTid = (acIt->first == primaryAc ? currTid : acIt->second.GetHighTid());
            tids.push_back(firstTid);
            tids.push_back(acIt->second.GetOtherTid(firstTid));
        }
    }
    else
    {
        tids.push_back(currTid);
    }
 
 auto staIttemp1 = m_staListDl[primaryAc].begin();
    m_candidates.clear();

    // std::vector<uint8_t> ruAllocations;
    // auto numRuAllocs = m_txParams.m_txVector.GetChannelWidth() / 20;
    // ruAllocations.resize(numRuAllocs);


    ////////////dummy mcandidates to sort DL queue//////

    while (staIttemp1 != m_staListDl[primaryAc].end()
          )
    { 
        if (m_txParams.m_txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU &&
            !m_apMac->GetEhtSupported(staIttemp1->address))
        {
           // NS_LOG_DEBUG("Skipping non-EHT STA because this DL MU PPDU is sent to EHT STAs only");
            staIttemp1++;
            continue;
        }
 
        // check if the AP has at least one frame to be sent to the current station
        for (uint8_t tid : tids)
        {
            AcIndex ac = QosUtilsMapTidToAc(tid);
            NS_ASSERT(ac >= primaryAc);
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for DL MU PPDUs require block ack
            if (m_apMac->GetBaAgreementEstablishedAsOriginator(staIttemp1->address, tid))
            {

                mpdu = m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIttemp1->address);
                int sz = m_apMac->GetQosTxop(ac)->GetQosQueueSize(tid,staIttemp1->address);
                
                // std::cout<<"Station with MAC address: "<<staIttemp1->address<<" has DL queue size: "<<sz<<"\n";
                // we only check if the first frame of the current TID meets the size
                // and duration constraints. We do not explore the queues further.
                if (mpdu)
                {   
                     
                    dlqueueinfo.emplace(*staIttemp1, sz);
                    
                     // the frame meets the constraints
                        NS_LOG_DEBUG("Adding candidate STA (MAC=" << staIttemp1->address
                                                                  << ", AID=" << staIttemp1->aid
                                                                  << ") TID=" << +tid);
                        // m_candidates.emplace_back(staIttemp1, mpdu);
                        // std::cout << "Adding candidate STA (MAC=" << staIttemp1->address
                        //                                           << ", AID=" << staIttemp1->aid
                        //                                           << ") TID=" << +tid<<"\n";
                        break; // terminate the for loop
                    
                }
                else
                {
                    NS_LOG_DEBUG("No frames to send to " << staIttemp1->address << " with TID=" << +tid);
                }
            }
        }

        // move to the next station in the list
        staIttemp1++;
    }


////////////////////////////

m_staListDl[primaryAc].sort([this](const MasterInfo& a, const MasterInfo& b) { 
auto mp = this->dlqueueinfo;
int qa = this->dlqueueinfo.find(a)!=this->dlqueueinfo.end()? mp[a]:0;
int qb = this->dlqueueinfo.find(b)!=this->dlqueueinfo.end()? mp[b]:0;
        return (qa>qb); });



 auto staIttemp = m_staListDl[primaryAc].begin();
    m_candidates.clear();

    while (staIttemp != m_staListDl[primaryAc].end() &&
           m_candidates.size() <
               std::min(static_cast<std::size_t>(m_nStations), count + nCentral26TonesRus))
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIttemp->address << ", AID=" << staIttemp->aid << ")");

        if (m_txParams.m_txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU &&
            !m_apMac->GetEhtSupported(staIttemp->address))
        {
            NS_LOG_DEBUG("Skipping non-EHT STA because this DL MU PPDU is sent to EHT STAs only");
            staIttemp++;
            continue;
        }
 
        // check if the AP has at least one frame to be sent to the current station
        for (uint8_t tid : tids)
        {
            AcIndex ac = QosUtilsMapTidToAc(tid);
            NS_ASSERT(ac >= primaryAc);
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for DL MU PPDUs require block ack
            if (m_apMac->GetBaAgreementEstablishedAsOriginator(staIttemp->address, tid))
            {

                mpdu = m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIttemp->address);
                // int sz = m_apMac->GetQosTxop(ac)->GetQosQueueSize(tid,staIttemp->address);
                // std::cout<<"Station with MAC address: "<<staIttemp->address<<" has DL queue size: "<<sz<<"\n";
                // we only check if the first frame of the current TID meets the size
                // and duration constraints. We do not explore the queues further.
                if (mpdu)
                {
                    
                     // the frame meets the constraints
                        NS_LOG_DEBUG("Adding candidate STA (MAC=" << staIttemp->address
                                                                  << ", AID=" << staIttemp->aid
                                                                  << ") TID=" << +tid);
                        m_candidates.emplace_back(staIttemp, mpdu);
                        // std::cout << "Adding candidate STA (MAC=" << staIttemp->address
                        //                                           << ", AID=" << staIttemp->aid
                        //                                           << ") TID=" << +tid<<"\n";
                        break; // terminate the for loop
                    
                }
                else
                {
                    NS_LOG_DEBUG("No frames to send to " << staIttemp->address << " with TID=" << +tid);
                }
            }
        }

        // move to the next station in the list
        staIttemp++;
    }



count = std::min (static_cast<std::size_t> (m_nStations), m_candidates.size ());
if(count==0)count=1;
//   std::cout<<count<<" Printing count \n";
  std::size_t limit = 9;
  if(m_apMac->GetWifiPhy()->GetChannelWidth() == 20){
        limit = 9;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 40){
        limit = 18;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 80){
        limit = 37;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 160){
        limit = 74;
    }

  count = std::min(count, limit);
    std::cout << "width: "<<m_allowedWidth <<"\n";
    std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
    HeRu::RuType ruType =
        HeRu::GetEqualSizedRusForStations(m_apMac->GetWifiPhy()->GetChannelWidth(), count, nCentral26TonesRus, scheduler_x);
    NS_ASSERT(count >= 1);

    // std::cout << "DL count after allocation: "<<count <<"\n";

    if (!m_useCentral26TonesRus)
    {
        nCentral26TonesRus = 0;
    }



    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration);

    m_txParams.Clear();
    m_txParams.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_MU);
    m_txParams.m_txVector.SetChannelWidth(m_apMac->GetWifiPhy()->GetChannelWidth());
    m_txParams.m_txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());
    m_txParams.m_txVector.SetBssColor(heConfiguration->GetBssColor());

    // The TXOP limit can be exceeded by the TXOP holder if it does not transmit more
    // than one Data or Management frame in the TXOP and the frame is not in an A-MPDU
    // consisting of more than one MPDU (Sec. 10.22.2.8 of 802.11-2016).
    // For the moment, we are considering just one MPDU per receiver.
    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);

    // iterate over the associated stations until an enough number of stations is identified
    auto staIt = m_staListDl[primaryAc].begin();
    m_candidates.clear();

    std::vector<uint8_t> ruAllocations;
    auto numRuAllocs = m_txParams.m_txVector.GetChannelWidth() / 20;
    ruAllocations.resize(numRuAllocs);
    NS_ASSERT((m_candidates.size() % numRuAllocs) == 0);

    while (staIt != m_staListDl[primaryAc].end() &&
           m_candidates.size() <
               std::min(static_cast<std::size_t>(m_nStations), count + nCentral26TonesRus))
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");

        if (m_txParams.m_txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU &&
            !m_apMac->GetEhtSupported(staIt->address))
        {
            NS_LOG_DEBUG("Skipping non-EHT STA because this DL MU PPDU is sent to EHT STAs only");
            staIt++;
            continue;
        }

        HeRu::RuType currRuType = (m_candidates.size() < count ? ruType : HeRu::RU_26_TONE);

        // check if the AP has at least one frame to be sent to the current station
        for (uint8_t tid : tids)
        {
            AcIndex ac = QosUtilsMapTidToAc(tid);
            NS_ASSERT(ac >= primaryAc);
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for DL MU PPDUs require block ack
            if (m_apMac->GetBaAgreementEstablishedAsOriginator(staIt->address, tid))
            {
                mpdu = m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIt->address);

                // we only check if the first frame of the current TID meets the size
                // and duration constraints. We do not explore the queues further.
                if (mpdu)
                {
                    mpdu = GetHeFem(m_linkId)->CreateAliasIfNeeded(mpdu);
                    // Use a temporary TX vector including only the STA-ID of the
                    // candidate station to check if the MPDU meets the size and time limits.
                    // An RU of the computed size is tentatively assigned to the candidate
                    // station, so that the TX duration can be correctly computed.
                    WifiTxVector suTxVector =
                        GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(mpdu->GetHeader(),
                                                                               m_apMac->GetWifiPhy()->GetChannelWidth());

                    WifiTxVector txVectorCopy = m_txParams.m_txVector;

                    // the first candidate STA determines the preamble type for the DL MU PPDU
                    if (m_candidates.empty() &&
                        suTxVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU)
                    {
                        m_txParams.m_txVector.SetPreambleType(WIFI_PREAMBLE_EHT_MU);
                        m_txParams.m_txVector.SetEhtPpduType(0); // indicates DL OFDMA transmission
                    }

                    m_txParams.m_txVector.SetHeMuUserInfo(staIt->aid,
                                                          {{currRuType, 1, true},
                                                           suTxVector.GetMode().GetMcsValue(),
                                                           suTxVector.GetNss()});

                    if (!GetHeFem(m_linkId)->TryAddMpdu(mpdu, m_txParams, actualAvailableTime))
                    {
                        NS_LOG_DEBUG("Adding the peeked frame violates the time constraints");
                        m_txParams.m_txVector = txVectorCopy;
                    }
                    else
                    {
                        // the frame meets the constraints
                        NS_LOG_DEBUG("Adding candidate STA (MAC=" << staIt->address
                                                                  << ", AID=" << staIt->aid
                                                                  << ") TID=" << +tid);
                        m_candidates.emplace_back(staIt, mpdu);
                        break; // terminate the for loop
                    }
                }
                else
                {
                    NS_LOG_DEBUG("No frames to send to " << staIt->address << " with TID=" << +tid);
                }
            }
        }

        // move to the next station in the list
        staIt++;
    }

    if (m_candidates.empty())
    {
        if (m_forceDlOfdma)
        {
            NS_LOG_DEBUG("The AP does not have suitable frames to transmit: return NO_TX");
            return NO_TX;
        }
        NS_LOG_DEBUG("The AP does not have suitable frames to transmit: return SU_TX");
        return SU_TX;
    }

    return TxFormat::DL_MU_TX;
}

void
RrMultiUserScheduler::FinalizeTxVector(WifiTxVector& txVector, std::string scheduler_logic, bool ul, bool basictf)
{

    if(!ul){ // DL code

        // std::cout << "At time "<< Simulator::Now()<<" FinalizeTxVector called for ComputeDLMUInfo"<<"\n";
        bool scheduler_x = true; // rr
        if(scheduler_logic == "Bellalta") scheduler_x = false;
        else scheduler_x = true;

        // Do not log txVector because GetTxVectorForUlMu() left RUs undefined and
        // printing them will crash the simulation
        NS_LOG_FUNCTION(this);
        NS_ASSERT(txVector.GetHeMuUserInfoMap().size() == m_candidates.size());

        // compute how many stations can be granted an RU and the RU size
        std::size_t nRusAssigned = m_candidates.size();
        std::size_t nCentral26TonesRus;
        std::size_t limit = 9;
        if(m_apMac->GetWifiPhy()->GetChannelWidth() == 20){
        limit = 9;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 40){
        limit = 18;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 80){
        limit = 37;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 160){
        limit = 74;
    }

    if(prop_scheduler && false){

        std::cout << "width: "<<m_allowedWidth <<"\n";
        std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
        
        // re-allocate RUs based on the actual number of candidate stations
        WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
        std::swap(heMuUserInfoMap, txVector.GetHeMuUserInfoMap());

        auto candidateIt = m_candidates.begin(); // iterator over the list of candidate receivers
        std::cout << "prop dl"<<"\n";
        std::vector<HeRu::RuSpec> RU_array;
        RU_array = prop_scheduler_fun(m_candidates, m_apMac->GetWifiPhy()->GetChannelWidth(), false);

        for (std::size_t i = 0; i < RU_array.size(); i++)
        {
            NS_ASSERT(candidateIt != m_candidates.end());
            auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
            NS_ASSERT(mapIt != heMuUserInfoMap.end());

            txVector.SetHeMuUserInfo(mapIt->first,
                                     {RU_array[i],
                                      mapIt->second.mcs,
                                      mapIt->second.nss});
            candidateIt++;
            if(candidateIt == m_candidates.end ()){
                    break;
                  }
        }

        // remove candidates that will not be served
        m_candidates.erase(candidateIt, m_candidates.end());

    }else{

        nRusAssigned = std::min(nRusAssigned, limit);
        std::cout << "width: "<<m_allowedWidth <<"\n";
        std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
        HeRu::RuType ruType =
            HeRu::GetEqualSizedRusForStations(m_apMac->GetWifiPhy()->GetChannelWidth(), nRusAssigned, nCentral26TonesRus, scheduler_x);

        NS_LOG_DEBUG(nRusAssigned << " stations are being assigned a " << ruType << " RU");
        std::cout << "In DL "<< nRusAssigned << " stations are being assigned a " << ruType << " RU"<<"\n";
        if (!m_useCentral26TonesRus || m_candidates.size() == nRusAssigned)
        {
            nCentral26TonesRus = 0;
        }
        else
        {
            nCentral26TonesRus = std::min(m_candidates.size() - nRusAssigned, nCentral26TonesRus);
            NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
        }

        // re-allocate RUs based on the actual number of candidate stations
        WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
        std::swap(heMuUserInfoMap, txVector.GetHeMuUserInfoMap());

        auto candidateIt = m_candidates.begin(); // iterator over the list of candidate receivers
        auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), ruType, 0, false);
        auto ruSetIt = ruSet.begin();
        auto central26TonesRus = HeRu::GetCentral26TonesRus(m_apMac->GetWifiPhy()->GetChannelWidth(), ruType);
        auto central26TonesRusIt = central26TonesRus.begin();

        for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
        {
            NS_ASSERT(candidateIt != m_candidates.end());
            auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
            NS_ASSERT(mapIt != heMuUserInfoMap.end());

            std::cout << "DL station: " << candidateIt->first->address << "RU: "<< *ruSetIt <<"\n"; 

            txVector.SetHeMuUserInfo(mapIt->first,
                                     {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                                      mapIt->second.mcs,
                                      mapIt->second.nss});
            candidateIt++;
            if(candidateIt == m_candidates.end ()){
                    break;
                  }
        }

        // remove candidates that will not be served
        m_candidates.erase(candidateIt, m_candidates.end());
    }

////////////////////////////////////////////////////////////
    }else{ // UL code

        if(basictf){//Basic TF

        // std::cout << "At time "<< Simulator::Now()<<" FinalizeTxVector called for Basic TF"<<"\n";
        bool scheduler_x = true; // rr
        if(scheduler_logic == "Bellalta") scheduler_x = false;
        else scheduler_x = true;
    
        // Do not log txVector because GetTxVectorForUlMu() left RUs undefined and
        // printing them will crash the simulation
        NS_LOG_FUNCTION(this);
        NS_ASSERT(txVector.GetHeMuUserInfoMap().size() == m_candidates.size());
    
        // compute how many stations can be granted an RU and the RU size
        std::size_t nRusAssigned = m_candidates.size();
        std::size_t nCentral26TonesRus;
        std::size_t limit = 9;
        if(m_apMac->GetWifiPhy()->GetChannelWidth() == 20){
        limit = 9;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 40){
        limit = 18;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 80){
        limit = 37;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 160){
        limit = 74;
    }

    if(prop_scheduler){
        // re-allocate RUs based on the actual number of candidate stations
        WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
        std::swap(heMuUserInfoMap, txVector.GetHeMuUserInfoMap());
    
        auto candidateIt = m_candidates.begin(); // iterator over the list of candidate receivers
        std::cout << "prop ul"<<"\n";
        std::vector<HeRu::RuSpec> RU_array;
        RU_array = prop_scheduler_fun(m_candidates, m_apMac->GetWifiPhy()->GetChannelWidth(), true);
    
        for (std::size_t i = 0; i < RU_array.size(); i++)
        {
            NS_ASSERT(candidateIt != m_candidates.end());
            auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
            NS_ASSERT(mapIt != heMuUserInfoMap.end());
            txVector.SetHeMuUserInfo(mapIt->first,
                                     {RU_array[i],
                                      mapIt->second.mcs,
                                      mapIt->second.nss});
            candidateIt++;
            if(candidateIt == m_candidates.end ()){
                    break;
                  }
        }
    
        // remove candidates that will not be served
        m_candidates.erase(candidateIt, m_candidates.end());

    }else{

        nRusAssigned = std::min(nRusAssigned, limit);
        std::cout << "width: "<<m_allowedWidth <<"\n";
        std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
        HeRu::RuType ruType =
            HeRu::GetEqualSizedRusForStations(m_apMac->GetWifiPhy()->GetChannelWidth(), nRusAssigned, nCentral26TonesRus, scheduler_x);
    
        NS_LOG_DEBUG(nRusAssigned << " stations are being assigned a " << ruType << " RU");
        // std::cout << "In UL Basic TF"<< nRusAssigned << " stations are being assigned a " << ruType << " RU"<<"\n";
        if (!m_useCentral26TonesRus || m_candidates.size() == nRusAssigned)
        {
            nCentral26TonesRus = 0;
        }
        else
        {
            nCentral26TonesRus = std::min(m_candidates.size() - nRusAssigned, nCentral26TonesRus);
            NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
        }
    
        // re-allocate RUs based on the actual number of candidate stations
        WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
        std::swap(heMuUserInfoMap, txVector.GetHeMuUserInfoMap());
    
        auto candidateIt = m_candidates.begin(); // iterator over the list of candidate receivers
        auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), ruType, 0, false);
        auto ruSetIt = ruSet.begin();
        auto central26TonesRus = HeRu::GetCentral26TonesRus(m_apMac->GetWifiPhy()->GetChannelWidth(), ruType);
        auto central26TonesRusIt = central26TonesRus.begin();
    
        for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
        {
            NS_ASSERT(candidateIt != m_candidates.end());
            auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
            NS_ASSERT(mapIt != heMuUserInfoMap.end());
        // std::cout<<"Served station address "<<(candidateIt->first->address)<<" has buffer : "<<
        // unsigned(m_apMac->GetMaxBufferStatus(candidateIt->first->address))<<"\n";
            txVector.SetHeMuUserInfo(mapIt->first,
                                     {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                                      mapIt->second.mcs,
                                      mapIt->second.nss});
            candidateIt++;
            if(candidateIt == m_candidates.end ()){
                    break;
                  }
        }
    
        // remove candidates that will not be served
        m_candidates.erase(candidateIt, m_candidates.end());
    }

////////////////////////////////////////////////////////////
        }else{//BSRP TF

        std::cout << "At time "<< Simulator::Now()<<" FinalizeTxVector called for BSRP TF"<<"\n";
        bool scheduler_x = false; // equal aplit always so all STA will send BSR
        
        // Do not log txVector because GetTxVectorForUlMu() left RUs undefined and
        // printing them will crash the simulation
        NS_LOG_FUNCTION(this);
        NS_ASSERT(txVector.GetHeMuUserInfoMap().size() == m_candidates.size());
    
        // compute how many stations can be granted an RU and the RU size
        std::size_t nRusAssigned = m_candidates.size();
        std::size_t nCentral26TonesRus;
        std::size_t limit = 9;
        if(m_apMac->GetWifiPhy()->GetChannelWidth() == 20){
        limit = 9;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 40){
        limit = 18;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 80){
        limit = 37;
    }else if(m_apMac->GetWifiPhy()->GetChannelWidth() == 160){
        limit = 74;
    }

        nRusAssigned = std::min(nRusAssigned, limit);
        std::cout << "width: "<<m_allowedWidth <<"\n";
        std::cout << "width in apmac: "<< m_apMac->GetWifiPhy()->GetChannelWidth() << "\n";
        HeRu::RuType ruType =
            HeRu::GetEqualSizedRusForStations(m_apMac->GetWifiPhy()->GetChannelWidth(), nRusAssigned, nCentral26TonesRus, scheduler_x);
    
        NS_LOG_DEBUG(nRusAssigned << " stations are being assigned a " << ruType << " RU");
        std::cout << "In UL BSRP TF"<< nRusAssigned << " stations are being assigned a " << ruType << " RU"<<"\n";
        if (!m_useCentral26TonesRus || m_candidates.size() == nRusAssigned)
        {
            nCentral26TonesRus = 0;
        }
        else
        {
            nCentral26TonesRus = std::min(m_candidates.size() - nRusAssigned, nCentral26TonesRus);
            NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
        }
    
        // re-allocate RUs based on the actual number of candidate stations
        WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
        std::swap(heMuUserInfoMap, txVector.GetHeMuUserInfoMap());
    
        auto candidateIt = m_candidates.begin(); // iterator over the list of candidate receivers
        auto ruSet = HeRu::GetRusOfType(m_apMac->GetWifiPhy()->GetChannelWidth(), ruType, 0, false);
        auto ruSetIt = ruSet.begin();
        auto central26TonesRus = HeRu::GetCentral26TonesRus(m_apMac->GetWifiPhy()->GetChannelWidth(), ruType);
        auto central26TonesRusIt = central26TonesRus.begin();
    
        for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
        {
            NS_ASSERT(candidateIt != m_candidates.end());
            auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
            NS_ASSERT(mapIt != heMuUserInfoMap.end());
        // std::cout<<"Served station address "<<(candidateIt->first->address)<<" has buffer : "<<
        // unsigned(m_apMac->GetMaxBufferStatus(candidateIt->first->address))<<"\n";
            txVector.SetHeMuUserInfo(mapIt->first,
                                     {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                                      mapIt->second.mcs,
                                      mapIt->second.nss});
            candidateIt++;
            if(candidateIt == m_candidates.end ()){
                    break;
                  }
        }
    
        // remove candidates that will not be served
        m_candidates.erase(candidateIt, m_candidates.end());

    }
    }


}

void
RrMultiUserScheduler::UpdateCredits(std::list<MasterInfo>& staList,
                                    Time txDuration,
                                    const WifiTxVector& txVector)
{

    NS_LOG_FUNCTION(this << txDuration.As(Time::US) << txVector);
// std::cout << txDuration.As(Time::US) << "tx duration "<<txVector<<"\n";
    // find how many RUs have been allocated for each RU type
    std::map<HeRu::RuType, std::size_t> ruMap;
    for (const auto& userInfo : txVector.GetHeMuUserInfoMap())
    {
        ruMap.insert({userInfo.second.ru.GetRuType(), 0}).first->second++;
    }


    // The amount of credits received by each station equals the TX duration (in
    // microseconds) divided by the number of stations.
    double creditsPerSta = txDuration.ToDouble(Time::US) / staList.size();
    // Transmitting stations have to pay a number of credits equal to the TX duration
    // (in microseconds) times the allocated bandwidth share.
    double debitsPerMhz =
        txDuration.ToDouble(Time::US) /
        std::accumulate(ruMap.begin(), ruMap.end(), 0, [](uint16_t sum, auto pair) {
            return sum + pair.second * HeRu::GetBandwidth(pair.first);
        });
    
    // assign credits to all stations
    for (auto& sta : staList)
    {
        sta.credits += creditsPerSta;
        sta.credits = std::min(sta.credits, m_maxCredits.ToDouble(Time::US));
    }

    // subtract debits to the selected stations
    for (auto& candidate : m_candidates)
    {
        auto mapIt = txVector.GetHeMuUserInfoMap().find(candidate.first->aid);
        NS_ASSERT(mapIt != txVector.GetHeMuUserInfoMap().end());

        candidate.first->credits -= debitsPerMhz * HeRu::GetBandwidth(mapIt->second.ru.GetRuType());
    }
    
    // sort the list in decreasing order of credits
    staList.sort([](const MasterInfo& a, const MasterInfo& b) { 
     
        return a.credits > b.credits; });
}

MultiUserScheduler::DlMuInfo
RrMultiUserScheduler::ComputeDlMuInfo()
{
    NS_LOG_FUNCTION(this);
    // std::cout << "At time "<< Simulator::Now()<<" ComputeDlMuInfo called"<<"\n";
    if (m_candidates.empty())
    {
        return DlMuInfo();
    }

    DlMuInfo dlMuInfo;
    std::swap(dlMuInfo.txParams.m_txVector, m_txParams.m_txVector);
    // std::cout << "finaltx called for DL"<<"\n";
    FinalizeTxVector(dlMuInfo.txParams.m_txVector, m_dlschedulerLogic, false, true);

    m_txParams.Clear();
    Ptr<WifiMpdu> mpdu;

    // Compute the TX params (again) by using the stored MPDUs and the final TXVECTOR
    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);

    for (const auto& candidate : m_candidates)
    {
        mpdu = candidate.second;
        NS_ASSERT(mpdu);

        bool ret [[maybe_unused]] =
            GetHeFem(m_linkId)->TryAddMpdu(mpdu, dlMuInfo.txParams, actualAvailableTime);
        NS_ASSERT_MSG(ret,
                      "Weird that an MPDU does not meet constraints when "
                      "transmitted over a larger RU");
    }

    // We have to complete the PSDUs to send
    Ptr<WifiMacQueue> queue;

    for (const auto& candidate : m_candidates)
    {
        // Let us try first A-MSDU aggregation if possible
        mpdu = candidate.second;
        
        NS_ASSERT(mpdu);
        uint8_t tid = mpdu->GetHeader().GetQosTid();
        NS_ASSERT_MSG(mpdu->GetOriginal()->GetHeader().GetAddr1() == candidate.first->address,
                      "RA of the stored MPDU must match the stored address");

        NS_ASSERT(mpdu->IsQueued());
        Ptr<WifiMpdu> item = mpdu;

        if (!mpdu->GetHeader().IsRetry())
        {
            // this MPDU must have been dequeued from the AC queue and we can try
            // A-MSDU aggregation
            item = GetHeFem(m_linkId)->GetMsduAggregator()->GetNextAmsdu(mpdu,
                                                                         dlMuInfo.txParams,
                                                                         m_availableTime);

            if (!item)
            {
                // A-MSDU aggregation failed or disabled
                item = mpdu;
            }
            m_apMac->GetQosTxop(QosUtilsMapTidToAc(tid))->AssignSequenceNumber(item);
        }

        // Now, let's try A-MPDU aggregation if possible
        std::vector<Ptr<WifiMpdu>> mpduList =
            GetHeFem(m_linkId)->GetMpduAggregator()->GetNextAmpdu(item,
                                                                  dlMuInfo.txParams,
                                                                  m_availableTime);
       // item->

        if (mpduList.size() > 1)
        {
            // A-MPDU aggregation succeeded, update psduMap
            // std::cout<<"AMPDU aggregation done :"<<candidate.first->address<<"\n";
            dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu>(std::move(mpduList));

        }
        else
        {
            
            // std::cout<<"AMPDU aggregation not done"<<candidate.first->address<<"\n";
            dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu>(item, true);
        }

        // std::cout<<"AMPDU aggregation not done"<<candidate.first->address<<"\n";
        //     dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu>(item, true);
    }

    AcIndex primaryAc = m_edca->GetAccessCategory();
    UpdateCredits(m_staListDl[primaryAc],
                  dlMuInfo.txParams.m_txDuration,
                  dlMuInfo.txParams.m_txVector);

    NS_LOG_DEBUG("Next station to serve has AID=" << m_staListDl[primaryAc].front().aid);

    return dlMuInfo;
}

MultiUserScheduler::UlMuInfo
RrMultiUserScheduler::ComputeUlMuInfo()
{
    return UlMuInfo{m_trigger, m_triggerMacHdr, std::move(m_txParams)};
}

} // namespace ns3
