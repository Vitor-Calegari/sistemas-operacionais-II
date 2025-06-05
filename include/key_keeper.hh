#ifndef KEY_KEEPER_HH
#define KEY_KEEPER_HH

#include "mac_structs.hh"
#include <map>
#include <vector>

class KeyKeeper {
public:
    KeyKeeper(){}
    ~KeyKeeper(){}

    void setKeys(std::vector<MacKeyEntry> entries) {
        keys.clear();
        for (MacKeyEntry entry : entries) {
            std::vector<char> bytes;
            for (char byte : entry.bytes) {
                bytes.push_back(byte);
            }
            keys[entry.id] = bytes;
        }
    }

    std::vector<char> getKey(int rsu_id) {
        return keys[rsu_id];
    }

private:
    std::map<int, std::vector<char>> keys;
};

#endif
