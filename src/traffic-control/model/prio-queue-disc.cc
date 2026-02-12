/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "prio-queue-disc.h"

#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/pointer.h"
#include "ns3/socket.h"

#include <algorithm>
#include <iterator>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PrioQueueDisc");
NS_OBJECT_ENSURE_REGISTERED(PrioQueueDisc);
ATTRIBUTE_HELPER_CPP(Priomap);

std::ostream&
operator<<(std::ostream& os, const Priomap& priomap)
{
    std::copy(priomap.begin(), priomap.end() - 1, std::ostream_iterator<uint16_t>(os, " "));
    os << priomap.back();
    return os;
}

std::istream&
operator>>(std::istream& is, Priomap& priomap)
{
    for (int i = 0; i < 16; i++)
    {
        if (!(is >> priomap[i]))
        {
            NS_FATAL_ERROR("Incomplete priomap");
        }
    }
    return is;
}

TypeId
PrioQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::PrioQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<PrioQueueDisc>()
            .AddAttribute("Priomap",
                          "The priority to band mapping.",
                          PriomapValue(Priomap{{1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}}),
                          MakePriomapAccessor(&PrioQueueDisc::m_prio2band),
                          MakePriomapChecker());
    return tid;
}

PrioQueueDisc::PrioQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::NO_LIMITS)
{
}

PrioQueueDisc::~PrioQueueDisc()
{
}

void
PrioQueueDisc::SetBandForPriority(uint8_t prio, uint16_t band)
{
    NS_ASSERT(prio < 16);
    m_prio2band[prio] = band;
}

uint16_t
PrioQueueDisc::GetBandForPriority(uint8_t prio) const
{
    NS_LOG_FUNCTION(this << (uint16_t)prio);

    NS_ASSERT_MSG(prio < 16, "Priority must be a value between 0 and 15");

    uint16_t band = m_prio2band[prio];

    if (band >= GetNQueueDiscClasses())
    {
        return 0;
    }

    return band;
}

int32_t
PrioQueueDisc::DoClassify(Ptr<QueueDiscItem> item) const
{
    uint32_t band = m_prio2band[0];
    SocketPriorityTag priorityTag;

    if (item->GetPacket()->PeekPacketTag(priorityTag))
    {
        uint32_t mappedBand = m_prio2band[priorityTag.GetPriority() & 0x0f];

        if (mappedBand < GetNQueueDiscClasses())
        {
            band = mappedBand;
        }
        else
        {
            NS_LOG_DEBUG("Priority mapped to non-existent band " << mappedBand
                                                                 << ". Defaulting to 0.");
            band = 0;
        }
    }

    if (band >= GetNQueueDiscClasses())
    {
        band = 0;
    }

    return static_cast<int32_t>(band);
}

bool
PrioQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    uint32_t band = GetBandForPriority(0);
    int32_t ret = Classify(item);

    if (ret == PacketFilter::PF_NO_MATCH)
    {
        SocketPriorityTag priorityTag;
        if (item->GetPacket()->PeekPacketTag(priorityTag))
        {
            band = GetBandForPriority(priorityTag.GetPriority() & 0x0f);
        }
    }
    else
    {
        // Safety for filter returns
        if (ret >= 0 && static_cast<uint32_t>(ret) < GetNQueueDiscClasses())
        {
            band = static_cast<uint32_t>(ret);
        }
    }

    // Final safety check before accessing the class
    if (band >= GetNQueueDiscClasses())
    {
        band = 0;
    }

    bool retval = GetQueueDiscClass(band)->GetQueueDisc()->Enqueue(item);
    return retval;
}

Ptr<QueueDiscItem>
PrioQueueDisc::DoDequeue()
{
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        Ptr<QueueDiscItem> item = GetQueueDiscClass(i)->GetQueueDisc()->Dequeue();
        if (item)
        {
            return item;
        }
    }
    return nullptr;
}

Ptr<const QueueDiscItem>
PrioQueueDisc::DoPeek()
{
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        Ptr<const QueueDiscItem> item = GetQueueDiscClass(i)->GetQueueDisc()->Peek();
        if (item)
        {
            return item;
        }
    }
    return nullptr;
}

bool
PrioQueueDisc::CheckConfig()
{
    if (GetNInternalQueues() > 0)
    {
        return false;
    }
    if (GetNQueueDiscClasses() == 0)
    {
        ObjectFactory factory;
        factory.SetTypeId("ns3::FifoQueueDisc");
        for (uint8_t i = 0; i < 2; i++)
        {
            Ptr<QueueDisc> qd = factory.Create<QueueDisc>();
            qd->Initialize();
            Ptr<QueueDiscClass> c = CreateObject<QueueDiscClass>();
            c->SetQueueDisc(qd);
            AddQueueDiscClass(c);
        }
    }
    return GetNQueueDiscClasses() >= 2;
}

void
PrioQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);
}

} // namespace ns3
