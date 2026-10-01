#include "player_gui.hpp"

namespace nigamp {

// Built without FLTK: --gui reports that the GUI is unavailable
std::unique_ptr<IPlayerGui> create_player_gui() {
    return nullptr;
}

}
