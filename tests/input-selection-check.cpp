#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <stdexcept>
#include "types.h"
#include "git.h"
FCEUGI::FCEUGI() {}
FCEUGI::~FCEUGI() {}

struct TestConfig {
    std::map<std::string, std::string> values;
    int saves = 0;
    void setOption(const std::string& key, const std::string& value) { values[key] = value; }
    void save() { ++saves; }
} config;
TestConfig* g_config = &config;
int UsrInputType[3] = {SI_GAMEPAD, SI_GAMEPAD, SIFC_NONE};
int CurInputType[3] = {SI_GAMEPAD, SI_GAMEPAD, SIFC_NONE};
int gametype = 0, cspec = 0;
#include "input-selection-under-test.hpp"

void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
int main() {
    try {
        FCEUGI game;
        game.input[0] = SI_GAMEPAD; game.input[1] = SI_GAMEPAD; game.inputfc = SIFC_NONE;
        for (int chosen : {SI_ZAPPER, SI_MIRACLE_PIANO, SI_GAMEPAD, SI_NONE}) {
            CurInputType[1] = chosen;
            SaveUserInputSelection();
            require(UsrInputType[1] == chosen, "Manual choice was not retained");
            require(decodeStoredInput(1, config.values["SDL.Input.1"]) == chosen, "Saved choice does not reload");
            InputUserActiveFix();
            ParseGIInput(&game);
            require(CurInputType[1] == chosen, "ROM metadata overrode manual choice");
        }
        CurInputType[1] = SI_MIRACLE_PIANO; SaveUserInputSelection();
        CurInputType[1] = SI_GAMEPAD; // Temporary movie input must not become the preference.
        InputUserActiveFix();
        require(CurInputType[1] == SI_MIRACLE_PIANO, "Temporary movie input replaced preference");
        require(decodeStoredInput(1, "MiraclePiano") == SI_MIRACLE_PIANO, "Legacy piano option regressed");
        require(decodeStoredInput(1, "GamePad.1") == SI_GAMEPAD, "Legacy gamepad option regressed");
        require(decodeStoredInput(1, "Zapper") == SI_ZAPPER, "Legacy zapper option regressed");
        require(decodeStoredInput(1, "99") == SI_NONE, "Invalid device index accepted");
        CurInputType[2] = SIFC_FKB; SaveUserInputSelection();
        require(decodeStoredInput(2, config.values["SDL.Input.2"]) == SIFC_FKB, "Expansion selection lost");
        ParseGIInput(&game);
        require(CurInputType[2] == SIFC_FKB, "ROM overrode expansion selection");
        std::puts("PASS: user selection survives ROM load/close, saved values reload, movies do not overwrite preferences");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
