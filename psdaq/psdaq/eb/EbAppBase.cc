#include "EbAppBase.hh"

#include "Endpoint.hh"
#include "EbEvent.hh"

#include "EbLfServer.hh"

#include "utilities.hh"

#include "psalg/utils/SysLog.hh"
#include "xtcdata/xtc/Dgram.hh"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <climits>
#include <bitset>
#include <atomic>
#include <thread>
#include <chrono>                       // Revisit: Temporary?

using namespace XtcData;
using namespace Pds;
using namespace Pds::Fabrics;
using namespace Pds::Eb;
using logging          = psalg::SysLog;
using MetricExporter_t = std::shared_ptr<MetricExporter>;
using ms_t             = std::chrono::milliseconds;


EbAppBase::EbAppBase(const EbParams&         prms,
                     const MetricExporter_t& exporter,
                     const std::string&      pfx,
                     const uint64_t          duration,
                     const unsigned          maxEntries,
                     const unsigned          maxBuffers) :
  EventBuilder (maxBuffers + TransitionId::NumberOf,
                maxEntries,
                MAX_DRPS, //Revisit: std::bitset<64>(prms.contributors).count(),
                duration,
                prms.verbose),
  _transport   (prms.verbose),
  _maxEntries  (maxEntries),
  _maxBuffers  (maxBuffers),
  _verbose     (prms.verbose),
  _bufferCnt   (0),
  _tmoEvtCnt   (0),
  _fixupCnt    (0),
  _contributors(0),
  _id          (-1)
{
  std::map<std::string, std::string> labels{{"instrument", prms.instrument},
                                            {"partition", std::to_string(prms.partition)}};
  exporter->add(pfx+"_EpAlCt", labels, MetricType::Counter, [&](){ return  epochAllocCnt();     });
  exporter->add(pfx+"_EpFrCt", labels, MetricType::Counter, [&](){ return  epochFreeCnt();      });
  exporter->add(pfx+"_EvAlCt", labels, MetricType::Counter, [&](){ return  eventAllocCnt();     });
  exporter->add(pfx+"_EvFrCt", labels, MetricType::Counter, [&](){ return  eventFreeCnt();      });
  exporter->add(pfx+"_RxPdg",  labels, MetricType::Gauge,   [&](){ return _transport.pending(); });
  exporter->add(pfx+"_BfInCt", labels, MetricType::Counter, [&](){ return _bufferCnt;           }); // Inbound
  exporter->add(pfx+"_ToEvCt", labels, MetricType::Counter, [&](){ return _tmoEvtCnt;           });
  exporter->add(pfx+"_FxUpCt", labels, MetricType::Counter, [&](){ return _fixupCnt;            });

  // Revisit: nCtrbs isn't known yet
  unsigned nCtrbs = 64; //std::bitset<64>(prms.contributors).count();
  _fixupSrc = exporter->add(pfx+"_FxUpSc", labels, nCtrbs);
  _ctrbSrc  = exporter->add(pfx+"_CtrbSc", labels, nCtrbs); // Revisit: For testing
}

void EbAppBase::shutdown()
{
  if (_id != unsigned(-1))              // Avoid shutting down if already done
  {
    unconfigure();
    disconnect();

    _transport.shutdown();
  }
}

void EbAppBase::disconnect()
{
  for (auto link : _links)
  {
    _transport.disconnect(link);
  }
  _links.clear();

  _id           = -1;
  _contributors = 0;
  _contract.fill(0);
}

void EbAppBase::unconfigure()
{
  EventBuilder::dump(0);
  EventBuilder::clear();

  for (auto& region : _region)
  {
    if (region)  free(region);
    region = nullptr;
  }
  _region.clear();

  _bufRegSize.clear();
  _maxBufSize.clear();
  _maxTrSize .clear();
}

int EbAppBase::resetCounters()
{
  _bufferCnt = 0;
  _tmoEvtCnt = 0;
  _fixupCnt  = 0;
  _fixupSrc->clear();
  _ctrbSrc ->clear();

  return 0;
}

int EbAppBase::startConnection(const std::string& ifAddr,
                               std::string&       port,
                               unsigned           nLinks)
{
  int rc = linksStart(_transport, ifAddr, port, nLinks, "DRP");
  if (rc)  return rc;

  return 0;
}

int EbAppBase::connect(const EbParams& prms)
{
  unsigned nCtrbs = std::bitset<64>(prms.contributors).count();

  _links        .resize(nCtrbs);
  _id           = prms.id;
  _contributors = prms.contributors;
  _contract     = prms.contractors;

  int rc = linksConnect(_transport, _links, "DRP");
  if (rc)  return rc;

  return 0;
}

int EbAppBase::configure(const EbParams& prms)
{
  unsigned nCtrbs = _links.size();

  _bufRegSize.resize(nCtrbs);
  _maxTrSize .resize(nCtrbs);
  _maxBufSize.resize(nCtrbs);

  int rc = _linksConfigure(prms, _links, _id, "DRP");
  if (rc)  return rc;

  for (unsigned buf = 0; buf < NUM_TRANSITION_BUFFERS; ++buf)
  {
    for (auto link : _links)
    {
      uint64_t imm  = ImmData::value(ImmData::Transition, _id, buf);

      if (unlikely(_verbose >= VL_EVENT))
        printf("EbAp posts transition buffer index %u to src %2u, %08lx\n",
               buf, link->id(), imm);

      rc = link->post(nullptr, 0, imm);
      if (rc)
      {
        logging::error("%s:\n  Failed to post buffer number to DRP ID %d: rc %d, imm %08lx",
                       __PRETTY_FUNCTION__, link->id(), rc, imm);
      }
    }
  }

  return 0;
}

int EbAppBase::_linksConfigure(const EbParams&            prms,
                               std::vector<EbLfSvrLink*>& links,
                               unsigned                   id,
                               const char*                name)
{
  std::vector<EbLfSvrLink*> tmpLinks(links.size());
  _region.resize(links.size());

  for (auto link : links)
  {
    auto   t0(std::chrono::steady_clock::now());
    int    rc;
    size_t regSize;
    if ( (rc = link->prepare(id, &regSize)) )
    {
      logging::error("%s:\n  Failed to prepare link with %s ID %d",
                     __PRETTY_FUNCTION__, name, link->id());
      return rc;
    }
    unsigned rmtId     = link->id();
    tmpLinks[rmtId]    = link;

    _bufRegSize[rmtId] = regSize;
    _maxBufSize[rmtId] = regSize / (_maxBuffers * _maxEntries);
    _maxTrSize[rmtId]  = prms.maxTrSize[rmtId];
    regSize           += roundUpSize(NUM_TRANSITION_BUFFERS * _maxTrSize[rmtId]);  // Ctrbs don't have a transition space

    void* region = allocRegion(regSize);
    if (!region)
    {
      logging::error("%s:\n  "
                     "No memory found for Input MR for %s ID %d of size %zd",
                     __PRETTY_FUNCTION__, name, rmtId, regSize);
      return ENOMEM;
    }
    _region[rmtId] = region;

    if ( (rc = link->setupMr(region, regSize)) )
    {
      logging::error("%s:\n  Failed to set up Input MR for %s ID %d, "
                     "%p:%p, size %zd", __PRETTY_FUNCTION__, name, rmtId,
                     region, static_cast<char*>(region) + regSize, regSize);
      if (region)  free(region);
      _region[rmtId] = nullptr;
      return rc;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto dT = std::chrono::duration_cast<ms_t>(t1 - t0).count();
    logging::info("Inbound link with %s ID %d configured in %lu ms",
                  name, rmtId, dT);
  }

  links = tmpLinks;                     // Now in remote ID sorted order

  return 0;
}

int EbAppBase::process()
{
  int rc;

  // Pend for an input datagram and pass it to the event builder
  uint64_t  data;
  const int msTmo = 100;       // Also see EbEvent.cc::MaxTimeouts
  if ( (rc = _transport.pend(&data, msTmo)) < 0)
  {
    // Time out incomplete events
    if (rc == -FI_ETIMEDOUT)  EventBuilder::expired();
    else logging::error("%s:\n  pend() error %d\n", __PRETTY_FUNCTION__, rc);
    return rc;
  }

  ++_bufferCnt;

  unsigned       flg = ImmData::flg(data);
  unsigned       src = ImmData::src(data);
  unsigned       idx = ImmData::idx(data);
  EbLfSvrLink*   lnk = _links[src];
  size_t         ofs = (ImmData::buf(flg) == ImmData::Buffer)
                     ? (                   idx * _maxBufSize[src]) // In batch/buffer region
                     : (_bufRegSize[src] + idx * _maxTrSize[src]); // Tr region for non-selected EB is after batch/buffer region
  const EbDgram* idg = static_cast<EbDgram*>(lnk->lclAdx(ofs));

  if (src != idg->xtc.src.value())
    logging::warning("Link src (%d) != dgram src (%d)", src, idg->xtc.src.value());

  _ctrbSrc->observe(double(src));       // Revisit: For testing

  if (unlikely(_verbose >= VL_BATCH))
  {
    unsigned    env = idg->env;
    uint64_t    pid = idg->pulseId();
    unsigned    ctl = idg->control();
    const char* svc = TransitionId::name(idg->service());
    printf("EbAp rcvd %9lu %15s[%8u]   @ "
           "%16p, ctl %02x, pid %014lx, env %08x,            src %2u, data %08lx, lnk %p, src %2u\n",
           _bufferCnt, svc, idx, idg, ctl, pid, env, lnk->id(), data, lnk, src);
  }

  // Tr space bufSize value is irrelevant since maxEntries will be 1 for that case
  EventBuilder::process(idg, _maxBufSize[src], data);

  return 0;
}

void EbAppBase::post(const EbDgram* const* begin, const EbDgram** const end)
{
  for (auto pdg = begin; pdg < end; ++pdg)
  {
    auto     idg = *pdg;
    unsigned src = idg->xtc.src.value();
    auto     lnk = _links[src];
    size_t   ofs = lnk->lclOfs(reinterpret_cast<const void*>(idg));
    unsigned buf = (ofs - _bufRegSize[src]) / _maxTrSize[src];
    uint64_t imm = ImmData::value(ImmData::Transition, _id, buf);

    if (unlikely(_verbose >= VL_EVENT))
      printf("EbAp posts transition buffer index %u to src %2u, %08lx\n",
             buf, src, imm);

    int rc = lnk->post(nullptr, 0, imm);
    if (rc)
    {
      logging::error("%s:\n  Failed to post buffer number to DRP ID %d: rc %d, imm %08lx",
                     __PRETTY_FUNCTION__, src, rc, imm);
    }
  }
}

void EbAppBase::trim(unsigned dst)
{
  for (unsigned group = 0; group < _contract.size(); ++group)
  {
    _contract[group]  &= ~(1 << dst);
    //_receivers[group] &= ~(1 << dst);
  }
}

uint64_t EbAppBase::contract(const EbDgram* ctrb) const
{
  // This method is called when the event is created, which happens when the event
  // builder recognizes the first contribution.  This contribution contains
  // information from the L1 trigger that identifies which readout groups are
  // involved.  This routine can thus look up the expected list of contributors
  // (the contract) to the event for each of the readout groups and logically OR
  // them together to provide the overall contract.  The list of contributors
  // participating in each readout group is provided at configuration time.

  uint64_t contract = 0;
  uint16_t groups   = ctrb->readoutGroups();

  while (groups)
  {
    unsigned group = __builtin_ffs(groups) - 1;
    groups &= ~(1 << group);

    contract |= _contract[group];
  }
  return contract;
}

void EbAppBase::fixup(EbEvent* event, unsigned srcId)
{
  if (event->alive())
    ++_fixupCnt;
  else
    ++_tmoEvtCnt;

  _fixupSrc->observe(double(srcId));

  //if (_verbose >= VL_EVENT)
  {
    using ms_t  = std::chrono::milliseconds;   // Revisit: Temporary?
    auto  now   = fast_monotonic_clock::now(); // Revisit: Temporary?
    const EbDgram* dg = event->creator();
    printf("%s %15s %014lx, size %2zu, for source %2u, RoGs %04hx, contract %016lx, remaining %016lx, age %ld\n",
           event->alive() ? "Fixed-up" : "Timed-out",
           TransitionId::name(dg->service()), event->sequence(), event->size(),
           srcId, dg->readoutGroups(), event->contract(), event->remaining(),
           std::chrono::duration_cast<ms_t>(now - event->t0).count());
  }

  event->damage(Damage::DroppedContribution);
}
