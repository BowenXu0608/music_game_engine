#include "Material.h"

const char* kindName(MaterialKind k) {
    switch (k) {
        case MaterialKind::Unlit:    return "unlit";
        case MaterialKind::Glow:     return "glow";
        case MaterialKind::Scroll:   return "scroll";
        case MaterialKind::Pulse:    return "pulse";
        case MaterialKind::Gradient: return "gradient";
        case MaterialKind::Custom:   return "custom";
        default:                     return "unlit";
    }
}

MaterialKind parseKind(const std::string& s) {
    if (s == "glow"     || s == "Glow")     return MaterialKind::Glow;
    if (s == "scroll"   || s == "Scroll")   return MaterialKind::Scroll;
    if (s == "pulse"    || s == "Pulse")    return MaterialKind::Pulse;
    if (s == "gradient" || s == "Gradient") return MaterialKind::Gradient;
    if (s == "custom"   || s == "Custom")   return MaterialKind::Custom;
    return MaterialKind::Unlit;
}
