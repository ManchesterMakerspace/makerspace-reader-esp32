#include "Reservation.h"
#if READER_ENABLE_RESERVATIONS
#include <algorithm>
namespace reservation {
static bool visible(const Record &r, int64_t now) {
  return !r.released && r.end > now && (r.arrived || now <= r.start + grace);
}
void Model::reset() { *this = Model{}; }
Record *Model::find(const std::string &id) {
  for (auto &r : records) if (r.id == id) return &r;
  return nullptr;
}
void Model::setSession(const Session &next) {
  if (next.id != session.id) { warnings.clear(); windowKey.clear(); newCues.clear(); firstSessionTick=true; }
  session = next;
}
void Model::replace(std::vector<Record> next, bool all, int64_t now) {
  if (next.size() > capacity) { next.resize(capacity); all = false; }
  seen.erase(std::remove_if(seen.begin(), seen.end(), [now](const Seen &s) { return s.end <= now; }), seen.end());
  for (auto &r : next) {
    auto previous = find(r.id);
    if (previous && previous->revision == r.revision) {
      r.arrived = std::max(r.arrived, previous->arrived);
      r.released = previous->released;
      r.associatedSession=previous->associatedSession;
    }
    bool known = std::any_of(seen.begin(), seen.end(), [&](const Seen &s) { return s.id == r.id; });
    if (!known) {
      if (seen.size()<128 && newCues.size()<capacity && baseline && !session.id.empty() && r.start >= now && r.start <= now + 4 * hour)
        newCues.push_back({r.id,r.start,false});
      if (seen.size() < 128) seen.push_back({r.id, r.end});
    } else {
      for (auto &s : seen) if (s.id == r.id) s.end = std::max(s.end,r.end);
    }
  }
  std::sort(next.begin(),next.end(),[](const Record &a,const Record &b) { return a.start == b.start ? a.id < b.id : a.start < b.start; });
  records = std::move(next); complete = all; baseline = true;
  warnings.erase(std::remove_if(warnings.begin(),warnings.end(),[&](const Warning &w){
    return !std::any_of(records.begin(),records.end(),[&](const Record &r){
      return w.key==r.id+":end:"+std::to_string(r.end) || w.key==r.id+":start:"+std::to_string(r.start);
    });
  }),warnings.end());
}
bool Model::warn(const Record &r, bool own, int64_t now) {
  int64_t deadline = own ? r.end : r.start, remaining = deadline - now;
  if (remaining <= 0 || remaining > grace) return false;
  std::string key = r.id + (own ? ":end:" : ":start:") + std::to_string(deadline);
  auto it = std::find_if(warnings.begin(),warnings.end(),[&](const Warning &w) { return w.key == key; });
  if (it == warnings.end()) {
    if (warnings.size() >= 128) warnings.erase(warnings.begin());
    warnings.push_back({key,0}); it=warnings.end()-1;
  }
  unsigned mask = remaining <= 300000 ? 3 : 1;
  bool fire = (it->mask & mask) != mask;
  it->mask |= mask;
  return fire;
}
Decision Model::tick(int64_t now) {
  Decision d;
  if (now <= 0 || session.id.empty()) return d;
  // Use the durable scan timestamp on the first evaluation, so a delayed loop
  // cannot turn an arrival exactly at +15 minutes into a no-show.
  int64_t claimAt=firstSessionTick && session.started>0 && session.started<=now ? session.started : now;
  firstSessionTick=false;
  d.cues.swap(newCues); d.newReservationCue = !d.cues.empty(); d.flash = d.newReservationCue;
  const Record *current = nullptr, *next = nullptr, *own = nullptr;
  for (const auto &r : records) {
    if (visible(r,now) && r.arrived && r.member == session.member && now >= r.start - hour) { if (!own) own=&r; }
    if (!visible(r,claimAt)) continue;
    if (r.start <= claimAt && !current) current=&r;
    if (r.start > claimAt && !next) next=&r;
  }
  const Record *candidate = current ? current : next;
  if (!own && complete && candidate && candidate->member == session.member &&
      claimAt >= candidate->start - hour && claimAt <= candidate->start + grace && now<candidate->end) {
    bool otherActive = std::any_of(records.begin(),records.end(),[&](const Record &r) {
      return !r.released && r.end>claimAt && r.id != candidate->id && r.start <= claimAt;
    });
    if (!otherActive) {d.claim=candidate->id; d.observedAt=claimAt;}
  }
  if (own && warn(*own,true,now)) d.warnings.push_back({own->id,own->end,true});
  const Record *upcoming = nullptr;
  for (const auto &r : records) {
    if (visible(r,now) && r.member != session.member && r.start <= now + 4*hour) {
      if (!upcoming) upcoming=&r;
      if(warn(r,false,now)) d.warnings.push_back({r.id,r.start,false});
    }
  }
  std::string key = upcoming ? upcoming->id + ":" + std::to_string(upcoming->start) : "";
  if (!key.empty() && key != windowKey) d.flash = true;
  windowKey=key;
  d.jeopardy=!d.warnings.empty();
  return d;
}
View Model::view(int64_t now) const {
  View v;
  if (now<=0) return v;
  if (session.id.empty()) {
    for (const auto &r : records) if (visible(r,now)) {
      v.present=true; v.name=r.name; v.start=r.start; v.end=r.end; return v;
    }
    return v;
  }
  for (const auto &r : records) {
    if (!visible(r,now)) continue;
    bool own = r.member == session.member && r.arrived && now >= r.start - hour;
    if (!own && (r.member == session.member || r.start > now + 4*hour)) continue;
    int64_t deadline=own ? r.end : r.start;
    if (!v.present || deadline < v.deadline) {
      v.present=v.countdown=true; v.own=own; v.deadline=deadline;
      v.name=r.name; v.start=r.start; v.end=r.end; v.due=!own && deadline<=now;
    }
  }
  return v;
}
} // namespace reservation
#endif
