#include "Application/Persistency/PersistencyDocument.h"
#include "Application/Persistency/PersistencyService.h"
#include "doctest/doctest.h"

TEST_CASE("R4: PersistencyDocument rejects corrupted bytes") {
  PersistencyDocument doc;
  const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF};
  CHECK(doc.LoadFromBuffer(garbage, sizeof(garbage)) == false);
  CHECK(doc.HadError() == true);
}

TEST_CASE("R1: SaveToBuffer + LoadFromBuffer round-trip") {
  PersistencyService svc;  // local instance, no auto-register
  uint8_t buf[4096] = {0};
  size_t written = 0;

  // First call: no sub-services are registered in a host test, so
  // SaveToBuffer should still emit the root <PICOTRACKER> element and
  // return true with a non-zero written length.
  bool ok = svc.SaveToBuffer(buf, sizeof(buf), written);
  CHECK(ok == true);
  CHECK(written > 0);

  // Now load the same buffer back; the document should accept it.
  PersistencyDocument doc;
  CHECK(doc.LoadFromBuffer(buf, written) == true);
}
