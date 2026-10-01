#include "media_controls.hpp"

namespace nigamp {

std::unique_ptr<IMediaControls> create_media_controls() {
    return std::make_unique<NullMediaControls>();
}

}
