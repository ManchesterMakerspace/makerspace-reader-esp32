#pragma once
#include <cstdint>
#include <string>

// Pure session decision engine. Call only after authenticating the physical card.
// The adapter must persist a start before energizing the relay, and must persist
// the returned end event before clearing the durable session journal.
namespace toolsession {
constexpr int64_t confirmationMs = 15000;
enum class Action { denied, start, logoff, confirmKick, kick };
struct Card {
  std::string uid, member;
  bool valid = false, checkout = false, managesShop = false, boardOrAdmin = false;
  bool canKick() const { return valid && (checkout || managesShop || boardOrAdmin); }
};
struct State {
  std::string sessionId, ownerUid, ownerMember, candidateUid, candidateMember;
  int64_t confirmationUntil = 0;
  bool active() const { return !sessionId.empty(); }
  void cancelConfirmation() {
    candidateUid.clear();
    candidateMember.clear();
    confirmationUntil = 0;
  }
  void begin(const std::string &id, const Card &owner) {
    sessionId = id;
    ownerUid = owner.uid;
    ownerMember = owner.member;
    cancelConfirmation();
  }
  void end() {
    sessionId.clear();
    ownerUid.clear();
    ownerMember.clear();
    cancelConfirmation();
  }
  Action scan(const Card &card, int64_t now, bool available, bool outOfService) {
    if (confirmationUntil && now >= confirmationUntil) cancelConfirmation();
    if (card.uid != candidateUid || card.member != candidateMember) cancelConfirmation();
    if (!card.valid || card.uid.empty() || card.member.empty()) {
      cancelConfirmation();
      return Action::denied;
    }
    if (!active()) {
      cancelConfirmation();
      return available && !outOfService && card.checkout ? Action::start : Action::denied;
    }
    if (card.uid == ownerUid && card.member == ownerMember) {
      cancelConfirmation();
      return Action::logoff;
    }
    if (!card.canKick()) {
      cancelConfirmation();
      return Action::denied;
    }
    if (candidateUid == card.uid && candidateMember == card.member && now < confirmationUntil) {
      cancelConfirmation();
      return Action::kick;
    }
    candidateUid = card.uid;
    candidateMember = card.member;
    confirmationUntil = now + confirmationMs;
    return Action::confirmKick;
  }
};
} // namespace toolsession
