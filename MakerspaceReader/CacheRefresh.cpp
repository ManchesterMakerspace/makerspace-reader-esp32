#include "Reader.h"
#include <algorithm>
#include <unordered_set>

// Caller holds stateMutex. Only card/grant state is merged here; configuration
// and tool status are applied by refresh before it rechecks the active session.
bool mergeCacheSnapshot(JsonVariantConst result, const std::vector<std::string> &requested,
                        uint32_t sequence, bool sameScope) {
    bool changed = false;
    std::unordered_map<std::string, Card> next;
    for (JsonObjectConst o : result["cards"].as<JsonArrayConst>()) {
      Card c;
      c.refreshedAt = millis() + 1;
      c.sourceSequence = sequence;
      c.uid = o["uid"] | "";
      c.id = o["_id"] | "";
      c.member = o["memberId"] | "";
      c.holder = o["holder"] | "";
      c.firstName = o["firstName"] | "";
      c.lastInitial = o["lastInitial"] | "";
      c.managesTool = o["managesTool"] | false;
      c.status = o["validity"] | "unknown";
      c.expiry = o["expiry"] | int64_t(0);
      c.key = o["credential"]["keyA"] | "";
      c.digest = o["credential"]["digest"] | "";
      auto previous = cards.find(c.uid.c_str());
      if (previous != cards.end()) {
        if (sameScope && previous->second.sourceSequence > sequence)
          c = previous->second;
        c.lastUsed = previous->second.lastUsed;
        c.first = previous->second.first;
        c.pending = previous->second.pending;
        c.observedUs = previous->second.observedUs;
        auto &p = previous->second;
        changed |= p.managesTool != c.managesTool || p.firstName != c.firstName || p.lastInitial != c.lastInitial || p.id != c.id || p.holder != c.holder || p.status != c.status || p.expiry != c.expiry || p.key != c.key || p.digest != c.digest || p.member != c.member;
      } else
        changed = true;
      next[c.uid.c_str()] = c;
    }
    // A scoped snapshot is authoritative for requested UIDs and ALL privileged
    // cards. Lookups may finish between UID capture and the snapshot API call.
    std::unordered_set<std::string> retainedMembers, coveredMembers;
    for (const auto &p : next) coveredMembers.insert(p.second.member.c_str());
    for (const auto &p : cards)
      if (p.second.managesTool || std::find(requested.begin(), requested.end(), p.first) != requested.end())
        coveredMembers.insert(p.second.member.c_str());
    for (const auto &pair : cards) {
      if (next.count(pair.first)) continue;
      const auto &previous = pair.second;
      bool unrequested = readerProfile::compact && !previous.managesTool &&
        std::find(requested.begin(), requested.end(), pair.first) == requested.end();
      bool retain = sameScope && (previous.sourceSequence > sequence || unrequested);
      if (retain) {
        next[pair.first] = previous;
        if (unrequested && !previous.id.isEmpty()) retainedMembers.insert(previous.member.c_str());
      } else if (next.size() < readerProfile::cards && (previous.first || previous.pending)) {
        Card c; c.uid = previous.uid; c.first = previous.first;
        c.pending = previous.pending; c.observedUs = previous.observedUs;
        next[pair.first] = c;
        changed |= !previous.id.isEmpty();
      } else changed = true;
    }
    changed |= next.size() != cards.size();
    cards.swap(next);
    std::unordered_map<std::string, Grant> ng;
    for (JsonObjectConst o : result["grants"].as<JsonArrayConst>()) {
      Grant g;
      g.sourceSequence = sequence;
      g.member = o["memberId"] | "";
      g.id = o["checkoutId"] | "";
      g.until = o["until"] | int64_t(0);
      auto p = grants.find(g.member.c_str());
      changed |= p == grants.end() || p->second.id != g.id || p->second.until != g.until;
      ng[g.member.c_str()] = g;
    }
    changed |= ng.size() != grants.size();
    // A lookup completed after this snapshot request must not lose its grant.
    for (const auto &p : grants) {
      if (sameScope && (p.second.sourceSequence > sequence ||
          (retainedMembers.count(p.first) && !coveredMembers.count(p.first) && !ng.count(p.first))))
        ng[p.first] = p.second;
    }
    grants.swap(ng);
    trimCardCache();
    return changed;
}
