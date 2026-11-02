#include "../MakerspaceReader/Reservation.h"
#include "../MakerspaceReader/Audio.h"
#include "../MakerspaceReader/Display.h"
#include <cassert>
static_assert(READER_ENABLE_RESERVATIONS == 0, "Compile-out coverage requires the feature off");
int main() {
  assert(audio::melody(audio::MelodyId::Coin).count==3);
  display::Model model(0); display::Status status;
  status.net=status.api=status.mqtt=status.trusted=true;
  auto frame=model.frame(status,3000);
  assert(frame.row2[0]=='H');
}
