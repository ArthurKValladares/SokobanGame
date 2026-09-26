#include "engine/render/RenderTypes.hpp"

#include "engine/render/WaterConfig.hpp"

namespace sokoban {

RenderFrameData::WaterRendering RenderFrameData::defaultWaterRendering()
{
    return {
        .surfaceColor = config::waterSurfaceColor,
        .primaryRippleOpacity = config::waterPrimaryRippleOpacity,
        .secondaryRippleOpacity = config::waterSecondaryRippleOpacity,
        .rippleSpatialFrequency = config::waterRippleSpatialFrequency,
        .rippleSpeed = config::waterRippleSpeed,
        .refractionStrength = config::waterRefractionStrength,
        .rippleCrestHalfWidth = config::waterRippleCrestHalfWidth,
        .rippleHaloWidth = config::waterRippleHaloWidth,
        .rippleHaloStrength = config::waterRippleHaloStrength,
        .rippleCrestStrength = config::waterRippleCrestStrength,
        .secondaryRippleThicknessScale =
            config::waterSecondaryRippleThicknessScale,
        .underwaterCausticStrength = config::waterUnderwaterCausticStrength,
        .visualizeCausticsOnly = false,
        .primaryShorelineOpacity = config::waterPrimaryShorelineOpacity,
        .secondaryShorelineOpacity = config::waterSecondaryShorelineOpacity,
    };
}

} // namespace sokoban
