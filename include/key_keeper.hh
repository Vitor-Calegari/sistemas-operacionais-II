#ifndef KEY_KEEPER_HH
#define KEY_KEEPER_HH

#include "mac.hh"
#include "mac_structs.hh"
#include <map>
#include <vector>

class KeyKeeper {
public:
  KeyKeeper() = default;
  ~KeyKeeper() = default;

  void setKeys(const std::vector<MacKeyEntry> &entries) {
    keys.clear();
    for (const MacKeyEntry &entry : entries) {
      keys[entry.id] = entry.key;
    }
  }

  MAC::Key getKey(int rsu_id) {
    return keys[rsu_id];
  }

private:
  std::map<int, MAC::Key> keys;
};

#endif
